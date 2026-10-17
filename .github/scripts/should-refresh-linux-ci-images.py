#!/usr/bin/env python3
"""Decide whether the promoted Linux CI image is old enough to refresh."""

from __future__ import annotations

import argparse
import os
import re
import sys
from urllib.error import HTTPError
from datetime import datetime, timedelta, timezone
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parent))
from ghcr_retention import GitHubPackagesClient, Version  # noqa: E402


OWNER = "WSJTX"
PACKAGE = "wsjtx-internal/linux-noble"
COHORT_PACKAGES = (
    "wsjtx-internal/linux-arm64-bookworm",
    "wsjtx-internal/linux-armhf-cross-bookworm", "wsjtx-internal/linux-armv7-bookworm",
    "wsjtx-internal/linux-armhf-cross-trixie", "wsjtx-internal/linux-armv7-trixie",
)
# Stable is a promoted pointer; age-gate scheduled refreshes to avoid image churn.


def stable_version(versions: list[Version]) -> Version:
    stable = [version for version in versions if "stable" in version.tags]
    if len(stable) != 1:
        raise RuntimeError(f"expected one stable Linux CI image, found {len(stable)}")
    return stable[0]


def should_refresh(
    versions: list[Version], *, now: datetime, max_age_days: int
) -> tuple[bool, int]:
    version = stable_version(versions)
    age = now.astimezone(timezone.utc) - version.created_at
    age_days = max(0, age.days)
    return age >= timedelta(days=max_age_days), age_days


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--max-age-days", type=int, default=30)
    args = parser.parse_args()
    if args.max_age_days < 1:
        parser.error("--max-age-days must be positive")

    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GITHUB_TOKEN is required", file=sys.stderr)
        return 2

    client = GitHubPackagesClient(token)
    versions = client.list_versions(OWNER, PACKAGE)
    if cohort_incomplete(client, versions):
        print("Linux CI stable image cohort is incomplete; decision: refresh")
        return 0
    refresh, age_days = should_refresh(
        versions,
        now=datetime.now(timezone.utc),
        max_age_days=args.max_age_days,
    )
    decision = "refresh" if refresh else "keep"
    print(f"Linux CI stable image age: {age_days} days; decision: {decision}")
    return 0 if refresh else 1


def cohort_incomplete(client, versions: list[Version]) -> bool:
    if not any("stable" in version.tags for version in versions):
        return True
    generation = [tag for tag in stable_version(versions).tags if re.fullmatch(r"build-\d{8}-\d+-\d+", tag)]
    if len(generation) != 1:
        return True
    for package in COHORT_PACKAGES:
        try:
            members = client.list_versions(OWNER, package)
        except RuntimeError as error:
            if isinstance(error.__cause__, HTTPError) and error.__cause__.code == 404:
                return True
            raise
        if not any(generation[0] in version.tags for version in members):
            return True
    return False


if __name__ == "__main__":
    raise SystemExit(main())
