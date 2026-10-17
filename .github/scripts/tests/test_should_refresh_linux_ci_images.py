#!/usr/bin/env python3

import importlib.util
import sys
import unittest
from unittest.mock import Mock
from urllib.error import HTTPError
from datetime import datetime, timedelta, timezone
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "should-refresh-linux-ci-images.py"
SPEC = importlib.util.spec_from_file_location("should_refresh_linux_ci_images", SCRIPT)
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


class RefreshDecisionTests(unittest.TestCase):
    def setUp(self):
        self.now = datetime(2026, 8, 25, tzinfo=timezone.utc)

    def test_keeps_recent_stable_image(self):
        refresh, age_days = MODULE.should_refresh(
            [version(1, 29, ("stable", "build-20260801-1-1"), self.now)],
            now=self.now,
            max_age_days=30,
        )
        self.assertFalse(refresh)
        self.assertEqual(age_days, 29)

    def test_incomplete_cohort_refreshes_even_when_stable_is_recent(self):
        versions = [version(1, 1, ("stable", "build-20260801-1-1"), self.now)]
        for missing in MODULE.COHORT_PACKAGES:
            client = Mock()
            client.list_versions.side_effect = lambda owner, package: [] if package == missing else versions
            with self.subTest(missing=missing):
                self.assertTrue(MODULE.cohort_incomplete(client, versions))
        client.list_versions.side_effect = lambda owner, package: versions
        self.assertFalse(MODULE.cohort_incomplete(client, versions))

    def test_missing_stable_bootstraps_the_cohort(self):
        self.assertTrue(MODULE.cohort_incomplete(Mock(), []))

    def test_unpublished_package_refreshes_but_other_registry_errors_fail(self):
        versions = [version(1, 1, ("stable", "build-20260801-1-1"), self.now)]
        for status in (404, 403, 500):
            error = RuntimeError("registry error")
            error.__cause__ = HTTPError("https://example.invalid", status, "error", {}, None)
            client = Mock()
            client.list_versions.side_effect = error
            with self.subTest(status=status):
                if status == 404:
                    self.assertTrue(MODULE.cohort_incomplete(client, versions))
                else:
                    with self.assertRaises(RuntimeError):
                        MODULE.cohort_incomplete(client, versions)

    def test_refreshes_old_stable_image(self):
        refresh, age_days = MODULE.should_refresh(
            [version(1, 30, ("stable", "build-20260726-1-1"), self.now)],
            now=self.now,
            max_age_days=30,
        )
        self.assertTrue(refresh)
        self.assertEqual(age_days, 30)

    def test_rejects_missing_or_ambiguous_stable_image(self):
        with self.assertRaisesRegex(RuntimeError, "found 0"):
            MODULE.should_refresh([], now=self.now, max_age_days=30)
        with self.assertRaisesRegex(RuntimeError, "found 2"):
            MODULE.should_refresh(
                [
                    version(1, 1, ("stable",), self.now),
                    version(2, 2, ("stable",), self.now),
                ],
                now=self.now,
                max_age_days=30,
            )


if __name__ == "__main__":
    unittest.main()
