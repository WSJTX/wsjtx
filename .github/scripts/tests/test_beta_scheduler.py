#!/usr/bin/env python3
"""Scheduled beta state machine, continuation, alerts, git mechanics and workflow wiring."""
import importlib.util
import io
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import urllib.error
import urllib.parse
import zipfile

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("beta_scheduler", Path(__file__).parents[1] / "beta-scheduler.py")
beta = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(beta)

BOT = "wsjtx-release-bot[bot]"
BOT_EMAIL = f"41+{BOT}@users.noreply.github.com"
MAINTAINER = "maintainer@example.invalid"
LINE = "release/3.3"
TIP = "1" * 40
OLD_LINE_TIP = "2" * 40
DEVELOP = "d" * 40
GREEN = "e" * 40
CUT = "c" * 40
RECIPES = {key: str(index) * 64 for index, key in enumerate(beta.images.IMAGES, start=3)}
DRIFTED = {key: "f" * 64 for key in beta.images.IMAGES}
STATE = beta.STATE_FILE
LOCK = beta.LOCK_FILE
RUN_IDS = itertools.count(1000)


def state(channel="BETA", number=2, version="3.3.0"):
    lines = [f"version={version}", f"channel={channel}", f"prerelease={number if channel in ('BETA', 'RC') else ''}",
             "revision=$Format:%H$"]
    if channel == "BETA":
        lines.append("windows_signing=unsigned")
    return ("\n".join(lines) + "\n").encode()


OLD_CONTRACT = b"version=3.2.0\nchannel=RC\nrc=1\nrevision=$Format:%H$\nwindows_signing=unsigned\n"


def lock(recipes=RECIPES, generation="validated-build-20260929-600-1"):
    return (json.dumps({"schema": 2, "generation": generation, "images": {
        key: {"reference": f"{beta.images.PUBLIC}/{spec[0]}@sha256:{'a' * 64}", "recipe_sha256": recipes[key]}
        for key, spec in beta.images.IMAGES.items()
    }}, indent=2) + "\n").encode()


def run(workflow, *, status="completed", conclusion="success", sha=TIP, branch=LINE, event="push", title=None,
        created="2026-10-06T18:00:00Z", actor=BOT, triggering_actor=None, run_id=None):
    run_id = next(RUN_IDS) if run_id is None else run_id
    return {
        "id": run_id, "path": f".github/workflows/{workflow}", "status": status,
        "conclusion": conclusion if status == "completed" else None, "head_sha": sha, "head_branch": branch,
        "event": event, "display_title": title or workflow, "created_at": created,
        "actor": {"login": actor, "type": "Bot" if actor.endswith("[bot]") else "User"},
        "triggering_actor": {"login": triggering_actor or actor},
        "html_url": f"https://example.invalid/runs/{run_id}",
    }


def candidate(version="3.3.0-beta2", operation="create", **fields):
    fields.setdefault("event", "workflow_dispatch")
    return run(beta.CANDIDATE, title=f"Prepare Release Candidate build/v{version} ({operation})", **fields)


def preparation(generation, **fields):
    fields.setdefault("event", "workflow_dispatch")
    fields.setdefault("branch", "develop")
    fields.setdefault("sha", DEVELOP)
    return run(beta.PREPARATION, title=f"Prepare Release Dependencies develop {generation}", **fields)


class FakeRepository:
    def __init__(self):
        self.heads = {}
        self.files = {}
        self.tags = {}
        self.ancestors = set()
        self.absent = set()
        self.missing = []
        self.recipes = {}
        self.conflicts = []
        self.validation_error = None
        self.push_error = None
        self.merge = None
        self.written = None
        self.committed = None
        self.pushed = []
        self.discarded = []
        self.synced = []

    def sync(self, *, shallow=False):
        self.synced.append(shallow)

    def release_heads(self):
        return dict(self.heads)

    def develop(self):
        return DEVELOP

    def tag(self, name):
        return self.tags.get(name)

    def read(self, commit, path):
        return self.files.get((commit, path))

    def contains(self, commit):
        return commit not in self.absent

    def is_ancestor(self, ancestor, descendant):
        return ancestor == descendant or (ancestor, descendant) in self.ancestors

    def commits_missing_from_develop(self, tip):
        return list(self.missing)

    def fingerprints(self, commit):
        return self.recipes.get(commit)

    def start_merge(self, tip, commit, name, email):
        self.merge = (tip, commit)
        return Path("merge-worktree"), list(self.conflicts)

    def write(self, worktree, files):
        self.written = dict(files)

    def validate_lock(self, worktree):
        if self.validation_error:
            raise RuntimeError(self.validation_error)

    def commit(self, worktree, name, email, message):
        self.committed = (name, email, message)
        return CUT

    def push(self, commit, branch):
        if self.push_error:
            raise RuntimeError(self.push_error)
        self.pushed.append((commit, branch))

    def discard(self, worktree):
        self.discarded.append(worktree)


class FakeGitHub:
    def __init__(self):
        self.workflow_runs = {}
        self.queries = []
        self.dispatches = []
        self.published = set()
        self.artifacts = {}
        self.job_lists = {}

    def add(self, *runs):
        for item in runs:
            self.workflow_runs.setdefault(Path(item["path"]).name, []).append(item)

    def runs(self, workflow, **filters):
        self.queries.append(workflow)
        selected = []
        for item in self.workflow_runs.get(workflow, []):
            if "branch" in filters and item["head_branch"] != filters["branch"]:
                continue
            if "event" in filters and item["event"] != filters["event"]:
                continue
            if "head_sha" in filters and item["head_sha"] != filters["head_sha"]:
                continue
            if "status" in filters and filters["status"] not in (item["status"], item["conclusion"]):
                continue
            selected.append(item)
        return selected

    def jobs(self, run_id):
        return self.job_lists.get(run_id, [])

    def dispatch(self, workflow, ref, inputs):
        self.dispatches.append((workflow, ref, inputs))

    def release_published(self, tag):
        return tag in self.published

    def artifact_file(self, run_id, name, member):
        return self.artifacts.get(run_id) if (name, member) == (beta.PREPARED_ARTIFACT, LOCK) else None


class FakePackages:
    def __init__(self):
        self.tags = {package: set() for package in beta.IMAGE_PACKAGES}
        self.calls = []

    def list_versions(self, owner, package):
        self.calls.append((owner, package))
        return [SimpleNamespace(tags=tuple(sorted(self.tags[package]))), SimpleNamespace(tags=("stable",))]


class SchedulerTestCase(unittest.TestCase):
    """A published beta2 on release/3.3; develop moved to a green commit with unchanged recipes."""

    def setUp(self):
        self.build()

    def build(self):
        self.repo = FakeRepository()
        self.internal = FakeGitHub()
        self.public = FakeGitHub()
        self.packages = FakePackages()
        self.repo.heads = {"release/3.2": OLD_LINE_TIP, LINE: TIP}
        self.repo.files[(OLD_LINE_TIP, STATE)] = OLD_CONTRACT
        self.repo.files[(TIP, STATE)] = state(number=2)
        self.repo.files[(TIP, LOCK)] = lock()
        self.repo.tags["build/v3.3.0-beta2"] = TIP
        self.public.published.add("v3.3.0-beta2")
        self.repo.ancestors.add((GREEN, DEVELOP))
        self.repo.recipes[GREEN] = dict(RECIPES)
        self.repo.recipes[DEVELOP] = dict(RECIPES)
        self.repo.missing = [
            beta.Commit(TIP, BOT_EMAIL, ("CMakeLists.txt", STATE)),
            beta.Commit("3" * 40, MAINTAINER, (LOCK, STATE)),
        ]
        self.internal.add(run(beta.CI, sha=GREEN, branch="develop", created="2026-10-06T10:00:00Z", actor="k1jt"))

    def cut(self):
        return beta.Scheduler(self.repo, self.internal, self.public, self.packages,
                              bot_login=BOT, bot_email=BOT_EMAIL).cut()

    def stop(self):
        with self.assertRaises(beta.Stop) as raised:
            self.cut()
        return raised.exception

    def assertNothingDone(self):
        self.assertEqual(self.internal.dispatches, [])
        self.assertEqual(self.repo.pushed, [])


class BetaLineTest(SchedulerTestCase):
    def test_no_beta_line_between_a_freeze_and_the_next_line_is_a_quiet_skip(self):
        self.repo.files[(TIP, STATE)] = state("RC", 1)
        with self.assertRaisesRegex(beta.Skip, "No release/X.Y branch is in BETA state"):
            self.cut()
        self.assertNothingDone()

    def test_lines_without_state_with_the_old_contract_or_another_name_are_not_beta(self):
        self.repo.heads.update({"release/3.0": "5" * 40, "release/3.4-hotfix": "6" * 40})
        self.repo.files[("6" * 40, STATE)] = state(version="3.4.0", number=1)
        self.cut()
        self.assertEqual(self.repo.pushed, [(CUT, LINE)])

    def test_several_beta_lines_stop(self):
        self.repo.heads["release/3.4"] = "6" * 40
        self.repo.files[("6" * 40, STATE)] = state(version="3.4.0", number=1)
        stop = self.stop()
        self.assertEqual(stop.stage, "line")
        self.assertIn("release/3.3, release/3.4", str(stop))
        self.assertNothingDone()

    def test_an_invalid_beta_state_stops_instead_of_reading_as_no_line(self):
        self.repo.files[(TIP, STATE)] = b"version=3.3.0\nchannel=BETA\nprerelease=2\nrevision=$Format:%H$\n"
        stop = self.stop()
        self.assertEqual(stop.stage, "line")
        self.assertIn(f"{LINE}'s {STATE} says channel=BETA but does not validate", str(stop))
        self.assertNothingDone()

    def test_beta_state_for_another_line_stops(self):
        self.repo.files[(TIP, STATE)] = state(version="3.4.0")
        self.assertEqual(self.stop().stage, "line")
        self.assertNothingDone()


