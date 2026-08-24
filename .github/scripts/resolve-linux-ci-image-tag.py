#!/usr/bin/env python3
"""Resolve the coherent immutable generation selected by linux-noble:stable."""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parent))
from ghcr_retention import GitHubPackagesClient, Version  # noqa: E402, F401


OWNER = "WSJTX"
NORMAL_PACKAGE = "wsjtx-internal/linux-noble"
TSAN_PACKAGE = "wsjtx-internal/linux-tsan-noble"
ARM64_PACKAGE = "wsjtx-internal/linux-arm64-bookworm"
ARMHF_PACKAGE = "wsjtx-internal/linux-armv7-bookworm"
ROUTINE_PACKAGES = (ARM64_PACKAGE,)
BUILD_TAG = re.compile(r"^build-\d{8}-\d+-\d+$")


def resolve(client: GitHubPackagesClient, *, require_armhf: bool = False) -> str:
    versions = client.list_versions(OWNER, NORMAL_PACKAGE)
    stable = [version for version in versions if "stable" in version.tags]
    if not stable:
        raise RuntimeError(
            "No promoted Linux CI image generation is available: "
            "linux-noble:stable is missing. Bootstrap the GHCR images with "
            "warm-dependency-caches.yml before running Linux CI."
        )
    if len(stable) != 1:
        raise RuntimeError(f"expected one linux-noble:stable version, found {len(stable)}")
    build_tags = [tag for tag in stable[0].tags if BUILD_TAG.fullmatch(tag)]
    if len(build_tags) != 1:
        raise RuntimeError(
            "linux-noble:stable must share a version with exactly one immutable build tag"
        )
    generation = build_tags[0]
    required_packages = ROUTINE_PACKAGES + ((ARMHF_PACKAGE,) if require_armhf else ())
    for package in required_packages:
        if not any(generation in version.tags for version in client.list_versions(OWNER, package)):
            raise RuntimeError(f"{package}:{generation} does not exist")
    return generation


def resolve_armhf(client: GitHubPackagesClient) -> str:
    versions = client.list_versions(OWNER, ARMHF_PACKAGE)
    stable = [version for version in versions if "stable" in version.tags]
    if not stable:
        raise RuntimeError(
            "No promoted armhf Linux CI image generation is available: "
            "linux-armv7-bookworm:stable is missing. Publish an armhf image "
            "with include_armhf=true before running the armhf build."
        )
    if len(stable) != 1:
        raise RuntimeError(
            "expected one linux-armv7-bookworm:stable version, "
            f"found {len(stable)}"
        )
    build_tags = [tag for tag in stable[0].tags if BUILD_TAG.fullmatch(tag)]
    if len(build_tags) != 1:
        raise RuntimeError(
            "linux-armv7-bookworm:stable must share a version with exactly "
            "one immutable build tag"
        )
    return build_tags[0]


def resolve_tsan(client: GitHubPackagesClient) -> str:
    versions = client.list_versions(OWNER, TSAN_PACKAGE)
    stable = [version for version in versions if "stable" in version.tags]
    if not stable:
        raise RuntimeError(
            "No promoted TSan Linux CI image generation is available: "
            "linux-tsan-noble:stable is missing. Publish an image with "
            "include_tsan=true before running the TSan workflow."
        )
    if len(stable) != 1:
        raise RuntimeError(
            "expected one linux-tsan-noble:stable version, "
            f"found {len(stable)}"
        )
    build_tags = [tag for tag in stable[0].tags if BUILD_TAG.fullmatch(tag)]
    if len(build_tags) != 1:
        raise RuntimeError(
            "linux-tsan-noble:stable must share a version with exactly one "
            "immutable build tag"
        )
    return build_tags[0]


def main() -> int:
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GITHUB_TOKEN is required", file=sys.stderr)
        return 2
    client = GitHubPackagesClient(token)
    if os.environ.get("RESOLVE_ARMHF") == "true":
        generation = resolve_armhf(client)
    elif os.environ.get("RESOLVE_TSAN") == "true":
        generation = resolve_tsan(client)
    else:
        require_armhf = os.environ.get("REQUIRE_ARMHF") == "true"
        generation = resolve(client, require_armhf=require_armhf)
    print(f"Resolved Linux CI image generation: {generation}")
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"image_tag={generation}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
