#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep one GitHub Actions cache entry per Linux ccache configuration on a ref."""

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
from datetime import datetime, timezone
from typing import Callable, Iterable


API_VERSION = "2022-11-28"
KEY_PREFIX = "ccache-linux-"
PER_PAGE = 100
MAX_PAGES = 100
# Everything before the source SHA identifies one configuration: architecture
# (and armhf distribution), sanitizer profile, compiler compatibility and image
# generation. Per-attempt refresh keys belong to the configuration they refresh.
CACHE_KEY = re.compile(
    r"^(?P<group>ccache-linux-.+)-[0-9a-f]{40}(?:-refresh-\d+-\d+)?$"
)
# The cache_key values of .github/actions/build-linux-sanitizers/action.yml;
# their entries share the ccache-linux-x86_64- prefix with the normal build.
SANITIZER_PROFILES = ("asan-ubsan-gcc", "tsan-gcc")
REPOSITORY = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")


@dataclass(frozen=True)
class Cache:
    id: int
    key: str
    ref: str
    size_in_bytes: int
    created_at: datetime


@dataclass(frozen=True)
class Decision:
    cache: Cache
    group: str | None
    delete: bool
    reason: str


def parse_timestamp(value: object) -> datetime:
    if not isinstance(value, str):
        raise ValueError("created_at must be a string")
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("created_at must include a timezone")
    return parsed.astimezone(timezone.utc)


def parse_cache(raw: object) -> Cache:
    if not isinstance(raw, dict):
        raise ValueError("cache entry must be an object")
    cache_id = raw.get("id")
    key = raw.get("key")
    ref = raw.get("ref")
    size = raw.get("size_in_bytes")
    if not isinstance(cache_id, int) or not isinstance(key, str) or not isinstance(ref, str):
        raise ValueError("cache entry requires integer id and string key and ref")
    if not isinstance(size, int) or size < 0:
        raise ValueError(f"cache entry {cache_id} has an invalid size")
    return Cache(
        id=cache_id,
        key=key,
        ref=ref,
        size_in_bytes=size,
        created_at=parse_timestamp(raw.get("created_at")),
    )


def cache_group(key: str) -> str | None:
    match = CACHE_KEY.fullmatch(key)
    return match.group("group") if match else None


def is_x86_64_build_group(group: str | None) -> bool:
    prefix = f"{KEY_PREFIX}x86_64-"
    if group is None or not group.startswith(prefix):
        return False
    profile = group[len(prefix):]
    return not any(profile.startswith(f"{name}-") for name in SANITIZER_PROFILES)


def plan_prune(caches: Iterable[Cache], ref: str) -> list[Decision]:
    ordered = sorted(caches, key=lambda item: (item.created_at, item.id), reverse=True)
    newest: set[str] = set()
    decisions: list[Decision] = []
    for cache in ordered:
        group = cache_group(cache.key)
        if cache.ref != ref:
            decisions.append(Decision(cache, group, False, "other-ref"))
        elif group is None:
            decisions.append(Decision(cache, None, False, "unparseable-key"))
        elif group not in newest:
            newest.add(group)
            decisions.append(Decision(cache, group, False, "newest-in-group"))
        else:
            decisions.append(Decision(cache, group, True, "superseded"))
    return decisions


def guard_violation(decisions: Iterable[Decision], max_deletions: int) -> str | None:
    planned = sum(1 for decision in decisions if decision.delete)
    if planned > max_deletions:
        return (
            f"planned {planned} deletions exceeds --max-deletions {max_deletions}; "
            "nothing was deleted"
        )
    return None


def sha_warning(decisions: Iterable[Decision], sha: str) -> str | None:
    if not sha:
        return None
    builds = [item for item in decisions if is_x86_64_build_group(item.group)]
    if not builds:
        return (
            "no Linux x86_64 ccache entry exists; expected the one this develop CI "
            f"run's Linux x86_64 job saves for {sha}"
        )
    latest = max(builds, key=lambda item: (item.cache.created_at, item.cache.id))
    if sha not in latest.cache.key:
        return (
            f"newest Linux x86_64 ccache entry {latest.cache.key} "
            f"does not contain {sha}; the save in this develop CI run's Linux x86_64 job "
            "may not have landed"
        )
    return None


class CacheMissing(Exception):
    """A cache entry that was already gone when deleted."""


class GitHubCachesClient:
    def __init__(
        self,
        token: str,
        api_url: str = "https://api.github.com",
        opener: Callable[..., object] | None = None,
    ) -> None:
        self.api_url = api_url.rstrip("/")
        self.opener = opener or urllib.request.urlopen
        self.headers = {
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": API_VERSION,
            "User-Agent": "wsjtx-prune-linux-ccache",
        }

    def request(self, method: str, path: str) -> object | None:
        request = urllib.request.Request(
            f"{self.api_url}{path}", headers=self.headers, method=method
        )
        try:
            with self.opener(request, timeout=30) as response:
                if method == "DELETE":
                    if response.status != 204:
                        raise RuntimeError(f"DELETE {path} returned HTTP {response.status}")
                    return None
                return json.load(response)
        except urllib.error.HTTPError as error:
            if method == "DELETE" and error.code == 404:
                raise CacheMissing(path) from error
            headers = error.headers or {}
            request_id = headers.get("x-github-request-id", "unknown")
            raise RuntimeError(
                f"{method} {path} failed with HTTP {error.code} "
                f"(GitHub request {request_id})"
            ) from error

    def list_caches(self, repository: str, ref: str) -> list[Cache]:
        caches: list[Cache] = []
        for page in range(1, MAX_PAGES + 1):
            query = urllib.parse.urlencode({
                "ref": ref,
                "key": KEY_PREFIX,
                "sort": "created_at",
                "direction": "desc",
                "per_page": PER_PAGE,
                "page": page,
            })
            raw = self.request("GET", f"/repos/{repository}/actions/caches?{query}")
            entries = raw.get("actions_caches") if isinstance(raw, dict) else None
            if not isinstance(entries, list):
                raise RuntimeError(f"GitHub returned no cache list for {repository}")
            caches.extend(parse_cache(item) for item in entries)
            if len(entries) < PER_PAGE:
                return caches
        raise RuntimeError(f"more than {MAX_PAGES} pages of caches for {repository}")

    def delete_cache(self, repository: str, cache_id: int) -> bool:
        """Delete one cache entry; False when it was already gone."""
        try:
            self.request("DELETE", f"/repos/{repository}/actions/caches/{cache_id}")
        except CacheMissing:
            return False
        return True