class RetryCutTest(SchedulerTestCase):
    def setUp(self):
        super().setUp()
        del self.repo.tags["build/v3.3.0-beta2"]
        self.public.published.clear()

    def expected_candidate(self, version="3.3.0-beta2"):
        return [(beta.CANDIDATE, LINE, {"version": version, "expected_sha": TIP, "operation": "create"})]

    def test_new_line_first_run_cuts_beta1_at_its_tip_without_a_merge(self):
        self.repo.files[(TIP, STATE)] = state(number=1)
        self.repo.missing = [beta.Commit(TIP, MAINTAINER, (LOCK, STATE))]
        self.repo.ancestors.add((GREEN, TIP))
        self.internal.add(run(beta.CI, sha=TIP, actor="k1jt"))
        self.cut()
        self.assertEqual(self.internal.dispatches, self.expected_candidate("3.3.0-beta1"))
        self.assertIsNone(self.repo.merge)
        self.assertEqual(self.repo.pushed, [])

    def test_retry_waits_while_the_tip_ci_runs(self):
        self.internal.add(run(beta.CI, sha=TIP, conclusion="failure"), run(beta.CI, sha=TIP, status="in_progress"))
        with self.assertRaisesRegex(beta.Skip, "still running"):
            self.cut()
        self.assertNothingDone()

    def test_failed_tip_ci_recuts_the_same_number_from_newer_green_develop(self):
        self.internal.add(run(beta.CI, sha=TIP),
                          run(beta.CI, sha=TIP, conclusion="failure", created="2026-10-06T19:00:00Z"))
        message = self.cut()
        self.assertEqual(self.repo.merge, (TIP, GREEN))
        self.assertEqual(self.repo.written, {STATE: beta.render_state("3.3.0", 2).encode(), LOCK: lock()})
        self.assertEqual(self.repo.committed[:2], (BOT, BOT_EMAIL))
        self.assertTrue(self.repo.committed[2].startswith("Cut 3.3.0-beta2 from develop\n\n"))
        self.assertEqual(self.repo.pushed, [(CUT, LINE)])
        self.assertEqual(self.internal.dispatches, [])
        self.assertIn("3.3.0-beta2", message)

    def test_failed_tip_ci_without_newer_green_develop_stops(self):
        failed = run(beta.CI, sha=TIP, conclusion="failure", created="2026-10-06T19:00:00Z")
        for name, setup in (
            ("develop already merged", lambda: self.repo.ancestors.add((GREEN, TIP))),
            ("no green develop", lambda: self.internal.workflow_runs.update({beta.CI: []})),
        ):
            with self.subTest(name=name):
                self.setUp()
                setup()
                self.internal.add(run(beta.CI, sha=TIP), failed)
                stop = self.stop()
                self.assertEqual((stop.stage, stop.link), ("ci", failed["html_url"]))
                self.assertIsNone(self.repo.merge)
                self.assertNothingDone()

    def test_recut_after_failed_tip_ci_follows_the_lock_and_conflict_rules(self):
        self.internal.add(run(beta.CI, sha=TIP, conclusion="cancelled"))
        self.repo.recipes[GREEN] = dict(DRIFTED)
        self.repo.recipes[DEVELOP] = dict(DRIFTED)
        self.repo.recipes["a5" * 20] = dict(DRIFTED)
        self.internal.add(run(beta.WARMER, sha="a5" * 20, branch="develop", actor="k1jt", run_id=713))
        for package in beta.IMAGE_PACKAGES:
            self.packages.tags[package].add("validated-build-20261001-713-1")
        with self.assertRaisesRegex(beta.Skip, "Dispatched Prepare Release Dependencies"):
            self.cut()
        self.assertEqual([workflow for workflow, _ref, _inputs in self.internal.dispatches], [beta.PREPARATION])
        self.assertEqual(self.repo.pushed, [])
        self.setUp()
        self.internal.add(run(beta.CI, sha=TIP, conclusion="failure"))
        self.repo.conflicts = ["CMakeLists.txt"]
        self.assertEqual(self.stop().stage, "merge")
        self.assertNothingDone()

    def test_retry_stops_without_a_tip_ci_run(self):
        self.assertEqual(self.stop().stage, "ci")
        self.assertNothingDone()

    def test_a_running_tip_ci_or_candidate_a_person_started_alerts_because_nothing_resumes_the_cut(self):
        for stage, runs in (
            ("ci", lambda: [run(beta.CI, sha=TIP, status="in_progress", actor="k1jt")]),
            ("candidate", lambda: [run(beta.CI, sha=TIP), candidate(status="queued", actor="k1jt")]),
        ):
            with self.subTest(stage=stage):
                self.setUp()
                added = runs()
                self.internal.add(*added)
                stop = self.stop()
                self.assertEqual((stop.stage, stop.link), (stage, added[-1]["html_url"]))
                self.assertIn("dispatch Scheduled Beta Cut after it does", str(stop))
                self.assertNothingDone()

    def test_retry_waits_for_a_running_candidate(self):
        self.internal.add(run(beta.CI, sha=TIP), candidate(status="queued"))
        with self.assertRaisesRegex(beta.Skip, "candidate"):
            self.cut()
        self.assertNothingDone()

    def test_failed_candidate_is_retried_with_the_same_number(self):
        self.internal.add(
            run(beta.CI, sha=TIP),
            candidate(conclusion="failure"),
            candidate(operation="validate", status="in_progress", actor="k1jt"),
            candidate("3.3.0-beta1", status="in_progress"),
        )
        self.cut()
        self.assertEqual(self.internal.dispatches, self.expected_candidate())

    def test_retry_stops_on_a_fix_committed_only_to_the_release_branch(self):
        self.internal.add(run(beta.CI, sha=TIP))
        self.repo.missing.append(beta.Commit("4" * 40, MAINTAINER, ("main.cpp",)))
        stop = self.stop()
        self.assertEqual(stop.stage, "fixes")
        self.assertIn("4" * 40, str(stop))
        self.assertNothingDone()


