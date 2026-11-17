#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

import contextlib
import importlib.util
import io
import json
import re
import sys
import tempfile
import unittest
import urllib.error
import urllib.parse
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).parents[1] / "prune_linux_ccache.py"
SPEC = importlib.util.spec_from_file_location("prune_linux_ccache", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
DEVELOP = "refs/heads/develop"
NOW = datetime(2026, 11, 17, 6, 0, tzinfo=timezone.utc)
GENERATION = "build-20261109-42679434784-1"
OLDER_GENERATION = "build-20261102-42000000000-1"


def sha(number):
    return f"{number:040x}"


def key(configuration, number, generation=GENERATION, refresh=""):
    return f"ccache-linux-{configuration}-{generation}-{sha(number)}{refresh}"


def cache(cache_id, cache_key, age_hours, size=1_000_000_000, ref=DEVELOP):
    return MODULE.Cache(
        id=cache_id,
        key=cache_key,
        ref=ref,
        size_in_bytes=size,
        created_at=NOW - timedelta(hours=age_hours),
    )


def raw(entry):
    return {
        "id": entry.id,
        "ref": entry.ref,
        "key": entry.key,
        "version": "0" * 64,
        "last_accessed_at": entry.created_at.isoformat().replace("+00:00", "Z"),
        "created_at": entry.created_at.isoformat().replace("+00:00", "Z"),
        "size_in_bytes": entry.size_in_bytes,
    }


def plan(caches, ref=DEVELOP):
    return {item.cache.id: item for item in MODULE.plan_prune(caches, ref)}


class GroupingTests(unittest.TestCase):
    def test_architecture_profile_distribution_and_compatibility_stay_distinct(self):
        keys = (
            key("x86_64-gcc13-v2", 1),
            key("x86_64-asan-ubsan-gcc-gcc13-v2", 1),
            key("x86_64-tsan-gcc-gcc13-v2", 1),
            key("aarch64-gcc12-v2", 1),
            key("armhf-bookworm-gcc12-v2", 1),
            key("armhf-trixie-gcc14-v2", 1),
            key("x86_64-gcc14-v2", 1),
        )
        groups = [MODULE.cache_group(item) for item in keys]
        self.assertNotIn(None, groups)
        self.assertEqual(len(set(groups)), len(keys))
        self.assertEqual(
            MODULE.cache_group(key("armhf-trixie-gcc14-v2", 1)),
            f"ccache-linux-armhf-trixie-gcc14-v2-{GENERATION}",
        )

    def test_image_generation_is_part_of_the_group(self):
        current = MODULE.cache_group(key("x86_64-gcc13-v2", 1))
        older = MODULE.cache_group(key("x86_64-gcc13-v2", 1, OLDER_GENERATION))
        self.assertNotEqual(current, older)
        decisions = plan([
            cache(1, key("x86_64-gcc13-v2", 1, OLDER_GENERATION), 30),
            cache(2, key("x86_64-gcc13-v2", 2, GENERATION), 1),
        ])
        self.assertFalse(decisions[1].delete, "the newest entry of an older image generation is kept")
        self.assertFalse(decisions[2].delete)

    def test_refresh_keys_join_the_configuration_they_refresh(self):
        base = MODULE.cache_group(key("x86_64-gcc13-v2", 1))
        refreshed = MODULE.cache_group(key("x86_64-gcc13-v2", 1, refresh="-refresh-19876543210-2"))
        self.assertEqual(base, refreshed)
        decisions = plan([
            cache(1, key("x86_64-gcc13-v2", 1), 5),
            cache(2, key("x86_64-gcc13-v2", 1, refresh="-refresh-19876543210-2"), 1),
        ])
        self.assertTrue(decisions[1].delete)
        self.assertFalse(decisions[2].delete)

    def test_unparseable_keys_are_kept(self):
        oddities = [
            "ccache-linux-x86_64-gcc13-v2-build-20261109-1-1-notasha",
            f"ccache-linux-x86_64-gcc13-v2-{GENERATION}-{sha(1)[:39]}",
            f"ccache-linux-x86_64-gcc13-v2-{GENERATION}-{sha(1).upper()}A",
            f"ccache-linux-{sha(1)}",
            f"ccache-macos-arm64-{GENERATION}-{sha(1)}",
            f"ccache-linux-x86_64-gcc13-v2-{GENERATION}-{sha(1)}-refresh-1",
        ]
        caches = [cache(index + 1, item, 100 + index) for index, item in enumerate(oddities)]
        caches.append(cache(99, key("x86_64-gcc13-v2", 2), 1))
        decisions = plan(caches)
        for index, item in enumerate(oddities):
            with self.subTest(key=item):
                self.assertIsNone(MODULE.cache_group(item))
                self.assertFalse(decisions[index + 1].delete)
                self.assertEqual(decisions[index + 1].reason, "unparseable-key")

    def test_other_refs_are_never_deleted(self):
        decisions = plan([
            cache(1, key("x86_64-gcc13-v2", 1), 10, ref="refs/heads/topic"),
            cache(2, key("x86_64-gcc13-v2", 2), 9, ref="refs/heads/topic"),
            cache(3, key("x86_64-gcc13-v2", 3), 1),
        ])
        self.assertEqual(decisions[1].reason, "other-ref")
        self.assertEqual(decisions[2].reason, "other-ref")
        self.assertFalse(decisions[1].delete or decisions[2].delete or decisions[3].delete)

    def test_sanitizer_profiles_match_the_sanitizer_action(self):
        action = (REPOSITORY_ROOT / ".github/actions/build-linux-sanitizers/action.yml").read_text()
        profiles = re.findall(r'echo "cache_key=([^"]+)" >> "\$GITHUB_OUTPUT"', action)
        self.assertTrue(profiles)
        self.assertEqual(sorted(profiles), sorted(MODULE.SANITIZER_PROFILES))


class KeepNewestTests(unittest.TestCase):
    def test_newest_created_entry_of_each_group_is_kept_and_older_ones_deleted(self):
        caches = [
            cache(11, key("x86_64-gcc13-v2", 2), 20),
            cache(12, key("x86_64-gcc13-v2", 3), 1),
            cache(13, key("x86_64-gcc13-v2", 1), 40),
            cache(21, key("armhf-trixie-gcc14-v2", 1), 30),
            cache(22, key("armhf-trixie-gcc14-v2", 2), 2),
            cache(31, key("x86_64-asan-ubsan-gcc-gcc13-v2", 1), 50),
        ]
        decisions = plan(caches)
        self.assertEqual(
            {cache_id for cache_id, item in decisions.items() if not item.delete},
            {12, 22, 31},
        )
        self.assertEqual(
            {cache_id for cache_id, item in decisions.items() if item.delete},
            {11, 13, 21},
        )
        self.assertEqual(decisions[12].reason, "newest-in-group")
        self.assertEqual(decisions[13].reason, "superseded")

    def test_input_order_does_not_matter(self):
        caches = [
            cache(1, key("x86_64-gcc13-v2", 1), 3),
            cache(2, key("x86_64-gcc13-v2", 2), 2),
            cache(3, key("x86_64-gcc13-v2", 3), 1),
        ]
        for ordering in (caches, list(reversed(caches)), [caches[1], caches[2], caches[0]]):
            decisions = plan(ordering)
            self.assertEqual([cache_id for cache_id, item in decisions.items() if not item.delete], [3])

    def test_equal_timestamps_use_id_as_tiebreaker(self):
        decisions = plan([
            cache(1, key("x86_64-gcc13-v2", 1), 5),
            cache(2, key("x86_64-gcc13-v2", 2), 5),
        ])
        self.assertTrue(decisions[1].delete)
        self.assertFalse(decisions[2].delete)

    def test_a_lone_entry_is_never_deleted(self):
        decisions = plan([cache(1, key("aarch64-gcc12-v2", 1), 500)])
        self.assertFalse(decisions[1].delete)

    def test_malformed_cache_entry_fails_closed(self):
        for entry in (
            {"id": "1", "key": "k", "ref": DEVELOP, "size_in_bytes": 1, "created_at": "2026-11-16T00:00:00Z"},
            {"id": 1, "key": "k", "ref": DEVELOP, "size_in_bytes": -1, "created_at": "2026-11-16T00:00:00Z"},
            {"id": 1, "key": "k", "ref": DEVELOP, "size_in_bytes": 1, "created_at": "2026-11-16T00:00:00"},
            {"id": 1, "key": "k", "size_in_bytes": 1, "created_at": "2026-11-16T00:00:00Z"},
        ):
            with self.subTest(entry=entry), self.assertRaises(ValueError):
                MODULE.parse_cache(entry)


class Response(io.BytesIO):
    def __init__(self, status, body=b""):
        super().__init__(body)
        self.status = status


class FakeApi:
    """Stands in for urllib.request.urlopen and records each request."""

    def __init__(self, caches, page_size=100, delete_status=None, list_status=None):
        self.caches = list(caches)
        self.page_size = page_size
        self.delete_status = delete_status or {}
        self.list_status = list_status
        self.requests = []
        self.deleted = []

    def __call__(self, request, timeout):
        url = urllib.parse.urlsplit(request.full_url)
        method = request.get_method()
        self.requests.append((method, url.path, urllib.parse.parse_qs(url.query)))
        if method == "GET":
            if self.list_status:
                raise urllib.error.HTTPError(request.full_url, self.list_status, "error", {}, None)
            page = int(urllib.parse.parse_qs(url.query)["page"][0])
            chunk = self.caches[(page - 1) * self.page_size:page * self.page_size]
            body = {"total_count": len(self.caches), "actions_caches": [raw(item) for item in chunk]}
            return Response(200, json.dumps(body).encode())
        cache_id = int(url.path.rsplit("/", 1)[1])
        status = self.delete_status.get(cache_id, 204)
        if status >= 400:
            raise urllib.error.HTTPError(request.full_url, status, "error", {}, None)
        self.deleted.append(cache_id)
        return Response(status)


def client(api):
    return MODULE.GitHubCachesClient("token", opener=api)


def run_prune(api, apply=False, max_deletions=20, sha_value="", summary=None):
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        status = MODULE.prune(
            client(api), "WSJTX/wsjtx-internal", DEVELOP, apply, max_deletions, sha_value, summary
        )
    return status, output.getvalue()


def three_superseded():
    return [
        cache(1, key("x86_64-gcc13-v2", 4), 1),
        cache(2, key("x86_64-gcc13-v2", 3), 2),
        cache(3, key("x86_64-gcc13-v2", 2), 3),
        cache(4, key("x86_64-gcc13-v2", 1), 4),
    ]


class ClientTests(unittest.TestCase):
    def test_listing_is_paginated_and_filtered_to_linux_ccache_on_the_ref(self):
        caches = [cache(index, key("armhf-trixie-gcc14-v2", index), index) for index in range(1, 131)]
        api = FakeApi(caches)
        listed = client(api).list_caches("WSJTX/wsjtx-internal", DEVELOP)
        self.assertEqual([item.id for item in listed], list(range(1, 131)))
        self.assertEqual(len(api.requests), 2)
        for page, (method, path, query) in enumerate(api.requests, start=1):
            self.assertEqual(method, "GET")
            self.assertEqual(path, "/repos/WSJTX/wsjtx-internal/actions/caches")
            self.assertEqual(query["ref"], [DEVELOP])
            self.assertEqual(query["key"], ["ccache-linux-"])
            self.assertEqual(query["sort"], ["created_at"])
            self.assertEqual(query["direction"], ["desc"])
            self.assertEqual(query["per_page"], ["100"])
            self.assertEqual(query["page"], [str(page)])

    def test_exact_page_boundary_reads_the_empty_next_page(self):
        api = FakeApi([cache(index, key("aarch64-gcc12-v2", index), index) for index in range(1, 101)])
        self.assertEqual(len(client(api).list_caches("WSJTX/wsjtx-internal", DEVELOP)), 100)
        self.assertEqual(len(api.requests), 2)

    def test_delete_404_counts_as_already_gone(self):
        api = FakeApi([], delete_status={7: 404})
        self.assertFalse(client(api).delete_cache("WSJTX/wsjtx-internal", 7))
        self.assertTrue(client(api).delete_cache("WSJTX/wsjtx-internal", 8))
        self.assertEqual(api.requests[0][:2], ("DELETE", "/repos/WSJTX/wsjtx-internal/actions/caches/7"))

    def test_other_delete_errors_raise(self):
        for status in (403, 409, 500):
            with self.subTest(status=status):
                api = FakeApi([], delete_status={7: status})
                with self.assertRaisesRegex(RuntimeError, f"HTTP {status}"):
                    client(api).delete_cache("WSJTX/wsjtx-internal", 7)

    def test_listing_404_is_an_error(self):
        with self.assertRaisesRegex(RuntimeError, "HTTP 404"):
            client(FakeApi([], list_status=404)).list_caches("WSJTX/wsjtx-internal", DEVELOP)


class PruneTests(unittest.TestCase):
    def test_dry_run_never_deletes(self):
        api = FakeApi(three_superseded())
        status, output = run_prune(api)
        self.assertEqual(status, 0)
        self.assertEqual(api.deleted, [])
        self.assertTrue(all(method == "GET" for method, _, _ in api.requests))
        self.assertIn("planned=3 ", output)
        self.assertIn("apply=false", output)

    def test_apply_deletes_every_superseded_entry_and_keeps_the_newest(self):
        api = FakeApi(three_superseded())
        status, output = run_prune(api, apply=True)
        self.assertEqual(status, 0)
        self.assertEqual(sorted(api.deleted), [2, 3, 4])
        self.assertIn("deleted=3 already_gone=0", output)

    def test_apply_tolerates_entries_already_gone(self):
        api = FakeApi(three_superseded(), delete_status={3: 404})
        status, output = run_prune(api, apply=True)
        self.assertEqual(status, 0)
        self.assertEqual(sorted(api.deleted), [2, 4])
        self.assertIn("deleted=2 already_gone=1", output)

    def test_apply_fails_on_other_delete_errors(self):
        api = FakeApi(three_superseded(), delete_status={3: 500})
        with self.assertRaisesRegex(RuntimeError, "HTTP 500"):
            run_prune(api, apply=True)

    def test_max_deletions_guard_refuses_and_deletes_nothing(self):
        api = FakeApi(three_superseded())
        status, output = run_prune(api, apply=True, max_deletions=2)
        self.assertEqual(status, 1)
        self.assertEqual(api.deleted, [])
        self.assertIn("::error::planned 3 deletions exceeds --max-deletions 2", output)
        api = FakeApi(three_superseded())
        status, _ = run_prune(api, apply=True, max_deletions=3)
        self.assertEqual(status, 0)
        self.assertEqual(sorted(api.deleted), [2, 3, 4])

    def test_default_max_deletions_is_twenty(self):
        self.assertEqual(
            MODULE.parse_args(["--repository", "WSJTX/wsjtx-internal", "--ref", DEVELOP]).max_deletions,
            20,
        )
        caches = [cache(index, key("x86_64-gcc13-v2", index), 100 - index) for index in range(1, 23)]
        decisions = MODULE.plan_prune(caches, DEVELOP)
        self.assertIsNotNone(MODULE.guard_violation(decisions, 20))
        self.assertIsNone(MODULE.guard_violation(decisions, 21))

    def test_summary_table_and_totals(self):
        api = FakeApi(three_superseded() + [cache(9, "ccache-linux-unparseable", 9, size=5)])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "summary.md"
            status, _ = run_prune(api, apply=True, summary=str(path))
            text = path.read_text()
        self.assertEqual(status, 0)
        self.assertIn("### Linux ccache pruning", text)
        self.assertIn("mode apply: 3 of 5 entries planned for deletion (3.00 GB); 2 kept (1.00 GB)", text)
        self.assertIn("| Key | Size | Created | Action | Reason |", text)
        self.assertIn(f"| `{key('x86_64-gcc13-v2', 4)}` | 1.00 GB | 2026-11-17T05:00:00Z | keep | newest-in-group |", text)
        self.assertIn(f"| `{key('x86_64-gcc13-v2', 1)}` | 1.00 GB | 2026-11-17T02:00:00Z | deleted | superseded |", text)
        self.assertIn("| `ccache-linux-unparseable` | 0.00 GB | 2026-11-16T21:00:00Z | keep | unparseable-key |", text)

    def test_summary_is_written_when_a_delete_fails(self):
        api = FakeApi(three_superseded(), delete_status={3: 500})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "summary.md"
            with self.assertRaises(RuntimeError):
                run_prune(api, apply=True, summary=str(path))
            text = path.read_text()
        self.assertEqual(api.deleted, [2])
        self.assertIn(f"| `{key('x86_64-gcc13-v2', 3)}` | 1.00 GB | 2026-11-17T04:00:00Z | deleted | superseded |", text)
        self.assertIn(f"| `{key('x86_64-gcc13-v2', 2)}` | 1.00 GB | 2026-11-17T03:00:00Z | failed | superseded |", text)
        self.assertIn(f"| `{key('x86_64-gcc13-v2', 1)}` | 1.00 GB | 2026-11-17T02:00:00Z | not attempted | superseded |", text)

    def test_summary_records_each_entry_outcome(self):
        def actions(api, **arguments):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "summary.md"
                run_prune(api, summary=str(path), **arguments)
                rows = re.findall(r"^\| `[^`]+` \| [^|]+ \| [^|]+ \| ([^|]+) \| [^|]+ \|$", path.read_text(), re.MULTILINE)
            return [row.strip() for row in rows]

        self.assertEqual(actions(FakeApi(three_superseded())), ["keep"] + ["would delete"] * 3)
        self.assertEqual(actions(FakeApi(three_superseded()), apply=True, max_deletions=2), ["keep"] + ["refused"] * 3)
        self.assertEqual(actions(FakeApi(three_superseded(), delete_status={3: 404}), apply=True),
                         ["keep", "deleted", "already gone", "deleted"])

    def test_warns_when_newest_x86_64_entry_lacks_the_run_sha(self):
        caches = [
            cache(1, key("x86_64-gcc13-v2", 1), 3),
            cache(2, key("x86_64-asan-ubsan-gcc-gcc13-v2", 2), 1),
            cache(3, key("armhf-trixie-gcc14-v2", 2), 1),
        ]
        status, output = run_prune(FakeApi(caches), sha_value=sha(2))
        self.assertEqual(status, 0)
        self.assertIn(f"::warning::newest Linux x86_64 ccache entry {key('x86_64-gcc13-v2', 1)} does not contain {sha(2)}; "
                      "the save in this develop CI run's Linux x86_64 job may not have landed", output)
        status, output = run_prune(FakeApi(caches), sha_value=sha(1))
        self.assertNotIn("::warning::", output)
        refreshed = caches + [cache(4, key("x86_64-gcc13-v2", 2, refresh="-refresh-5-1"), 0)]
        _, output = run_prune(FakeApi(refreshed), sha_value=sha(2))
        self.assertNotIn("::warning::", output)
        _, output = run_prune(FakeApi(caches[1:]), sha_value=sha(2))
        self.assertIn("::warning::no Linux x86_64 ccache entry exists", output)
        _, output = run_prune(FakeApi(caches), sha_value="")
        self.assertNotIn("::warning::", output)

    def test_main_rejects_bad_arguments_and_reports_api_errors(self):
        for argv in (
            ["--repository", "WSJTX/wsjtx-internal/x", "--ref", DEVELOP],
            ["--repository", "WSJTX/wsjtx-internal", "--ref", "develop"],
            ["--repository", "WSJTX/wsjtx-internal", "--ref", DEVELOP, "--max-deletions", "-1"],
        ):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(MODULE.main(argv), 2)
        environment = {"GITHUB_TOKEN": "token", "GITHUB_SHA": "", "GITHUB_STEP_SUMMARY": ""}
        output = io.StringIO()
        with mock.patch.dict(MODULE.os.environ, environment), \
                mock.patch.object(MODULE.urllib.request, "urlopen", FakeApi([], list_status=403)), \
                contextlib.redirect_stdout(output):
            status = MODULE.main(["--repository", "WSJTX/wsjtx-internal", "--ref", DEVELOP])
        self.assertEqual(status, 1)
        self.assertIn("::error::Linux ccache pruning failed: GET", output.getvalue())
        self.assertIn("HTTP 403", output.getvalue())

    def test_skip_sha_check_silences_the_warning_for_callers_other_than_develop_ci(self):
        caches = [cache(1, key("x86_64-gcc13-v2", 1), 3)]
        environment = {"GITHUB_TOKEN": "token", "GITHUB_SHA": sha(2), "GITHUB_STEP_SUMMARY": ""}
        for extra, warned in (([], True), (["--skip-sha-check"], False)):
            output = io.StringIO()
            with self.subTest(arguments=extra), mock.patch.dict(MODULE.os.environ, environment), \
                    mock.patch.object(MODULE.urllib.request, "urlopen", FakeApi(caches)), \
                    contextlib.redirect_stdout(output):
                status = MODULE.main(["--repository", "WSJTX/wsjtx-internal", "--ref", DEVELOP] + extra)
            self.assertEqual(status, 0)
            self.assertEqual("::warning::" in output.getvalue(), warned)
        self.assertFalse(MODULE.parse_args(["--repository", "WSJTX/wsjtx-internal", "--ref", DEVELOP]).skip_sha_check)


if __name__ == "__main__":
    unittest.main()
