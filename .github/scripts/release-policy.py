#!/usr/bin/env python3
"""Validate release identity and installer artifact policy."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tarfile
import zipfile
from collections.abc import Callable
from pathlib import Path


VERSION_RE = re.compile(r"^(?P<numeric>\d+\.\d+\.\d+)(?:-(?P<label>beta|rc)(?P<number>[1-9]\d*))?$")
RELEASE_BRANCH_RE = re.compile(r"^release/(?P<major>\d+)\.(?P<minor>\d+)$")
OBJECT_ID_RE = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")
MACOS_MODES = ("validation", "distribution")
WINDOWS_MODES = ("signpath", "unsigned")
CHANNEL_RANK = {"BETA": 0, "RC": 1, "GA": 2}
BETA_TAG_PATTERN = "v*-beta*"
ARMHF_TARGETS = ("linux-armhf-bookworm", "linux-armhf-trixie")
RELEASE_TARGETS = ("arm64-macOS", "x86_64-macOS", "linux-x86_64", "linux-aarch64", *ARMHF_TARGETS, "windows-x86_64")
RELEASE_TARBALLS_FILE = Path(__file__).resolve().parents[2] / "CMake" / "release-tarballs.txt"
# Artifact name endings that already mean another kind of release asset.
RESERVED_ASSET_KINDS = ("deb", "rpm", "installer", "signed", "unsigned", "src")


def classify(version: str) -> dict[str, str]:
    match = VERSION_RE.fullmatch(version)
    if not match:
        raise ValueError("version must be X.Y.Z, X.Y.Z-betaN, or X.Y.Z-rcN")
    label = match.group("label")
    numeric = match.group("numeric")
    major, minor, _ = numeric.split(".")
    return {
        "version": version,
        "numeric": numeric,
        "channel": label.upper() if label else "GA",
        "prerelease_number": match.group("number") or "",
        "release_branch": f"release/{major}.{minor}",
    }


def release_order(identity: dict[str, str]) -> tuple[int, ...]:
    return (
        *(int(part) for part in identity["numeric"].split(".")),
        CHANNEL_RANK[identity["channel"]],
        int(identity["prerelease_number"] or 0),
    )


def parse_public_ref_listing(output: str) -> tuple[dict[str, str], str]:
    refs: dict[str, str] = {}
    peeled_tags: dict[str, str] = {}
    records = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) != 2 or not OBJECT_ID_RE.fullmatch(fields[0]):
            raise ValueError("public git ref listing is malformed")
        object_id, name = fields
        records.append(f"{object_id} {name}")
        if name.endswith("^{}"):
            tag = name[:-3]
            if tag.startswith("refs/tags/v"):
                peeled_tags[tag] = object_id
        elif (
            name == "refs/heads/master"
            or name.startswith("refs/heads/release/")
            or name.startswith("refs/tags/v")
        ):
            refs[name] = object_id
    refs.update(peeled_tags)
    digest = hashlib.sha256("\n".join(sorted(records)).encode("utf-8")).hexdigest()
    return refs, digest


def public_release_line_policy(version: str, refs: dict[str, str]) -> dict[str, object]:
    identity = classify(version)
    ga_versions: list[tuple[int, int, int]] = []
    line_tags: list[tuple[tuple[int, ...], str, str]] = []
    release_branch_count = 0
    for name, sha in refs.items():
        if name.startswith("refs/heads/release/"):
            branch = name.removeprefix("refs/heads/")
            match = RELEASE_BRANCH_RE.fullmatch(branch)
            if not match:
                raise ValueError(f"public release branch is malformed: {branch}")
            release_branch_count += 1
        elif name.startswith("refs/tags/v"):
            tag_version = name.removeprefix("refs/tags/v")
            try:
                tag_identity = classify(tag_version)
            except ValueError as error:
                raise ValueError(f"public version tag is malformed: {name}") from error
            numeric_version = tuple(int(part) for part in tag_identity["numeric"].split("."))
            line_tags.append((release_order(tag_identity), name.removeprefix("refs/tags/"), sha))
            if tag_identity["channel"] == "GA":
                ga_versions.append(numeric_version)
    if not ga_versions:
        raise ValueError(
            "cannot determine the newest GA line: no immutable public vX.Y.Z GA tags exist"
        )
    candidate_version = tuple(int(part) for part in identity["numeric"].split("."))
    candidate_line = candidate_version[:2]
    newest_version = max(ga_versions)
    candidate_line_versions = [
        ga_version for ga_version in ga_versions if ga_version[:2] == candidate_line
    ]
    highest_candidate_line_version = (
        max(candidate_line_versions) if candidate_line_versions else None
    )
    candidate_line_tags = [tag for tag in line_tags if tag[0][:2] == candidate_line]
    latest_candidate_line_tag = max(candidate_line_tags, default=None)
    newest_line = newest_version[:2]
    return {
        "candidate_line": f"{candidate_line[0]}.{candidate_line[1]}",
        "newest_ga_line": f"{newest_line[0]}.{newest_line[1]}",
        "ga_line_is_newest": candidate_line >= newest_line,
        "newest_ga_version": ".".join(str(part) for part in newest_version),
        "highest_ga_version_on_candidate_line": (
            ".".join(str(part) for part in highest_candidate_line_version)
            if highest_candidate_line_version
            else None
        ),
        "latest_public_tag_on_candidate_line": (
            latest_candidate_line_tag[1] if latest_candidate_line_tag else None
        ),
        "latest_public_tag_sha_on_candidate_line": (
            latest_candidate_line_tag[2] if latest_candidate_line_tag else None
        ),
        "ga_release_is_latest": (
            identity["channel"] == "GA" and candidate_version >= newest_version
        ),
        "legacy_tag_only": release_branch_count == 0,
    }


def plan_public_promotion(
    version: str,
    sha: str,
    refs: dict[str, str],
    is_ancestor: Callable[[str, str], bool],
) -> dict[str, object]:
    if not OBJECT_ID_RE.fullmatch(sha):
        raise ValueError("candidate SHA must be a full lowercase Git object ID")
    identity = classify(version)
    line_policy = public_release_line_policy(version, refs)
    public_tag = f"v{version}"
    tag_ref = f"refs/tags/{public_tag}"
    branch_ref = f"refs/heads/{identity['release_branch']}"
    current_tag = refs.get(tag_ref)
    current_branch = refs.get(branch_ref)
    if current_tag and current_tag != sha:
        raise ValueError(f"public {public_tag} already points to {current_tag}; tags are never moved")
    if current_branch and not is_ancestor(current_branch, sha):
        raise ValueError(
            f"public {identity['release_branch']} at {current_branch} is not an ancestor of {sha}; "
            "refusing a non-fast-forward update"
        )
    if not current_branch and line_policy["latest_public_tag_sha_on_candidate_line"]:
        tag_sha = line_policy["latest_public_tag_sha_on_candidate_line"]
        if not is_ancestor(tag_sha, sha):
            tag = line_policy["latest_public_tag_on_candidate_line"]
            raise ValueError(
                f"public {identity['release_branch']} is missing and its latest public tag "
                f"{tag} at {tag_sha} is not an ancestor of {sha}; "
                "refusing a disconnected branch initialization"
            )

    highest_candidate_line_version = line_policy["highest_ga_version_on_candidate_line"]
    if (
        identity["channel"] == "GA"
        and highest_candidate_line_version
        and tuple(int(part) for part in identity["numeric"].split("."))
        < tuple(int(part) for part in highest_candidate_line_version.split("."))
    ):
        raise ValueError(
            f"public GA tags already reach {highest_candidate_line_version} on this line; "
            "a lower GA version cannot advance it"
        )
    latest_line_tag = line_policy["latest_public_tag_on_candidate_line"]
    if (
        not current_tag
        and latest_line_tag
        and release_order(identity) <= release_order(classify(latest_line_tag.removeprefix("v")))
    ):
        raise ValueError(
            f"{latest_line_tag} is the latest public tag on this line; {public_tag} must sort above it"
        )

    advance_master = identity["channel"] == "GA" and line_policy["ga_line_is_newest"]
    current_master = refs.get("refs/heads/master")
    if advance_master:
        if not current_master:
            raise ValueError("public master is missing; cannot fast-forward the newest GA line")
        if not is_ancestor(current_master, sha):
            raise ValueError(
                f"public master at {current_master} is not an ancestor of {sha}; "
                "review and complete the private release history bridge before GA promotion"
            )

    updates = [branch_ref]
    if not current_tag:
        updates.append(tag_ref)
    if advance_master:
        updates.append("refs/heads/master")
    return {
        **line_policy,
        "version": version,
        "channel": identity["channel"],
        "sha": sha,
        "public_tag": public_tag,
        "release_branch": identity["release_branch"],
        "prior_release_branch": current_branch,
        "prior_master": current_master,
        "tag_exists": current_tag == sha,
        "advance_master": advance_master,
        "updates": updates,
    }


def git_output(
    arguments: list[str], error_context: str, *, cwd: Path | None = None
) -> str:
    result = subprocess.run(["git", *arguments], capture_output=True, text=True, cwd=cwd)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise ValueError(f"{error_context}: {detail}")
    return result.stdout.strip()


def git_is_ancestor(ancestor: str, descendant: str, *, cwd: Path | None = None) -> bool:
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", ancestor, descendant],
        capture_output=True,
        text=True,
        cwd=cwd,
    )
    if result.returncode not in (0, 1):
        detail = result.stderr.strip() or result.stdout.strip()
        raise ValueError(f"unable to verify public commit ancestry: {detail}")
    return result.returncode == 0


def public_refs(remote: str, *, cwd: Path | None = None) -> tuple[dict[str, str], str]:
    listing = git_output(
        [
            "ls-remote",
            remote,
            "refs/heads/master",
            "refs/heads/release/*",
            "refs/tags/v*",
        ],
        "unable to read public release refs",
        cwd=cwd,
    )
    return parse_public_ref_listing(listing)


def inspect_public_promotion(
    version: str, sha: str, remote: str, *, cwd: Path | None = None
) -> dict[str, object]:
    refs, digest = public_refs(remote, cwd=cwd)
    identity = classify(version)
    refs_to_fetch = ["refs/heads/master", f"refs/heads/{identity['release_branch']}"]
    if f"refs/heads/{identity['release_branch']}" not in refs:
        line_policy = public_release_line_policy(version, refs)
        latest_tag = line_policy["latest_public_tag_on_candidate_line"]
        if latest_tag:
            refs_to_fetch.append(f"refs/tags/{latest_tag}")
    for name in refs_to_fetch:
        if name in refs:
            git_output(
                ["fetch", "--no-tags", remote, name],
                f"unable to fetch {name}",
                cwd=cwd,
            )
    plan = plan_public_promotion(
        version,
        sha,
        refs,
        lambda ancestor, descendant: git_is_ancestor(ancestor, descendant, cwd=cwd),
    )
    plan["public_refs_digest"] = digest
    return plan


def push_public_ref_updates(
    remote: str, plan: dict[str, object], *, cwd: Path | None = None
) -> None:
    refspecs = [f"{plan['sha']}:{ref}" for ref in plan["updates"]]
    result = subprocess.run(
        ["git", "push", "--atomic", remote, *refspecs],
        capture_output=True,
        text=True,
        cwd=cwd,
    )
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise ValueError(f"atomic public ref update failed; no refs were changed: {detail}")


def promote_public_source(
    version: str,
    sha: str,
    remote: str,
    expected_refs_digest: str,
    *,
    cwd: Path | None = None,
) -> dict[str, object]:
    plan = inspect_public_promotion(version, sha, remote, cwd=cwd)
    if plan["public_refs_digest"] != expected_refs_digest:
        raise ValueError("public release refs changed after validation; rerun validation before promotion")
    push_public_ref_updates(remote, plan, cwd=cwd)
    return plan


def inspect_public_release_line(
    version: str, remote: str, *, cwd: Path | None = None
) -> dict[str, object]:
    refs, digest = public_refs(remote, cwd=cwd)
    return {
        **public_release_line_policy(version, refs),
        "public_refs_digest": digest,
    }


def publication_environment(channel: str) -> str:
    return "beta-release" if channel == "BETA" else "public-release"


def validate_publication_environment(
    environment: object, channel: str = "GA", deployment_policies: object = None
) -> None:
    name = publication_environment(channel)
    if not isinstance(environment, dict) or environment.get("name") != name:
        raise ValueError(f"{name} environment response is malformed")
    rules = environment.get("protection_rules")
    if not isinstance(rules, list):
        raise ValueError(f"{name} environment has no protection_rules list")
    reviewer_rules = [
        rule for rule in rules
        if isinstance(rule, dict) and rule.get("type") == "required_reviewers"
    ]
    if channel == "BETA":
        if reviewer_rules:
            raise ValueError("beta-release environment must not require reviewers")
        if environment.get("deployment_branch_policy") != {
            "protected_branches": False, "custom_branch_policies": True
        }:
            raise ValueError("beta-release environment must restrict deployments to custom deployment patterns")
        policies = deployment_policies.get("branch_policies") if isinstance(deployment_policies, dict) else None
        if (
            not isinstance(policies, list)
            or len(policies) != 1
            or not isinstance(policies[0], dict)
            or (policies[0].get("name"), policies[0].get("type")) != (BETA_TAG_PATTERN, "tag")
        ):
            raise ValueError(f"beta-release environment must admit exactly the {BETA_TAG_PATTERN} tag pattern")
        return
    if len(reviewer_rules) != 1:
        raise ValueError("public-release environment must have one required-reviewers rule")
    rule = reviewer_rules[0]
    if rule.get("prevent_self_review") is not False:
        raise ValueError("public-release required-reviewers rule must allow self-review")
    reviewers = rule.get("reviewers")
    if not isinstance(reviewers, list) or not reviewers:
        raise ValueError("public-release required-reviewers rule must list eligible reviewers")
    for reviewer in reviewers:
        identity = reviewer.get("reviewer") if isinstance(reviewer, dict) else None
        reviewer_type = reviewer.get("type") if isinstance(reviewer, dict) else None
        if (
            not isinstance(reviewer_type, str)
            or reviewer_type not in {"User", "Team"}
            or not isinstance(identity, dict)
            or type(identity.get("id")) is not int
            or identity["id"] <= 0
        ):
            raise ValueError("public-release required-reviewers rule contains a malformed reviewer")


def parse_state(contents: str, *, expected_revision: str | None = None) -> dict[str, str]:
    state = {}
    for line in contents.splitlines():
        key, separator, value = line.partition("=")
        if not separator or key in state:
            raise ValueError("release-state.txt must contain unique key=value lines")
        state[key] = value
    required_keys = {"version", "channel", "prerelease", "revision"}
    if not required_keys <= set(state) or set(state) - required_keys - {"windows_signing"}:
        raise ValueError("release-state.txt must define version, channel, prerelease, revision, and optionally windows_signing")
    state.setdefault("windows_signing", "signpath")
    if state["windows_signing"] not in WINDOWS_MODES:
        raise ValueError("release-state.txt windows_signing must be signpath or unsigned")
    if state["channel"] not in {"DEVEL", "BETA", "RC", "GA"}:
        raise ValueError("release-state.txt channel must be DEVEL, BETA, RC, or GA")
    if state["channel"] == "BETA" and state["windows_signing"] != "unsigned":
        raise ValueError("BETA release state requires windows_signing=unsigned")
    if not re.fullmatch(r"\d+\.\d+\.\d+", state["version"]):
        raise ValueError("release-state.txt version must be X.Y.Z")
    if state["channel"] in {"BETA", "RC"}:
        if not re.fullmatch(r"[1-9]\d*", state["prerelease"]):
            raise ValueError("BETA and RC release states require a positive prerelease number")
    elif state["prerelease"]:
        raise ValueError("DEVEL and GA release states require an empty prerelease number")
    required_revision = expected_revision or "$Format:%H$"
    if state["revision"].lower() != required_revision.lower():
        raise ValueError(f"release-state.txt revision must be {required_revision}")
    return state


def read_state(root: Path, *, expected_revision: str | None = None) -> dict[str, str]:
    return parse_state(
        (root / "release-state.txt").read_text(encoding="utf-8"),
        expected_revision=expected_revision,
    )


def validate_source(root: Path, version: str) -> dict[str, str]:
    identity = classify(version)
    state = read_state(root)
    errors = []
    if state["version"] != identity["numeric"]:
        errors.append(f"release-state version {state['version']} does not match {identity['numeric']}")
    if state["channel"] != identity["channel"]:
        errors.append(f"release channel {state['channel']} does not match {identity['channel']}")
    if state["prerelease"] != identity["prerelease_number"]:
        errors.append(f"prerelease number {state['prerelease'] or '<empty>'} does not match {identity['prerelease_number'] or '<empty>'}")
    if errors:
        raise ValueError("; ".join(errors))
    identity["windows_signing"] = state["windows_signing"]
    return identity


def validate_archive(path: Path, version: str, commit: str) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", commit):
        raise ValueError("archive commit must be a full lowercase Git object ID")
    names: list[str]
    release_state_contents: str
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            matches = [name for name in names if name.count("/") == 1 and name.endswith("/release-state.txt")]
            if len(matches) != 1:
                raise ValueError("source archive must contain one top-level release-state.txt")
            release_state_contents = archive.read(matches[0]).decode("utf-8")
    else:
        with tarfile.open(path, "r:*") as archive:
            names = archive.getnames()
            matches = [name for name in names if name.count("/") == 1 and name.endswith("/release-state.txt")]
            if len(matches) != 1:
                raise ValueError("source archive must contain one top-level release-state.txt")
            member = archive.extractfile(matches[0])
            if member is None:
                raise ValueError("source archive release-state.txt is not a regular file")
            release_state_contents = member.read().decode("utf-8")
    if any(part == ".git" for name in names for part in Path(name).parts):
        raise ValueError("source archive must not contain .git metadata")
    identity = classify(version)
    state = parse_state(release_state_contents, expected_revision=commit)
    if state["version"] != identity["numeric"] or state["channel"] != identity["channel"] or state["prerelease"] != identity["prerelease_number"]:
        raise ValueError("source archive release identity does not match its tag")


def tarball_groups(path: Path = RELEASE_TARBALLS_FILE) -> list[str]:
    groups = []
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields or fields[0].startswith("#"):
            continue
        if len(fields) < 2:
            raise ValueError(f"release tarball {fields[0]} names no programs in {path.name}")
        if not re.fullmatch(r"[a-z0-9]+", fields[0]) or fields[0] in RESERVED_ASSET_KINDS:
            raise ValueError(f"release tarball name {fields[0]} cannot name a release asset")
        groups.append(fields[0])
    if not groups or len(groups) != len(set(groups)):
        raise ValueError(f"{path.name} must name each release tarball once")
    return groups


def release_tarballs(version: str) -> list[str]:
    return [f"wsjtx-{version}-{target}-{group}" for target in RELEASE_TARGETS for group in tarball_groups()]


def expected_assets(version: str, distribution: bool) -> list[str]:
    mac_suffix = "macOS.pkg" if distribution else "macOS-unsigned.pkg"
    return [
        f"wsjtx-{version}-arm64-{mac_suffix}",
        f"wsjtx-{version}-x86_64-{mac_suffix}",
        f"wsjtx-{version}-linux-x86_64-AppImage",
        f"wsjtx-{version}-linux-aarch64-AppImage",
        *(f"wsjtx-{version}-{target}-AppImage" for target in ARMHF_TARGETS),
        f"wsjtx-{version}-windows-x86_64-installer-signed" if distribution else f"wsjtx-{version}-windows-x86_64-installer",
        *release_tarballs(version),
        *(f"wsjtx-{version}-{target}-{kind}" for target in ARMHF_TARGETS for kind in ("deb", "rpm")),
    ]


def public_expected_assets(version: str, macos_mode: str, windows_mode: str = "signpath") -> list[str]:
    if macos_mode not in MACOS_MODES:
        raise ValueError(f"unsupported macOS release mode: {macos_mode}")
    if windows_mode not in WINDOWS_MODES:
        raise ValueError(f"unsupported Windows release mode: {windows_mode}")
    mac_suffix = "macOS.pkg" if macos_mode == "distribution" else "macOS-unsigned.pkg"
    return [
        f"wsjtx-{version}-arm64-{mac_suffix}",
        f"wsjtx-{version}-x86_64-{mac_suffix}",
        f"wsjtx-{version}-linux-x86_64-AppImage",
        f"wsjtx-{version}-linux-aarch64-AppImage",
        *(f"wsjtx-{version}-{target}-AppImage" for target in ARMHF_TARGETS),
        f"wsjtx-{version}-windows-x86_64-installer{'-signed' if windows_mode == 'signpath' else ''}",
        *release_tarballs(version),
        *(f"wsjtx-{version}-{target}-{kind}" for target in ARMHF_TARGETS for kind in ("deb", "rpm")),
    ]


def asset_suffix(artifact: str) -> str:
    kind = artifact.rsplit("-", 1)[-1]
    if kind in ("deb", "rpm"):
        return f".{kind}"
    if kind in tarball_groups():
        return ".tar.gz"
    return ".pkg" if "macOS" in artifact else ".AppImage" if "linux" in artifact else ".exe"


def collect_asset_files(root: Path, expected: list[str]) -> list[Path]:
    files: list[Path] = []
    for artifact in expected:
        directory = root / artifact
        if not directory.is_dir():
            raise ValueError(f"missing artifact directory: {artifact}")
        suffix = asset_suffix(artifact)
        matches = sorted(path for path in directory.rglob(f"*{suffix}") if path.is_file())
        if len(matches) != 1:
            raise ValueError(f"{artifact} must contain exactly one {suffix} file; found {len(matches)}")
        if matches[0].stat().st_size == 0:
            raise ValueError(f"artifact is empty: {matches[0]}")
        if any(f"-{target}-" in artifact for target in ARMHF_TARGETS):
            filename = artifact if suffix == ".tar.gz" else artifact.rsplit("-", 1)[0]
            if matches[0].name != f"{filename}{suffix}":
                raise ValueError(f"unexpected ARMHF asset filename: {matches[0].name}; expected {filename}{suffix}")
        files.append(matches[0])
    if len({path.name for path in files}) != len(files):
        raise ValueError("release assets must have unique filenames")
    return files


def reject_unqualified_armhf_assets(root: Path, version: str) -> None:
    for path in root.rglob(f"wsjtx-{version}-linux-armhf*"):
        suffix = path.name.removeprefix(f"wsjtx-{version}-linux-armhf")
        if path.is_dir() and suffix in ("-bookworm", "-trixie"):
            continue
        if not suffix.startswith(("-bookworm-", "-bookworm.", "-trixie-", "-trixie.")):
            raise ValueError(f"ARMHF release assets must identify Bookworm or Trixie: {path.name}")


def find_asset_files(root: Path, version: str, distribution: bool) -> list[Path]:
    reject_unqualified_armhf_assets(root, version)
    files = collect_asset_files(root, expected_assets(version, distribution))
    if distribution:
        unsigned = sorted(root.rglob("*-unsigned.pkg"))
        if unsigned:
            raise ValueError(f"unsigned macOS packages cannot be published: {unsigned[0]}")
    return files


def find_public_asset_files(
    root: Path, version: str, macos_mode: str, windows_mode: str = "signpath"
) -> list[Path]:
    reject_unqualified_armhf_assets(root, version)
    expected = public_expected_assets(version, macos_mode, windows_mode)
    files = collect_asset_files(root, expected)
    if macos_mode == "distribution":
        unsigned = sorted(root.rglob("*-unsigned.pkg"))
        if unsigned:
            raise ValueError(f"unsigned macOS packages cannot be published: {unsigned[0]}")
    if windows_mode == "unsigned":
        installer = files[expected.index(f"wsjtx-{version}-windows-x86_64-installer")]
        if installer.name != f"wsjtx-{version}-win64.exe":
            raise ValueError(f"unexpected unsigned Windows installer: {installer.name}")
        if (root / f"wsjtx-{version}-windows-x86_64-installer-signed").exists():
            raise ValueError("signed Windows installer cannot accompany unsigned release")
    return files


def release_files(
    root: Path, version: str, macos_mode: str = "distribution", windows_mode: str = "signpath"
) -> list[Path]:
    files = find_public_asset_files(root, version, macos_mode, windows_mode)
    for arch in ("x86_64", "aarch64"):
        for package_type, suffix in (("deb", ".deb"), ("rpm", ".rpm")):
            directory = root / f"wsjtx-{version}-linux-{arch}-{package_type}"
            if not directory.is_dir():
                raise ValueError(f"missing artifact directory: {directory.name}")
            matches = sorted(path for path in directory.rglob(f"*{suffix}") if path.is_file())
            if len(matches) != 1 or matches[0].stat().st_size == 0:
                raise ValueError(f"{directory.name} must contain exactly one non-empty {suffix} file")
            files.append(matches[0])
    source = root / f"wsjtx-{version}-src.tar.gz"
    if not source.is_file() or source.stat().st_size == 0:
        raise ValueError(f"missing source archive: {source.name}")
    files.append(source)
    names = [path.name for path in files]
    if len(names) != len(set(names)):
        raise ValueError("release assets must have unique filenames")
    return sorted(files, key=lambda item: item.name)


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_single_json(directory: Path) -> dict:
    matches = sorted(directory.glob("*.json"))
    if len(matches) != 1:
        raise ValueError(f"{directory.name} must contain exactly one JSON report")
    return json.loads(matches[0].read_text(encoding="utf-8"))


def verify_signing_reports(
    root: Path, version: str, commit: str, tag: str, macos_mode: str = "distribution",
    windows_mode: str = "signpath",
) -> None:
    if macos_mode not in MACOS_MODES:
        raise ValueError(f"unsupported macOS release mode: {macos_mode}")
    if windows_mode not in WINDOWS_MODES:
        raise ValueError(f"unsupported Windows release mode: {windows_mode}")
    for arch in ("arm64", "x86_64"):
        report = read_single_json(root / f"macos-signing-report-{version}-{arch}")
        if report.get("mode") != macos_mode or report.get("git_sha") != commit:
            raise ValueError(f"macOS {arch} report does not bind {macos_mode} packaging to {commit}")
        if macos_mode == "distribution":
            if report.get("notarization", {}).get("status") != "Accepted":
                raise ValueError(f"macOS {arch} notarization was not accepted")
            if report.get("stapled") is not True or report.get("gatekeeper_accepted") is not True:
                raise ValueError(f"macOS {arch} trust verification is incomplete")
            if not re.fullmatch(r"[A-Z0-9]{10}", report.get("team_id", "")):
                raise ValueError(f"macOS {arch} report has no verified Apple Team ID")
            for field in ("application_certificate_sha1", "installer_certificate_sha1"):
                if not re.fullmatch(r"[0-9A-F]{40}", report.get(field, "")):
                    raise ValueError(f"macOS {arch} report has no verified {field}")
            artifact_dir = f"wsjtx-{version}-{arch}-macOS.pkg"
        else:
            if any(report.get(field) is not False for field in ("signed", "notarized", "publishable")):
                raise ValueError(f"macOS {arch} validation report does not describe an unsigned package")
            artifact_dir = f"wsjtx-{version}-{arch}-macOS-unsigned.pkg"
        package = root / artifact_dir / report.get("artifact", "")
        if not package.is_file() or report.get("sha256") != hash_file(package):
            raise ValueError(f"macOS {arch} report hash does not match its package")

    if windows_mode == "unsigned":
        for suffix in ("windows-signing-request", "windows-signing-verification"):
            if (root / f"wsjtx-{version}-{suffix}").exists():
                raise ValueError(f"Windows signing report cannot accompany unsigned release: {suffix}")
        return

    request = read_single_json(root / f"wsjtx-{version}-windows-signing-request")
    verification = read_single_json(root / f"wsjtx-{version}-windows-signing-verification")
    if request.get("policy") != "release-signing" or request.get("commit") != commit:
        raise ValueError("Windows signing request does not match the release commit and policy")
    if request.get("tag") != tag:
        raise ValueError("Windows signing request does not match the release tag")
    if verification.get("commit") != commit or verification.get("tag") != tag:
        raise ValueError("Windows verification report does not match the release ref")
    if verification.get("status") != "Valid" or not verification.get("timestamp_thumbprint"):
        raise ValueError("Windows signature or timestamp is not trusted")
    if verification.get("identity_verified") is not True:
        raise ValueError("Windows signer identity was not verified against the release allowlist")
    if not verification.get("signer_subject") or not verification.get("signer_thumbprint"):
        raise ValueError("Windows signer identity is missing from the verification report")
    installer = root / f"wsjtx-{version}-windows-x86_64-installer-signed" / verification.get("artifact", "")
    if not installer.is_file() or verification.get("sha256") != hash_file(installer):
        raise ValueError("Windows verification report hash does not match its installer")


def write_manifest(args: argparse.Namespace) -> None:
    root = Path(args.artifacts)
    digests = {
        "x86_64": args.linux_x86_64_digest,
        "aarch64": args.linux_aarch64_digest,
        "armhf_cross": args.linux_armhf_cross_digest,
        "armhf_runtime": args.linux_armhf_digest,
        "armhf_trixie_cross": args.linux_armhf_trixie_cross_digest,
        "armhf_trixie_runtime": args.linux_armhf_trixie_digest,
    }
    for arch, digest in digests.items():
        if not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
            raise ValueError(f"Linux {arch} builder digest is not an immutable sha256 digest")
    beta = classify(args.version)["channel"] == "BETA"
    if beta and args.macos_mode != "validation":
        raise ValueError("BETA releases publish the validated unsigned macOS packages")
    files = release_files(root, args.version, args.macos_mode, args.windows_mode)
    replaceable_macos = (
        {f"wsjtx-{args.version}-{arch}-macOS.pkg" for arch in ("arm64", "x86_64")}
        if args.macos_mode == "validation" and not beta
        else set()
    )
    entries = [
        {"name": path.name, "sha256": hash_file(path), "size": path.stat().st_size}
        for path in sorted(files, key=lambda item: item.name)
        if path.name not in replaceable_macos
    ]
    manifest = {
        "schema": 2,
        "repository": args.repository,
        "tag": f"v{args.version}",
        "commit": args.commit,
        "workflow_run": args.run_id,
        "linux_builders": digests,
        "macos_signing": {
            "mode": "unsigned" if beta else "manual" if args.macos_mode == "validation" else "distribution",
            "replaceable_assets": sorted(replaceable_macos),
        },
        "windows_signing": {"mode": args.windows_mode},
        "assets": entries,
    }
    output = root / "release-manifest.json"
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    checksums = root / "SHA256SUMS"
    checksums.write_text(
        "".join(f"{entry['sha256']}  {entry['name']}\n" for entry in entries),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    classify_parser = subparsers.add_parser("classify")
    classify_parser.add_argument("version")
    line_parser = subparsers.add_parser("public-release-line")
    line_parser.add_argument("version")
    line_parser.add_argument("--remote", required=True)
    plan_parser = subparsers.add_parser("plan-public-promotion")
    plan_parser.add_argument("version")
    plan_parser.add_argument("--sha", required=True)
    plan_parser.add_argument("--remote", required=True)
    promote_parser = subparsers.add_parser("promote-public-source")
    promote_parser.add_argument("version")
    promote_parser.add_argument("--sha", required=True)
    promote_parser.add_argument("--remote", required=True)
    promote_parser.add_argument("--expected-refs-digest", required=True)
    environment_parser = subparsers.add_parser("validate-publication-environment")
    environment_parser.add_argument("environment_json")
    environment_parser.add_argument("--channel", choices=sorted(CHANNEL_RANK), required=True)
    environment_parser.add_argument("--deployment-policies")
    state_parser = subparsers.add_parser("read-state")
    state_parser.add_argument("--root", default=".")
    validate_parser = subparsers.add_parser("validate-source")
    validate_parser.add_argument("version")
    validate_parser.add_argument("--root", default=".")
    archive_parser = subparsers.add_parser("validate-archive")
    archive_parser.add_argument("version")
    archive_parser.add_argument("archive")
    archive_parser.add_argument("--commit", required=True)
    assets_parser = subparsers.add_parser("verify-assets")
    assets_parser.add_argument("version")
    assets_parser.add_argument("artifacts")
    assets_parser.add_argument("--distribution", action="store_true")
    assets_parser.add_argument("--macos-mode", choices=MACOS_MODES)
    assets_parser.add_argument("--windows-mode", choices=WINDOWS_MODES, default="signpath")
    files_parser = subparsers.add_parser("release-files")
    files_parser.add_argument("version")
    files_parser.add_argument("artifacts")
    files_parser.add_argument("--macos-mode", choices=MACOS_MODES, default="distribution")
    files_parser.add_argument("--windows-mode", choices=WINDOWS_MODES, default="signpath")
    reports_parser = subparsers.add_parser("verify-signing-reports")
    reports_parser.add_argument("version")
    reports_parser.add_argument("artifacts")
    reports_parser.add_argument("--commit", required=True)
    reports_parser.add_argument("--tag", required=True)
    reports_parser.add_argument("--macos-mode", choices=MACOS_MODES, default="distribution")
    reports_parser.add_argument("--windows-mode", choices=WINDOWS_MODES, default="signpath")
    manifest_parser = subparsers.add_parser("write-manifest")
    manifest_parser.add_argument("version")
    manifest_parser.add_argument("artifacts")
    manifest_parser.add_argument("--repository", required=True)
    manifest_parser.add_argument("--commit", required=True)
    manifest_parser.add_argument("--run-id", required=True)
    manifest_parser.add_argument("--linux-x86-64-digest", required=True)
    manifest_parser.add_argument("--linux-aarch64-digest", required=True)
    manifest_parser.add_argument("--linux-armhf-cross-digest", required=True)
    manifest_parser.add_argument("--linux-armhf-digest", required=True)
    manifest_parser.add_argument("--linux-armhf-trixie-cross-digest", required=True)
    manifest_parser.add_argument("--linux-armhf-trixie-digest", required=True)
    manifest_parser.add_argument("--macos-mode", choices=MACOS_MODES, default="distribution")
    manifest_parser.add_argument("--windows-mode", choices=WINDOWS_MODES, default="signpath")
    args = parser.parse_args()
    try:
        if args.command == "classify":
            print(json.dumps(classify(args.version)))
        elif args.command == "public-release-line":
            print(json.dumps(inspect_public_release_line(args.version, args.remote)))
        elif args.command == "plan-public-promotion":
            print(json.dumps(inspect_public_promotion(args.version, args.sha, args.remote)))
        elif args.command == "promote-public-source":
            print(json.dumps(promote_public_source(
                args.version, args.sha, args.remote, args.expected_refs_digest
            )))
        elif args.command == "validate-publication-environment":
            policies = (
                json.loads(Path(args.deployment_policies).read_text(encoding="utf-8"))
                if args.deployment_policies
                else None
            )
            validate_publication_environment(
                json.loads(Path(args.environment_json).read_text(encoding="utf-8")), args.channel, policies
            )
            if args.channel == "BETA":
                print(f"Validated beta-release: no reviewer, deployments only from {BETA_TAG_PATTERN} tags")
            else:
                print("Validated public-release required-reviewer protection")
        elif args.command == "read-state":
            print(json.dumps(read_state(Path(args.root))))
        elif args.command == "validate-source":
            print(json.dumps(validate_source(Path(args.root), args.version)))
        elif args.command == "validate-archive":
            validate_archive(Path(args.archive), args.version, args.commit)
        elif args.command == "verify-assets":
            files = (
                find_public_asset_files(Path(args.artifacts), args.version, args.macos_mode, args.windows_mode)
                if args.macos_mode
                else find_asset_files(Path(args.artifacts), args.version, args.distribution)
            )
            for path in files:
                print(path)
        elif args.command == "release-files":
            for path in release_files(Path(args.artifacts), args.version, args.macos_mode, args.windows_mode):
                print(path)
        elif args.command == "verify-signing-reports":
            verify_signing_reports(
                Path(args.artifacts), args.version, args.commit, args.tag, args.macos_mode,
                args.windows_mode,
            )
        else:
            write_manifest(args)
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f"release policy error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
