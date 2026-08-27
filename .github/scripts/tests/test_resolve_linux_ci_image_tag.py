#!/usr/bin/env python3

import importlib.util
import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "resolve-linux-ci-image-tag.py"
SPEC = importlib.util.spec_from_file_location("resolve_linux_ci_image_tag", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def version(version_id, tags):
    return MODULE.Version(
        id=version_id,
        digest=f"sha256:{version_id:064x}",
        created_at=datetime(2026, 8, 18, tzinfo=timezone.utc),
        tags=tuple(tags),
    )


class FakeClient:
    def __init__(self, packages):
        self.packages = packages

    def list_versions(self, owner, package):
        return self.packages.get(package, [])


class ResolveTests(unittest.TestCase):
    def coherent(self):
        tag = "build-20260818-123-1"
        return tag, {
            package: [
                version(
                    index,
                    (tag, "stable")
                    if package in (MODULE.NORMAL_PACKAGE, MODULE.ARMHF_PACKAGE)
                    else (tag,),
                )
            ]
            for index, package in enumerate(
                (MODULE.NORMAL_PACKAGE, *MODULE.ROUTINE_PACKAGES, MODULE.ARMHF_PACKAGE),
                start=1,
            )
        }

    def test_resolves_build_tag_attached_to_stable_pointer(self):
        tag, packages = self.coherent()
        self.assertEqual(MODULE.resolve(FakeClient(packages)), tag)

    def test_rejects_missing_generation_member(self):
        _, packages = self.coherent()
        packages[MODULE.ARM64_PACKAGE] = []
        with self.assertRaisesRegex(RuntimeError, "does not exist"):
            MODULE.resolve(FakeClient(packages))

    def test_strict_resolution_rejects_missing_armhf(self):
        _, packages = self.coherent()
        packages[MODULE.ARMHF_PACKAGE] = []
        with self.assertRaisesRegex(RuntimeError, "does not exist"):
            MODULE.resolve(FakeClient(packages), require_armhf=True)

    def test_routine_resolution_does_not_require_armhf(self):
        tag, packages = self.coherent()
        packages.pop(MODULE.ARMHF_PACKAGE)
        self.assertEqual(MODULE.resolve(FakeClient(packages)), tag)

    def test_armhf_resolution_uses_armhf_stable_pointer(self):
        tag, packages = self.coherent()
        self.assertEqual(MODULE.resolve_armhf(FakeClient(packages)), tag)

    def test_armhf_resolution_requires_armhf_stable_pointer(self):
        _, packages = self.coherent()
        packages[MODULE.ARMHF_PACKAGE] = []
        with self.assertRaisesRegex(RuntimeError, "armhf.*stable is missing"):
            MODULE.resolve_armhf(FakeClient(packages))

    def test_tsan_resolution_uses_tsan_stable_pointer(self):
        tag, packages = self.coherent()
        packages[MODULE.TSAN_PACKAGE] = [version(8, (tag, "stable"))]
        self.assertEqual(MODULE.resolve_tsan(FakeClient(packages)), tag)

    def test_tsan_resolution_requires_tsan_stable_pointer(self):
        _, packages = self.coherent()
        packages[MODULE.TSAN_PACKAGE] = []
        with self.assertRaisesRegex(RuntimeError, "TSan.*stable is missing"):
            MODULE.resolve_tsan(FakeClient(packages))

    def test_rejects_ambiguous_stable_pointer(self):
        _, packages = self.coherent()
        packages[MODULE.NORMAL_PACKAGE].append(version(9, ("stable",)))
        with self.assertRaisesRegex(RuntimeError, "expected one"):
            MODULE.resolve(FakeClient(packages))

    def test_reports_missing_promoted_generation(self):
        _, packages = self.coherent()
        packages[MODULE.NORMAL_PACKAGE] = []
        with self.assertRaisesRegex(RuntimeError, "No promoted Linux CI image generation"):
            MODULE.resolve(FakeClient(packages))

    def test_rejects_stable_without_build_tag(self):
        _, packages = self.coherent()
        packages[MODULE.NORMAL_PACKAGE] = [version(1, ("stable", "candidate-123-1"))]
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            MODULE.resolve(FakeClient(packages))


if __name__ == "__main__":
    unittest.main()