class UnpublishedBetaTest(SchedulerTestCase):
    def setUp(self):
        super().setUp()
        self.public.published.clear()
        self.ci = run(beta.CI, sha=TIP)
        self.candidate = candidate()
        self.promotion = run(beta.PROMOTION, event="workflow_dispatch")
        self.publication = run(beta.PUBLICATION, branch="v3.3.0-beta2", actor="KJ5HST")

    def test_unpublished_beta_waits_quietly_while_any_of_its_runs_is_in_progress(self):
        for stage, running in (
            ("CI", lambda: run(beta.CI, status="waiting")),
            ("candidate", lambda: candidate(status="waiting")),
            ("promotion", lambda: run(beta.PROMOTION, event="workflow_dispatch", status="waiting")),
            ("public release", lambda: run(beta.PUBLICATION, branch="v3.3.0-beta2", status="waiting")),
        ):
            with self.subTest(stage=stage):
                self.setUp()
                self.internal.add(self.ci, self.candidate)
                target = self.public if stage == "public release" else self.internal
                target.add(running())
                with self.assertRaisesRegex(beta.Skip, f"its {stage} run is still in progress"):
                    self.cut()
                self.assertNothingDone()

    def test_unpublished_beta_alerts_on_a_running_run_a_person_started_but_waits_for_the_public_release(self):
        for stage, running, alerts in (
            ("ci", lambda: run(beta.CI, status="in_progress", actor="k1jt"), True),
            ("candidate", lambda: candidate(status="in_progress", actor="k1jt"), True),
            ("promotion", lambda: run(beta.PROMOTION, event="workflow_dispatch", status="in_progress", actor="k1jt"),
             True),
            ("public release", lambda: run(beta.PUBLICATION, branch="v3.3.0-beta2", status="in_progress",
                                           actor="KJ5HST"), False),
        ):
            with self.subTest(stage=stage):
                self.setUp()
                self.internal.add(self.ci, self.candidate)
                added = running()
                (self.public if stage == "public release" else self.internal).add(added)
                if alerts:
                    stop = self.stop()
                    self.assertEqual((stop.stage, stop.link), (stage, added["html_url"]))
                else:
                    with self.assertRaisesRegex(beta.Skip, "public release run is still in progress"):
                        self.cut()
                self.assertNothingDone()

    def test_candidate_failed_after_its_tag_spends_the_number_and_cuts_the_next_from_green_develop(self):
        self.internal.add(self.ci, candidate(conclusion="failure"))
        scheduler = beta.Scheduler(self.repo, self.internal, self.public, self.packages,
                                   bot_login=BOT, bot_email=BOT_EMAIL)
        scheduler.cut()
        self.assertEqual(self.repo.merge, (TIP, GREEN))
        self.assertEqual(self.repo.written[STATE], beta.render_state("3.3.0", 3).encode())
        self.assertTrue(self.repo.committed[2].startswith("Cut 3.3.0-beta3 from develop\n\n"))
        self.assertEqual(self.repo.pushed, [(CUT, LINE)])
        self.assertTrue(any("3.3.0-beta2 is spent" in note for note in scheduler.notes))
        # The continuation alerts on a failed candidate run at the tip, so this spend posts no notice of its own.
        self.assertEqual(scheduler.notices, [])

    def test_spent_number_without_newer_green_develop_alerts_with_the_failed_run(self):
        failed = candidate(conclusion="failure")
        self.internal.add(self.ci, failed)
        self.repo.ancestors.add((GREEN, TIP))
        stop = self.stop()
        self.assertEqual((stop.stage, stop.link), ("candidate", failed["html_url"]))
        self.assertIn("3.3.0-beta2 is spent", str(stop))
        self.assertIn("3.3.0-beta3", str(stop))
        self.assertIn("land the fix on develop", str(stop))
        self.assertNotIn("re-run the public build", str(stop))
        self.assertNothingDone()

    def test_failed_public_build_spends_the_number_and_cuts_the_next_from_green_develop(self):
        for conclusion in ("failure", "cancelled", "timed_out"):
            with self.subTest(conclusion=conclusion):
                self.setUp()
                self.internal.add(self.ci, self.candidate, self.promotion)
                failed = run(beta.PUBLICATION, branch="v3.3.0-beta2", conclusion=conclusion)
                self.public.add(failed)
                scheduler = beta.Scheduler(self.repo, self.internal, self.public, self.packages,
                                           bot_login=BOT, bot_email=BOT_EMAIL)
                result = scheduler.cut()
                self.assertEqual(self.repo.merge, (TIP, GREEN))
                self.assertEqual(self.repo.written[STATE], beta.render_state("3.3.0", 3).encode())
                self.assertEqual(self.repo.pushed, [(CUT, LINE)])
                self.assertTrue(any("3.3.0-beta2 is spent" in note for note in scheduler.notes))
                # The public repository's run is invisible to the continuation, so the spend itself alerts.
                self.assertEqual(len(scheduler.notices), 1)
                stage, reason, link = scheduler.notices[0]
                self.assertEqual((stage, link), ("publication", failed["html_url"]))
                self.assertIn("3.3.0-beta2 is spent", reason)
                self.assertIn(result, reason)
                self.assertIn("Do not re-run its public build", reason)

    def test_failed_public_build_without_newer_green_develop_alerts_at_publication(self):
        failed = run(beta.PUBLICATION, branch="v3.3.0-beta2", conclusion="failure")
        self.internal.add(self.ci, self.candidate, self.promotion)
        self.public.add(failed)
        self.repo.ancestors.add((GREEN, TIP))
        scheduler = beta.Scheduler(self.repo, self.internal, self.public, self.packages,
                                   bot_login=BOT, bot_email=BOT_EMAIL)
        with self.assertRaises(beta.Stop) as raised:
            scheduler.cut()
        stop = raised.exception
        self.assertEqual((stop.stage, stop.link), ("publication", failed["html_url"]))
        self.assertIn("3.3.0-beta2 is spent", str(stop))
        self.assertIn("3.3.0-beta3", str(stop))
        self.assertIn("re-run the public build", str(stop))
        # The stop's own alert reports it; a notice as well would post twice.
        self.assertEqual(scheduler.notices, [])
        self.assertNothingDone()

    def test_a_successful_candidate_at_the_tag_outweighs_a_newer_failed_duplicate(self):
        self.internal.add(self.ci, self.candidate,
                          candidate(conclusion="failure", created="2026-10-06T19:00:00Z"))
        stop = self.stop()
        self.assertEqual((stop.stage, stop.link), ("promotion", self.candidate["html_url"]))
        self.assertNothingDone()

    def test_a_promoted_beta_is_not_spent_by_a_candidate_run_that_now_reads_failed(self):
        self.internal.add(self.ci, candidate(conclusion="failure"), self.promotion)
        self.public.add(self.publication)
        stop = self.stop()
        self.assertEqual((stop.stage, stop.link), ("publication", self.publication["html_url"]))
        self.assertNothingDone()

    def test_candidate_and_promotion_runs_of_other_commits_do_not_count(self):
        elsewhere = {"sha": GREEN}
        for name, runs, stage in (
            ("successful candidate elsewhere", (candidate(conclusion="failure"), candidate(**elsewhere)), None),
            ("running candidate elsewhere", (candidate(conclusion="failure"), candidate(status="queued", **elsewhere)),
             None),
            ("promotion elsewhere", (self.candidate, run(beta.PROMOTION, event="workflow_dispatch", **elsewhere)),
             "promotion"),
        ):
            with self.subTest(name=name):
                self.build()
                self.public.published.clear()
                self.internal.add(self.ci, *runs)
                if stage is None:
                    self.cut()
                    self.assertEqual(self.repo.written[STATE], beta.render_state("3.3.0", 3).encode())
                else:
                    stop = self.stop()
                    self.assertEqual((stop.stage, stop.link), (stage, self.candidate["html_url"]))
                    self.assertNothingDone()

    def test_unpublished_beta_alerts_at_its_first_missing_or_failed_stage(self):
        failed_promotion = run(beta.PROMOTION, event="workflow_dispatch", conclusion="failure")
        promoted = (self.ci, self.candidate, self.promotion)
        for internal, public, stage, link in (
            ((self.ci,), (), "candidate", None),
            ((self.ci, self.candidate), (), "promotion", self.candidate["html_url"]),
            ((self.ci, self.candidate, failed_promotion), (), "promotion", failed_promotion["html_url"]),
            (promoted, (), "publication", self.promotion["html_url"]),
            (promoted, (self.publication,), "publication", self.publication["html_url"]),
        ):
            with self.subTest(stage=stage, link=link):
                self.build()
                self.public.published.clear()
                self.internal.add(*internal)
                self.public.add(*public)
                stop = self.stop()
                self.assertEqual((stop.stage, stop.link), (stage, link))
                self.assertNothingDone()


class NextCutTest(SchedulerTestCase):
    def test_published_beta_merges_the_newest_green_develop_as_the_next_number(self):
        self.internal.add(
            run(beta.CI, sha="9" * 40, branch="develop", conclusion="failure", created="2026-10-06T11:00:00Z"),
            run(beta.CI, sha="8" * 40, branch="develop", created="2026-10-05T11:00:00Z"),
            run(beta.CI, sha="7" * 40, branch="topic", created="2026-10-06T12:00:00Z"),
        )
        self.repo.ancestors.update({("9" * 40, DEVELOP), ("8" * 40, DEVELOP), ("7" * 40, DEVELOP)})
        message = self.cut()
        self.assertEqual(self.repo.merge, (TIP, GREEN))
        self.assertEqual(self.repo.written, {STATE: beta.render_state("3.3.0", 3).encode(), LOCK: lock()})
        name, email, commit_message = self.repo.committed
        self.assertEqual((name, email), (BOT, BOT_EMAIL))
        self.assertTrue(commit_message.startswith("Cut 3.3.0-beta3 from develop\n\n"))
        self.assertIn(GREEN, commit_message)
        self.assertTrue(all(len(line) <= 72 for line in commit_message.splitlines()))
        self.assertEqual(self.repo.pushed, [(CUT, LINE)])
        self.assertEqual(self.repo.discarded, [Path("merge-worktree")])
        self.assertEqual(self.internal.dispatches, [])
        self.assertIn(CUT, message)

    def test_cut_fetches_develop_and_the_release_branches_in_full(self):
        self.cut()
        self.assertEqual(self.repo.synced, [False])

    def test_green_runs_for_commits_not_on_develop_or_not_fetched_are_passed_over(self):
        not_on_develop, not_fetched = "9" * 40, "8" * 40
        self.internal.add(
            run(beta.CI, sha=not_on_develop, branch="develop", created="2026-10-06T12:00:00Z"),
            run(beta.CI, sha=not_fetched, branch="develop", created="2026-10-06T11:00:00Z"),
        )
        self.repo.ancestors.add((not_fetched, DEVELOP))
        self.repo.absent.add(not_fetched)
        self.cut()
        self.assertEqual(self.repo.merge, (TIP, GREEN))

    def test_cut_state_is_a_valid_beta_state_that_keeps_the_line_version(self):
        self.assertEqual(beta.policy.parse_state(beta.render_state("3.3.0", 3)), {
            "version": "3.3.0", "channel": "BETA", "prerelease": "3",
            "revision": "$Format:%H$", "windows_signing": "unsigned",
        })

    def test_unchanged_develop_skips_without_an_alert(self):
        self.repo.ancestors.add((GREEN, TIP))
        with self.assertRaisesRegex(beta.Skip, "develop has not moved"):
            self.cut()
        self.assertNothingDone()

    def test_no_green_develop_stops(self):
        self.internal.workflow_runs[beta.CI] = [run(beta.CI, sha=GREEN, branch="develop", conclusion="failure")]
        self.assertEqual(self.stop().stage, "develop")
        self.assertNothingDone()


