# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import pathlib
import re
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
CI = (ROOT / ".github/workflows/ci.yml").read_text()
SCRIPT = ROOT / ".github/scripts/select-ci-scope.sh"
SCOPE_LABELS = ["full-ci", "sanitizers", "thread-sanitizer"]
FULL_CI_LEGS = ("macos", "macos-intel", "linux-arm", "linux-armhf", "windows")
RETENTION_LEGS = ("linux", "linux-arm", "linux-armhf", "macos", "macos-intel", "windows")
PR = "refs/pull/852/merge"


def job(name: str) -> str:
    start = CI.index(f"\n  {name}:\n") + 1
    following = re.search(r"\n  [A-Za-z0-9_-]+:\n", CI[start:])
    return CI[start:start + following.start() + 1] if following else CI[start:]


def step(step_id: str) -> str:
    prepare = job("prepare")
    start = prepare.index(f"      - id: {step_id}\n")
    following = re.search(r"\n      (?:- |# )", prepare[start + 1:])
    return prepare[start:start + 2 + following.start()] if following else prepare[start:]


def prepare_if() -> str:
    return job("prepare").split("    if: >-\n", 1)[1].split("\n    runs-on:", 1)[0]


def concurrency_group() -> str:
    block = CI.split("\nconcurrency:\n", 1)[1].split("\n  cancel-in-progress:", 1)[0]
    return re.findall(r"\$\{\{(.*?)\}\}", block, re.DOTALL)[-1]


def label_lists(text: str) -> list:
    return [json.loads(literal) for literal in re.findall(r"fromJSON\('(\[[^']*\])'\)", text)]


def labeled_clause(text: str) -> str:
    match = re.search(
        r"\(\s*github\.event\.action != 'labeled'.*?startsWith\(github\.head_ref, 'release/'\)\)",
        text, re.DOTALL,
    )
    return " ".join(match.group(0).split())


class Text(str):
    """A context string: GitHub's == and != compare strings without regard to case."""

    def __eq__(self, other):
        return isinstance(other, str) and self.lower() == other.lower()

    def __ne__(self, other):
        return not self == other

    __hash__ = str.__hash__


def evaluate(expression: str, context: dict):
    """Evaluate one of ci.yml's GitHub expressions with Python's equivalent operators."""
    python = re.sub(r"\bgithub(?:\.[A-Za-z_][A-Za-z0-9_]*)+",
                    lambda match: f"value({match.group(0)!r})", expression)
    python = python.replace("&&", " and ").replace("||", " or ")
    python = re.sub(r"!(?!=)", " not ", python)

    def value(name):
        found = context.get(name)
        return Text(found) if isinstance(found, str) else found

    def text(value):
        return "" if value is None else str(value).lower()

    def contains(search, item):
        if isinstance(search, list):
            return any(text(element) == text(item) for element in search)
        return text(item) in text(search)

    functions = {
        "value": value,
        "contains": contains,
        "fromJSON": json.loads,
        "startsWith": lambda value, prefix: text(value).startswith(text(prefix)),
    }
    return eval(" ".join(python.split()), {"__builtins__": {}}, functions)


def run_scope(event, action="", label="", labels="", base="", head="", ref=""):
    with tempfile.TemporaryDirectory() as directory:
        output = pathlib.Path(directory) / "output"
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "EVENT_NAME": event, "EVENT_ACTION": action, "EVENT_LABEL": label,
            "PR_LABELS": labels, "BASE_REF": base, "HEAD_REF": head,
            "GITHUB_REF": ref, "GITHUB_OUTPUT": str(output),
        }
        subprocess.run(["bash", str(SCRIPT)], env=env, cwd=ROOT,
                       capture_output=True, text=True, check=True)
        return dict(line.split("=", 1) for line in output.read_text().splitlines())


def scope(full_ci, asan, tsan, runtime_tests):
    return {"full_ci": full_ci, "asan": asan, "tsan": tsan, "runtime_tests": runtime_tests}


