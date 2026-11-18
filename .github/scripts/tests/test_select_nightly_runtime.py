#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Nightly Tests: when the selector tests a commit, and what the workflow runs."""

import importlib.util
import io
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest
import urllib.error
import urllib.parse
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = ROOT / ".github/scripts/select_nightly_runtime.py"
SPEC = importlib.util.spec_from_file_location("select_nightly_runtime", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)
WORKFLOW = (ROOT / ".github/workflows/nightly-runtime-tests.yml").read_text()
CI = (ROOT / ".github/workflows/ci.yml").read_text()

HEAD = "a" * 40
OLD = "b" * 40


def run(run_id, sha, conclusion, *, day, event="schedule", status="completed"):
    return {
        "id": run_id,
        "head_sha": sha,
        "event": event,
        "status": status,
        "conclusion": conclusion,
        "created_at": f"2026-11-{day:02d}T06:17:00Z",
        "html_url": f"https://github.com/WSJTX/wsjtx-internal/actions/runs/{run_id}",
    }


def job(workflow, name):
    start = workflow.index(f"\n  {name}:\n") + 1
    following = re.search(r"\n  [A-Za-z0-9_-]+:\n", workflow[start:])
    return workflow[start:start + following.start() + 1] if following else workflow[start:]


class SelectorTests(unittest.TestCase):
    def test_schedule_skips_a_commit_the_newest_scheduled_run_passed(self):
        decision = MODULE.decide("schedule", HEAD, [run(2, HEAD, "success", day=16), run(1, OLD, "success", day=15)])
        self.assertFalse(decision.run)
        self.assertEqual(decision.previous["id"], 2)

    def test_schedule_retests_a_commit_the_newest_scheduled_run_failed(self):
        decision = MODULE.decide("schedule", HEAD, [run(1, OLD, "success", day=15), run(2, HEAD, "failure", day=16)])
        self.assertTrue(decision.run, "a night after a failed nightly retests the same commit")
        self.assertEqual(decision.previous["id"], 2)

    def test_the_newest_counted_result_at_the_commit_decides(self):
        passed_after_failing = [run(1, HEAD, "failure", day=15), run(2, HEAD, "success", day=16)]
        self.assertFalse(MODULE.decide("schedule", HEAD, passed_after_failing).run)
        failed_after_passing = [run(1, HEAD, "success", day=15), run(2, HEAD, "failure", day=16)]
        self.assertTrue(MODULE.decide("schedule", HEAD, failed_after_passing).run)

    def test_cancelled_runs_do_not_hide_the_result_before_them(self):
        for conclusion, tests in (("success", False), ("failure", True)):
            with self.subTest(conclusion=conclusion):
                runs = [run(1, HEAD, conclusion, day=15), run(2, HEAD, "cancelled", day=16), run(3, HEAD, "skipped", day=17)]
                decision = MODULE.decide("schedule", HEAD, runs)
                self.assertEqual(decision.run, tests)
                self.assertEqual(decision.previous["id"], 1)

    def test_schedule_tests_a_moved_develop(self):
        decision = MODULE.decide("schedule", HEAD, [run(2, OLD, "failure", day=16), run(1, HEAD, "success", day=15)])
        self.assertTrue(decision.run)
        self.assertEqual(decision.previous["head_sha"], OLD)

    def test_schedule_tests_when_no_scheduled_run_has_finished(self):
        decision = MODULE.decide("schedule", HEAD, [])
        self.assertTrue(decision.run)
        self.assertIsNone(decision.previous)

    def test_only_completed_scheduled_successes_and_failures_count(self):
        runs = [
            run(1, OLD, "success", day=10),
            run(2, HEAD, "cancelled", day=11),
            run(3, HEAD, "timed_out", day=12),
            run(4, HEAD, "success", day=13, event="workflow_dispatch"),
            run(5, HEAD, None, day=14, status="in_progress"),
            run(6, HEAD, "skipped", day=15),
        ]
        decision = MODULE.decide("schedule", HEAD, runs)
        self.assertTrue(decision.run)
        self.assertEqual(decision.previous["id"], 1)

    def test_newest_is_chosen_by_creation_time_not_list_order(self):
        older_first = [run(1, HEAD, "success", day=15), run(2, OLD, "success", day=16)]
        self.assertTrue(MODULE.decide("schedule", HEAD, older_first).run)
        self.assertFalse(MODULE.decide("schedule", OLD, list(reversed(older_first))).run)

    def test_dispatch_always_tests(self):
        for conclusion in ("success", "failure"):
            with self.subTest(conclusion=conclusion):
                self.assertTrue(MODULE.decide("workflow_dispatch", HEAD, [run(1, HEAD, conclusion, day=16)]).run)
        self.assertTrue(MODULE.decide("workflow_dispatch", HEAD, []).run)

    def test_other_events_are_rejected(self):
        for event in ("push", "pull_request", ""):
            with self.subTest(event=event), self.assertRaises(ValueError):
                MODULE.decide(event, HEAD, [])


class MainTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.output = pathlib.Path(directory.name) / "output"
        self.summary = pathlib.Path(directory.name) / "summary"

    def main(self, event, runs):
        environment = {
            "GITHUB_TOKEN": "token",
            "GITHUB_REPOSITORY": "WSJTX/wsjtx-internal",
            "GITHUB_SHA": HEAD,
            "GITHUB_EVENT_NAME": event,
            "GITHUB_SERVER_URL": "https://github.com",
            "GITHUB_OUTPUT": str(self.output),
            "GITHUB_STEP_SUMMARY": str(self.summary),
        }
        with mock.patch.dict(os.environ, environment, clear=True), \
                mock.patch.object(MODULE.GitHub, "scheduled_runs", return_value=runs), \
                mock.patch("sys.stdout", new_callable=io.StringIO) as stdout:
            self.assertEqual(MODULE.main(), 0)
        self.log = stdout.getvalue()
        return self.output.read_text(), self.summary.read_text()

    def test_a_moved_develop_writes_run_and_the_compare_link(self):
        output, summary = self.main("schedule", [run(7, OLD, "success", day=16)])
        self.assertEqual(output, "run=true\n")
        self.assertIn(f"https://github.com/WSJTX/wsjtx-internal/compare/{OLD}...{HEAD}", summary)
        self.assertIn("https://github.com/WSJTX/wsjtx-internal/actions/runs/7", summary)

    def test_a_skip_after_a_success_stays_green(self):
        output, summary = self.main("schedule", [run(7, HEAD, "success", day=16)])
        self.assertEqual(output, "run=false\n")
        self.assertIn("Skipping", summary)
        self.assertNotIn("/compare/", summary)
        self.assertNotIn("::error::", self.log)

    def test_a_night_after_a_failure_retests_the_same_commit(self):
        output, summary = self.main("schedule", [run(7, HEAD, "failure", day=16)])
        self.assertEqual(output, "run=true\n")
        self.assertIn(f"Testing `{HEAD}`: the newest scheduled run failed at this commit, so it is tested again.", summary)
        self.assertIn("https://github.com/WSJTX/wsjtx-internal/actions/runs/7", summary)
        self.assertNotIn("/compare/", summary)
        self.assertNotIn("::error::", self.log)

    def test_a_dispatch_after_a_failure_tests_the_same_commit(self):
        output, _ = self.main("workflow_dispatch", [run(7, HEAD, "failure", day=16)])
        self.assertEqual(output, "run=true\n")

    def test_missing_token_fails(self):
        with mock.patch.dict(os.environ, {}, clear=True), mock.patch("sys.stderr", new_callable=io.StringIO):
            self.assertEqual(MODULE.main(), 2)


class ClientTests(unittest.TestCase):
    def test_lists_completed_scheduled_runs_of_this_workflow(self):
        response = mock.MagicMock()
        response.__enter__.return_value = io.BytesIO(json.dumps({"workflow_runs": [{"id": 1}]}).encode())
        with mock.patch("urllib.request.urlopen", return_value=response) as urlopen:
            runs = MODULE.GitHub("token", "WSJTX/wsjtx-internal").scheduled_runs()
        self.assertEqual(runs, [{"id": 1}])
        request = urlopen.call_args.args[0]
        url = urllib.parse.urlsplit(request.full_url)
        self.assertEqual(url.path, "/repos/WSJTX/wsjtx-internal/actions/workflows/nightly-runtime-tests.yml/runs")
        self.assertEqual(urllib.parse.parse_qs(url.query), {"event": ["schedule"], "status": ["completed"], "per_page": ["100"]})
        self.assertEqual(request.get_method(), "GET")

    def test_api_errors_fail(self):
        error = urllib.error.HTTPError("https://api.github.com", 403, "Forbidden", {}, None)
        with mock.patch("urllib.request.urlopen", side_effect=error), self.assertRaisesRegex(RuntimeError, "HTTP 403"):
            MODULE.GitHub("token", "WSJTX/wsjtx-internal").scheduled_runs()