class FixesOnDevelopTest(SchedulerTestCase):
    def test_bot_merges_and_metadata_only_commits_are_not_fixes(self):
        self.repo.missing += [beta.Commit("7" * 40, MAINTAINER, (LOCK,)), beta.Commit("8" * 40, MAINTAINER, ())]
        self.cut()
        self.assertEqual(self.repo.pushed, [(CUT, LINE)])

    def test_release_branch_commits_missing_from_develop_stop_the_cut(self):
        for files, email in (
            (("main.cpp",), MAINTAINER),
            (("main.cpp", STATE), MAINTAINER),
            (("CMakeLists.txt", STATE, LOCK), "someone-else@example.invalid"),
        ):
            with self.subTest(files=files, email=email):
                self.build()
                self.repo.missing.append(beta.Commit("4" * 40, email, files))
                stop = self.stop()
                self.assertEqual(stop.stage, "fixes")
                self.assertIn("4" * 40, str(stop))
                self.assertNothingDone()


class ImageLockTest(SchedulerTestCase):
    def setUp(self):
        super().setUp()
        self.repo.recipes[GREEN] = dict(DRIFTED)
        self.repo.recipes[DEVELOP] = dict(DRIFTED)

    def publish_generation(self, run_id, sha, recipes, *, created="2026-10-01T00:00:00Z", attempt=1,
                           packages=beta.IMAGE_PACKAGES, status="completed"):
        generation = f"validated-build-20261001-{run_id}-{attempt}"
        for package in packages:
            self.packages.tags[package].add(generation)
        self.repo.recipes[sha] = dict(recipes)
        self.internal.add(run(beta.WARMER, sha=sha, branch="develop", created=created, actor="k1jt",
                              status=status, run_id=run_id))
        return generation

    def test_unchanged_recipes_keep_the_branch_lock_without_image_lookups(self):
        self.repo.recipes[GREEN] = dict(RECIPES)
        self.cut()
        self.assertEqual(self.repo.written[LOCK], lock())
        self.assertEqual(self.packages.calls, [])
        self.assertNotIn(beta.WARMER, self.internal.queries)
        self.assertNotIn(beta.PREPARATION, self.internal.queries)

    def test_green_commit_before_dual_distro_support_defers_without_mutation(self):
        self.repo.recipes[GREEN] = {key: value for key, value in RECIPES.items()
                                    if not key.startswith("armhf_trixie")}
        with self.assertRaisesRegex(beta.Skip, "predates dual-distro ARMHF"):
            self.cut()
        self.assertEqual(self.packages.calls, [])
        self.assertEqual(self.internal.dispatches, [])
        self.assertEqual(self.repo.pushed, [])

    def test_schema_one_lock_is_not_reused_for_dual_distro_source(self):
        old = json.loads(lock())
        old["schema"] = 1
        self.assertIsNone(beta.lock_recipes(json.dumps(old).encode()))

    def test_recipe_drift_prepares_the_newest_generation_built_for_those_recipes(self):
        self.publish_generation(702, "a2" * 20, DRIFTED, created="2026-10-01T00:00:00Z")
        newest = self.publish_generation(703, "a3" * 20, DRIFTED, created="2026-10-03T00:00:00Z")
        rerun = newest.removesuffix("-1") + "-2"
        for package in beta.IMAGE_PACKAGES:
            self.packages.tags[package].add(rerun)
        self.publish_generation(704, "a4" * 20, RECIPES, created="2026-10-04T00:00:00Z")
        self.internal.add(preparation("validated-build-20261001-702-1", status="in_progress"))
        with self.assertRaisesRegex(beta.Skip, "Dispatched Prepare Release Dependencies"):
            self.cut()
        self.assertEqual(self.internal.dispatches, [
            (beta.PREPARATION, "develop", {"source_branch": "develop", "generation": rerun}),
        ])
        self.assertEqual(self.repo.pushed, [])

    def assertRefreshAlert(self, refresh):
        stop = self.stop()
        self.assertEqual((stop.stage, stop.link), ("lock", refresh["html_url"]))
        self.assertIn("still being refreshed", str(stop))
        self.assertIn("dispatch Scheduled Beta Cut after it does", str(stop))
        self.assertNothingDone()

    def test_recipe_drift_alerts_while_the_image_refresh_runs_because_nothing_resumes_the_cut(self):
        self.publish_generation(705, "b1" * 20, RECIPES)
        self.repo.recipes["b2" * 20] = dict(DRIFTED)
        refresh = run(beta.WARMER, sha="b2" * 20, branch="develop", status="in_progress", actor="k1jt")
        self.internal.add(refresh)
        self.assertRefreshAlert(refresh)

    def test_only_a_warmer_run_that_refreshes_the_images_holds_the_cut(self):
        detect = {"name": "detect", "status": "completed", "conclusion": "success"}
        for name, jobs, waits in (
            ("refresh skipped", [detect, {"name": "refresh-linux-images", "status": "completed",
                                          "conclusion": "skipped"}], False),
            ("TSan image only", [detect, {"name": "refresh-linux-images / build-normal", "status": "completed",
                                          "conclusion": "skipped"}], False),
            ("normal images building", [detect, {"name": "refresh-linux-images / build-normal",
                                                 "status": "in_progress", "conclusion": None}], True),
            ("built, not yet promoted", [detect, {"name": "refresh-linux-images / build-normal",
                                                  "status": "completed", "conclusion": "success"}], True),
            ("refresh pending", [detect, {"name": "refresh-linux-images", "status": "queued",
                                          "conclusion": None}], True),
            ("not yet decided", [{"name": "detect", "status": "in_progress", "conclusion": None}], True),
        ):
            with self.subTest(name=name):
                self.setUp()
                self.publish_generation(720, "c1" * 20, RECIPES)
                self.repo.recipes["c2" * 20] = dict(DRIFTED)
                warmer = run(beta.WARMER, sha="c2" * 20, branch="develop", status="in_progress", actor="k1jt")
                self.internal.add(warmer)
                self.internal.job_lists[warmer["id"]] = jobs
                if waits:
                    self.assertRefreshAlert(warmer)
                else:
                    stop = self.stop()
                    self.assertEqual((stop.stage, stop.link), ("lock", None))
                    self.assertIn("No validated Linux image generation", str(stop))
                    self.assertNothingDone()

    def test_a_generation_promoted_by_a_run_still_in_progress_is_used(self):
        generation = self.publish_generation(722, "c4" * 20, DRIFTED, status="in_progress")
        with self.assertRaisesRegex(beta.Skip, "Dispatched"):
            self.cut()
        self.assertEqual(self.internal.dispatches[0][2]["generation"], generation)

    def test_generations_from_a_direct_image_publication_on_develop_count(self):
        generation = "validated-build-20261002-723-1"
        for package in beta.IMAGE_PACKAGES:
            self.packages.tags[package].add(generation)
        self.repo.recipes["c5" * 20] = dict(DRIFTED)
        self.internal.add(run(beta.PUBLISHER, sha="c5" * 20, branch="develop", event="workflow_dispatch",
                              actor="dchristle", run_id=723))
        with self.assertRaisesRegex(beta.Skip, "Dispatched"):
            self.cut()
        self.assertEqual(self.internal.dispatches[0][2]["generation"], generation)
        self.setUp()
        building = run(beta.PUBLISHER, sha="c5" * 20, branch="develop", event="workflow_dispatch",
                       status="in_progress", actor="dchristle")
        self.repo.recipes["c5" * 20] = dict(DRIFTED)
        self.internal.add(building)
        self.internal.job_lists[building["id"]] = [{"name": "build-normal", "status": "in_progress",
                                                     "conclusion": None}]
        self.assertRefreshAlert(building)

    def test_generation_reruns_are_ordered_by_attempt_number(self):
        generation = self.publish_generation(721, "c3" * 20, DRIFTED, attempt=9)
        tenth = generation.removesuffix("-9") + "-10"
        for package in beta.IMAGE_PACKAGES:
            self.packages.tags[package].add(tenth)
        with self.assertRaisesRegex(beta.Skip, "Dispatched"):
            self.cut()
        self.assertEqual(self.internal.dispatches[0][2]["generation"], tenth)

    def test_recipe_drift_without_a_generation_or_refresh_stops(self):
        self.publish_generation(706, "b3" * 20, RECIPES)
        self.publish_generation(707, "b4" * 20, DRIFTED, packages=beta.IMAGE_PACKAGES[:3])
        self.repo.recipes["b5" * 20] = dict(RECIPES)
        self.internal.add(run(beta.WARMER, sha="b5" * 20, branch="develop", status="in_progress", actor="k1jt"))
        self.assertEqual(self.stop().stage, "lock")
        self.assertNothingDone()

    def test_recipe_drift_waits_for_a_running_preparation(self):
        generation = self.publish_generation(708, "b6" * 20, DRIFTED)
        self.internal.add(preparation(generation, status="in_progress"))
        with self.assertRaisesRegex(beta.Skip, "Prepare Release Dependencies"):
            self.cut()
        self.assertNothingDone()

    def test_a_spend_at_publication_is_noticed_once_by_the_cut_that_commits_the_prepared_lock(self):
        self.public.published.clear()
        failed = run(beta.PUBLICATION, branch="v3.3.0-beta2", conclusion="failure")
        self.internal.add(run(beta.CI, sha=TIP), candidate(), run(beta.PROMOTION, event="workflow_dispatch"))
        self.public.add(failed)
        generation = self.publish_generation(711, "b9" * 20, DRIFTED)
        first = beta.Scheduler(self.repo, self.internal, self.public, self.packages, bot_login=BOT,
                               bot_email=BOT_EMAIL)
        with self.assertRaisesRegex(beta.Skip, "Dispatched Prepare Release Dependencies"):
            first.cut()
        self.assertEqual(first.notices, [])
        prepared = preparation(generation, created="2026-10-06T08:00:00Z")
        self.internal.add(prepared)
        self.internal.artifacts[prepared["id"]] = lock(DRIFTED, generation)
        second = beta.Scheduler(self.repo, self.internal, self.public, self.packages, bot_login=BOT,
                                bot_email=BOT_EMAIL)
        second.cut()
        self.assertEqual([(stage, link) for stage, _, link in second.notices], [("publication", failed["html_url"])])
        self.assertEqual(self.repo.pushed, [(CUT, LINE)])

    def test_recipe_drift_commits_the_prepared_lock_in_the_cut(self):
        generation = self.publish_generation(709, "b7" * 20, DRIFTED)
        prepared = preparation(generation, created="2026-10-06T08:00:00Z")
        failed_earlier = preparation(generation, created="2026-10-06T07:00:00Z", conclusion="failure")
        self.internal.add(prepared, failed_earlier)
        self.internal.artifacts[prepared["id"]] = lock(DRIFTED, generation)
        self.cut()
        self.assertEqual(self.repo.written[LOCK], lock(DRIFTED, generation))
        self.assertIn(generation, self.repo.committed[2])
        self.assertEqual(self.repo.pushed, [(CUT, LINE)])
        self.assertEqual(self.internal.dispatches, [])

    def test_prepared_lock_for_other_recipes_or_generation_is_prepared_again(self):
        generation = self.publish_generation(710, "b8" * 20, DRIFTED)
        for prepared_lock in (lock(RECIPES, generation), lock(DRIFTED, "validated-build-20261001-1-1"), None):
            with self.subTest(prepared_lock=prepared_lock):
                self.internal.dispatches.clear()
                stale = preparation(generation)
                self.internal.add(stale)
                self.internal.artifacts[stale["id"]] = prepared_lock
                with self.assertRaisesRegex(beta.Skip, "Dispatched"):
                    self.cut()
                self.assertEqual(self.internal.dispatches, [
                    (beta.PREPARATION, "develop", {"source_branch": "develop", "generation": generation}),
                ])

    def test_failed_preparation_stops_with_its_run(self):
        generation = self.publish_generation(711, "b9" * 20, DRIFTED)
        failed = preparation(generation, conclusion="failure")
        self.internal.add(failed)
        stop = self.stop()
        self.assertEqual((stop.stage, stop.link), ("lock", failed["html_url"]))
        self.assertNothingDone()

    def test_a_develop_tip_with_other_recipes_alerts_instead_of_preparing_a_lock_that_would_fail(self):
        generation = self.publish_generation(724, "c6" * 20, DRIFTED)
        self.repo.recipes[DEVELOP] = {key: "9" * 64 for key in beta.images.IMAGES}
        stop = self.stop()
        self.assertEqual(stop.stage, "lock")
        self.assertIn(f"develop's tip {DEVELOP} has other Linux image recipes", str(stop))
        self.assertIn(generation, str(stop))
        self.assertNothingDone()

    def test_a_running_preparation_someone_else_started_alerts(self):
        generation = self.publish_generation(725, "c7" * 20, DRIFTED)
        running = preparation(generation, status="in_progress", actor="dchristle")
        self.internal.add(running)
        stop = self.stop()
        self.assertEqual((stop.stage, stop.link), ("lock", running["html_url"]))
        self.assertNothingDone()

    def test_missing_branch_lock_is_prepared(self):
        self.repo.recipes[GREEN] = dict(RECIPES)
        self.repo.recipes[DEVELOP] = dict(RECIPES)
        del self.repo.files[(TIP, LOCK)]
        generation = self.publish_generation(712, "ba" * 20, RECIPES)
        with self.assertRaisesRegex(beta.Skip, "Dispatched"):
            self.cut()
        self.assertEqual(self.internal.dispatches[0][2]["generation"], generation)