class CiScopePolicyTests(unittest.TestCase):
    def test_scope_script_self_test_passes(self):
        result = subprocess.run(["bash", str(SCRIPT), "--self-test"],
                                env={"PATH": os.environ.get("PATH", "/usr/bin:/bin")},
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("select-ci-scope.sh self-test passed", result.stdout)

    def test_scope_truth_table(self):
        routine = scope("false", "false", "false", "exclude")
        full = scope("true", "true", "false", "include")
        cases = [
            ("routine PR opened", dict(event="pull_request", action="opened", base="develop", head="topic", ref=PR), routine),
            ("routine PR synchronize", dict(event="pull_request", action="synchronize", base="develop", head="topic", ref=PR), routine),
            ("push to a PR still carrying full-ci", dict(event="pull_request", action="synchronize", labels="full-ci", base="develop", head="topic", ref=PR), routine),
            ("full-ci applied", dict(event="pull_request", action="labeled", label="full-ci", labels="full-ci", base="develop", head="topic", ref=PR), full),
            ("opened with full-ci", dict(event="pull_request", action="opened", labels="full-ci", base="develop", head="topic", ref=PR), full),
            ("reopened with full-ci", dict(event="pull_request", action="reopened", labels="bug,full-ci", base="develop", head="topic", ref=PR), full),
            ("another label on a full-ci PR", dict(event="pull_request", action="labeled", label="documentation", labels="documentation,full-ci", base="develop", head="topic", ref=PR), routine),
            ("sanitizers on any PR event", dict(event="pull_request", action="synchronize", labels="sanitizers", base="develop", head="topic", ref=PR), scope("false", "true", "false", "exclude")),
            ("sanitizers applied to a full-ci PR", dict(event="pull_request", action="labeled", label="sanitizers", labels="full-ci,sanitizers", base="develop", head="topic", ref=PR), scope("false", "true", "false", "exclude")),
            ("thread-sanitizer", dict(event="pull_request", action="synchronize", labels="thread-sanitizer", base="develop", head="topic", ref=PR), scope("false", "false", "true", "exclude")),
            ("full-ci applied with thread-sanitizer", dict(event="pull_request", action="labeled", label="full-ci", labels="full-ci,thread-sanitizer", base="develop", head="topic", ref=PR), scope("true", "true", "true", "include")),
            ("not-full-ci is not full-ci", dict(event="pull_request", action="labeled", label="not-full-ci", labels="not-full-ci", base="develop", head="topic", ref=PR), routine),
            ("labels match exactly", dict(event="pull_request", action="opened", labels="full-ci-later,no-sanitizers,thread-sanitizer-off", base="develop", head="topic", ref=PR), routine),
            ("Full-CI applied", dict(event="pull_request", action="labeled", label="Full-CI", labels="Full-CI", base="develop", head="topic", ref=PR), full),
            ("opened with FULL-CI", dict(event="pull_request", action="opened", labels="bug,FULL-CI", base="develop", head="topic", ref=PR), full),
            ("Sanitizers and Thread-Sanitizer", dict(event="pull_request", action="synchronize", labels="Sanitizers,Thread-Sanitizer", base="develop", head="topic", ref=PR), scope("false", "true", "true", "exclude")),
            ("release PR keeps label presence", dict(event="pull_request", action="synchronize", labels="full-ci", base="release/3.2", head="topic", ref=PR), full),
            ("PR out of a release branch", dict(event="pull_request", action="synchronize", labels="full-ci", base="develop", head="release/3.2", ref=PR), full),
            ("release PR, other label", dict(event="pull_request", action="labeled", label="documentation", labels="documentation,full-ci", base="release/3.2", head="topic", ref=PR), full),
            ("release PR without labels", dict(event="pull_request", action="synchronize", base="release/3.2", head="topic", ref=PR), scope("false", "false", "false", "include")),
            ("Release/ base counts as release/", dict(event="pull_request", action="synchronize", labels="full-ci", base="Release/3.3", head="topic", ref=PR), full),
            ("release-like head is not a release branch", dict(event="pull_request", action="synchronize", labels="full-ci", base="develop", head="release-notes", ref=PR), routine),
            ("push to develop leaves runtime tests to the nightly", dict(event="push", ref="refs/heads/develop"), scope("false", "false", "false", "exclude")),
            ("push to a release branch", dict(event="push", ref="refs/heads/release/3.2"), scope("false", "false", "false", "include")),
            ("manual run", dict(event="workflow_dispatch", ref="refs/heads/develop"), scope("true", "false", "false", "include")),
        ]
        for name, inputs, expected in cases:
            with self.subTest(name):
                self.assertEqual(run_scope(**inputs), expected)

    def test_prepare_and_script_share_the_label_list_and_concurrency_names_only_full_ci(self):
        script_list = re.search(r"^SCOPE_LABELS='(\[[^']*\])'$", SCRIPT.read_text(), re.MULTILINE).group(1)
        self.assertEqual(json.loads(script_list), SCOPE_LABELS)
        self.assertEqual(label_lists(CI), [SCOPE_LABELS])
        self.assertEqual(label_lists(prepare_if()), [SCOPE_LABELS])
        full_ci_only = (
            "( github.event.action != 'labeled' || github.event.label.name == 'full-ci' || "
            "startsWith(github.base_ref, 'release/') || startsWith(github.head_ref, 'release/'))"
        )
        self.assertEqual(labeled_clause(concurrency_group()), full_ci_only)
        scope_list = "contains(fromJSON('%s'), github.event.label.name)" % json.dumps(SCOPE_LABELS, separators=(",", ":"))
        self.assertEqual(labeled_clause(prepare_if()).replace(scope_list, "github.event.label.name == 'full-ci'"),
                         full_ci_only)

    def test_every_scope_label_changes_the_scope_and_no_other_label_does(self):
        routine = run_scope("pull_request", "synchronize", "", "", "develop", "topic", PR)
        for label in SCOPE_LABELS:
            with self.subTest(label=label):
                self.assertNotEqual(run_scope("pull_request", "labeled", label, label, "develop", "topic", PR), routine)
        for label in ("documentation", "dependencies", "not-full-ci", "full-ci-later"):
            with self.subTest(label=label):
                self.assertEqual(run_scope("pull_request", "labeled", label, label, "develop", "topic", PR), routine)

    def test_only_full_ci_joins_the_pr_group_and_other_labels_cancel_nothing(self):
        base = {"github.repository": "WSJTX/wsjtx-internal", "github.workflow": "CI", "github.run_id": 9001}

        def pr(action, label=None, base_ref="develop", head_ref="topic"):
            return {**base, "github.event_name": "pull_request", "github.event.action": action,
                    "github.event.label.name": label, "github.base_ref": base_ref,
                    "github.head_ref": head_ref, "github.ref": PR, "github.event.pull_request.number": 852}

        def push(ref, repository="WSJTX/wsjtx-internal"):
            return {**base, "github.repository": repository, "github.event_name": "push", "github.ref": ref}

        cases = [
            ("other label", pr("labeled", "documentation"), False, 9001),
            ("full-ci applied", pr("labeled", "full-ci"), True, 852),
            ("Full-CI applied", pr("labeled", "Full-CI"), True, 852),
            # A sanitizer label starts its own run beside a full run in progress.
            ("sanitizers applied", pr("labeled", "sanitizers"), True, 9001),
            ("thread-sanitizer applied", pr("labeled", "thread-sanitizer"), True, 9001),
            ("Sanitizers applied", pr("labeled", "Sanitizers"), True, 9001),
            ("not-full-ci applied", pr("labeled", "not-full-ci"), False, 9001),
            ("Documentation applied", pr("labeled", "Documentation"), False, 9001),
            ("synchronize", pr("synchronize"), True, 852),
            ("opened", pr("opened"), True, 852),
            ("reopened", pr("reopened"), True, 852),
            ("release PR, other label", pr("labeled", "documentation", base_ref="release/3.2"), True, 852),
            ("release PR, sanitizers applied", pr("labeled", "sanitizers", base_ref="release/3.2"), True, 852),
            ("PR out of release, other label", pr("labeled", "documentation", head_ref="release/3.2"), True, 852),
            ("PR into Release/3.3, other label", pr("labeled", "documentation", base_ref="Release/3.3"), True, 852),
            ("push to develop", push("refs/heads/develop"), True, "refs/heads/develop"),
            ("push to a release branch", push("refs/heads/release/3.2"), True, 9001),
            ("public release-branch push", push("refs/heads/release/3.2", "WSJTX/wsjtx"), False, 9001),
            ("manual run", {**base, "github.event_name": "workflow_dispatch", "github.ref": "refs/heads/develop"}, True, 9001),
        ]
        for name, context, runs, group in cases:
            with self.subTest(name):
                self.assertEqual(bool(evaluate(prepare_if(), context)), runs)
                self.assertEqual(evaluate(concurrency_group(), context), group)

    def test_only_pull_request_runs_cancel_the_run_in_progress(self):
        line = re.findall(r"^  cancel-in-progress: (.+)$", CI, re.MULTILINE)
        self.assertEqual(line, ["${{ github.event_name == 'pull_request' }}"])
        expression = line[0][len("${{"):-len("}}")]
        for event, cancels in (("pull_request", True), ("push", False), ("workflow_dispatch", False)):
            with self.subTest(event=event):
                self.assertIs(evaluate(expression, {"github.event_name": event}), cancels)

    def test_opt_in_legs_key_on_the_scope_outputs(self):
        for name in FULL_CI_LEGS:
            with self.subTest(job=name):
                self.assertIn("\n    if: needs.prepare.outputs.full_ci == 'true'\n", job(name))
        self.assertIn("\n    if: needs.prepare.outputs.asan == 'true'\n", job("linux-sanitizers"))
        self.assertIn("\n    if: needs.prepare.outputs.tsan == 'true'\n", job("linux-tsan"))
        self.assertIn("validate_fortran_fallback: ${{ needs.prepare.outputs.full_ci == 'true' }}\n", job("linux"))
        self.assertNotIn("\n    if:", job("linux"))
        self.assertIn("FULL_CI: ${{ steps.scope.outputs.full_ci }}\n", step("packaging-policy"))
        self.assertIn("        if: steps.scope.outputs.full_ci == 'true'\n", step("armhf-image"))
        # Labels reach the run only through the scope step.
        self.assertEqual(CI.count("github.event.pull_request.labels"), 1)
        self.assertIn("PR_LABELS: ${{ join(github.event.pull_request.labels.*.name, ',') }}\n", step("scope"))
        self.assertNotIn("contains(github.event.pull_request.labels", CI)

    def test_prepare_selects_the_scope_and_self_tests_it(self):
        prepare = job("prepare")
        for output in ("full_ci", "asan", "tsan", "runtime_tests"):
            self.assertIn(f"\n      {output}: ${{{{ steps.scope.outputs.{output} }}}}\n", prepare)
        self.assertIn("\n          .github/scripts/select-ci-scope.sh --self-test\n", prepare)
        scope_step = step("scope")
        for name, value in (("EVENT_NAME", "github.event_name"), ("EVENT_ACTION", "github.event.action"),
                            ("EVENT_LABEL", "github.event.label.name"), ("BASE_REF", "github.base_ref"),
                            ("HEAD_REF", "github.head_ref")):
            self.assertIn(f"\n          {name}: ${{{{ {value} }}}}\n", scope_step)
        self.assertIn("\n        run: .github/scripts/select-ci-scope.sh\n", scope_step)
        self.assertEqual(scope_step.split("        run:", 1)[1].count("${{"), 0)
        self.assertLess(prepare.index("- id: scope\n"), prepare.index("- id: packaging-policy\n"))
        self.assertLess(prepare.index("- id: scope\n"), prepare.index("- id: armhf-image\n"))
        self.assertTrue(os.access(SCRIPT, os.X_OK))

    def test_runtime_scope_reaches_only_the_linux_leg(self):
        self.assertIn("\n      runtime_tests: ${{ needs.prepare.outputs.runtime_tests }}\n", job("linux"))
        self.assertEqual(CI.count("\n      runtime_tests: "), 2)  # the prepare output and the linux leg

    def test_ci_packages_are_kept_seven_days(self):
        for name in RETENTION_LEGS:
            with self.subTest(job=name):
                self.assertIn("\n      artifact_retention_days: 7\n", job(name))
        for name in ("linux-sanitizers", "linux-tsan", "prepare", "prune-linux-ccache"):
            with self.subTest(job=name):
                self.assertNotIn("artifact_retention_days", job(name))

    def test_prune_job_is_the_only_cache_writer_and_never_fails_the_run(self):
        prune = job("prune-linux-ccache")
        self.assertIn("\n    needs: [prepare, linux, linux-arm, linux-armhf]\n", prune)
        self.assertIn("\n    if: always() && !cancelled() && needs.prepare.outputs.save_linux_ccache == 'true'\n", prune)
        self.assertIn("\n    runs-on: ubuntu-latest\n", prune)
        self.assertIn("\n    timeout-minutes: 5\n", prune)
        self.assertIn("\n    continue-on-error: true\n", prune)
        self.assertIn("\n    permissions:\n      actions: write\n      contents: read\n    steps:\n", prune)
        self.assertIn("\n          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}\n", prune)
        self.assertIn(
            "\n        run: python3 .github/scripts/prune_linux_ccache.py --repository ${{ github.repository }}"
            " --ref refs/heads/develop --apply\n",
            prune,
        )
        workflow_permissions = CI.split("\npermissions:\n", 1)[1].split("\njobs:\n", 1)[0]
        self.assertEqual([line.strip() for line in workflow_permissions.strip().splitlines()], ["contents: read", "packages: read"])
        self.assertEqual(re.findall(r"^ *actions: *write *$", CI, re.MULTILINE), ["      actions: write"])
        self.assertNotIn("write-all", CI)
        self.assertEqual(re.findall(r"^ *continue-on-error:.*$", CI, re.MULTILINE), ["    continue-on-error: true"])


if __name__ == "__main__":
    unittest.main()