class WorkflowTests(unittest.TestCase):
    def test_runs_nightly_and_on_dispatch_one_at_a_time(self):
        self.assertIn('  schedule:\n    - cron: "17 6 * * *"\n  workflow_dispatch:\n', WORKFLOW)
        self.assertIn("concurrency:\n  group: nightly-runtime-tests\n  cancel-in-progress: false\n", WORKFLOW)

    def test_reads_only_except_the_failure_report(self):
        self.assertIn("permissions:\n  contents: read\n  packages: read\n  actions: read\n", WORKFLOW)
        self.assertEqual(re.findall(r"^ *[a-z-]+: write$", WORKFLOW, re.MULTILINE), ["      issues: write"])
        self.assertIn("    permissions:\n      issues: write\n", job(WORKFLOW, "report-failure"))

    def test_runs_only_in_the_project_repository(self):
        prepare = job(WORKFLOW, "prepare")
        self.assertIn("    if: github.repository == 'WSJTX/wsjtx-internal'\n", prepare)
        self.assertIn("    needs: [prepare]\n", job(WORKFLOW, "runtime"))

    def test_prepare_derives_the_version_as_ci_does_and_selects_before_the_image(self):
        prepare = job(WORKFLOW, "prepare")
        version = prepare[prepare.index("      - id: v\n"):prepare.index("      - id: select\n")]
        self.assertIn(version, CI)
        self.assertIn("      run_tests: ${{ steps.select.outputs.run }}\n", prepare)
        self.assertIn("        run: python3 .github/scripts/select_nightly_runtime.py\n", prepare)
        select = prepare[prepare.index("      - id: select\n"):prepare.index("      - id: linux-image\n")]
        self.assertIn("        env:\n          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}\n", select)
        image = prepare[prepare.index("      - id: linux-image\n"):]
        self.assertIn("        if: steps.select.outputs.run == 'true'\n", image)
        self.assertIn("        run: .github/scripts/resolve-linux-ci-image-tag.py\n", image)

    def test_runtime_job_builds_linux_x86_64_and_runs_only_the_runtime_tests(self):
        runtime = job(WORKFLOW, "runtime")
        self.assertIn("    if: needs.prepare.outputs.run_tests == 'true'\n", runtime)
        self.assertIn("    uses: ./.github/workflows/build-linux.yml\n", runtime)
        # build-linux.yml pulls the CI image from GHCR, so the call grants packages: read.
        self.assertIn("    permissions:\n      contents: read\n      packages: read\n    with:\n", runtime)
        for line in (
            'arch: "x86_64"',
            "image_tag: ${{ needs.prepare.outputs.linux_image_tag }}",
            "allow_stale_image: true",
            "build_packages: false",
            "runtime_tests: only",
            "artifact_retention_days: 7",
        ):
            with self.subTest(line=line):
                self.assertIn(f"\n      {line}\n", runtime)
        self.assertNotIn("save_ccache", runtime)

    def test_windows_job_builds_and_runs_the_whole_suite_when_the_runtime_job_does(self):
        windows = job(WORKFLOW, "windows")
        self.assertIn("    needs: [prepare]\n", windows)
        self.assertIn("    if: needs.prepare.outputs.run_tests == 'true'\n", windows)
        self.assertIn("    uses: ./.github/workflows/build-windows.yml\n", windows)
        self.assertIn("    permissions:\n      contents: read\n    with:\n", windows)
        for line in (
            "version: ${{ needs.prepare.outputs.version }}",
            'hamlib_branch: "4.7.2"',
            "artifact_retention_days: 1",
        ):
            with self.subTest(line=line):
                self.assertIn(f"\n      {line}\n", windows)
        # The nightly saves no compiler cache (build-windows.yml still refreshes its dependency caches on a
        # miss outside pull requests) and selects no tests: Windows runs every test.
        for absent in ("save_ccache", "recache", "cold_build", "runtime_tests"):
            with self.subTest(absent=absent):
                self.assertNotIn(absent, windows)

    def test_a_failed_scheduled_run_opens_or_updates_one_issue(self):
        report = job(WORKFLOW, "report-failure")
        self.assertIn("    needs: [prepare, runtime, windows]\n", report)
        self.assertIn("    if: failure() && github.event_name == 'schedule'\n", report)
        self.assertIn('title="Nightly tests failed on develop"', report)
        for result in ("PREPARE: ${{ needs.prepare.result }}", "RUNTIME: ${{ needs.runtime.result }}",
                       "WINDOWS: ${{ needs.windows.result }}"):
            with self.subTest(result=result):
                self.assertIn(f"          {result}\n", report)
        self.assertIn('gh issue comment "$issue" --body "$body"', report)
        self.assertIn('gh issue create --title "$title" --body "$body"', report)