class MergeTest(SchedulerTestCase):
    def test_metadata_conflicts_are_resolved_by_the_bot(self):
        self.repo.conflicts = [STATE, LOCK]
        self.cut()
        self.assertEqual(self.repo.written[STATE], beta.render_state("3.3.0", 3).encode())
        self.assertEqual(self.repo.pushed, [(CUT, LINE)])

    def test_other_conflicts_stop_without_a_push(self):
        self.repo.conflicts = [STATE, "CMakeLists.txt"]
        stop = self.stop()
        self.assertEqual(stop.stage, "merge")
        self.assertIn("CMakeLists.txt", str(stop))
        self.assertIsNone(self.repo.written)
        self.assertEqual(self.repo.discarded, [Path("merge-worktree")])
        self.assertNothingDone()

    def test_lock_that_fails_validation_stops_without_a_push(self):
        self.repo.validation_error = "Stale recipe for x86_64"
        stop = self.stop()
        self.assertEqual(stop.stage, "lock")
        self.assertIn("Stale recipe", str(stop))
        self.assertIsNone(self.repo.committed)
        self.assertNothingDone()

    def test_rejected_push_stops(self):
        self.repo.push_error = "non-fast-forward"
        self.assertEqual(self.stop().stage, "merge")
        self.assertEqual(self.repo.discarded, [Path("merge-worktree")])


class ContinueTest(SchedulerTestCase):
    def setUp(self):
        super().setUp()
        self.repo.files[(TIP, STATE)] = state(number=3)

    def proceed(self, event_run):
        return beta.Scheduler(self.repo, self.internal, bot_login=BOT).continue_after(event_run)

    def skipped(self, event_run):
        with self.assertRaises(beta.Skip):
            self.proceed(event_run)
        self.assertEqual(self.internal.dispatches, [])

    def stopped(self, event_run):
        with self.assertRaises(beta.Stop) as raised:
            self.proceed(event_run)
        self.assertEqual(self.internal.dispatches, [])
        return raised.exception

    def test_green_cut_ci_dispatches_candidate_creation(self):
        self.proceed(run(beta.CI))
        self.assertEqual(self.internal.dispatches, [
            (beta.CANDIDATE, LINE, {"version": "3.3.0-beta3", "expected_sha": TIP, "operation": "create"}),
        ])
        self.assertEqual(self.repo.synced, [True])

    def test_runs_not_started_by_the_release_bot_do_nothing(self):
        for actor in ("k1jt", "dependabot[bot]", "github-actions[bot]"):
            with self.subTest(actor=actor):
                self.skipped(run(beta.CI, actor=actor))
        self.assertEqual(self.repo.synced, [])

    def test_a_person_rerunning_a_release_bot_run_resumes_the_beta(self):
        self.proceed(run(beta.CI, triggering_actor="k1jt"))
        self.repo.tags["build/v3.3.0-beta3"] = TIP
        rerun = candidate("3.3.0-beta3", triggering_actor="dchristle")
        self.proceed(rerun)
        self.assertEqual([workflow for workflow, _ref, _inputs in self.internal.dispatches],
                         [beta.CANDIDATE, beta.PROMOTION])

    def test_green_cut_ci_waits_for_a_candidate_already_running(self):
        self.internal.add(candidate("3.3.0-beta3", status="in_progress"))
        self.skipped(run(beta.CI))

    def test_runs_away_from_the_beta_line_tip_do_nothing(self):
        for fields in ({"sha": GREEN}, {"branch": "release/3.2", "sha": OLD_LINE_TIP}, {"branch": "release/3.2"},
                       {"event": "pull_request"}):
            with self.subTest(fields=fields):
                self.skipped(run(beta.CI, **fields))

    def test_failed_cut_ci_alerts(self):
        failed = run(beta.CI, conclusion="failure")
        stop = self.stopped(failed)
        self.assertEqual((stop.stage, stop.link), ("ci", failed["html_url"]))

    def test_existing_candidate_tag_is_not_created_again(self):
        self.repo.tags["build/v3.3.0-beta3"] = TIP
        self.skipped(run(beta.CI))

    def test_successful_candidate_dispatches_promotion(self):
        self.repo.tags["build/v3.3.0-beta3"] = TIP
        done = candidate("3.3.0-beta3")
        self.proceed(done)
        self.assertEqual(self.internal.dispatches, [(beta.PROMOTION, LINE, {
            "version": "3.3.0-beta3", "candidate_run_id": str(done["id"]), "operation": "promote",
        })])

    def test_failed_candidate_alerts_that_a_rerun_resumes_or_the_next_cut_moves_on(self):
        self.repo.tags["build/v3.3.0-beta3"] = TIP
        failed = candidate("3.3.0-beta3", conclusion="failure")
        stop = self.stopped(failed)
        self.assertEqual((stop.stage, stop.link), ("candidate", failed["html_url"]))
        self.assertIn("Re-run it if the failure is transient", str(stop))
        self.assertIn("moves on to BETA 4", str(stop))

    def test_candidate_refused_before_its_tag_alerts_that_the_next_cut_retries_the_number(self):
        failed = candidate("3.3.0-beta3", conclusion="failure")
        stop = self.stopped(failed)
        self.assertEqual((stop.stage, stop.link), ("candidate", failed["html_url"]))
        self.assertIn("the next scheduled cut retries 3.3.0-beta3", str(stop))
        self.assertNotIn("BETA 4", str(stop))

    def test_candidate_without_its_tag_at_the_tip_alerts(self):
        for tag_sha in (None, GREEN):
            with self.subTest(tag_sha=tag_sha):
                self.repo.tags["build/v3.3.0-beta3"] = tag_sha
                self.assertEqual(self.stopped(candidate("3.3.0-beta3")).stage, "candidate")

    def test_other_candidate_runs_do_nothing(self):
        for other in (
            candidate("3.3.0-beta3", "validate"),
            candidate("3.3.0-beta2"),
            run(beta.CANDIDATE, event="workflow_dispatch", title="Prepare Release Candidate"),
        ):
            with self.subTest(title=other["display_title"]):
                self.repo.tags["build/v3.3.0-beta3"] = TIP
                self.skipped(other)

    def test_failed_promotion_alerts_so_a_rerun_can_resume(self):
        failed = run(beta.PROMOTION, event="workflow_dispatch", conclusion="failure")
        stop = self.stopped(failed)
        self.assertEqual((stop.stage, stop.link), ("promotion", failed["html_url"]))

    def test_successful_promotion_leaves_publication_to_the_public_release_run(self):
        self.skipped(run(beta.PROMOTION, event="workflow_dispatch"))

    def test_successful_bot_preparation_dispatches_the_beta_cut_on_develop(self):
        self.proceed(preparation("validated-build-20261001-714-1"))
        self.assertEqual(self.internal.dispatches, [(beta.BETA_CUT, "develop", {})])
        self.assertEqual(self.repo.synced, [])

    def test_other_preparation_and_develop_runs_do_nothing(self):
        for other in (
            preparation("validated-build-20261001-714-1", actor="dchristle"),
            preparation("validated-build-20261001-714-1", branch=LINE),
            preparation("validated-build-20261001-714-1", event="push"),
            run(beta.PREPARATION, event="workflow_dispatch", branch="develop", sha=DEVELOP,
                title="Prepare Release Dependencies release/3.2 validated-build-20260918-35390550558-1"),
            run(beta.PREPARATION, event="workflow_dispatch", branch="develop", sha=DEVELOP,
                title="Prepare Release Dependencies"),
            run(beta.CI, branch="develop", sha=DEVELOP),
        ):
            with self.subTest(path=other["path"], title=other["display_title"], branch=other["head_branch"],
                              event=other["event"], actor=other["actor"]["login"]):
                self.skipped(other)

    def test_failed_bot_preparation_alerts(self):
        failed = preparation("validated-build-20261001-714-1", conclusion="failure")
        stop = self.stopped(failed)
        self.assertEqual((stop.stage, stop.link), ("lock", failed["html_url"]))


