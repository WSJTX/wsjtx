#!/usr/bin/env python3
"""Plan and apply conservative retention for WSJT-X GHCR build images."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Iterable


API_VERSION = "2022-11-28"
BUILD_TAG = re.compile(
    r"^(?:build-\d{8}-\d+-\d+|validated-build-\d{8}-\d+-\d+|candidate-\d+-\d+)$"
)


@dataclass(frozen=True)
class Version:
    id: int
    digest: str
    created_at: datetime
    tags: tuple[str, ...]


@dataclass(frozen=True)
class Decision:
    version: Version
    delete: bool
    reason: str


def parse_timestamp(value: object) -> datetime:
    if not isinstance(value, str):
        raise ValueError("created_at must be a string")
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("created_at must include a timezone")
    return parsed.astimezone(timezone.utc)


def parse_version(raw: object) -> Version:
    if not isinstance(raw, dict):
        raise ValueError("package version must be an object")
    version_id = raw.get("id")
    digest = raw.get("name")
    metadata = raw.get("metadata")
    if not isinstance(version_id, int) or not isinstance(digest, str):
        raise ValueError("package version requires integer id and string name")
    if not isinstance(metadata, dict) or metadata.get("package_type") != "container":
        raise ValueError(f"package version {version_id} is not a container")
    container = metadata.get("container")
    tags = container.get("tags") if isinstance(container, dict) else None
    if not isinstance(tags, list) or not all(isinstance(tag, str) for tag in tags):
        raise ValueError(f"package version {version_id} has invalid tags")
    return Version(
        id=version_id,
        digest=digest,
        created_at=parse_timestamp(raw.get("created_at")),
        tags=tuple(sorted(tags)),
    )


def is_retirable(version: Version) -> bool:
    return bool(version.tags) and all(
        tag == "stable" or BUILD_TAG.fullmatch(tag) for tag in version.tags
    )


def plan_retention(
    versions: Iterable[Version], now: datetime, max_age_days: int, keep: int
) -> list[Decision]:
    ordered = sorted(versions, key=lambda item: (item.created_at, item.id), reverse=True)
    retirable = [version for version in ordered if is_retirable(version)]
    kept_ids = {version.id for version in retirable[:keep]}
    cutoff = now.astimezone(timezone.utc) - timedelta(days=max_age_days)
    decisions: list[Decision] = []
    for version in ordered:
        if "stable" in version.tags:
            decisions.append(Decision(version, False, "stable"))
        elif not is_retirable(version):
            decisions.append(Decision(version, False, "unknown-or-untagged"))
        elif version.id in kept_ids:
            decisions.append(Decision(version, False, "newest-generation"))
        elif version.created_at >= cutoff:
            decisions.append(Decision(version, False, "within-retention-window"))
        else:
            decisions.append(Decision(version, True, "expired"))
    return decisions


class GitHubPackagesClient:
    def __init__(self, token: str, api_url: str = "https://api.github.com") -> None:
        self.api_url = api_url.rstrip("/")
        self.headers = {
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": API_VERSION,
            "User-Agent": "wsjtx-ghcr-retention",
        }

    def request(self, method: str, path: str) -> object | None:
        request = urllib.request.Request(
            f"{self.api_url}{path}", headers=self.headers, method=method
        )
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                if method == "DELETE":
                    if response.status != 204:
                        raise RuntimeError(f"DELETE {path} returned HTTP {response.status}")
                    return None
                return json.load(response)
        except urllib.error.HTTPError as error:
            request_id = error.headers.get("x-github-request-id", "unknown")
            raise RuntimeError(
                f"{method} {path} failed with HTTP {error.code} "
                f"(GitHub request {request_id})"
            ) from error

    @staticmethod
    def package_path(owner: str, package: str) -> str:
        encoded = urllib.parse.quote(package, safe="")
        return f"/orgs/{owner}/packages/container/{encoded}"

    def list_versions(self, owner: str, package: str) -> list[Version]:
        base = self.package_path(owner, package)
        versions: list[Version] = []
        page = 1
        while True:
            raw = self.request(
                "GET", f"{base}/versions?state=active&per_page=100&page={page}"
            )
            if not isinstance(raw, list):
                raise RuntimeError(f"GitHub returned a non-list for package {package}")
            versions.extend(parse_version(item) for item in raw)
            if len(raw) < 100:
                return versions
            page += 1

    def get_version(self, owner: str, package: str, version_id: int) -> Version:
        raw = self.request(
            "GET", f"{self.package_path(owner, package)}/versions/{version_id}"
        )
        return parse_version(raw)

    def delete_version(self, owner: str, package: str, version_id: int) -> None:
        self.request(
            "DELETE", f"{self.package_path(owner, package)}/versions/{version_id}"
        )


def process_package(
    client: GitHubPackagesClient,
    owner: str,
    package: str,
    now: datetime,
    max_age_days: int,
    keep: int,
    apply: bool,
) -> int:
    decisions = plan_retention(client.list_versions(owner, package), now, max_age_days, keep)
    deleted = 0
    for decision in decisions:
        version = decision.version
        action = "delete" if decision.delete else "keep"
        print(
            f"{package} id={version.id} digest={version.digest} "
            f"created={version.created_at.isoformat()} tags={','.join(version.tags) or '-'} "
            f"action={action} reason={decision.reason}"
        )
        if not apply or not decision.delete:
            continue
        refreshed = client.get_version(owner, package, version.id)
        refreshed_decision = next(
            item
            for item in plan_retention([refreshed], now, max_age_days, 0)
            if item.version.id == version.id
        )
        if not refreshed_decision.delete:
            print(
                f"{package} id={version.id} action=keep "
                f"reason=changed-before-delete-{refreshed_decision.reason}"
            )
            continue
        client.delete_version(owner, package, version.id)
        deleted += 1
    return deleted


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--owner", required=True)
    parser.add_argument("--package", action="append", required=True)
    parser.add_argument("--max-age-days", type=int, default=60)
    parser.add_argument("--keep", type=int, default=3)
    parser.add_argument("--apply", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.max_age_days < 1 or args.keep < 1:
        print("max-age-days and keep must be positive", file=sys.stderr)
        return 2
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GITHUB_TOKEN is required", file=sys.stderr)
        return 2
    client = GitHubPackagesClient(token)
    now = datetime.now(timezone.utc)
    deleted = 0
    for package in args.package:
        deleted += process_package(
            client,
            args.owner,
            package,
            now,
            args.max_age_days,
            args.keep,
            args.apply,
        )
    print(f"total_deleted={deleted} apply={str(args.apply).lower()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
