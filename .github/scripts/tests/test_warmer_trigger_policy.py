# SPDX-License-Identifier: GPL-3.0-or-later
import fnmatch
import os
import pathlib
import re
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
WARMER = ".github/workflows/warm-dependency-caches.yml"
WARM = (ROOT / WARMER).read_text()
STEP_REFERENCE = re.compile(r"steps\.([A-Za-z0-9_-]+)\.outputs\.")
SCRIPT = re.compile(r"\.github/scripts/[A-Za-z0-9_.+-]+")


def push_filters():
    push = WARM.split("\non:\n", 1)[1].split("\n  schedule:", 1)[0]
    return re.findall(r'^      - "([^"]+)"$', push, re.MULTILINE)


def github_glob(pattern):
    """on.push.paths semantics for the metacharacters the warmer uses: * stops at /, ** does not."""
    if re.search(r"[?+\[\]!]", pattern):
        raise AssertionError(f"unsupported filter syntax: {pattern}")
    regex = re.escape(pattern).replace(r"\*\*/", "(?:.*/)?").replace(r"\*\*", ".*").replace(r"\*", "[^/]*")
    return re.compile(regex + r"\Z")


def matches(pattern, path):
    return bool(github_glob(pattern).match(path))


def tracked_files():
    output = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
    return sorted(path for path in output.decode().split("\0") if path)


def image_inputs():
    """Every file linux_ci_image_inputs returns for any profile: the Linux CI image fingerprints."""
    script = ".github/scripts/linux-ci-image-inputs.sh"
    cases = (ROOT / script).read_text().split('case "$profile" in', 1)[1]
    profiles = [profile for label in re.findall(r"^    ([a-z0-9|-]+)\)$", cases, re.MULTILINE)
                for profile in label.split("|")]
    files = set()
    for profile in profiles:
        files.update(subprocess.check_output(
            ["bash", "-c", 'source "$1"; linux_ci_image_inputs "$2"', "inputs", script, profile],
            cwd=ROOT, text=True,
        ).split())
    return profiles, files


def warmer_closure():
    """The warmer and every local workflow or composite action it reaches through uses:."""
    seen, pending = set(), [WARMER]
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        text = (ROOT / path).read_text()
        for target in re.findall(r"^\s+(?:- )?uses: \./(\.github/\S+)$", text, re.MULTILINE):
            pending.append(target if target.endswith(".yml") else f"{target}/action.yml")
    return seen


def dependency_key_inputs(path):
    """Files that change a non-ccache cache key in one workflow or action.

    A key depends on the files its hashFiles() names and, through steps.<id>.outputs,
    on the scripts the producing steps run. ccache keys carry the commit SHA, so the
    develop CI and the weekly schedule refresh them; they are not dependency caches.
    """
    text = (ROOT / path).read_text()
    chunks = re.split(r"(?m)^(?=\s+- [A-Za-z_-]+:(?: |$))", text)
    by_id = {}
    for chunk in chunks:
        step_id = re.search(r"^\s+(?:- )?id: ([A-Za-z0-9_-]+)\s*$", chunk, re.MULTILINE)
        if step_id:
            by_id[step_id[1]] = chunk
    inputs, pending, seen = set(), [], set()

    def follow(key):
        if key.startswith("ccache-"):
            return
        for arguments in re.findall(r"hashFiles\(([^)]*)\)", key):
            inputs.update(re.findall(r"'([^']+)'", arguments))
        pending.extend(STEP_REFERENCE.findall(key))

    for chunk in chunks:
        for key in re.findall(r"^\s+key: (.+)$", chunk, re.MULTILINE):
            follow(key)
    while pending:
        step_id = pending.pop()
        if step_id in seen or step_id not in by_id:
            continue
        seen.add(step_id)
        chunk = by_id[step_id]
        keys = re.findall(r"^\s+key: (.+)$", chunk, re.MULTILINE)
        for key in keys:
            follow(key)
        if not keys:
            inputs.update(SCRIPT.findall(chunk))
            pending.extend(STEP_REFERENCE.findall(chunk))
    # A key script may hash its sibling scripts (macos-qt-cache-key.sh hashes build-qt-macos.sh).
    for script in [item for item in inputs if item.endswith(".sh")]:
        for names in re.findall(r"for script in ([^;]+); do", (ROOT / script).read_text()):
            inputs.update(f".github/scripts/{name}" for name in names.split())
    return inputs


def key_and_image_inputs():
    """Files that feed a dependency-cache key the warmer saves or a Linux CI image it refreshes.

    The composite actions that hold dependency keys count as inputs too. build-macos.yml
    also holds dependency keys, but they must equal warm-macos-deps' keys
    (test_native_cache_policy.py), so editing a key there also edits the action.

    Keys also carry values their callers pass as literals: hamlib_branch, and the macOS
    runner and deployment_target, in the workflows that call the builds (ci.yml,
    release.yml and the warmer among them). No workflow file is a filter, so a push that
    changes one of those values does not start the warmer; the comment above the filters
    says to dispatch it.
    """
    _, files = image_inputs()
    for path in warmer_closure():
        found = dependency_key_inputs(path)
        files.update(found)
        if found and path.startswith(".github/actions/"):
            files.add(path)
    return files


def detect_patterns():
    push = WARM.split("            push)\n", 1)[1].split("--classify", 1)[0]
    block = push.split('case "$path" in\n', 1)[1].split("esac", 1)[0]
    return [pattern for label in re.findall(r"^\s+([^\s;()]+)\)$", block, re.MULTILINE)
            for pattern in label.split("|")]