class StepReportTest(unittest.TestCase):
    def outcome(self, action):
        return beta.run_step("cut", action, ["Beta line: release/3.3."])

    def test_actions_and_skips_report_without_an_alert(self):
        status, summary, alert = self.outcome(lambda: "Dispatched the candidate.")
        self.assertEqual((status, alert), (0, None))
        self.assertIn("- Beta line: release/3.3.", summary)
        self.assertIn("Dispatched the candidate.", summary)

        def skip():
            raise beta.Skip("develop has not moved")
        status, summary, alert = self.outcome(skip)
        self.assertEqual((status, alert), (0, None))
        self.assertIn("develop has not moved", summary)

    def test_stops_and_unexpected_errors_alert_and_fail(self):
        def stop():
            raise beta.Stop("fixes", "release/3.3 holds a fix.", "https://example.invalid/runs/1")

        def crash():
            raise RuntimeError("API outage")
        status, summary, alert = self.outcome(stop)
        self.assertEqual((status, alert), (1, ("fixes", "release/3.3 holds a fix.", "https://example.invalid/runs/1")))
        self.assertIn("release/3.3 holds a fix.", summary)
        with patch("traceback.print_exc"):
            status, summary, alert = self.outcome(crash)
        self.assertEqual((status, alert), (1, ("cut", "Unexpected error: API outage", None)))


class EntryPointTest(unittest.TestCase):
    ENV = {"GITHUB_REPOSITORY": "example/internal", "GITHUB_RUN_ID": "5", "GITHUB_TOKEN": "workflow-token",
           "RELEASE_BOT_TOKEN": "bot-token", "RELEASE_BOT_SLUG": "wsjtx-release-bot",
           "PUBLIC_READ_TOKEN": "public-token"}

    def test_scheduler_acts_as_the_release_bot(self):
        with patch.object(beta.GitHub, "user_id", lambda github, login: 41):
            cut = beta.build_scheduler("cut", self.ENV, [])
            proceed = beta.build_scheduler("continue", self.ENV, [])
        self.assertEqual((cut.bot_login, cut.bot_email), (BOT, BOT_EMAIL))
        self.assertEqual((proceed.bot_login, proceed.bot_email), (BOT, None))
        self.assertEqual(cut.public.repository, beta.PUBLIC_REPOSITORY)

    def main(self, outcome, notices=()):
        posted = []
        with tempfile.TemporaryDirectory() as directory:
            output, summary = Path(directory, "output"), Path(directory, "summary")
            output.touch()
            env = {**self.ENV, "GITHUB_OUTPUT": str(output), "GITHUB_STEP_SUMMARY": str(summary)}
            scheduler = SimpleNamespace(cut=outcome, notices=list(notices))
            with patch.dict(os.environ, env), \
                    patch.object(beta, "build_scheduler", lambda command, environ, notes: scheduler), \
                    patch.object(beta.Alerts, "post", lambda alerts, *alert: posted.append(alert)), \
                    patch("sys.stdout", io.StringIO()):
                status = beta.main(["cut"])
            return status, posted, output.read_text(), summary.read_text()

    def test_a_stop_is_posted_once_and_recorded_so_the_fallback_alert_stays_silent(self):
        def stop():
            raise beta.Stop("candidate", "The candidate failed.", "https://example.invalid/runs/2")
        status, posted, output, summary = self.main(stop)
        self.assertEqual(status, 1)
        self.assertEqual(posted, [("candidate", "The candidate failed.", "https://example.invalid/runs/2")])
        self.assertEqual(output, "alerted=true\n")
        self.assertIn("The candidate failed.", summary)

    def test_a_number_spent_at_publication_is_posted_while_the_cut_succeeds(self):
        notice = ("publication", "3.3.0-beta2 is spent. Pushed c to release/3.3.", "https://example.invalid/runs/3")
        status, posted, output, summary = self.main(lambda: "Pushed c to release/3.3.", [notice])
        self.assertEqual((status, posted, output), (0, [notice], "alerted=true\n"))
        self.assertIn("Pushed c to release/3.3.", summary)

    def test_a_notice_that_cannot_be_posted_fails_the_job_and_leaves_it_in_the_summary(self):
        notice = ("publication", "3.3.0-beta2 is spent.", "https://example.invalid/runs/3")
        scheduler = SimpleNamespace(cut=lambda: "Pushed c to release/3.3.", notices=[notice])

        def unreachable(alerts, *alert):
            raise OSError("HTTP 502")
        with tempfile.TemporaryDirectory() as directory:
            output, summary = Path(directory, "output"), Path(directory, "summary")
            output.touch()
            env = {**self.ENV, "GITHUB_OUTPUT": str(output), "GITHUB_STEP_SUMMARY": str(summary)}
            with patch.dict(os.environ, env), \
                    patch.object(beta, "build_scheduler", lambda command, environ, notes: scheduler), \
                    patch.object(beta.Alerts, "post", unreachable), \
                    patch("sys.stdout", io.StringIO()), patch("sys.stderr", io.StringIO()):
                status = beta.main(["cut"])
            self.assertEqual((status, output.read_text()), (1, ""))
            self.assertIn("Pushed c to release/3.3.", summary.read_text())
            self.assertIn("3.3.0-beta2 is spent.", summary.read_text())
            self.assertIn("HTTP 502", summary.read_text())

    def test_a_skip_posts_nothing(self):
        def skip():
            raise beta.Skip("develop has not moved")
        status, posted, output, summary = self.main(skip)
        self.assertEqual((status, posted, output), (0, [], ""))
        self.assertIn("develop has not moved", summary)


class FakeIssuesApi:
    repository = "example/internal"

    def __init__(self, issues):
        self.issues = issues
        self.calls = []

    def call(self, method, path, *, params=None, body=None, missing_ok=False):
        self.calls.append((method, path, params, body))
        return self.issues if method == "GET" else {}


