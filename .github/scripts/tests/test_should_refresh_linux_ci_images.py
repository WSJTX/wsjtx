#!/usr/bin/env python3

import importlib.util
import sys
import unittest
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