def select_warmers(path):
    selector = re.search(r"        run: \|\n(?P<body>.*?)(?=^  refresh-linux-images:)",
                         WARM, re.MULTILINE | re.DOTALL).group("body")
    git_stub = textwrap.dedent("""
        git() {
          case "$1" in
            cat-file) return 0 ;;
            diff) printf '%s\\n' "$CHANGED_PATHS" ;;
            *) return 2 ;;
          esac
        }
    """)
    with tempfile.TemporaryDirectory() as directory:
        output = pathlib.Path(directory) / "output"
        result = subprocess.run(
            ["bash", "-c", git_stub + textwrap.dedent(selector)], cwd=ROOT,
            env={**os.environ, "EVENT_NAME": "push", "TARGETS": "", "SCHEDULE": "",
                 "BEFORE_SHA": "previous", "CURRENT_SHA": "current", "CHANGED_PATHS": path,
                 "HAMLIB_BRANCH": "4.7.2", "GITHUB_OUTPUT": str(output),
                 "GITHUB_STEP_SUMMARY": str(pathlib.Path(directory) / "summary")},
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)
        return dict(line.split("=", 1) for line in output.read_text().splitlines())


class WarmerTriggerPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.filters = push_filters()
        cls.inputs = key_and_image_inputs()
        cls.tracked = tracked_files()

    def test_derivation_finds_each_kind_of_input(self):
        profiles, _ = image_inputs()
        classifier = (ROOT / ".github/scripts/is-linux-ci-image-input.sh").read_text()
        classified = re.findall(r'is_linux_ci_image_input ([a-z0-9-]+) "\$path"', classifier)
        self.assertCountEqual(profiles, classified)
        for path in (
            ".github/images/linux-ci/Dockerfile.noble",            # image fingerprint
            ".github/scripts/build-hamlib-armhf-cross.sh",          # image fingerprint (armhf)
            ".github/scripts/build-hamlib-windows.sh",              # hashFiles
            ".github/scripts/build-qt-linux.sh",                    # Jammy image fingerprint
            ".github/scripts/macos-qt-cache-key.sh",                # step output is the Qt key
            ".github/scripts/build-qt-macos.sh",                    # hashed by the Qt key script
            ".github/actions/warm-macos-deps/action.yml",           # holds dependency keys
            ".github/actions/build-windows-deps/action.yml",        # holds dependency keys
        ):
            with self.subTest(path=path):
                self.assertIn(path, self.inputs)
        for path in (".github/scripts/select-ccache-key-suffix.sh", ".github/workflows/build-macos.yml",
                     "CMakeLists.txt", ".github/scripts/build-linux-armhf-cross.sh"):
            with self.subTest(path=path):
                self.assertNotIn(path, self.inputs)

    def test_every_filter_matches_a_key_or_image_input(self):
        for pattern in self.filters:
            with self.subTest(pattern=pattern):
                self.assertTrue(any(matches(pattern, path) for path in self.inputs),
                                f"{pattern} feeds no dependency-cache key or Linux CI image")

    def test_filters_match_no_other_tracked_file(self):
        for pattern in self.filters:
            if pattern.endswith("/**"):
                continue
            with self.subTest(pattern=pattern):
                extra = [path for path in self.tracked if matches(pattern, path) and path not in self.inputs]
                self.assertEqual(extra, [], f"{pattern} starts the warmer for files that feed no key or image")

    def test_every_key_and_image_input_starts_the_warmer(self):
        for path in sorted(self.inputs):
            with self.subTest(path=path):
                self.assertTrue(any(matches(pattern, path) for pattern in self.filters),
                                f"{path} feeds a key or image but no push filter matches it")

    def test_every_detect_branch_is_covered_by_a_filter(self):
        patterns = detect_patterns()
        self.assertIn(".github/scripts/boost-log-queue.patch", patterns)
        for pattern in patterns:
            with self.subTest(pattern=pattern):
                selected = [path for path in self.tracked if fnmatch.fnmatchcase(path, pattern)]
                self.assertTrue(selected, f"detect branch {pattern} matches no tracked file")
                for path in selected:
                    self.assertTrue(any(matches(f, path) for f in self.filters),
                                    f"detect branch {pattern} routes {path}, which no push filter starts")

    def test_every_key_and_image_input_selects_a_warmer(self):
        targets = ("linux_arm64", "linux_armhf", "linux_asan_ubsan", "windows", "macos_arm64", "macos_x86_64")
        for path in sorted(self.inputs):
            with self.subTest(path=path):
                outputs = select_warmers(path)
                self.assertTrue(outputs["image_set"] != "none" or any(outputs[t] == "true" for t in targets),
                                f"{path} starts the warmer but selects nothing")

    def test_github_glob_semantics(self):
        self.assertTrue(matches(".github/scripts/build-*-macos.sh", ".github/scripts/build-qt-macos.sh"))
        self.assertFalse(matches(".github/scripts/build-*-macos.sh", ".github/scripts/x/build-qt-macos.sh"))
        self.assertTrue(matches(".github/images/linux-ci/**", ".github/images/linux-ci/a/b"))
        self.assertTrue(matches("tests/**/CMakeLists.txt", "tests/CMakeLists.txt"))
        self.assertFalse(matches("CMakeLists.txt", "tests/CMakeLists.txt"))


if __name__ == "__main__":
    unittest.main()