class AlertsTest(unittest.TestCase):
    RUN = "https://example.invalid/runs/1"

    def test_first_alert_opens_the_tracking_issue(self):
        api = FakeIssuesApi([
            {"number": 5, "title": beta.ALERT_TITLE + " (old)", "user": {"login": beta.ALERT_CREATOR}},
            {"number": 6, "title": beta.ALERT_TITLE, "user": {"login": "k1jt"}},
            {"number": 7, "title": beta.ALERT_TITLE, "user": {"login": beta.ALERT_CREATOR}, "pull_request": {}},
        ])
        beta.Alerts(api, self.RUN).post("candidate", "The candidate failed.", "https://example.invalid/runs/2")
        self.assertEqual(api.calls[0][:3], ("GET", "/repos/example/internal/issues",
                                            {"state": "open", "creator": beta.ALERT_CREATOR, "per_page": 100}))
        method, path, _params, body = api.calls[-1]
        self.assertEqual((method, path, body["title"]), ("POST", "/repos/example/internal/issues", beta.ALERT_TITLE))
        for text in ("**Stage:** candidate", "**Reason:** The candidate failed.",
                     "**Run:** https://example.invalid/runs/2", f"**Reported by:** {self.RUN}"):
            self.assertIn(text, body["body"])

    def test_later_alerts_comment_on_the_open_tracking_issue(self):
        api = FakeIssuesApi([
            {"number": 12, "title": beta.ALERT_TITLE, "user": {"login": beta.ALERT_CREATOR}},
            {"number": 9, "title": beta.ALERT_TITLE, "user": {"login": beta.ALERT_CREATOR}},
        ])
        beta.Alerts(api, self.RUN).post("lock", "No generation.")
        method, path, _params, body = api.calls[-1]
        self.assertEqual((method, path), ("POST", "/repos/example/internal/issues/9/comments"))
        self.assertIn(f"**Run:** {self.RUN}", body["body"])
        self.assertNotIn("Reported by", body["body"])


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class GitHubClientTest(unittest.TestCase):
    def test_missing_release_is_unpublished_and_other_errors_raise(self):
        github = beta.GitHub("token", "example/public")

        def missing(request, timeout):
            raise urllib.error.HTTPError(request.full_url, 404, "Not Found", {}, None)
        with patch("urllib.request.urlopen", missing):
            self.assertFalse(github.release_published("v3.3.0-beta2"))
        with patch("urllib.request.urlopen", lambda request, timeout: FakeResponse(b'{"draft": false}')):
            self.assertTrue(github.release_published("v3.3.0-beta2"))

        def denied(request, timeout):
            raise urllib.error.HTTPError(request.full_url, 403, "Forbidden", {}, None)
        with patch("urllib.request.urlopen", denied), self.assertRaisesRegex(RuntimeError, "HTTP 403"):
            github.release_published("v3.3.0-beta2")

    def test_run_and_job_listings_send_their_filters(self):
        requests = []

        def urlopen(request, timeout):
            requests.append(request)
            return FakeResponse(json.dumps({"workflow_runs": [{"id": 1}], "jobs": [{"id": 2}]}).encode())

        github = beta.GitHub("token", "example/internal")
        with patch("urllib.request.urlopen", urlopen):
            self.assertEqual(github.runs(beta.CI, branch="develop", event="push", status="success",
                                         head_sha="ab" * 20), [{"id": 1}])
            self.assertEqual(github.jobs(77), [{"id": 2}])
        runs, jobs = (urllib.parse.urlsplit(request.full_url) for request in requests)
        self.assertEqual(runs.path, f"/repos/example/internal/actions/workflows/{beta.CI}/runs")
        self.assertEqual(urllib.parse.parse_qs(runs.query), {
            "per_page": ["100"], "branch": ["develop"], "event": ["push"], "status": ["success"],
            "head_sha": ["ab" * 20],
        })
        self.assertEqual((jobs.path, urllib.parse.parse_qs(jobs.query)),
                         ("/repos/example/internal/actions/runs/77/jobs", {"per_page": ["100"]}))

    def test_artifact_download_follows_the_redirect_without_the_token(self):
        bundle = io.BytesIO()
        with zipfile.ZipFile(bundle, "w") as archive:
            archive.writestr(LOCK, lock())
        requests = []

        class Opener:
            def open(self, request, timeout):
                requests.append(request)
                location = {"Location": "https://blob.example.invalid/a"}
                raise urllib.error.HTTPError(request.full_url, 302, "Found", location, None)

        def urlopen(request, timeout):
            requests.append(request)
            if isinstance(request, str):
                return FakeResponse(bundle.getvalue())
            return FakeResponse(json.dumps({"artifacts": [
                {"id": 9, "name": beta.PREPARED_ARTIFACT, "expired": True},
                {"id": 8, "name": beta.PREPARED_ARTIFACT, "expired": False},
            ]}).encode())

        github = beta.GitHub("token", "example/internal")
        with patch("urllib.request.urlopen", urlopen), patch("urllib.request.build_opener", lambda *handlers: Opener()):
            self.assertEqual(github.artifact_file(77, beta.PREPARED_ARTIFACT, LOCK), lock())
        self.assertIn("/actions/runs/77/artifacts?", requests[0].full_url)
        self.assertTrue(requests[1].full_url.endswith("/actions/artifacts/8/zip"))
        self.assertEqual(requests[1].get_header("Authorization"), "Bearer token")
        self.assertEqual(requests[2], "https://blob.example.invalid/a")