def gigabytes(size: int) -> str:
    return f"{size / 1e9:.2f} GB"


def summary_markdown(
    decisions: list[Decision],
    ref: str,
    apply: bool,
    outcomes: dict[int, str],
    notes: list[str],
) -> str:
    planned = [item for item in decisions if item.delete]
    kept = [item for item in decisions if not item.delete]
    lines = [
        "### Linux ccache pruning\n",
        f"Ref `{ref}`, mode {'apply' if apply else 'dry run'}: "
        f"{len(planned)} of {len(decisions)} entries planned for deletion "
        f"({gigabytes(sum(item.cache.size_in_bytes for item in planned))}); "
        f"{len(kept)} kept ({gigabytes(sum(item.cache.size_in_bytes for item in kept))}).\n",
    ]
    lines.extend(f"> {note}\n" for note in notes)
    lines.append("| Key | Size | Created | Action | Reason |")
    lines.append("| --- | ---: | --- | --- | --- |")
    for item in decisions:
        # A planned deletion has no outcome only when an earlier delete failed.
        action = outcomes.get(item.cache.id, "not attempted" if item.delete else "keep")
        lines.append(
            f"| `{item.cache.key}` | {gigabytes(item.cache.size_in_bytes)} | "
            f"{item.cache.created_at.strftime('%Y-%m-%dT%H:%M:%SZ')} | {action} | {item.reason} |"
        )
    return "\n".join(lines) + "\n"


def prune(
    client: GitHubCachesClient,
    repository: str,
    ref: str,
    apply: bool,
    max_deletions: int,
    sha: str = "",
    summary_path: str | None = None,
) -> int:
    decisions = plan_prune(client.list_caches(repository, ref), ref)
    for item in decisions:
        print(
            f"id={item.cache.id} key={item.cache.key} size={item.cache.size_in_bytes} "
            f"created={item.cache.created_at.isoformat()} "
            f"action={'delete' if item.delete else 'keep'} reason={item.reason}"
        )
    notes: list[str] = []
    warning = sha_warning(decisions, sha)
    if warning:
        print(f"::warning::{warning}")
        notes.append(f"Warning: {warning}")
    refusal = guard_violation(decisions, max_deletions)
    outcomes: dict[int, str] = {}
    deleted = missing = 0
    if refusal:
        print(f"::error::{refusal}")
        notes.append(f"Refused: {refusal}")
    try:
        for item in decisions:
            if not item.delete:
                continue
            if refusal:
                outcomes[item.cache.id] = "refused"
            elif not apply:
                outcomes[item.cache.id] = "would delete"
            else:
                # Stays "failed" when the delete raises.
                outcomes[item.cache.id] = "failed"
                if client.delete_cache(repository, item.cache.id):
                    deleted += 1
                    outcomes[item.cache.id] = "deleted"
                else:
                    missing += 1
                    outcomes[item.cache.id] = "already gone"
    finally:
        if summary_path:
            with open(summary_path, "a", encoding="utf-8") as summary:
                summary.write(summary_markdown(decisions, ref, apply, outcomes, notes))
    planned = [item for item in decisions if item.delete]
    print(
        f"planned={len(planned)} "
        f"planned_bytes={sum(item.cache.size_in_bytes for item in planned)} "
        f"deleted={deleted} already_gone={missing} apply={str(apply).lower()}"
    )
    return 1 if refusal else 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--max-deletions", type=int, default=20)
    parser.add_argument(
        "--skip-sha-check",
        action="store_true",
        help="do not warn when the newest Linux x86_64 entry lacks GITHUB_SHA "
        "(for callers other than develop CI, such as the cache warmer)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not REPOSITORY.fullmatch(args.repository):
        print("--repository must be owner/name", file=sys.stderr)
        return 2
    if not args.ref.startswith("refs/"):
        print("--ref must be a full ref such as refs/heads/develop", file=sys.stderr)
        return 2
    if args.max_deletions < 0:
        print("--max-deletions must not be negative", file=sys.stderr)
        return 2
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GITHUB_TOKEN is required", file=sys.stderr)
        return 2
    try:
        return prune(
            GitHubCachesClient(token),
            args.repository,
            args.ref,
            args.apply,
            args.max_deletions,
            "" if args.skip_sha_check else os.environ.get("GITHUB_SHA", ""),
            os.environ.get("GITHUB_STEP_SUMMARY"),
        )
    except (RuntimeError, ValueError, OSError) as error:
        print(f"::error::Linux ccache pruning failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