def report_script() -> str:
    report = job(WORKFLOW, "report-failure")
    script = []
    for line in report.split("        run: |\n", 1)[1].splitlines():
        if line.startswith("          "):
            script.append(line[10:])
        elif line.strip():
            break
        else:
            script.append("")
    return "\n".join(script) + "\n"


# Logs each call's arguments, NUL-separated and ended by a record separator, so a multi-line body survives.
FAKE_GH = """#!/bin/bash
{ printf '%s\\0' "$@"; printf '\\036'; } >> "$GH_LOG"
if [ "$1 $2" = "issue list" ]; then
  [ -n "${FAKE_GH_FAIL:-}" ] && exit 1
  printf '%s' "${FAKE_OPEN_ISSUE:-}"
fi
"""


class ReportScriptTests(unittest.TestCase):
    TITLE = "Nightly tests failed on develop"

    def report(self, open_issue="", fail=False, prepare="success", runtime="success", windows="success"):
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            gh = directory / "gh"
            gh.write_text(FAKE_GH)
            gh.chmod(0o755)
            script = directory / "report.sh"
            script.write_text(report_script())
            log = directory / "log"
            log.write_text("")
            env = {"PATH": f"{directory}:/usr/bin:/bin", "GH_LOG": str(log), "FAKE_OPEN_ISSUE": open_issue,
                   "GH_TOKEN": "token", "GH_REPO": "WSJTX/wsjtx-internal", "RUN_URL": "https://example.invalid/runs/7",
                   "SHA": HEAD, "PREPARE": prepare, "RUNTIME": runtime, "WINDOWS": windows}
            if fail:
                env["FAKE_GH_FAIL"] = "1"
            # GitHub runs a step's script with bash -eo pipefail.
            result = subprocess.run(["bash", "-eo", "pipefail", str(script)], env=env, capture_output=True, text=True)
            calls = [call.split("\0")[:-1] for call in log.read_text().split("\036") if call]
            return result.returncode, calls

    def test_lists_open_issues_with_the_exact_title(self):
        _, calls = self.report(windows="failure")
        self.assertEqual(calls[0], ["issue", "list", "--state", "open", "--search", f'in:title "{self.TITLE}"',
                                    "--json", "number,title", "--jq",
                                    f'.[] | select(.title == "{self.TITLE}") | .number'])

    def test_opens_the_issue_when_none_is_open(self):
        status, calls = self.report(windows="failure")
        self.assertEqual(status, 0)
        self.assertEqual(calls[1], ["issue", "create", "--title", self.TITLE, "--body",
                                    f"Nightly tests on develop at {HEAD[:9]} failed: https://example.invalid/runs/7"
                                    "\n\nFailed: Windows x86_64"])

    def test_comments_on_the_open_issue(self):
        status, calls = self.report(open_issue="900", runtime="failure")
        self.assertEqual(status, 0)
        self.assertEqual(calls[1][:4], ["issue", "comment", "900", "--body"])
        self.assertTrue(calls[1][4].endswith("\n\nFailed: Linux x86_64 runtime tests"))
        self.assertEqual(len(calls), 2)

    def test_names_every_failed_job(self):
        for results, named in (
            (dict(prepare="failure", runtime="skipped", windows="skipped"), "prepare"),
            (dict(runtime="failure", windows="failure"), "Linux x86_64 runtime tests, Windows x86_64"),
            (dict(runtime="cancelled", windows="failure"), "Windows x86_64"),
        ):
            with self.subTest(**results):
                status, calls = self.report(**results)
                self.assertEqual(status, 0)
                self.assertTrue(calls[-1][-1].endswith(f"\n\nFailed: {named}"), calls[-1][-1])

    def test_a_failed_lookup_fails_the_step_and_opens_nothing(self):
        status, calls = self.report(fail=True, windows="failure")
        self.assertNotEqual(status, 0)
        self.assertEqual([call[:2] for call in calls], [["issue", "list"]])


if __name__ == "__main__":
    unittest.main()