class RepositoryTest(unittest.TestCase):
    """The git layer against a local origin, with no global git identity, as on a runner."""

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        environment = patch.dict(os.environ, {"GIT_CONFIG_GLOBAL": str(self.root / "no-global-config"),
                                              "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_COUNT": "1",
                                              "GIT_CONFIG_KEY_0": "user.useConfigOnly",
                                              "GIT_CONFIG_VALUE_0": "true"})
        environment.start()
        self.addCleanup(environment.stop)
        self.origin = self.root / "origin.git"
        self.work = self.root / "work"
        self.git(self.root, "init", "--bare", "--initial-branch=develop", str(self.origin))
        self.git(self.root, "init", "--initial-branch=develop", str(self.work))
        # Background auto-maintenance would race the directory's removal.
        for repository in (self.origin, self.work):
            self.git(repository, "config", "maintenance.auto", "false")
        (self.work / "app.txt").write_text("one\n")
        (self.work / STATE).write_bytes(state("DEVEL", version="9.1.0"))
        self.base = self.commit("develop base")
        (self.work / STATE).write_bytes(state(number=1, version="9.1.0"))
        (self.work / LOCK).write_bytes(lock())
        self.line_start = self.commit("open 9.1 beta line")
        self.git(self.work, "push", str(self.origin), f"{self.line_start}:refs/heads/release/9.1")
        self.git(self.work, "checkout", "--quiet", "--detach", self.base)
        (self.work / "app.txt").write_text("two\n")
        self.develop = self.commit("develop work")
        self.git(self.work, "push", str(self.origin), f"{self.develop}:refs/heads/develop")
        self.checkout = self.root / "checkout"
        self.git(self.root, "clone", "--quiet", "--config", "maintenance.auto=false",
                 str(self.origin), str(self.checkout))
        self.repository = beta.Repository(self.checkout, str(self.origin))
        self.repository.sync()

    def git(self, cwd, *arguments):
        return subprocess.run(["git", *arguments], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()

    def commit(self, message, author=MAINTAINER):
        self.git(self.work, "add", "--all")
        self.git(self.work, "-c", "user.name=Maintainer", "-c", f"user.email={author}",
                 "commit", "--quiet", "-m", message)
        return self.git(self.work, "rev-parse", "HEAD")

    def origin_git(self, *arguments):
        return self.git(self.origin, *arguments)

    def test_reads_refs_files_tags_and_ancestry(self):
        self.assertEqual(self.repository.release_heads(), {"release/9.1": self.line_start})
        self.assertEqual(self.repository.develop(), self.develop)
        self.assertEqual(self.repository.read(self.line_start, STATE), state(number=1, version="9.1.0"))
        self.assertIsNone(self.repository.read(self.base, LOCK))
        self.assertTrue(self.repository.contains(self.develop))
        self.assertFalse(self.repository.contains("0" * 40))
        self.assertTrue(self.repository.is_ancestor(self.base, self.develop))
        self.assertFalse(self.repository.is_ancestor(self.line_start, self.develop))
        self.assertIsNone(self.repository.tag("build/v9.1.0-beta1"))
        self.git(self.work, "push", str(self.origin), f"{self.line_start}:refs/tags/build/v9.1.0-beta1")
        self.git(self.work, "-c", "user.name=Maintainer", "-c", f"user.email={MAINTAINER}",
                 "tag", "-a", "-m", "annotated", "annotated", self.base)
        self.git(self.work, "push", str(self.origin), "refs/tags/annotated")
        self.assertEqual(self.repository.tag("build/v9.1.0-beta1"), self.line_start)
        self.assertEqual(self.repository.tag("annotated"), self.base)

    def test_cut_merges_develop_writes_metadata_and_pushes_as_the_bot(self):
        self.assertEqual(self.repository.commits_missing_from_develop(self.line_start),
                         [beta.Commit(self.line_start, MAINTAINER, (LOCK, STATE))])
        worktree, conflicts = self.repository.start_merge(self.line_start, self.develop, BOT, BOT_EMAIL)
        self.assertEqual(conflicts, [])
        self.repository.write(worktree, {STATE: beta.render_state("9.1.0", 2).encode(), LOCK: lock(DRIFTED)})
        cut = self.repository.commit(worktree, BOT, BOT_EMAIL, "Cut 9.1.0-beta2 from develop\n")
        self.repository.push(cut, "release/9.1")
        self.repository.discard(worktree)
        self.assertFalse(worktree.exists())
        self.assertEqual(self.origin_git("rev-parse", "refs/heads/release/9.1"), cut)
        self.assertEqual(self.origin_git("show", "-s", "--format=%P%n%an <%ae>%n%cn <%ce>", cut).splitlines(), [
            f"{self.line_start} {self.develop}", f"{BOT} <{BOT_EMAIL}>", f"{BOT} <{BOT_EMAIL}>",
        ])
        self.assertEqual(self.origin_git("show", f"{cut}:app.txt"), "two")
        self.assertEqual(self.origin_git("show", f"{cut}:{STATE}") + "\n", beta.render_state("9.1.0", 2))
        self.assertEqual((self.origin_git("show", f"{cut}:{LOCK}") + "\n").encode(), lock(DRIFTED))
        self.repository.sync()
        self.assertEqual(self.repository.commits_missing_from_develop(cut), [
            beta.Commit(cut, BOT_EMAIL, ("app.txt", LOCK, STATE)),
            beta.Commit(self.line_start, MAINTAINER, (LOCK, STATE)),
        ])

    def test_fix_detection_reads_the_author_not_the_committer(self):
        self.git(self.work, "checkout", "--quiet", "--detach", self.line_start)
        (self.work / "app.txt").write_text("release fix\n")
        self.git(self.work, "add", "--all")
        subprocess.run(["git", "-c", "user.name=Maintainer", "-c", f"user.email={MAINTAINER}",
                        "commit", "--quiet", "-m", "fix committed by the bot"], cwd=self.work, check=True,
                       env={**os.environ, "GIT_COMMITTER_NAME": BOT, "GIT_COMMITTER_EMAIL": BOT_EMAIL})
        fixed = self.git(self.work, "rev-parse", "HEAD")
        self.git(self.work, "push", str(self.origin), f"{fixed}:refs/heads/release/9.1")
        self.repository.sync()
        self.assertEqual(self.repository.commits_missing_from_develop(fixed)[0],
                         beta.Commit(fixed, MAINTAINER, ("app.txt",)))

    def test_full_sync_forgets_deleted_release_branches(self):
        self.git(self.work, "push", str(self.origin), f"{self.line_start}:refs/heads/release/9.2")
        self.repository.sync()
        self.assertIn("release/9.2", self.repository.release_heads())
        self.git(self.work, "push", str(self.origin), ":refs/heads/release/9.2")
        self.repository.sync()
        self.assertEqual(self.repository.release_heads(), {"release/9.1": self.line_start})

    def test_merge_reports_conflicts_and_push_refuses_to_rewind(self):
        self.git(self.work, "checkout", "--quiet", "--detach", self.line_start)
        (self.work / "app.txt").write_text("release fix\n")
        fixed = self.commit("fix on the release branch")
        self.git(self.work, "push", str(self.origin), f"{fixed}:refs/heads/release/9.1")
        self.repository.sync()
        worktree, conflicts = self.repository.start_merge(fixed, self.develop, BOT, BOT_EMAIL)
        self.addCleanup(self.repository.discard, worktree)
        self.assertEqual(conflicts, ["app.txt"])
        with self.assertRaisesRegex(RuntimeError, "git push"):
            self.repository.push(self.line_start, "release/9.1")

    def test_fingerprints_run_each_commits_own_recipe_scripts(self):
        shutil.copytree(ROOT / ".github", self.work / ".github")
        recipes = self.commit("add the pipeline")
        self.git(self.work, "push", str(self.origin), f"{recipes}:refs/heads/develop")
        dockerfile = self.work / ".github/images/linux-ci/Dockerfile.noble"
        dockerfile.write_text(dockerfile.read_text() + "# drift\n")
        drifted = self.commit("drift the noble recipe")
        self.git(self.work, "push", str(self.origin), f"{drifted}:refs/heads/develop")
        self.repository.sync()
        expected = {
            key: subprocess.run(["bash", ".github/scripts/linux-ci-image-fingerprint.sh", spec[1]], cwd=ROOT,
                                check=True, capture_output=True, text=True).stdout.strip()
            for key, spec in beta.images.IMAGES.items()
        }
        self.assertEqual(self.repository.fingerprints(recipes), expected)
        changed = self.repository.fingerprints(drifted)
        self.assertNotEqual(changed["x86_64"], expected["x86_64"])
        self.assertEqual(changed["armhf_runtime"], expected["armhf_runtime"])
        self.assertIsNone(self.repository.fingerprints("0" * 40))

    def test_lock_validation_runs_the_merged_trees_validator(self):
        shutil.copytree(ROOT / ".github", self.work / ".github")
        (self.work / LOCK).write_text('{"schema": 3}\n')
        self.commit("add the pipeline")
        with self.assertRaisesRegex(RuntimeError, "Unsupported Linux image selection schema"):
            self.repository.validate_lock(self.work)


class WorkflowWiringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.CUT = (ROOT / ".github/workflows/beta-cut.yml").read_text()
        cls.CONTINUE = (ROOT / ".github/workflows/beta-continue.yml").read_text()

    @staticmethod
    def jobs(workflow):
        body = workflow.split("\njobs:\n", 1)[1]
        names = re.findall(r"^  ([A-Za-z0-9_-]+):$", body, re.M)
        blocks = re.split(r"^  [A-Za-z0-9_-]+:$", body, flags=re.M)[1:]
        return dict(zip(names, blocks))

    def test_candidate_run_name_is_what_the_continuation_parses(self):
        helper = (ROOT / ".github/workflows/release-tag-helper.yml").read_text()
        run_name = re.search(r"^run-name: (.+)$", helper, re.M).group(1)
        rendered = run_name.replace("${{ inputs.version }}", "3.3.0-beta2").replace("${{ inputs.operation }}", "create")
        self.assertEqual(beta.candidate_of({"display_title": rendered}), ("3.3.0-beta2", "create"))

    def test_preparation_run_name_is_what_the_scheduler_parses(self):
        workflow = (ROOT / ".github/workflows/prepare-release-dependencies.yml").read_text()
        run_name = re.search(r"^run-name: (.+)$", workflow, re.M).group(1)
        rendered = run_name.replace("${{ inputs.source_branch }}", "develop").replace(
            "${{ inputs.generation }}", "validated-build-20261001-1-1")
        self.assertEqual(beta.preparation_of({"display_title": rendered}), ("develop", "validated-build-20261001-1-1"))

    def test_beta_workflows_act_only_when_beta_automation_is_exactly_enabled(self):
        for workflow in (self.CUT, self.CONTINUE):
            jobs = self.jobs(workflow)
            gate = jobs["gate"]
            self.assertIn("BETA_AUTOMATION: ${{ vars.BETA_AUTOMATION }}", gate)
            self.assertRegex(
                gate, r'if \[ "\$BETA_AUTOMATION" = enabled \]; then\s+echo "enabled=true" >> "\$GITHUB_OUTPUT"')
            self.assertIn('>> "$GITHUB_STEP_SUMMARY"', gate)
            # Expression comparisons ignore case, so the variable is compared only in the shell.
            self.assertEqual(workflow.count("vars.BETA_AUTOMATION"), 1)
            acting = [name for name, block in jobs.items() if "environment: beta-automation" in block]
            self.assertEqual(len(acting), 1)
            self.assertIn("needs: gate", jobs[acting[0]])
            # Neither a manual dispatch nor always() may bypass the pause.
            self.assertRegex(jobs[acting[0]], r"\n    if: needs\.gate\.outputs\.enabled == 'true'\n")

    def test_beta_cut_runs_weekly_on_tuesday_at_2000_utc_and_on_dispatch(self):
        triggers = re.search(r"^on:\n((?:  .*\n)+)", self.CUT, re.M).group(1)
        self.assertEqual(triggers, '  schedule:\n    - cron: "0 20 * * 2"\n  workflow_dispatch:\n')

    def test_cut_and_continuation_act_one_at_a_time(self):
        for workflow in (self.CUT, self.CONTINUE):
            # A workflow-level group would also hold the gate runs, whose arrival cancels a pending acting run.
            self.assertNotRegex(workflow, r"(?m)^concurrency:")
            acting = [block for block in self.jobs(workflow).values() if "environment: beta-automation" in block]
            self.assertIn("    concurrency:\n      group: beta-scheduler\n      cancel-in-progress: false\n", acting[0])

    def test_beta_continue_follows_beta_runs_and_preparations_from_develop(self):
        self.assertIn(
            'on:\n  workflow_run:\n'
            '    workflows: ["CI", "Prepare Release Candidate", "Prepare Release Dependencies", "Promote Release Source"]\n'
            '    types: [completed]\n    branches: ["release/**", "develop"]\n', self.CONTINUE)
        # Every other run on develop, such as a person's push CI, skips the gate and starts no job.
        self.assertIn(
            "    if: >-\n"
            "      github.event.workflow_run.actor.type == 'Bot' &&\n"
            "      (github.event.workflow_run.head_branch != 'develop' ||\n"
            "       github.event.workflow_run.path == '.github/workflows/prepare-release-dependencies.yml')\n",
            self.jobs(self.CONTINUE)["gate"])

    def test_release_bot_token_is_minted_only_in_the_beta_automation_job(self):
        for workflow in (self.CUT, self.CONTINUE):
            for name, block in self.jobs(workflow).items():
                minted = "actions/create-github-app-token@" in block
                self.assertEqual(minted, "environment: beta-automation" in block, name)
                if minted:
                    self.assertRegex(block, r"uses: actions/create-github-app-token@[0-9a-f]{40} # v\d")
                    self.assertIn("client-id: ${{ vars.RELEASE_BOT_APP_ID }}", block)
                    self.assertIn("private-key: ${{ secrets.RELEASE_BOT_PRIVATE_KEY }}", block)
                    self.assertIn("persist-credentials: false", block)
                    self.assertIn("issues: write", block)


if __name__ == "__main__":
    unittest.main()
