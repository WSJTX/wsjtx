#!/usr/bin/env python3
"""Decide whether the promoted Linux CI image is old enough to refresh."""

from __future__ import annotations

import argparse
import os
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parent))
from ghcr_retention import GitHubPackagesClient, Version  # noqa: E402


OWNER = "WSJTX"
PACKAGE = "wsjtx-internal/linux-noble"
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
    refresh, age_days = should_refresh(
        client.list_versions(OWNER, PACKAGE),
        now=datetime.now(timezone.utc),
        max_age_days=args.max_age_days,
    )
    decision = "refresh" if refresh else "keep"
    print(f"Linux CI stable image age: {age_days} days; decision: {decision}")
    return 0 if refresh else 1


if __name__ == "__main__":
    raise SystemExit(main())
