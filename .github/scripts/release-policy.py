#!/usr/bin/env python3
"""Validate release identity and installer artifact policy."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tarfile
import zipfile
from pathlib import Path


VERSION_RE = re.compile(r"^(?P<numeric>\d+\.\d+\.\d+)(?:-rc(?P<rc>[1-9]\d*))?$")
MACOS_MODES = ("validation", "distribution")
WINDOWS_MODES = ("signpath", "unsigned")


def classify(version: str) -> dict[str, str]:
    match = VERSION_RE.fullmatch(version)
    if not match:
        raise ValueError("version must be X.Y.Z or X.Y.Z-rcN")
    rc = match.group("rc") or ""
    numeric = match.group("numeric")
    major, minor, _ = numeric.split(".")
    return {
        "version": version,
        "numeric": numeric,
        "channel": "RC" if rc else "GA",
        "rc_number": rc,
        "release_branch": f"release/{major}.{minor}",
    }


def parse_state(contents: str, *, expected_revision: str | None = None) -> dict[str, str]:
    state = {}
    for line in contents.splitlines():
        key, separator, value = line.partition("=")
        if not separator or key in state:
            raise ValueError("release-state.txt must contain unique key=value lines")
        state[key] = value
    required_keys = {"version", "channel", "rc", "revision"}
    if not required_keys <= set(state) or set(state) - required_keys - {"windows_signing"}:
        raise ValueError("release-state.txt must define version, channel, rc, revision, and optionally windows_signing")
    state.setdefault("windows_signing", "signpath")
    if state["windows_signing"] not in WINDOWS_MODES:
        raise ValueError("release-state.txt windows_signing must be signpath or unsigned")
    if state["channel"] not in {"DEVEL", "RC", "GA"}:
        raise ValueError("release-state.txt channel must be DEVEL, RC, or GA")
    if not re.fullmatch(r"\d+\.\d+\.\d+", state["version"]):
        raise ValueError("release-state.txt version must be X.Y.Z")
    if state["channel"] == "RC":
        if not re.fullmatch(r"[1-9]\d*", state["rc"]):
            raise ValueError("RC release state requires a positive RC number")
    elif state["rc"]:
        raise ValueError("DEVEL and GA release states require an empty RC number")
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
    if state["rc"] != identity["rc_number"]:
        errors.append(f"RC number {state['rc'] or '<empty>'} does not match {identity['rc_number'] or '<empty>'}")
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
    if state["version"] != identity["numeric"] or state["channel"] != identity["channel"] or state["rc"] != identity["rc_number"]:
        raise ValueError("source archive release identity does not match its tag")


def expected_assets(version: str, distribution: bool) -> list[str]:
    mac_suffix = "macOS.pkg" if distribution else "macOS-unsigned.pkg"
    return [
        f"wsjtx-{version}-arm64-{mac_suffix}",
        f"wsjtx-{version}-x86_64-{mac_suffix}",
        f"wsjtx-{version}-linux-x86_64-AppImage",
        f"wsjtx-{version}-linux-aarch64-AppImage",
        f"wsjtx-{version}-linux-armhf-AppImage",
        f"wsjtx-{version}-windows-x86_64-installer-signed" if distribution else f"wsjtx-{version}-windows-x86_64-installer",
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
        f"wsjtx-{version}-linux-armhf-AppImage",
        f"wsjtx-{version}-windows-x86_64-installer{'-signed' if windows_mode == 'signpath' else ''}",
    ]


def collect_asset_files(root: Path, expected: list[str]) -> list[Path]:
    files: list[Path] = []
    for artifact in expected:
        directory = root / artifact
        if not directory.is_dir():
            raise ValueError(f"missing artifact directory: {artifact}")
        suffix = ".pkg" if "macOS" in artifact else ".AppImage" if "linux" in artifact else ".exe"
        matches = sorted(path for path in directory.rglob(f"*{suffix}") if path.is_file())
        if len(matches) != 1:
            raise ValueError(f"{artifact} must contain exactly one {suffix} file; found {len(matches)}")
        if matches[0].stat().st_size == 0:
            raise ValueError(f"artifact is empty: {matches[0]}")
        files.append(matches[0])
    return files


def find_asset_files(root: Path, version: str, distribution: bool) -> list[Path]:
    files = collect_asset_files(root, expected_assets(version, distribution))
    if distribution:
        unsigned = sorted(root.rglob("*-unsigned.pkg"))
        if unsigned:
            raise ValueError(f"unsigned macOS packages cannot be published: {unsigned[0]}")
    return files


def find_public_asset_files(
    root: Path, version: str, macos_mode: str, windows_mode: str = "signpath"
) -> list[Path]:
    files = collect_asset_files(root, public_expected_assets(version, macos_mode, windows_mode))
    if macos_mode == "distribution":
        unsigned = sorted(root.rglob("*-unsigned.pkg"))
        if unsigned:
            raise ValueError(f"unsigned macOS packages cannot be published: {unsigned[0]}")
    if windows_mode == "unsigned":
        installer = files[-1]
        if installer.name != f"wsjtx-{version}-win64.exe":
            raise ValueError(f"unexpected unsigned Windows installer: {installer.name}")
        if (root / f"wsjtx-{version}-windows-x86_64-installer-signed").exists():
            raise ValueError("signed Windows installer cannot accompany unsigned release")
    return files


def release_files(
    root: Path, version: str, macos_mode: str = "distribution", windows_mode: str = "signpath"
) -> list[Path]:
    files = find_public_asset_files(root, version, macos_mode, windows_mode)
    for arch in ("x86_64", "aarch64", "armhf"):
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
    }
    for arch, digest in digests.items():
        if not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
            raise ValueError(f"Linux {arch} builder digest is not an immutable sha256 digest")
    files = release_files(root, args.version, args.macos_mode, args.windows_mode)
    replaceable_macos = (
        {f"wsjtx-{args.version}-{arch}-macOS.pkg" for arch in ("arm64", "x86_64")}
        if args.macos_mode == "validation"
        else set()
    )
    entries = [
        {"name": path.name, "sha256": hash_file(path), "size": path.stat().st_size}
        for path in sorted(files, key=lambda item: item.name)
        if path.name not in replaceable_macos
    ]
    manifest = {
        "schema": 1,
        "repository": args.repository,
        "tag": f"v{args.version}",
        "commit": args.commit,
        "workflow_run": args.run_id,
        "linux_builders": digests,
        "macos_signing": {
            "mode": "manual" if args.macos_mode == "validation" else "distribution",
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
    manifest_parser.add_argument("--macos-mode", choices=MACOS_MODES, default="distribution")
    manifest_parser.add_argument("--windows-mode", choices=WINDOWS_MODES, default="signpath")
    args = parser.parse_args()
    try:
        if args.command == "classify":
            print(json.dumps(classify(args.version)))
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
