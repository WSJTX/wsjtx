#!/usr/bin/env python3

import importlib.util
import sys
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "ghcr_retention.py"
SPEC = importlib.util.spec_from_file_location("ghcr_retention", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def version(version_id, age_days, tags, now):
    return MODULE.Version(
        id=version_id,
        digest=f"sha256:{version_id:064x}",
        created_at=now - timedelta(days=age_days),
        tags=tuple(tags),
    )


class RetentionPlanTests(unittest.TestCase):
    def setUp(self):
        self.now = datetime(2026, 8, 18, tzinfo=timezone.utc)

    def plan(self, versions, keep=3):
        return {
            item.version.id: item
            for item in MODULE.plan_retention(versions, self.now, 60, keep)
        }

    def test_deletes_only_expired_recognized_versions_outside_keep_set(self):
        versions = [
            version(1, 90, ("build-20260101-1-1",), self.now),
            version(2, 10, ("build-20260808-2-1",), self.now),
            version(3, 20, ("build-20260729-3-1",), self.now),
            version(4, 30, ("build-20260719-4-1",), self.now),
        ]
        plan = self.plan(versions)
        self.assertTrue(plan[1].delete)
        self.assertFalse(plan[2].delete)
        self.assertFalse(plan[3].delete)
        self.assertFalse(plan[4].delete)

    def test_stable_unknown_and_untagged_versions_are_protected(self):
        versions = [
            version(1, 100, ("stable", "build-20260101-1-1"), self.now),
            version(2, 100, ("debug",), self.now),
            version(3, 100, (), self.now),
        ]
        plan = self.plan(versions, keep=1)
        self.assertEqual(plan[1].reason, "stable")
        self.assertEqual(plan[2].reason, "unknown-or-untagged")
        self.assertEqual(plan[3].reason, "unknown-or-untagged")

    def test_exact_sixty_day_boundary_is_kept(self):
        plan = self.plan([version(1, 60, ("candidate-1-1",), self.now)], keep=1)
        self.assertFalse(plan[1].delete)

    def test_equal_timestamps_use_id_as_tiebreaker(self):
        versions = [
            version(1, 90, ("candidate-1-1",), self.now),
            version(2, 90, ("candidate-2-1",), self.now),
        ]
        plan = self.plan(versions, keep=1)
        self.assertTrue(plan[1].delete)
        self.assertFalse(plan[2].delete)

    def test_nested_package_name_is_encoded(self):
        path = MODULE.GitHubPackagesClient.package_path(
            "WSJTX", "wsjtx-internal/linux-noble"
        )
        self.assertEqual(
            path,
            "/orgs/WSJTX/packages/container/wsjtx-internal%2Flinux-noble",
        )

    def test_malformed_version_fails_closed(self):
        with self.assertRaises(ValueError):
            MODULE.parse_version({"id": 1, "name": "sha256:x", "metadata": {}})


class FakeClient:
    def __init__(self, versions, refreshed=None):
        self.versions = versions
        self.refreshed = refreshed or {}
        self.deleted = []

    def list_versions(self, owner, package):
        return list(self.versions)

    def get_version(self, owner, package, version_id):
        return self.refreshed.get(
            version_id,
            next(item for item in self.versions if item.id == version_id),
        )

    def delete_version(self, owner, package, version_id):
        self.deleted.append(version_id)


class RetentionApplicationTests(unittest.TestCase):
    def setUp(self):
        self.now = datetime(2026, 8, 18, tzinfo=timezone.utc)
        self.versions = [
            version(1, 100, ("candidate-1-1",), self.now),
            version(2, 30, ("candidate-2-1",), self.now),
        ]

    def run_process(self, client, apply):
        return MODULE.process_package(
            client,
            "WSJTX",
            "wsjtx-internal/linux-noble",
            self.now,
            60,
            1,
            apply,
        )

    def test_dry_run_never_deletes(self):
        client = FakeClient(self.versions)
        self.assertEqual(self.run_process(client, False), 0)
        self.assertEqual(client.deleted, [])

    def test_apply_deletes_planned_version(self):
        client = FakeClient(self.versions)
        self.assertEqual(self.run_process(client, True), 1)
        self.assertEqual(client.deleted, [1])

    def test_refetch_protects_new_stable_tag(self):
        stable = version(1, 100, ("stable", "candidate-1-1"), self.now)
        client = FakeClient(self.versions, {1: stable})
        self.assertEqual(self.run_process(client, True), 0)
        self.assertEqual(client.deleted, [])


if __name__ == "__main__":
    unittest.main()
