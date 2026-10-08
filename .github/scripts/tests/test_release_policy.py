import importlib.util
import json
import re
import io
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "release-policy.py"
SPEC = importlib.util.spec_from_file_location("release_policy", SCRIPT)
release_policy = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(release_policy)


class ReleasePolicyTest(unittest.TestCase):
    def test_candidate_and_public_inventories_require_both_macos_architectures(self):
        version = "3.2.0-rc1"
        for distribution in (False, True):
            mode = "distribution" if distribution else "validation"
            suffix = "macOS.pkg" if distribution else "macOS-unsigned.pkg"
            required = {f"wsjtx-{version}-{arch}-{suffix}" for arch in ("arm64", "x86_64")}
            for inventory in (
                release_policy.expected_assets(version, distribution),
                release_policy.public_expected_assets(version, mode),
            ):
                with self.subTest(mode=mode, inventory=inventory):
                    self.assertEqual({name for name in inventory if name.endswith(suffix)}, required)

    def test_newest_ga_line_uses_immutable_ga_tags_not_release_order_or_rc_line(self):
        refs = {
            "refs/tags/v2.7.0": "a" * 40,
            "refs/tags/v3.0.2": "b" * 40,
            "refs/tags/v3.2.0-rc1": "c" * 40,
        }
        newest = release_policy.public_release_line_policy("3.2.0", refs)
        latest_ga_patch = release_policy.public_release_line_policy("3.0.3", refs)
        self.assertEqual(newest["newest_ga_line"], "3.0")
        self.assertTrue(newest["ga_line_is_newest"])
        self.assertTrue(newest["legacy_tag_only"])
        self.assertTrue(latest_ga_patch["ga_line_is_newest"])

    def test_public_release_line_rejects_missing_or_ambiguous_history(self):
        with self.assertRaisesRegex(ValueError, "no immutable public vX.Y.Z GA tags"):
            release_policy.public_release_line_policy("3.2.0", {})
        with self.assertRaisesRegex(ValueError, "no immutable public vX.Y.Z GA tags"):
            release_policy.public_release_line_policy("3.2.0", {
                "refs/tags/v3.2.0-rc1": "a" * 40,
                "refs/tags/v3.3.0-beta1": "c" * 40,
                "refs/heads/release/3.2": "b" * 40,
            })
        for refs in (
            {"refs/heads/release/next": "a" * 40},
            {"refs/tags/v3.2.0-beta0": "a" * 40},
            {"refs/tags/v3.2.0-alpha1": "a" * 40},
        ):
            with self.subTest(refs=refs), self.assertRaisesRegex(ValueError, "malformed"):
                release_policy.public_release_line_policy("3.2.0", refs)

    def test_latest_line_tag_orders_beta_below_rc_below_ga_and_numbers_numerically(self):
        for tags, latest in (
            (("v3.3.0-beta9", "v3.3.0-beta10"), "v3.3.0-beta10"),
            (("v3.3.0-beta10", "v3.3.0-rc1"), "v3.3.0-rc1"),
            (("v3.3.0-rc2", "v3.3.0"), "v3.3.0"),
            (("v3.3.0", "v3.3.1-beta1"), "v3.3.1-beta1"),
        ):
            refs = {"refs/tags/v3.0.2": "a" * 40}
            refs.update({f"refs/tags/{tag}": "b" * 40 for tag in tags})
            with self.subTest(tags=tags):
                policy = release_policy.public_release_line_policy("3.3.2", refs)
                self.assertEqual(policy["latest_public_tag_on_candidate_line"], latest)

    def test_public_ref_listing_resolves_annotated_tags_and_hashes_the_snapshot(self):
        refs, digest = release_policy.parse_public_ref_listing(
            "".join((
                f"{'a' * 40} refs/heads/master\n",
                f"{'b' * 40} refs/tags/v2.7.0\n",
                f"{'c' * 40} refs/tags/v2.7.0^{{}}\n",
            ))
        )
        self.assertEqual(refs["refs/heads/master"], "a" * 40)
        self.assertEqual(refs["refs/tags/v2.7.0"], "c" * 40)
        line_policy = release_policy.public_release_line_policy("2.7.1", refs)
        self.assertEqual(line_policy["latest_public_tag_on_candidate_line"], "v2.7.0")
        self.assertEqual(line_policy["latest_public_tag_sha_on_candidate_line"], "c" * 40)
        self.assertEqual(len(digest), 64)

    def test_first_public_promotion_creates_release_branch_and_tag_without_master(self):
        master = "a" * 40
        candidate = "b" * 40
        plan = release_policy.plan_public_promotion(
            "3.2.0-rc1",
            candidate,
            {
                "refs/heads/master": master,
                "refs/tags/v3.0.2": master,
            },
            lambda ancestor, descendant: ancestor == master and descendant == candidate,
        )
        self.assertEqual(
            plan["updates"],
            ["refs/heads/release/3.2", "refs/tags/v3.2.0-rc1"],
        )
        self.assertIsNone(plan["prior_release_branch"])
        self.assertFalse(plan["advance_master"])

    def test_first_branch_initialization_requires_latest_same_line_tag_ancestry(self):
        baseline = "a" * 40
        candidate = "b" * 40
        refs = {
            "refs/heads/master": "c" * 40,
            "refs/tags/v3.0.2": "c" * 40,
            "refs/tags/v3.2.0-rc1": baseline,
        }
        plan = release_policy.plan_public_promotion(
            "3.2.0-rc2",
            candidate,
            refs,
            lambda ancestor, descendant: (ancestor, descendant) == (baseline, candidate),
        )
        self.assertEqual(plan["updates"], [
            "refs/heads/release/3.2", "refs/tags/v3.2.0-rc2"
        ])
        with self.assertRaisesRegex(ValueError, "disconnected branch initialization"):
            release_policy.plan_public_promotion(
                "3.2.0-rc2",
                candidate,
                refs,
                lambda ancestor, descendant: False,
            )

    def test_newest_ga_advances_release_branch_tag_and_master(self):
        branch = "a" * 40
        master = "b" * 40
        candidate = "c" * 40
        plan = release_policy.plan_public_promotion(
            "3.2.0",
            candidate,
            {
                "refs/heads/master": master,
                "refs/heads/release/3.2": branch,
                "refs/tags/v3.0.2": master,
                "refs/tags/v3.2.0-rc1": branch,
            },
            lambda ancestor, descendant: (ancestor, descendant) in {
                (branch, candidate),
                (master, candidate),
            },
        )
        self.assertEqual(
            plan["updates"],
            ["refs/heads/release/3.2", "refs/tags/v3.2.0", "refs/heads/master"],
        )
        self.assertTrue(plan["advance_master"])

    def test_newer_patch_on_current_ga_line_advances_master(self):
        current = "a" * 40
        candidate = "b" * 40
        plan = release_policy.plan_public_promotion(
            "3.2.2",
            candidate,
            {
                "refs/heads/master": current,
                "refs/heads/release/3.2": current,
                "refs/tags/v3.0.2": current,
                "refs/tags/v3.2.1": current,
            },
            lambda ancestor, descendant: ancestor == current and descendant == candidate,
        )
        self.assertTrue(plan["ga_release_is_latest"])
        self.assertEqual(
            plan["updates"],
            ["refs/heads/release/3.2", "refs/tags/v3.2.2", "refs/heads/master"],
        )

    def test_same_sha_public_promotion_skips_existing_tag_and_is_idempotent(self):
        candidate = "c" * 40
        plan = release_policy.plan_public_promotion(
            "3.2.0",
            candidate,
            {
                "refs/heads/master": candidate,
                "refs/heads/release/3.2": candidate,
                "refs/tags/v3.2.0": candidate,
                "refs/tags/v3.2.0-rc1": "a" * 40,
            },
            lambda ancestor, descendant: ancestor == descendant,
        )
        self.assertTrue(plan["tag_exists"])
        self.assertEqual(
            plan["updates"],
            ["refs/heads/release/3.2", "refs/heads/master"],
        )

    def test_public_promotion_rejects_conflicting_tag_and_non_fast_forward_branch(self):
        candidate = "c" * 40
        refs = {
            "refs/heads/master": "a" * 40,
            "refs/tags/v3.0.2": "a" * 40,
            "refs/tags/v3.2.0-rc1": "b" * 40,
        }
        with self.assertRaisesRegex(ValueError, "tags are never moved"):
            release_policy.plan_public_promotion(
                "3.2.0-rc1", candidate,
                {**refs, "refs/tags/v3.2.0-rc1": "d" * 40},
                lambda ancestor, descendant: True,
            )
        with self.assertRaisesRegex(ValueError, "non-fast-forward"):
            release_policy.plan_public_promotion(
                "3.2.0-rc2", candidate,
                {**refs, "refs/heads/release/3.2": "d" * 40},
                lambda ancestor, descendant: False,
            )

    def test_older_line_ga_advances_only_its_release_branch(self):
        old_branch = "a" * 40
        newest = "b" * 40
        candidate = "c" * 40
        plan = release_policy.plan_public_promotion(
            "3.0.3",
            candidate,
            {
                "refs/heads/master": newest,
                "refs/heads/release/3.0": old_branch,
                "refs/heads/release/3.2": newest,
                "refs/tags/v3.0.2": old_branch,
                "refs/tags/v3.2.0": newest,
            },
            lambda ancestor, descendant: (ancestor, descendant) == (old_branch, candidate),
        )
        self.assertEqual(
            plan["updates"],
            ["refs/heads/release/3.0", "refs/tags/v3.0.3"],
        )
        self.assertFalse(plan["advance_master"])

    def test_newest_ga_requires_master_history_bridge(self):
        with self.assertRaisesRegex(ValueError, "history bridge"):
            release_policy.plan_public_promotion(
                "3.2.0",
                "c" * 40,
                {
                    "refs/heads/master": "d" * 40,
                    "refs/heads/release/3.2": "a" * 40,
                    "refs/tags/v3.0.2": "d" * 40,
                    "refs/tags/v3.2.0-rc1": "a" * 40,
                },
                lambda ancestor, descendant: (ancestor, descendant) == ("a" * 40, "c" * 40),
            )

    def test_lower_ga_patch_on_current_line_cannot_be_promoted_as_latest(self):
        current = "a" * 40
        candidate = "b" * 40
        refs = {
            "refs/heads/master": current,
            "refs/heads/release/3.2": current,
            "refs/tags/v3.0.2": current,
            "refs/tags/v3.2.1": current,
        }
        decision = release_policy.public_release_line_policy("3.2.0", refs)
        self.assertFalse(decision["ga_release_is_latest"])
        with self.assertRaisesRegex(ValueError, "lower GA version cannot advance"):
            release_policy.plan_public_promotion(
                "3.2.0",
                candidate,
                refs,
                lambda ancestor, descendant: ancestor == current,
            )

    def test_lower_ga_patch_on_older_line_cannot_be_promoted(self):
        current = "a" * 40
        with self.assertRaisesRegex(ValueError, "lower GA version cannot advance"):
            release_policy.plan_public_promotion(
                "3.0.1",
                "b" * 40,
                {
                    "refs/heads/master": current,
                    "refs/tags/v3.0.2": current,
                    "refs/tags/v3.2.0": current,
                },
                lambda ancestor, descendant: True,
            )

    def test_first_beta_promotion_creates_line_branch_and_tag_without_master(self):
        master = "a" * 40
        plan = release_policy.plan_public_promotion(
            "3.3.0-beta1",
            "b" * 40,
            {
                "refs/heads/master": master,
                "refs/tags/v3.0.2": master,
                "refs/heads/release/3.2": "c" * 40,
                "refs/tags/v3.2.0-rc1": "c" * 40,
            },
            lambda ancestor, descendant: False,
        )
        self.assertEqual(plan["channel"], "BETA")
        self.assertEqual(plan["updates"], ["refs/heads/release/3.3", "refs/tags/v3.3.0-beta1"])
        self.assertFalse(plan["advance_master"])
        self.assertFalse(plan["ga_release_is_latest"])

    def test_beta_on_the_newest_line_never_advances_master(self):
        master = "a" * 40
        beta1 = "b" * 40
        candidate = "c" * 40
        plan = release_policy.plan_public_promotion(
            "3.3.0-beta2",
            candidate,
            {
                "refs/heads/master": master,
                "refs/tags/v3.2.0": master,
                "refs/heads/release/3.3": beta1,
                "refs/tags/v3.3.0-beta1": beta1,
            },
            lambda ancestor, descendant: (ancestor, descendant) in {(beta1, candidate), (master, candidate)},
        )
        self.assertTrue(plan["ga_line_is_newest"])
        self.assertFalse(plan["advance_master"])
        self.assertFalse(plan["ga_release_is_latest"])
        self.assertEqual(plan["updates"], ["refs/heads/release/3.3", "refs/tags/v3.3.0-beta2"])

    def test_promotion_requires_the_candidate_to_sort_above_the_latest_line_tag(self):
        def refs(latest: str) -> dict:
            line = latest[1:].rsplit(".", 1)[0]
            return {
                "refs/heads/master": "c" * 40,
                "refs/tags/v3.0.2": "c" * 40,
                f"refs/heads/release/{line}": "a" * 40,
                f"refs/tags/{latest}": "a" * 40,
            }

        for version, latest in (
            ("3.2.0-rc1", "v3.2.0-rc2"),
            ("3.2.0-rc3", "v3.2.0"),
            ("3.3.0-beta2", "v3.3.0-rc1"),
            ("3.3.0-beta9", "v3.3.0-beta10"),
        ):
            with self.subTest(version=version, latest=latest), self.assertRaisesRegex(
                ValueError, f"{latest} is the latest public tag on this line; v{version} must sort above it"
            ):
                release_policy.plan_public_promotion(
                    version, "b" * 40, refs(latest), lambda ancestor, descendant: True
                )
        for version, latest in (
            ("3.3.0-beta10", "v3.3.0-beta9"),
            ("3.3.0-rc1", "v3.3.0-beta5"),
            ("3.3.0", "v3.3.0-rc2"),
            ("3.3.1-beta1", "v3.3.0"),
        ):
            with self.subTest(version=version, latest=latest):
                plan = release_policy.plan_public_promotion(
                    version, "b" * 40, refs(latest), lambda ancestor, descendant: True
                )
                self.assertIn(f"refs/tags/v{version}", plan["updates"])

    def test_same_sha_beta_repromotion_is_idempotent(self):
        candidate = "c" * 40
        plan = release_policy.plan_public_promotion(
            "3.3.0-beta1",
            candidate,
            {
                "refs/heads/master": "a" * 40,
                "refs/tags/v3.0.2": "a" * 40,
                "refs/heads/release/3.3": candidate,
                "refs/tags/v3.3.0-beta1": candidate,
            },
            lambda ancestor, descendant: ancestor == descendant,
        )
        self.assertTrue(plan["tag_exists"])
        self.assertEqual(plan["updates"], ["refs/heads/release/3.3"])

    def test_old_line_ga_and_rc_plans_with_public_beta_tags(self):
        master = "a" * 40
        rc1 = "b" * 40
        beta1 = "d" * 40
        candidate = "c" * 40
        refs = {
            "refs/heads/master": master,
            "refs/tags/v3.0.2": master,
            "refs/heads/release/3.2": rc1,
            "refs/tags/v3.2.0-rc1": rc1,
            "refs/heads/release/3.3": beta1,
            "refs/tags/v3.3.0-beta1": beta1,
        }
        ancestry = {(rc1, candidate), (master, candidate)}

        ga = release_policy.plan_public_promotion(
            "3.2.0", candidate, refs, lambda ancestor, descendant: (ancestor, descendant) in ancestry
        )
        self.assertEqual(ga["newest_ga_line"], "3.0")
        self.assertTrue(ga["ga_line_is_newest"])
        self.assertTrue(ga["ga_release_is_latest"])
        self.assertEqual(
            ga["updates"],
            ["refs/heads/release/3.2", "refs/tags/v3.2.0", "refs/heads/master"],
        )

        rc2 = release_policy.plan_public_promotion(
            "3.2.0-rc2", candidate, refs, lambda ancestor, descendant: (ancestor, descendant) in ancestry
        )
        self.assertFalse(rc2["advance_master"])
        self.assertEqual(rc2["updates"], ["refs/heads/release/3.2", "refs/tags/v3.2.0-rc2"])

        self.assertFalse(release_policy.public_release_line_policy("3.2.0-rc2", refs)["ga_release_is_latest"])
        beta2 = release_policy.public_release_line_policy("3.3.0-beta2", refs)
        self.assertFalse(beta2["ga_release_is_latest"])
        self.assertEqual(beta2["latest_public_tag_on_candidate_line"], "v3.3.0-beta1")

    def test_older_line_ga_patch_with_newer_line_beta_tags_leaves_master(self):
        newest = "a" * 40
        branch = "b" * 40
        candidate = "c" * 40
        plan = release_policy.plan_public_promotion(
            "3.2.1",
            candidate,
            {
                "refs/heads/master": newest,
                "refs/tags/v3.3.0": newest,
                "refs/heads/release/3.2": branch,
                "refs/tags/v3.2.0": branch,
                "refs/tags/v3.4.0-beta1": "d" * 40,
            },
            lambda ancestor, descendant: (ancestor, descendant) == (branch, candidate),
        )
        self.assertEqual(plan["newest_ga_line"], "3.3")
        self.assertFalse(plan["advance_master"])
        self.assertFalse(plan["ga_release_is_latest"])
        self.assertEqual(plan["updates"], ["refs/heads/release/3.2", "refs/tags/v3.2.1"])

    def test_public_promotion_fetches_legacy_line_tag_before_branch_bootstrap(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bare = root / "public.git"
            source = root / "source"
            legacy = root / "legacy"
            subprocess.run(["git", "init", "--bare", bare], check=True, capture_output=True)
            # Background auto-maintenance would race the directory's removal.
            subprocess.run(["git", "--git-dir", bare, "config", "maintenance.auto", "false"], check=True)
            for repository in (source, legacy):
                subprocess.run(["git", "init", repository], check=True, capture_output=True)
                for key, value in (
                    ("user.name", "Release Test"),
                    ("user.email", "release-test@example.invalid"),
                    ("maintenance.auto", "false"),
                ):
                    subprocess.run(["git", "-C", repository, "config", key, value], check=True)
                (repository / "source.txt").write_text(repository.name, encoding="utf-8")
                subprocess.run(["git", "-C", repository, "add", "source.txt"], check=True)
                subprocess.run(
                    ["git", "-C", repository, "commit", "-m", repository.name],
                    check=True,
                    capture_output=True,
                )

            def head(repository: Path) -> str:
                return subprocess.run(
                    ["git", "-C", repository, "rev-parse", "HEAD"],
                    check=True,
                    capture_output=True,
                    text=True,
                ).stdout.strip()

            source_head = head(source)
            legacy_head = head(legacy)
            for repository in (source, legacy):
                subprocess.run(
                    ["git", "-C", repository, "remote", "add", "public", bare],
                    check=True,
                )
            subprocess.run(
                ["git", "-C", source, "push", "public", f"{source_head}:refs/heads/master"],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                ["git", "-C", source, "push", "public", f"{source_head}:refs/tags/v3.0.2"],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                ["git", "-C", legacy, "push", "public", f"{legacy_head}:refs/tags/v3.2.0-rc1"],
                check=True,
                capture_output=True,
            )

            (source / "source.txt").write_text("disconnected", encoding="utf-8")
            subprocess.run(["git", "-C", source, "add", "source.txt"], check=True)
            subprocess.run(
                ["git", "-C", source, "commit", "-m", "disconnected candidate"],
                check=True,
                capture_output=True,
            )
            with self.assertRaisesRegex(ValueError, "disconnected branch initialization"):
                release_policy.inspect_public_promotion(
                    "3.2.0-rc2", head(source), "public", cwd=source
                )

            subprocess.run(
                ["git", "-C", source, "checkout", "--detach", legacy_head],
                check=True,
                capture_output=True,
            )
            (source / "source.txt").write_text("descends from public RC", encoding="utf-8")
            subprocess.run(["git", "-C", source, "add", "source.txt"], check=True)
            subprocess.run(
                ["git", "-C", source, "commit", "-m", "connected candidate"],
                check=True,
                capture_output=True,
            )
            plan = release_policy.inspect_public_promotion(
                "3.2.0-rc2", head(source), "public", cwd=source
            )
            self.assertEqual(
                plan["updates"],
                ["refs/heads/release/3.2", "refs/tags/v3.2.0-rc2"],
            )

    def test_atomic_public_push_rejects_raced_branch_update_without_creating_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bare = root / "public.git"
            work = root / "source"
            subprocess.run(["git", "init", "--bare", bare], check=True, capture_output=True)
            subprocess.run(["git", "--git-dir", bare, "config", "maintenance.auto", "false"], check=True)
            subprocess.run(["git", "init", work], check=True, capture_output=True)
            for key, value in (("user.name", "Release Test"), ("user.email", "release-test@example.invalid"),
                               ("maintenance.auto", "false")):
                subprocess.run(["git", "-C", work, "config", key, value], check=True)

            def commit(parent: str | None, content: str, subject: str) -> str:
                if parent:
                    subprocess.run(["git", "-C", work, "checkout", "--detach", parent], check=True, capture_output=True)
                (work / "source.txt").write_text(content, encoding="utf-8")
                subprocess.run(["git", "-C", work, "add", "source.txt"], check=True)
                subprocess.run(["git", "-C", work, "commit", "-m", subject], check=True, capture_output=True)
                return subprocess.run(
                    ["git", "-C", work, "rev-parse", "HEAD"],
                    check=True,
                    capture_output=True,
                    text=True,
                ).stdout.strip()

            initial = commit(None, "initial", "initial")
            subprocess.run(["git", "-C", work, "remote", "add", "public", bare], check=True)
            subprocess.run(["git", "-C", work, "push", "public", f"{initial}:refs/heads/master"], check=True, capture_output=True)
            subprocess.run(["git", "-C", work, "push", "public", f"{initial}:refs/tags/v3.0.2"], check=True, capture_output=True)

            first_candidate = commit(initial, "rc1", "rc1")
            first_plan = release_policy.inspect_public_promotion(
                "3.2.0-rc1", first_candidate, "public", cwd=work
            )
            release_policy.push_public_ref_updates("public", first_plan, cwd=work)
            release_policy.push_public_ref_updates(
                "public",
                release_policy.inspect_public_promotion(
                    "3.2.0-rc1", first_candidate, "public", cwd=work
                ),
                cwd=work,
            )
            subprocess.run(
                ["git", "-C", work, "push", "public", f"{initial}:refs/heads/release/3.4"],
                check=True,
                capture_output=True,
            )
            with self.assertRaisesRegex(ValueError, "changed after validation"):
                release_policy.promote_public_source(
                    "3.2.0-rc1",
                    first_candidate,
                    "public",
                    first_plan["public_refs_digest"],
                    cwd=work,
                )

            second_candidate = commit(first_candidate, "rc2", "rc2")
            second_plan = release_policy.inspect_public_promotion(
                "3.2.0-rc2", second_candidate, "public", cwd=work
            )
            release_policy.push_public_ref_updates("public", second_plan, cwd=work)

            stale_candidate = commit(second_candidate, "rc3", "rc3")
            stale_plan = release_policy.inspect_public_promotion(
                "3.2.0-rc3", stale_candidate, "public", cwd=work
            )
            concurrent = commit(initial, "concurrent", "concurrent branch update")
            subprocess.run(["git", "-C", work, "push", "public", f"{concurrent}:refs/heads/race"], check=True, capture_output=True)
            subprocess.run(
                ["git", "--git-dir", bare, "update-ref", "refs/heads/release/3.2", concurrent],
                check=True,
            )

            with self.assertRaisesRegex(ValueError, "atomic public ref update failed"):
                release_policy.push_public_ref_updates("public", stale_plan, cwd=work)
            actual_branch = subprocess.run(
                ["git", "--git-dir", bare, "rev-parse", "refs/heads/release/3.2"],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            self.assertEqual(actual_branch, concurrent)
            missing_tag = subprocess.run(
                ["git", "--git-dir", bare, "show-ref", "--verify", "refs/tags/v3.2.0-rc3"],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(missing_tag.returncode, 0)

    def test_public_promotion_reads_annotated_public_beta_tags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bare = root / "public.git"
            work = root / "source"
            subprocess.run(["git", "init", "--bare", bare], check=True, capture_output=True)
            subprocess.run(["git", "--git-dir", bare, "config", "maintenance.auto", "false"], check=True)
            subprocess.run(["git", "init", work], check=True, capture_output=True)
            for key, value in (("user.name", "Release Test"), ("user.email", "release-test@example.invalid"),
                               ("maintenance.auto", "false")):
                subprocess.run(["git", "-C", work, "config", key, value], check=True)

            def git(*arguments: str) -> str:
                return subprocess.run(
                    ["git", "-C", work, *arguments], check=True, capture_output=True, text=True
                ).stdout.strip()

            def commit(parent: str, subject: str) -> str:
                if parent:
                    git("checkout", "--detach", parent)
                (work / "source.txt").write_text(subject, encoding="utf-8")
                git("add", "source.txt")
                git("commit", "-m", subject)
                return git("rev-parse", "HEAD")

            initial = commit("", "initial")
            rc1 = commit(initial, "rc1")
            beta1 = commit(rc1, "beta1")
            git("tag", "-a", "v3.3.0-beta1", "-m", "beta1", beta1)
            git("remote", "add", "public", str(bare))
            for source, ref in (
                (initial, "refs/heads/master"),
                (initial, "refs/tags/v3.0.2"),
                (rc1, "refs/heads/release/3.2"),
                (rc1, "refs/tags/v3.2.0-rc1"),
                (beta1, "refs/heads/release/3.3"),
                ("refs/tags/v3.3.0-beta1", "refs/tags/v3.3.0-beta1"),
            ):
                git("push", "public", f"{source}:{ref}")

            rc2 = commit(rc1, "rc2")
            self.assertEqual(
                release_policy.inspect_public_promotion("3.2.0-rc2", rc2, "public", cwd=work)["updates"],
                ["refs/heads/release/3.2", "refs/tags/v3.2.0-rc2"],
            )
            beta2 = commit(beta1, "beta2")
            plan = release_policy.inspect_public_promotion("3.3.0-beta2", beta2, "public", cwd=work)
            self.assertEqual(plan["latest_public_tag_sha_on_candidate_line"], beta1)
            self.assertEqual(plan["updates"], ["refs/heads/release/3.3", "refs/tags/v3.3.0-beta2"])

    def test_accepts_publication_environment_with_one_or_more_reviewers_and_self_review(self):
        release_policy.validate_publication_environment({
            "name": "public-release",
            "protection_rules": [{
                "type": "required_reviewers",
                "prevent_self_review": False,
                "reviewers": [
                    {"type": "User", "reviewer": {"id": 42}},
                    {"type": "Team", "reviewer": {"id": 7}},
                ],
            }],
        })

    def test_rejects_missing_publication_reviewers(self):
        with self.assertRaisesRegex(ValueError, "one required-reviewers rule"):
            release_policy.validate_publication_environment({
                "name": "public-release",
                "protection_rules": [],
            })

    def test_rejects_malformed_or_self_review_blocking_publication_rule(self):
        for rule in (
            {"type": "required_reviewers", "reviewers": [{"type": "User", "reviewer": {"id": 42}}]},
            {
                "type": "required_reviewers",
                "prevent_self_review": True,
                "reviewers": [{"type": "User", "reviewer": {"id": 42}}],
            },
            {
                "type": "required_reviewers",
                "prevent_self_review": False,
                "reviewers": [{"type": "User", "reviewer": {"id": "42"}}],
            },
        ):
            with self.subTest(rule=rule), self.assertRaises(ValueError):
                release_policy.validate_publication_environment({
                    "name": "public-release",
                    "protection_rules": [rule],
                })

    def test_beta_publication_requires_beta_release_without_reviewers_for_beta_tags_only(self):
        environment = {
            "name": "beta-release",
            "protection_rules": [{"type": "branch_policy"}],
            "deployment_branch_policy": {"protected_branches": False, "custom_branch_policies": True},
        }
        policies = {"total_count": 1, "branch_policies": [{"id": 3, "name": "v*-beta*", "type": "tag"}]}
        release_policy.validate_publication_environment(environment, "BETA", policies)
        reviewers = {
            "type": "required_reviewers",
            "prevent_self_review": False,
            "reviewers": [{"type": "Team", "reviewer": {"id": 7}}],
        }
        protected_only = {"protected_branches": True, "custom_branch_policies": False}
        only_beta_tags = "exactly the v\\*-beta\\* tag pattern"
        for broken, broken_policies, message in (
            ({**environment, "name": "public-release"}, policies, "beta-release environment response"),
            ({**environment, "protection_rules": [reviewers]}, policies, "must not require reviewers"),
            ({**environment, "deployment_branch_policy": None}, policies, "custom deployment patterns"),
            ({**environment, "deployment_branch_policy": protected_only}, policies, "custom deployment patterns"),
            (environment, None, only_beta_tags),
            (environment, {"branch_policies": []}, only_beta_tags),
            (environment, {"branch_policies": [{"name": "v*", "type": "tag"}]}, only_beta_tags),
            (environment, {"branch_policies": [{"name": "v*-beta*", "type": "branch"}]}, only_beta_tags),
            (
                environment,
                {"branch_policies": [{"name": "v*-beta*", "type": "tag"}, {"name": "v*-rc*", "type": "tag"}]},
                only_beta_tags,
            ),
        ):
            with self.subTest(message=message, environment=broken, policies=broken_policies):
                with self.assertRaisesRegex(ValueError, message):
                    release_policy.validate_publication_environment(broken, "BETA", broken_policies)

    def test_rc_and_ga_publication_keep_the_public_release_reviewer_rule(self):
        public = {
            "name": "public-release",
            "protection_rules": [{
                "type": "required_reviewers",
                "prevent_self_review": False,
                "reviewers": [{"type": "Team", "reviewer": {"id": 7}}],
            }],
        }
        beta = {
            "name": "beta-release",
            "protection_rules": [],
            "deployment_branch_policy": {"protected_branches": False, "custom_branch_policies": True},
        }
        policies = {"branch_policies": [{"name": "v*-beta*", "type": "tag"}]}
        for channel in ("RC", "GA"):
            with self.subTest(channel=channel):
                release_policy.validate_publication_environment(public, channel)
                with self.assertRaisesRegex(ValueError, "public-release environment response"):
                    release_policy.validate_publication_environment(beta, channel, policies)
                with self.assertRaisesRegex(ValueError, "one required-reviewers rule"):
                    release_policy.validate_publication_environment(
                        {"name": "public-release", "protection_rules": []}, channel
                    )

    def test_classifies_ga_beta_and_rc(self):
        ga = release_policy.classify("3.2.0")
        self.assertEqual((ga["channel"], ga["prerelease_number"]), ("GA", ""))
        identity = release_policy.classify("3.2.0-rc2")
        self.assertEqual(identity["channel"], "RC")
        self.assertEqual(identity["prerelease_number"], "2")
        self.assertEqual(identity["release_branch"], "release/3.2")
        beta = release_policy.classify("3.3.0-beta10")
        self.assertEqual(
            (beta["channel"], beta["prerelease_number"], beta["release_branch"]),
            ("BETA", "10", "release/3.3"),
        )

    def test_rejects_non_release_versions(self):
        for version in (
            "v3.2.0", "3.2", "3.2.0-rc0", "3.2.0-beta0", "3.2.0-beta", "3.2.0-alpha1",
            "3.2.0-beta1-rc1", "3.2.0-BETA1", "3.2.0-devel",
        ):
            with self.subTest(version=version), self.assertRaises(ValueError):
                release_policy.classify(version)

    def test_validates_tracked_release_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "release-state.txt").write_text(
                "version=3.2.0\n"
                "channel=RC\n"
                "prerelease=1\n"
                "revision=$Format:%H$\n"
            )
            identity = release_policy.validate_source(root, "3.2.0-rc1")
            self.assertEqual(identity["channel"], "RC")
            self.assertEqual(identity["windows_signing"], "signpath")
            with self.assertRaisesRegex(ValueError, "release channel"):
                release_policy.validate_source(root, "3.2.0")
            (root / "release-state.txt").write_text(
                "version=3.2.0\nchannel=RC\nprerelease=1\nrevision=" + "a" * 40 + "\n"
            )
            with self.assertRaisesRegex(ValueError, "revision"):
                release_policy.validate_source(root, "3.2.0-rc1")

    def test_parse_state_prerelease_contract(self):
        def state(channel: str, prerelease: str, *extra: str) -> str:
            lines = ("version=3.3.0", f"channel={channel}", f"prerelease={prerelease}", "revision=$Format:%H$")
            return "\n".join((*lines, *extra)) + "\n"

        beta = release_policy.parse_state(state("BETA", "1", "windows_signing=unsigned"))
        self.assertEqual(
            (beta["channel"], beta["prerelease"], beta["windows_signing"]),
            ("BETA", "1", "unsigned"),
        )
        self.assertEqual(release_policy.parse_state(state("RC", "2"))["windows_signing"], "signpath")
        self.assertEqual(release_policy.parse_state(state("DEVEL", ""))["prerelease"], "")
        for contents, message in (
            (state("BETA", "1"), "BETA release state requires windows_signing=unsigned"),
            (state("BETA", "1", "windows_signing=signpath"), "BETA release state requires windows_signing=unsigned"),
            (state("BETA", "", "windows_signing=unsigned"), "positive prerelease number"),
            (state("BETA", "0", "windows_signing=unsigned"), "positive prerelease number"),
            (state("RC", "0"), "positive prerelease number"),
            (state("DEVEL", "1"), "empty prerelease number"),
            (state("GA", "1"), "empty prerelease number"),
            (state("ALPHA", "1"), "channel must be DEVEL, BETA, RC, or GA"),
            (state("RC", "1").replace("prerelease=", "rc="), "must define version, channel, prerelease, revision"),
        ):
            with self.subTest(contents=contents), self.assertRaisesRegex(ValueError, message):
                release_policy.parse_state(contents)

    def test_validates_beta_source_and_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = (
                "version=3.3.0\nchannel=BETA\nprerelease=1\n"
                "revision=$Format:%H$\nwindows_signing=unsigned\n"
            )
            (root / "release-state.txt").write_text(state)
            identity = release_policy.validate_source(root, "3.3.0-beta1")
            self.assertEqual(
                (identity["channel"], identity["prerelease_number"], identity["windows_signing"]),
                ("BETA", "1", "unsigned"),
            )
            for version, message in (
                ("3.3.0-beta2", "prerelease number 1 does not match 2"),
                ("3.3.0-rc1", "release channel BETA does not match RC"),
            ):
                with self.subTest(version=version), self.assertRaisesRegex(ValueError, message):
                    release_policy.validate_source(root, version)
            commit = "e" * 40
            archived_state = state.replace("$Format:%H$", commit).encode()
            archive = root / "source.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                info = tarfile.TarInfo("wsjtx-3.3.0-beta1/release-state.txt")
                info.size = len(archived_state)
                output.addfile(info, io.BytesIO(archived_state))
            release_policy.validate_archive(archive, "3.3.0-beta1", commit)
            with self.assertRaisesRegex(ValueError, "release identity does not match"):
                release_policy.validate_archive(archive, "3.3.0-beta2", commit)

    def test_read_state_cli_accepts_single_digit_prerelease(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "release-state.txt").write_text(
                "version=3.2.0\n"
                "channel=RC\n"
                "prerelease=1\n"
                "revision=$Format:%H$\n"
            )
            result = subprocess.run(
                [sys.executable, SCRIPT, "read-state", "--root", root],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                json.loads(result.stdout),
                {
                    "version": "3.2.0",
                    "channel": "RC",
                    "prerelease": "1",
                    "revision": "$Format:%H$",
                    "windows_signing": "signpath",
                },
            )

            (root / "release-state.txt").write_text(
                "version=3.2.0\n"
                "channel=RC\n"
                "prerelease=1beta\n"
                "revision=$Format:%H$\n"
            )
            result = subprocess.run(
                [sys.executable, SCRIPT, "read-state", "--root", root],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("positive prerelease number", result.stderr)

    def test_source_pins_unsigned_mode_and_rejects_unknown_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = (
                "version=3.2.0\nchannel=RC\nprerelease=1\n"
                "revision=$Format:%H$\nwindows_signing=unsigned\n"
            )
            (root / "release-state.txt").write_text(state)
            self.assertEqual(
                release_policy.validate_source(root, "3.2.0-rc1")["windows_signing"],
                "unsigned",
            )
            archive = root / "source.zip"
            commit = "c" * 40
            with zipfile.ZipFile(archive, "w") as output:
                output.writestr(
                    "wsjtx-3.2.0-rc1/release-state.txt",
                    state.replace("$Format:%H$", commit),
                )
            release_policy.validate_archive(archive, "3.2.0-rc1", commit)
            with self.assertRaisesRegex(ValueError, "windows_signing"):
                release_policy.parse_state(state.replace("unsigned", "ephemeral"))

    def test_validates_exported_tar_and_zip_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            commit = "b" * 40
            state = f"version=3.2.0\nchannel=RC\nprerelease=1\nrevision={commit}\n".encode()
            tar_path = root / "source.tar.gz"
            with tarfile.open(tar_path, "w:gz") as archive:
                info = tarfile.TarInfo("wsjtx-3.2.0-rc1/release-state.txt")
                info.size = len(state)
                archive.addfile(info, io.BytesIO(state))
            zip_path = root / "source.zip"
            with zipfile.ZipFile(zip_path, "w") as archive:
                archive.writestr("wsjtx-3.2.0-rc1/release-state.txt", state)
            release_policy.validate_archive(tar_path, "3.2.0-rc1", commit)
            release_policy.validate_archive(zip_path, "3.2.0-rc1", commit)
            with self.assertRaisesRegex(ValueError, "revision"):
                release_policy.validate_archive(tar_path, "3.2.0-rc1", "c" * 40)

    def test_candidate_and_public_gates_reject_missing_intel_payloads(self):
        version = "3.3.0-beta1"
        for distribution in (False, True):
            mode = "distribution" if distribution else "validation"
            for public in (False, True):
                with self.subTest(mode=mode, public=public), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    expected = (
                        release_policy.public_expected_assets(version, mode)
                        if public else release_policy.expected_assets(version, distribution)
                    )
                    for name in expected:
                        target = root / name
                        target.mkdir()
                        suffix = release_policy.asset_suffix(name)
                        filename = name.replace("-unsigned", "")
                        if not filename.endswith(suffix):
                            filename += suffix
                        (target / filename).write_bytes(name.encode())
                    def verify():
                        if public:
                            return release_policy.find_public_asset_files(root, version, mode)
                        return release_policy.find_asset_files(root, version, distribution)
                    verify()
                    suffix = "macOS.pkg" if distribution else "macOS-unsigned.pkg"
                    required = [f"wsjtx-{version}-x86_64-{suffix}"] + [
                        f"wsjtx-{version}-x86_64-macOS-{group}"
                        for group in ("jt9", "codec", "wsprd", "utilities")
                    ]
                    for name in required:
                        artifact = root / name
                        held = root / "held"
                        artifact.rename(held)
                        with self.subTest(missing=name), self.assertRaisesRegex(ValueError, re.escape(name)):
                            verify()
                        held.rename(artifact)

    def test_distribution_gate_rejects_unsigned_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0-rc1"
            for name in release_policy.expected_assets(version, True):
                target = root / name
                target.mkdir()
                suffix = release_policy.asset_suffix(name)
                (target / f"artifact{suffix}").write_bytes(b"signed")
            (root / "extra").mkdir()
            (root / "extra" / "unexpected-unsigned.pkg").write_bytes(b"unsigned")
            with self.assertRaisesRegex(ValueError, "unsigned macOS"):
                release_policy.find_asset_files(root, version, True)

    def test_manifest_binds_assets_to_tag_and_commit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0"
            for name in release_policy.expected_assets(version, True):
                target = root / name
                target.mkdir()
                suffix = release_policy.asset_suffix(name)
                (target / f"{name}{suffix}" if not name.endswith(suffix) else target / name).write_bytes(name.encode())
            for arch in ("x86_64", "aarch64", "armhf"):
                for package_type in ("deb", "rpm"):
                    target = root / f"wsjtx-{version}-linux-{arch}-{package_type}"
                    target.mkdir()
                    (target / f"wsjtx-{arch}.{package_type}").write_bytes(arch.encode())
            (root / f"wsjtx-{version}-src.tar.gz").write_bytes(b"source")
            args = type("Args", (), {
                "artifacts": str(root), "version": version, "repository": "WSJTX/wsjtx",
                "commit": "a" * 40, "run_id": "123",
                "linux_x86_64_digest": "sha256:" + "1" * 64,
                "linux_aarch64_digest": "sha256:" + "2" * 64,
                "linux_armhf_cross_digest": "sha256:" + "3" * 64,
                "linux_armhf_digest": "sha256:" + "4" * 64,
                "macos_mode": "distribution",
                "windows_mode": "signpath",
            })()
            release_policy.write_manifest(args)
            manifest = json.loads((root / "release-manifest.json").read_text())
            self.assertEqual(manifest["tag"], "v3.2.0")
            self.assertEqual(manifest["commit"], "a" * 40)
            self.assertEqual(manifest["linux_builders"]["armhf_cross"], "sha256:" + "3" * 64)
            self.assertEqual(manifest["linux_builders"]["armhf_runtime"], "sha256:" + "4" * 64)
            self.assertEqual(manifest["macos_signing"]["mode"], "distribution")
            self.assertEqual(manifest["macos_signing"]["replaceable_assets"], [])
            self.assertEqual(manifest["windows_signing"]["mode"], "signpath")
            self.assertEqual(len(manifest["assets"]), 37)
            self.assertTrue(
                {f"{name}.tar.gz" for name in release_policy.release_tarballs(version)}
                <= {entry["name"] for entry in manifest["assets"]}
            )

    def test_manual_macos_assets_keep_release_names_without_immutable_hashes(self):
        for version in ("3.2.0", "3.2.0-rc1"):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                for name in release_policy.public_expected_assets(version, "validation"):
                    target = root / name
                    target.mkdir()
                    if name in release_policy.release_tarballs(version):
                        filename = f"{name}.tar.gz"
                    elif "macOS" in name:
                        filename = name.replace("-unsigned", "")
                    elif "linux" in name:
                        filename = f"{name}.AppImage"
                    else:
                        filename = "wsjtx-win64.exe"
                    (target / filename).write_bytes(name.encode())
                for arch in ("x86_64", "aarch64", "armhf"):
                    for package_type in ("deb", "rpm"):
                        target = root / f"wsjtx-{version}-linux-{arch}-{package_type}"
                        target.mkdir()
                        (target / f"wsjtx-{arch}.{package_type}").write_bytes(arch.encode())
                (root / f"wsjtx-{version}-src.tar.gz").write_bytes(b"source")
                args = type("Args", (), {
                    "artifacts": str(root), "version": version, "repository": "WSJTX/wsjtx",
                    "commit": "a" * 40, "run_id": "123",
                    "linux_x86_64_digest": "sha256:" + "1" * 64,
                    "linux_aarch64_digest": "sha256:" + "2" * 64,
                    "linux_armhf_cross_digest": "sha256:" + "3" * 64,
                    "linux_armhf_digest": "sha256:" + "4" * 64,
                    "macos_mode": "validation",
                    "windows_mode": "signpath",
                })()

                release_policy.write_manifest(args)

                manifest = json.loads((root / "release-manifest.json").read_text())
                replaceable = {
                    f"wsjtx-{version}-arm64-macOS.pkg",
                    f"wsjtx-{version}-x86_64-macOS.pkg",
                }
                release_names = {path.name for path in release_policy.release_files(root, version, "validation")}
                immutable_names = {entry["name"] for entry in manifest["assets"]}
                checksum_names = {
                    line.split("  ", 1)[1]
                    for line in (root / "SHA256SUMS").read_text().splitlines()
                }
                self.assertEqual(manifest["macos_signing"]["mode"], "manual")
                self.assertEqual(set(manifest["macos_signing"]["replaceable_assets"]), replaceable)
                self.assertTrue(replaceable <= release_names)
                self.assertTrue(replaceable.isdisjoint(immutable_names))
                self.assertTrue(replaceable.isdisjoint(checksum_names))
                self.assertIn("wsjtx-win64.exe", immutable_names)
                tarballs = {f"{name}.tar.gz" for name in release_policy.release_tarballs(version)}
                self.assertTrue(tarballs <= immutable_names & checksum_names)

    def test_unsigned_windows_manifest_selects_and_hashes_installer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0-rc1"
            for name in release_policy.public_expected_assets(version, "validation", "unsigned"):
                target = root / name
                target.mkdir()
                if name in release_policy.release_tarballs(version):
                    filename = f"{name}.tar.gz"
                elif "macOS" in name:
                    filename = name.replace("-unsigned", "")
                elif "linux" in name:
                    filename = f"{name}.AppImage"
                else:
                    filename = f"wsjtx-{version}-win64.exe"
                (target / filename).write_bytes(name.encode())
            for arch in ("x86_64", "aarch64", "armhf"):
                for package_type in ("deb", "rpm"):
                    target = root / f"wsjtx-{version}-linux-{arch}-{package_type}"
                    target.mkdir()
                    (target / f"wsjtx-{arch}.{package_type}").write_bytes(arch.encode())
            (root / f"wsjtx-{version}-src.tar.gz").write_bytes(b"source")
            args = type("Args", (), {
                "artifacts": str(root), "version": version, "repository": "WSJTX/wsjtx",
                "commit": "a" * 40, "run_id": "123",
                "linux_x86_64_digest": "sha256:" + "1" * 64,
                "linux_aarch64_digest": "sha256:" + "2" * 64,
                "linux_armhf_cross_digest": "sha256:" + "3" * 64,
                "linux_armhf_digest": "sha256:" + "4" * 64,
                "macos_mode": "validation", "windows_mode": "unsigned",
            })()
            release_policy.write_manifest(args)
            manifest = json.loads((root / "release-manifest.json").read_text())
            installer = f"wsjtx-{version}-win64.exe"
            self.assertEqual(manifest["windows_signing"]["mode"], "unsigned")
            self.assertIn(installer, {entry["name"] for entry in manifest["assets"]})
            for group in ("jt9", "codec", "wsprd", "utilities"):
                self.assertIn(
                    f"wsjtx-{version}-windows-x86_64-{group}.tar.gz", {entry["name"] for entry in manifest["assets"]}
                )
            self.assertIn(installer, (root / "SHA256SUMS").read_text())
            installer_path = root / f"wsjtx-{version}-windows-x86_64-installer" / installer
            installer_path.rename(installer_path.with_name("unexpected.exe"))
            with self.assertRaisesRegex(ValueError, "unexpected unsigned Windows installer"):
                release_policy.find_public_asset_files(root, version, "validation", "unsigned")
            installer_path.with_name("unexpected.exe").rename(installer_path)
            signed_dir = root / f"wsjtx-{version}-windows-x86_64-installer-signed"
            signed_dir.mkdir()
            with self.assertRaisesRegex(ValueError, "signed Windows installer"):
                release_policy.find_public_asset_files(root, version, "validation", "unsigned")

    def test_release_tarballs_are_required_by_candidate_and_public_gates(self):
        version = "3.2.0-rc1"
        tools = release_policy.release_tarballs(version)
        self.assertEqual(
            tools,
            [
                f"wsjtx-{version}-{target}-{group}"
                for target in ("arm64-macOS", "x86_64-macOS", "linux-x86_64", "linux-aarch64", "linux-armhf", "windows-x86_64")
                for group in ("jt9", "codec", "wsprd", "utilities")
            ],
        )
        self.assertEqual([release_policy.asset_suffix(name) for name in tools], [".tar.gz"] * 24)
        self.assertEqual(
            [release_policy.asset_suffix(name) for name in release_policy.expected_assets(version, False)[:6]],
            [".pkg", ".pkg", ".AppImage", ".AppImage", ".AppImage", ".exe"],
        )
        for distribution in (False, True):
            self.assertTrue(set(tools) <= set(release_policy.expected_assets(version, distribution)))
        for macos_mode in release_policy.MACOS_MODES:
            for windows_mode in release_policy.WINDOWS_MODES:
                self.assertTrue(
                    set(tools) <= set(release_policy.public_expected_assets(version, macos_mode, windows_mode))
                )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in release_policy.expected_assets(version, False):
                target = root / name
                target.mkdir()
                (target / f"{name}{release_policy.asset_suffix(name)}").write_bytes(name.encode())
            files = release_policy.find_asset_files(root, version, False)
            self.assertEqual(
                sorted(path.name for path in files if path.name.endswith(".tar.gz")),
                sorted(f"{name}.tar.gz" for name in tools),
            )
            armhf = root / f"wsjtx-{version}-linux-armhf-codec"
            (armhf / f"{armhf.name}.tar.gz").unlink()
            with self.assertRaisesRegex(ValueError, "linux-armhf-codec must contain exactly one .tar.gz"):
                release_policy.find_asset_files(root, version, False)

    def test_release_tarball_uploads_agree_with_the_group_file_and_the_policy(self):
        root = SCRIPT.parents[2]
        action = (root / ".github/actions/upload-release-tarballs/action.yml").read_text(encoding="utf-8")
        prefix = r"wsjtx-\$\{\{ inputs\.version \}\}-\$\{\{ inputs\.target \}\}-"
        names = re.findall(rf"\n        name: {prefix}(\S+)\n", action)
        paths = re.findall(rf"\n        path: \$\{{\{{ inputs\.directory \}}\}}/{prefix}(\S+)\.tar\.gz\n", action)
        self.assertEqual(names, release_policy.tarball_groups())
        self.assertEqual(paths, names)
        self.assertEqual(action.count("if-no-files-found: error"), len(names))

        version = "3.3.0"
        installers = []
        for name in release_policy.public_expected_assets(version, "validation", "unsigned"):
            if name not in release_policy.release_tarballs(version):
                installers.append(re.sub(r"-(unsigned\.pkg|AppImage|installer)$", "", name.removeprefix(f"wsjtx-{version}-")).removesuffix(".pkg"))
        self.assertEqual(installers, list(release_policy.RELEASE_TARGETS))
        macos_arches = [target.removesuffix("-macOS") for target in installers if target.endswith("-macOS")]
        linux_arches = [target.removeprefix("linux-") for target in installers if target.startswith("linux-")]

        uploads = []
        for path in (
            ".github/workflows/build-macos.yml", ".github/workflows/build-windows.yml",
            ".github/workflows/build-linux.yml", ".github/actions/build-linux-payload/action.yml",
        ):
            text = (root / path).read_text(encoding="utf-8")
            calls = re.findall(
                r"uses: \./\.github/actions/upload-release-tarballs\n\s+with:\n\s+version: \$\{\{ inputs\.version \}\}\n"
                r"\s+target: (.+)\n\s+directory: release-tarballs\n",
                text,
            )
            self.assertEqual(len(calls), text.count("upload-release-tarballs"), path)
            uploads += [(path, target) for target in calls]
        targets = {target for _, target in uploads if "${{" not in target}
        for path, target in uploads:
            if target == "${{ inputs.arch }}-macOS":
                targets |= {f"{arch}-macOS" for arch in macos_arches}
            elif target == "linux-${{ inputs.arch }}":
                targets |= {f"linux-{arch}" for arch in linux_arches if f"linux-{arch}" not in targets}
        self.assertEqual(targets, set(release_policy.RELEASE_TARGETS))

    def test_tarball_groups_come_from_the_group_file(self):
        self.assertEqual(release_policy.tarball_groups(), ["jt9", "codec", "wsprd", "utilities"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "release-tarballs.txt"
            path.write_bytes(b"# comment\r\njt9 jt9\r\n\r\nengine wsjt-codec wsprd\r\n")
            self.assertEqual(release_policy.tarball_groups(path), ["jt9", "engine"])
            for contents, message in (
                ("", "must name each release tarball once"),
                ("# only a comment\n", "must name each release tarball once"),
                ("jt9 jt9\njt9 wsjt-codec\n", "must name each release tarball once"),
                ("jt9 jt9\nutilities\n", "release tarball utilities names no programs"),
                ("jt9 jt9\ninstaller wsprd\n", "release tarball name installer cannot name a release asset"),
                ("jt9 jt9\nsrc wsprd\n", "release tarball name src cannot name a release asset"),
                ("jt9 jt9\nAppImage wsprd\n", "release tarball name AppImage cannot name a release asset"),
            ):
                with self.subTest(contents=contents):
                    path.write_text(contents, encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, message):
                        release_policy.tarball_groups(path)

    def test_asset_suffix_tells_tarballs_from_installers_on_every_target(self):
        version = "3.3.0-beta1"
        for name in release_policy.release_tarballs(version):
            with self.subTest(name=name):
                self.assertEqual(release_policy.asset_suffix(name), ".tar.gz")
        self.assertEqual(
            [release_policy.asset_suffix(name) for name in release_policy.public_expected_assets(version, "validation", "unsigned")[:6]],
            [".pkg", ".pkg", ".AppImage", ".AppImage", ".AppImage", ".exe"],
        )

    def test_beta_manifest_hashes_unsigned_macos_packages_as_immutable_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.3.0-beta2"
            for name in release_policy.public_expected_assets(version, "validation", "unsigned"):
                target = root / name
                target.mkdir()
                if name in release_policy.release_tarballs(version):
                    filename = f"{name}.tar.gz"
                elif "macOS" in name:
                    filename = name.replace("-unsigned", "")
                elif "linux" in name:
                    filename = f"{name}.AppImage"
                else:
                    filename = f"wsjtx-{version}-win64.exe"
                (target / filename).write_bytes(name.encode())
            for arch in ("x86_64", "aarch64", "armhf"):
                for package_type in ("deb", "rpm"):
                    target = root / f"wsjtx-{version}-linux-{arch}-{package_type}"
                    target.mkdir()
                    (target / f"wsjtx-{arch}.{package_type}").write_bytes(arch.encode())
            (root / f"wsjtx-{version}-src.tar.gz").write_bytes(b"source")
            args = type("Args", (), {
                "artifacts": str(root), "version": version, "repository": "WSJTX/wsjtx",
                "commit": "a" * 40, "run_id": "123",
                "linux_x86_64_digest": "sha256:" + "1" * 64,
                "linux_aarch64_digest": "sha256:" + "2" * 64,
                "linux_armhf_cross_digest": "sha256:" + "3" * 64,
                "linux_armhf_digest": "sha256:" + "4" * 64,
                "macos_mode": "validation", "windows_mode": "unsigned",
            })()

            release_policy.write_manifest(args)

            manifest = json.loads((root / "release-manifest.json").read_text())
            beta_assets = {f"wsjtx-{version}-{arch}-macOS.pkg" for arch in ("arm64", "x86_64")}
            beta_assets |= {f"{name}.tar.gz" for name in release_policy.release_tarballs(version)}
            immutable_names = {entry["name"] for entry in manifest["assets"]}
            checksum_names = {
                line.split("  ", 1)[1]
                for line in (root / "SHA256SUMS").read_text().splitlines()
            }
            self.assertEqual(manifest["macos_signing"], {"mode": "unsigned", "replaceable_assets": []})
            self.assertTrue(beta_assets <= immutable_names)
            self.assertTrue(beta_assets <= checksum_names)
            hashes = {entry["name"]: entry["sha256"] for entry in manifest["assets"]}
            for arch in ("arm64", "x86_64"):
                name = f"wsjtx-{version}-{arch}-macOS.pkg"
                package = root / f"wsjtx-{version}-{arch}-macOS-unsigned.pkg" / name
                self.assertEqual(hashes[name], release_policy.hash_file(package))

            args.macos_mode = "distribution"
            with self.assertRaisesRegex(ValueError, "BETA releases publish the validated unsigned macOS packages"):
                release_policy.write_manifest(args)

    def test_signing_reports_bind_hashes_and_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0-rc1"
            commit = "b" * 40
            tag = f"v{version}"
            for arch in ("arm64", "x86_64"):
                package_dir = root / f"wsjtx-{version}-{arch}-macOS.pkg"
                package_dir.mkdir()
                package = package_dir / f"wsjtx-{version}-{arch}-macOS.pkg"
                package.write_bytes(arch.encode())
                report_dir = root / f"macos-signing-report-{version}-{arch}"
                report_dir.mkdir()
                (report_dir / "macos-signing-report.json").write_text(json.dumps({
                    "mode": "distribution", "git_sha": commit, "artifact": package.name,
                    "sha256": release_policy.hash_file(package), "notarization": {"status": "Accepted"},
                    "stapled": True, "gatekeeper_accepted": True, "team_id": "ABCDE12345",
                    "application_certificate_sha1": "A" * 40,
                    "installer_certificate_sha1": "B" * 40,
                }))
            installer_dir = root / f"wsjtx-{version}-windows-x86_64-installer-signed"
            installer_dir.mkdir()
            installer = installer_dir / "wsjtx.exe"
            installer.write_bytes(b"windows")
            request_dir = root / f"wsjtx-{version}-windows-signing-request"
            request_dir.mkdir()
            (request_dir / "request.json").write_text(json.dumps({
                "policy": "release-signing", "commit": commit, "tag": tag,
            }))
            verification_dir = root / f"wsjtx-{version}-windows-signing-verification"
            verification_dir.mkdir()
            (verification_dir / "verification.json").write_text(json.dumps({
                "commit": commit, "tag": tag, "artifact": installer.name,
                "sha256": release_policy.hash_file(installer), "status": "Valid",
                "timestamp_thumbprint": "1234", "signer_subject": "WSJT-X",
                "signer_thumbprint": "5678", "identity_verified": True,
            }))
            release_policy.verify_signing_reports(root, version, commit, tag)
            intel_report = root / f"macos-signing-report-{version}-x86_64" / "macos-signing-report.json"
            contents = intel_report.read_bytes()
            intel_report.unlink()
            with self.assertRaisesRegex(ValueError, "macos-signing-report.*x86_64"):
                release_policy.verify_signing_reports(root, version, commit, tag)
            intel_report.write_bytes(contents)
            intel_report_data = json.loads(contents)
            intel_report_data["sha256"] = "0" * 64
            intel_report.write_text(json.dumps(intel_report_data))
            with self.assertRaisesRegex(ValueError, "macOS x86_64 report hash"):
                release_policy.verify_signing_reports(root, version, commit, tag)
            intel_report.write_bytes(contents)
            (verification_dir / "verification.json").write_text("{}")
            with self.assertRaisesRegex(ValueError, "release ref"):
                release_policy.verify_signing_reports(root, version, commit, tag)

    def test_validation_reports_allow_unsigned_macos_with_signed_windows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0-rc1"
            commit = "b" * 40
            tag = f"v{version}"
            for arch in ("arm64", "x86_64"):
                package_dir = root / f"wsjtx-{version}-{arch}-macOS-unsigned.pkg"
                package_dir.mkdir()
                package = package_dir / f"wsjtx-{version}-{arch}-macOS.pkg"
                package.write_bytes(arch.encode())
                report_dir = root / f"macos-signing-report-{version}-{arch}"
                report_dir.mkdir()
                (report_dir / "macos-signing-report.json").write_text(json.dumps({
                    "mode": "validation", "git_sha": commit, "artifact": package.name,
                    "sha256": release_policy.hash_file(package), "signed": False,
                    "notarized": False, "publishable": False,
                }))
            installer_dir = root / f"wsjtx-{version}-windows-x86_64-installer-signed"
            installer_dir.mkdir()
            installer = installer_dir / "wsjtx.exe"
            installer.write_bytes(b"windows")
            request_dir = root / f"wsjtx-{version}-windows-signing-request"
            request_dir.mkdir()
            (request_dir / "request.json").write_text(json.dumps({
                "policy": "release-signing", "commit": commit, "tag": tag,
            }))
            verification_dir = root / f"wsjtx-{version}-windows-signing-verification"
            verification_dir.mkdir()
            (verification_dir / "verification.json").write_text(json.dumps({
                "commit": commit, "tag": tag, "artifact": installer.name,
                "sha256": release_policy.hash_file(installer), "status": "Valid",
                "timestamp_thumbprint": "1234", "signer_subject": "WSJT-X",
                "signer_thumbprint": "5678", "identity_verified": True,
            }))

            release_policy.verify_signing_reports(root, version, commit, tag, "validation")
            intel_report = root / f"macos-signing-report-{version}-x86_64" / "macos-signing-report.json"
            contents = intel_report.read_bytes()
            intel_report.unlink()
            with self.assertRaisesRegex(ValueError, "macos-signing-report.*x86_64"):
                release_policy.verify_signing_reports(root, version, commit, tag, "validation")
            intel_report.write_bytes(contents)
            intel_report_data = json.loads(contents)
            intel_report_data["sha256"] = "0" * 64
            intel_report.write_text(json.dumps(intel_report_data))
            with self.assertRaisesRegex(ValueError, "macOS x86_64 report hash"):
                release_policy.verify_signing_reports(root, version, commit, tag, "validation")
            intel_report.write_bytes(contents)

            report = json.loads(
                (root / f"macos-signing-report-{version}-arm64" / "macos-signing-report.json").read_text()
            )
            report["signed"] = True
            (root / f"macos-signing-report-{version}-arm64" / "macos-signing-report.json").write_text(
                json.dumps(report)
            )
            with self.assertRaisesRegex(ValueError, "unsigned package"):
                release_policy.verify_signing_reports(root, version, commit, tag, "validation")

            report["signed"] = False
            (root / f"macos-signing-report-{version}-arm64" / "macos-signing-report.json").write_text(
                json.dumps(report)
            )
            with self.assertRaisesRegex(ValueError, "Windows signing report"):
                release_policy.verify_signing_reports(root, version, commit, tag, "validation", "unsigned")
            shutil.rmtree(request_dir)
            shutil.rmtree(verification_dir)
            release_policy.verify_signing_reports(root, version, commit, tag, "validation", "unsigned")


if __name__ == "__main__":
    unittest.main()
