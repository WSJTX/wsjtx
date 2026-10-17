#!/usr/bin/env python3
"""Prepare and validate the Linux dependency selection committed with a release."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import urlencode
from urllib.request import Request, urlopen

PUBLIC = "ghcr.io/wsjtx/wsjtx"
PRIVATE = "ghcr.io/wsjtx/wsjtx-internal"
IMAGES = {
    "x86_64": ("linux-noble", "normal-noble", "linux/amd64", "normal", "x86_64"),
    "aarch64": ("linux-arm64-bookworm", "normal-bookworm", "linux/arm64", "normal", "aarch64"),
    "armhf_cross": ("linux-armhf-cross-bookworm", "armhf-cross-bookworm", "linux/amd64", "cross-builder", "armhf"),
    "armhf_runtime": ("linux-armv7-bookworm", "armhf-runtime-bookworm", "linux/arm/v7", "runtime", "armhf"),
    "armhf_trixie_cross": ("linux-armhf-cross-trixie", "armhf-cross-trixie", "linux/amd64", "cross-builder", "armhf"),
    "armhf_trixie_runtime": ("linux-armv7-trixie", "armhf-runtime-trixie", "linux/arm/v7", "runtime", "armhf"),
}
DIGEST = re.compile(r"sha256:[0-9a-f]{64}\Z")
GENERATION = re.compile(r"validated-build-[0-9]{8}-[0-9]+-[0-9]+\Z")


def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, capture_output=True, **kwargs).stdout.strip()


def fingerprint(profile):
    return run(["bash", ".github/scripts/linux-ci-image-fingerprint.sh", profile])


def inspect(reference, anonymous=False, missing_ok=False):
    command = ["skopeo", "inspect"]
    if anonymous:
        command += ["--no-creds"]
    command += ["--raw", "docker://" + reference]
    try:
        manifest = subprocess.run(command, check=True, capture_output=True).stdout
    except subprocess.CalledProcessError as error:
        # Authentication, transport and server errors must not authorize an overwrite.
        if missing_ok and b"manifest unknown" in error.stderr.lower():
            return None
        raise
    return "sha256:" + hashlib.sha256(manifest).hexdigest()


def anonymous_digest(reference):
    repository, digest = reference.removeprefix("ghcr.io/").split("@", 1)
    query = urlencode({"service": "ghcr.io", "scope": f"repository:{repository}:pull"})
    with urlopen("https://ghcr.io/token?" + query, timeout=30) as response:
        token = json.load(response)["token"]
    request = Request(f"https://ghcr.io/v2/{repository}/manifests/{digest}", headers={
        "Authorization": f"Bearer {token}",
        "Accept": ", ".join(("application/vnd.oci.image.manifest.v1+json",
                              "application/vnd.oci.image.index.v1+json",
                              "application/vnd.docker.distribution.manifest.v2+json",
                              "application/vnd.docker.distribution.manifest.list.v2+json")),
    })
    with urlopen(request, timeout=30) as response:
        return "sha256:" + hashlib.sha256(response.read()).hexdigest()


def validate(lock, check_remote=False):
    if not isinstance(lock, dict) or lock.get("schema") != 2:
        raise ValueError("Unsupported Linux image selection schema")
    if not isinstance(lock.get("generation"), str) or not GENERATION.fullmatch(lock["generation"]):
        raise ValueError("Select an explicit validated-build generation")
    if not isinstance(lock.get("images"), dict) or set(lock["images"]) != set(IMAGES):
        raise ValueError("Linux image selection must contain all six release images")
    for key, (package, profile, *_rest) in IMAGES.items():
        image = lock["images"][key]
        if not isinstance(image, dict):
            raise ValueError(f"Malformed image selection: {key}")
        reference = image.get("reference", "")
        prefix = f"{PUBLIC}/{package}@"
        if not isinstance(reference, str) or not reference.startswith(prefix) or not DIGEST.fullmatch(reference[len(prefix):]):
            raise ValueError(f"Expected public digest-pinned image: {key}")
        if image.get("recipe_sha256") != fingerprint(profile):
            raise ValueError(f"Stale recipe for {key}; refresh and prepare the dependency images")
        if check_remote and anonymous_digest(reference) != reference.split("@", 1)[1]:
            raise ValueError(f"Public image digest mismatch: {key}")
    return lock


def provenance(lock_path):
    lock = validate(json.loads(Path(lock_path).read_text()))
    return {
        "lock_sha256": hashlib.sha256(Path(lock_path).read_bytes()).hexdigest(),
        "builders": {key: image["reference"].split("@", 1)[1] for key, image in lock["images"].items()},
    }


def image_manifest(reference, spec, recipe, generation):
    _package, profile, platform, role, arch = spec
    root = str(Path.cwd())
    helper = "verify-linux-ci-image.sh" if role == "normal" else "verify-armhf-ci-image.sh"
    hamlib = run(["bash", "-c", '. .github/scripts/linux-ci-image-config.sh; printf "%s" "$LINUX_HAMLIB_REF"'])
    command = ["docker", "run", "--rm", "--platform", platform, "--network", "none",
               "--mount", f"type=bind,source={root},target=/source,readonly", "--workdir", "/source",
               "-e", f"IMAGE_RECIPE_SHA256={recipe}"]
    if arch == "armhf":
        command += ["-e", f"ARMHF_DISTRO={profile.rsplit('-', 1)[1]}"]
    command.append(reference)
    run(command + ["bash", f".github/scripts/{helper}", role, arch, hamlib])
    raw = run(command + ["cat", "/opt/wsjtx-ci/image.env"])
    manifest = dict(line.split("=", 1) for line in raw.splitlines() if "=" in line)
    if manifest.get("generation") != generation.removeprefix("validated-"):
        raise ValueError(f"Image generation mismatch: {reference}")
    return manifest


def prepare(generation):
    if not GENERATION.fullmatch(generation):
        raise ValueError("Select an explicit validated-build generation")
    lock = {"schema": 2, "generation": generation, "images": {}}
    sources = {}
    manifests = {}
    # Freeze the entire cohort before downloading or copying any image.
    for key, spec in IMAGES.items():
        package, profile, *_rest = spec
        source = f"{PRIVATE}/{package}"
        digest = inspect(f"{source}:{generation}")
        sources[key] = f"{source}@{digest}"
        lock["images"][key] = {"reference": f"{PUBLIC}/{package}@{digest}", "recipe_sha256": fingerprint(profile)}
    for key, spec in IMAGES.items():
        manifests[key] = image_manifest(sources[key], spec, lock["images"][key]["recipe_sha256"], generation)
    for prefix, distro, time_bits in (("armhf", "bookworm", "32"), ("armhf_trixie", "trixie", "64")):
        cross, runtime = manifests[prefix + "_cross"], manifests[prefix + "_runtime"]
        for field in ("generation", "recipe_sha256", "toolchain_id"):
            if not cross.get(field) or runtime.get("cross_" + field) != cross[field]:
                raise ValueError(f"ARMHF {distro} image pair mismatch: {field}")
        for field, expected in (("distro", distro), ("time_bits", time_bits)):
            if cross.get(field) != expected or runtime.get(field) != expected:
                raise ValueError(f"ARMHF {distro} image pair mismatch: {field}")
        if not cross.get("runtime_sha256") or runtime.get("runtime_sha256") != cross["runtime_sha256"]:
            raise ValueError(f"ARMHF {distro} image pair mismatch: runtime_sha256")
    for key, spec in IMAGES.items():
        destination = f"{PUBLIC}/{spec[0]}:release-deps-{generation}"
        digest = sources[key].split("@", 1)[1]
        existing = inspect(destination, missing_ok=True)
        if existing is not None and existing != digest:
            raise ValueError(f"Conflicting retained image tag: {destination}")
        if existing is None:
            run(["skopeo", "copy", "--all", "--preserve-digests", "docker://" + sources[key], "docker://" + destination])
        if inspect(destination, anonymous=True) != digest:
            raise ValueError(f"Public image digest mismatch: {destination}")
    validate(lock, check_remote=True)
    return lock, manifests


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("validate")
    check.add_argument("lock")
    check.add_argument("--check-remote", action="store_true")
    check.add_argument("--github-output")
    binding = commands.add_parser("provenance")
    binding.add_argument("lock")
    verify = commands.add_parser("verify-provenance")
    verify.add_argument("lock")
    verify.add_argument("provenance")
    preparation = commands.add_parser("prepare")
    preparation.add_argument("generation")
    preparation.add_argument("--output-directory", required=True)
    args = parser.parse_args()
    if args.command == "prepare":
        output = Path(args.output_directory)
        if output.exists():
            raise ValueError("Preparation output directory must not already exist")
        lock, manifests = prepare(args.generation)
        output.mkdir(parents=True)
        (output / "release-linux-images.json").write_text(json.dumps(lock, indent=2) + "\n")
        record = {"source_sha": run(["git", "rev-parse", "HEAD"]), "run_url": os.environ.get("PREPARATION_RUN_URL"), "manifests": manifests}
        (output / "preparation-record.json").write_text(json.dumps(record, indent=2) + "\n")
    elif args.command == "provenance":
        print(json.dumps(provenance(args.lock)))
    elif args.command == "verify-provenance":
        if json.loads(Path(args.provenance).read_text()).get("linux_images") != provenance(args.lock):
            raise ValueError("Candidate Linux image provenance does not match the committed selection")
    else:
        lock = validate(json.loads(Path(args.lock).read_text()), args.check_remote)
        bound = provenance(args.lock)
        outputs = {"image_tag": lock["generation"], "lock_sha256": bound["lock_sha256"]}
        for key, digest in bound["builders"].items():
            outputs[key.removesuffix("_runtime") + "_digest"] = digest
        if args.github_output:
            with open(args.github_output, "a") as target:
                target.write("".join(f"{key}={value}\n" for key, value in outputs.items()))
        if os.environ.get("GITHUB_STEP_SUMMARY"):
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
                summary.write("## Linux release dependencies\n\n")
                summary.write(f"Generation: `{lock['generation']}`. All recipe fingerprints match.\n\n")
                for key, image in lock["images"].items():
                    summary.write(f"- {key}: `{image['reference']}`\n")
        print(json.dumps(bound))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        if isinstance(detail, bytes):
            detail = detail.decode("utf-8", errors="replace")
        print(f"Linux release dependencies: {detail}", file=sys.stderr)
        sys.exit(1)
