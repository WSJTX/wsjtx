import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "check-candidate-tag.sh"
TAG = "build/v3.2.0-rc1"


class CheckCandidateTagTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.repo = Path(self.directory.name)
        self.git("init", "-q")
        self.git(
            "-c", "user.name=Test", "-c", "user.email=test@example.com",
            "commit", "--allow-empty", "-qm", "first",
        )
        self.first = self.git("rev-parse", "HEAD").stdout.strip()
        self.git(
            "-c", "user.name=Test", "-c", "user.email=test@example.com",
            "commit", "--allow-empty", "-qm", "second",
        )
        self.second = self.git("rev-parse", "HEAD").stdout.strip()

    def git(self, *args, input=None):
        return subprocess.run(
            ["git", *args], cwd=self.repo, input=input, text=True,
            capture_output=True, check=True,
        )

    def check_tag(self, expected_sha=None, cwd=None):
        return subprocess.run(
            ["bash", SCRIPT, TAG, expected_sha or self.second],
            cwd=cwd or self.repo, text=True, capture_output=True,
        )

    def test_missing_tag_allows_creation(self):
        result = self.check_tag()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "exists=false\n")

    def test_matching_lightweight_tag_is_idempotent(self):
        self.git("tag", TAG, self.second)
        result = self.check_tag()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "exists=true\n")

    def test_matching_annotated_tag_is_idempotent(self):
        self.git(
            "-c", "user.name=Test", "-c", "user.email=test@example.com",
            "tag", "-am", "candidate", TAG, self.second,
        )
        result = self.check_tag()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "exists=true\n")

    def test_different_commit_is_rejected(self):
        self.git("tag", TAG, self.first)
        result = self.check_tag()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("release tags are never moved", result.stderr)

    def test_noncommit_tag_is_rejected(self):
        blob = self.git("hash-object", "-w", "--stdin", input="not a commit").stdout.strip()
        self.git("update-ref", f"refs/tags/{TAG}", blob)
        result = self.check_tag()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not identify a commit", result.stderr)

    def test_git_failure_is_not_treated_as_missing_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.check_tag(cwd=directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not inspect", result.stderr)


if __name__ == "__main__":
    unittest.main()
