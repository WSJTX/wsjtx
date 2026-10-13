import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "generate-change-list.py"
REPO = "WSJTX/wsjtx"


class ChangeListTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.repo = Path(self.directory.name)
        self.env = {
            **{key: value for key, value in os.environ.items() if not key.startswith("GIT_")},
            "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_AUTHOR_EMAIL": "author@example.com",
            "GIT_COMMITTER_NAME": "Committer", "GIT_COMMITTER_EMAIL": "committer@example.com",
        }
        self.git("init", "-q", "-b", "develop")
        # Background auto-maintenance would race the directory's removal.
        self.git("config", "maintenance.auto", "false")
        self.commit("chore: start")

    def stamp(self, author="Test Author"):
        # Distinct dates, so a cherry-pick onto the same parent is a real copy.
        self.clock = getattr(self, "clock", 0) + 1
        date = f"@{1_700_000_000 + self.clock} +0000"
        return {**self.env, "GIT_AUTHOR_NAME": author, "GIT_AUTHOR_DATE": date, "GIT_COMMITTER_DATE": date}

    def git(self, *args):
        return subprocess.run(
            ["git", *args], cwd=self.repo, env=self.stamp(), text=True, capture_output=True, check=True,
        ).stdout.strip()

    def commit(self, subject, author="Test Author", change=None):
        # A distinct change per commit: git treats all empty commits as cherry-picks of each other.
        self.changes = getattr(self, "changes", 0) + 1
        change = change or f"change-{self.changes}.txt"
        (self.repo / change).write_text(subject + "\n")
        self.git("add", change)
        subprocess.run(
            ["git", "commit", "-qm", subject],
            cwd=self.repo, env=self.stamp(author), check=True, capture_output=True,
        )
        return self.git("rev-parse", "HEAD")

    def merge(self, ref, subject):
        self.git("merge", "--no-ff", "-qm", subject, ref)

    def notes(self, tag):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), REPO, tag],
            cwd=self.repo, env=self.env, text=True, capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_a_later_beta_lists_the_changes_since_the_previous_beta_in_commit_order(self):
        self.git("checkout", "-qb", "release/3.3")
        self.commit("feat: old feature")
        self.git("tag", "v3.3.0-beta1")
        self.commit("feat(decoder): new mode", author="Joe Taylor")
        self.commit("fix(wav): keep the dial context", author="David Christle")
        self.commit("fix: a later fix")
        self.git("tag", "v3.3.0-beta2")
        notes = self.notes("v3.3.0-beta2")
        self.assertTrue(notes.startswith("## Changes since v3.3.0-beta1\n"))
        self.assertIn("### New features\n- decoder: new mode (Joe Taylor)\n", notes)
        self.assertIn("### Fixes\n- wav: keep the dial context (David Christle)\n- a later fix (Test Author)\n", notes)
        self.assertNotIn("old feature", notes)
        self.assertIn(f"[Full comparison](https://github.com/{REPO}/compare/v3.3.0-beta1...v3.3.0-beta2).", notes)

    def test_a_spent_beta_number_falls_back_to_the_newest_published_tag(self):
        self.git("checkout", "-qb", "release/3.3")
        self.git("tag", "v3.3.0-beta1")
        self.commit("fix: carried by the unpublished beta2")
        self.commit("fix: new in beta3")
        self.git("tag", "v3.3.0-beta3")
        notes = self.notes("v3.3.0-beta3")
        self.assertTrue(notes.startswith("## Changes since v3.3.0-beta1\n"))
        self.assertIn("- carried by the unpublished beta2", notes)
        self.assertIn("- new in beta3", notes)

    def test_a_release_candidate_follows_the_last_beta_of_its_line(self):
        self.git("checkout", "-qb", "release/3.3")
        self.git("tag", "v3.3.0-beta4")
        self.commit("fix: freeze-time fix")
        self.git("tag", "v3.3.0-rc1")
        notes = self.notes("v3.3.0-rc1")
        self.assertTrue(notes.startswith("## Changes since v3.3.0-beta4\n"))
        self.assertIn("- freeze-time fix", notes)

    def test_a_lines_first_release_leaves_out_what_the_previous_line_shipped(self):
        self.commit("feat: in 3.2 already")
        self.git("tag", "v3.0.2")
        self.git("checkout", "-qb", "release/3.0", "HEAD~1")
        self.commit("fix: only on the old line")
        self.git("tag", "v3.0.3")
        self.git("checkout", "-q", "develop")
        self.git("checkout", "-qb", "release/3.2")
        self.commit("fix: only on release/3.2")
        self.git("checkout", "-q", "develop")
        picked = self.commit("fix(gui): picked into 3.2", change="gui.txt")
        self.git("checkout", "-q", "release/3.2")
        self.git("cherry-pick", picked)
        self.git("tag", "v3.2.0-rc1")
        self.git("checkout", "-q", "develop")
        self.merge("v3.0.3", "Merge 3.0.3 into develop")
        self.commit("feat: new on develop")
        self.git("checkout", "-qb", "release/3.3")
        self.commit("build(release): start 3.3 at BETA 1")
        self.git("tag", "v3.3.0-beta1")
        notes = self.notes("v3.3.0-beta1")
        self.assertTrue(notes.startswith("## Changes since v3.2.0-rc1\n"))
        self.assertIn("- new on develop", notes)
        for shipped in ("in 3.2 already", "only on release/3.2", "picked into 3.2", "only on the old line"):
            self.assertNotIn(shipped, notes)
        self.assertIn(f"/compare/v3.2.0-rc1...v3.3.0-beta1", notes)

    def test_a_beta_after_the_previous_ga_is_merged_back_lists_only_new_changes(self):
        self.git("checkout", "-qb", "release/3.2")
        self.git("tag", "v3.2.0-rc1")
        self.git("checkout", "-q", "develop")
        original = self.commit("fix(gui): first on develop", change="gui.txt")
        self.git("checkout", "-qb", "release/3.3")
        self.git("tag", "v3.3.0-beta1")
        self.git("checkout", "-q", "release/3.2")
        self.git("cherry-pick", original)
        self.commit("fix: only on release/3.2")
        self.git("tag", "v3.2.0")
        self.git("checkout", "-q", "develop")
        self.merge("v3.2.0", "Merge 3.2.0 into develop")
        self.commit("fix: new on develop")
        self.git("checkout", "-q", "release/3.3")
        self.merge("develop", "Merge develop into release/3.3")
        self.git("tag", "v3.3.0-beta2")
        notes = self.notes("v3.3.0-beta2")
        self.assertTrue(notes.startswith("## Changes since v3.3.0-beta1\n"))
        self.assertIn("### Fixes\n- new on develop (Test Author)\n\n", notes)
        self.assertNotIn("first on develop", notes)
        self.assertNotIn("only on release/3.2", notes)

    def test_copies_that_a_merged_back_ga_shipped_are_not_listed_again(self):
        self.git("checkout", "-qb", "release/3.2")
        self.commit("fix: on release/3.2 before the picks")
        self.git("tag", "v3.2.0-rc1")
        self.git("checkout", "-q", "develop")
        self.git("checkout", "-qb", "release/3.3")
        self.git("tag", "v3.3.0-beta1")
        self.git("checkout", "-q", "develop")
        original = self.commit("fix(gui): develop original shipped in 3.2.0", change="gui.txt")
        self.git("checkout", "-q", "release/3.2")
        self.git("cherry-pick", original)
        port = self.commit("fix(cat): made on release/3.2 first", change="cat.txt")
        self.git("tag", "v3.2.0")
        self.git("checkout", "-q", "develop")
        self.git("cherry-pick", port)
        self.merge("v3.2.0", "Merge 3.2.0 into develop")
        self.commit("fix: new on develop")
        self.git("checkout", "-q", "release/3.3")
        self.merge("develop", "Merge develop into release/3.3")
        self.git("tag", "v3.3.0-beta2")
        self.git("tag", "v3.3.0")
        for tag in ("v3.3.0-beta2", "v3.3.0"):
            notes = self.notes(tag)
            self.assertIn("### Fixes\n- new on develop (Test Author)\n\n", notes, tag)
            self.assertNotIn("develop original", notes, tag)
            self.assertNotIn("made on release/3.2 first", notes, tag)

    def test_a_ga_lists_everything_since_the_previous_ga(self):
        self.git("tag", "v3.2.0")
        self.git("checkout", "-qb", "release/3.3")
        self.commit("feat: from beta1")
        self.git("tag", "v3.3.0-beta1")
        self.commit("fix: from rc1")
        self.git("tag", "v3.3.0-rc1")
        self.commit("fix: from GA")
        self.git("tag", "v3.3.0")
        notes = self.notes("v3.3.0")
        self.assertTrue(notes.startswith("## Changes since v3.2.0\n"))
        for change in ("from beta1", "from rc1", "from GA"):
            self.assertIn(change, notes)
        self.commit("fix: patch")
        self.git("tag", "v3.3.1-rc1")
        self.git("tag", "v3.3.1")
        patch = self.notes("v3.3.1")
        self.assertTrue(patch.startswith("## Changes since v3.3.0\n"))
        self.assertIn("- patch", patch)
        self.assertNotIn("from GA", patch)
        self.assertEqual(self.notes("v3.3.0"), notes)

    def test_entries_are_grouped_cleaned_and_counted(self):
        self.git("tag", "v3.3.0-beta1")
        self.commit("feat(jtty)!: change the macro syntax")
        self.commit("Fix(gui): uppercase type (#458)")
        self.commit("fix: address PR #569 review for @k1jt")
        self.commit("perf(fft): use SIMD")
        self.commit("fix(gui): uppercase type (#458)")
        self.commit("fix(jtty, tests): user-facing despite a test scope")
        self.commit("fix(release): pipeline change")
        self.commit("feat(ci, tests): pipeline feature")
        self.commit("test: cover the decoder")
        self.commit("Untyped subject that mentions fix")
        self.git("checkout", "-qb", "topic")
        self.commit("docs: topic change")
        self.git("checkout", "-q", "develop")
        self.merge("topic", "Merge pull request #12 from topic")
        self.git("tag", "v3.3.0-beta2")
        notes = self.notes("v3.3.0-beta2")
        self.assertIn("### New features\n- jtty: change the macro syntax (breaking) (Test Author)\n\n", notes)
        self.assertIn(
            "### Fixes\n"
            "- gui: uppercase type (Test Author)\n"
            "- address PR `#569` review for `@k1jt` (Test Author)\n"
            "- jtty, tests: user-facing despite a test scope (Test Author)\n\n",
            notes,
        )
        self.assertIn("### Performance\n- fft: use SIMD (Test Author)\n", notes)
        self.assertNotIn("pipeline", notes)
        self.assertNotIn("Merge pull request", notes)
        self.assertIn("Plus 5 other commits: [full comparison]", notes)

    def test_references_are_neutralized_outside_code_spans_only(self):
        self.git("tag", "v3.3.0-beta1")
        for subject in (
            "fix: mail ops@example.com about C# and see https://example.com/docs/#anchor",
            "fix: link WSJTX/wsjtx-internal#569, GH-12 and Hamlib/Hamlib#1940",
            "fix: keep `a #12 @b` and <CR> literal",
            "fix(@team): scope reference",
            "fix: handle a lone ` in macros, see #12 and @k1jt",
        ):
            self.commit(subject)
        self.git("tag", "v3.3.0-beta2")
        notes = self.notes("v3.3.0-beta2")
        self.assertIn("- mail ops@example.com about C# and see https://example.com/docs/#anchor (Test Author)\n", notes)
        self.assertIn("- link `WSJTX/wsjtx-internal#569`, `GH-12` and `Hamlib/Hamlib#1940` (Test Author)\n", notes)
        self.assertIn("- keep `a #12 @b` and &lt;CR> literal (Test Author)\n", notes)
        self.assertIn("- `@team`: scope reference (Test Author)\n", notes)
        self.assertIn("- handle a lone ` in macros, see `#12` and `@k1jt` (Test Author)\n", notes)

    def test_a_range_without_listed_changes_says_so(self):
        self.git("tag", "v3.3.0-beta1")
        self.commit("test: only tests")
        self.git("tag", "v3.3.0-beta2")
        self.assertIn("\nNo user-facing changes; 1 other commit: [full comparison]", self.notes("v3.3.0-beta2"))
        self.git("tag", "v3.3.0-beta3")
        notes = self.notes("v3.3.0-beta3")
        self.assertTrue(notes.startswith("## Changes since v3.3.0-beta2\n"))
        self.assertIn("No source changes since v3.3.0-beta2.", notes)

    def test_no_earlier_release_tag_prints_nothing(self):
        for tag in ("not-a-release", "build/v3.2.0-rc1", "v3.2", "v3.2.0-alpha1", "v3.3.0-beta1"):
            self.git("tag", tag)
        self.assertEqual(self.notes("v3.3.0-beta1"), "")


if __name__ == "__main__":
    unittest.main()
