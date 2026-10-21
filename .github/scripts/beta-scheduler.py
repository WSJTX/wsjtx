#!/usr/bin/env python3
"""Advance the scheduled beta line by one state-machine step per workflow run."""

from __future__ import annotations

import argparse
import base64
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import traceback
from typing import Callable, NamedTuple
import urllib.error
import urllib.parse
import urllib.request
import zipfile


def load_script(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


policy = load_script("release_policy", "release-policy.py")
images = load_script("release_linux_images", "release-linux-images.py")
image_tags = load_script("resolve_linux_ci_image_tag", "resolve-linux-ci-image-tag.py")

PUBLIC_REPOSITORY = "WSJTX/wsjtx"
ALERT_TITLE = "Scheduled beta alerts"
ALERT_CREATOR = "github-actions[bot]"
STATE_FILE = "release-state.txt"
LOCK_FILE = "release-linux-images.json"
METADATA_FILES = frozenset((STATE_FILE, LOCK_FILE))
PREPARED_ARTIFACT = "prepared-release-dependencies"
IMAGE_PACKAGES = (
    image_tags.NORMAL_PACKAGE,
    image_tags.ARM64_PACKAGE,
    image_tags.ARMHF_CROSS_PACKAGE,
    image_tags.ARMHF_PACKAGE,
    image_tags.ARMHF_TRIXIE_CROSS_PACKAGE,
    image_tags.ARMHF_TRIXIE_PACKAGE,
)
CI = "ci.yml"
CANDIDATE = "release-tag-helper.yml"
PROMOTION = "promote-release.yml"
PREPARATION = "prepare-release-dependencies.yml"
WARMER = "warm-dependency-caches.yml"
PUBLISHER = "publish-linux-ci-images.yml"
PUBLICATION = "public-release.yml"
BETA_CUT = "beta-cut.yml"
IMAGE_REFRESH_JOB = "refresh-linux-images"
NORMAL_IMAGES_JOB = "build-normal"
CANDIDATE_TITLE = re.compile(r"Prepare Release Candidate build/v(?P<version>\S+) \((?P<operation>validate|create)\)")
PREPARATION_TITLE = re.compile(r"Prepare Release Dependencies (?P<source>\S+) (?P<generation>\S+)")
GENERATION_TAG = re.compile(r"validated-build-\d{8}-(?P<run>\d+)-(?P<attempt>\d+)")


class Skip(Exception):
    """Nothing to do now; a later run tries again. Reported in the job summary only."""


class Stop(Exception):
    """A person must act. Reported in the job summary and on the tracking issue."""

    def __init__(self, stage: str, reason: str, link: str | None = None) -> None:
        super().__init__(reason)
        self.stage = stage
        self.link = link


class Spent(Exception):
    """The candidate or the public build failed after its tag was pushed; tags never move, so the number is used up."""

    def __init__(self, stage: str, reason: str, link: str | None = None) -> None:
        super().__init__(reason)
        self.stage = stage
        self.link = link


class Line(NamedTuple):
    branch: str
    tip: str
    version: str
    number: int

    def prerelease(self, number: int | None = None) -> str:
        return f"{self.version}-beta{self.number if number is None else number}"


class Commit(NamedTuple):
    sha: str
    author_email: str
    files: tuple[str, ...]


def candidate_tag(version: str) -> str:
    return f"build/v{version}"


def in_flight(run: dict) -> bool:
    return run.get("status") != "completed"


def run_order(run: dict) -> tuple[str, int]:
    return run.get("created_at") or "", run.get("id") or 0


def newest(runs: list[dict]) -> dict | None:
    return max(runs, key=run_order, default=None)


def candidate_of(run: dict) -> tuple[str, str] | None:
    match = CANDIDATE_TITLE.fullmatch(run.get("display_title") or "")
    return (match["version"], match["operation"]) if match else None


def preparation_of(run: dict) -> tuple[str, str] | None:
    match = PREPARATION_TITLE.fullmatch(run.get("display_title") or "")
    return (match["source"], match["generation"]) if match else None


def render_state(version: str, number: int) -> str:
    return f"version={version}\nchannel=BETA\nprerelease={number}\nrevision=$Format:%H$\nwindows_signing=unsigned\n"


def git_identity(name: str, email: str) -> dict[str, str]:
    return {"GIT_AUTHOR_NAME": name, "GIT_AUTHOR_EMAIL": email,
            "GIT_COMMITTER_NAME": name, "GIT_COMMITTER_EMAIL": email}


def lock_recipes(lock: bytes | None) -> dict[str, str] | None:
    try:
        parsed = json.loads(lock)
        if not isinstance(parsed, dict) or parsed.get("schema") != 2:
            return None
        selected = parsed["images"]
        if set(selected) != set(images.IMAGES):
            return None
        return {key: selected[key]["recipe_sha256"] for key in images.IMAGES}
    except (TypeError, ValueError, KeyError):
        return None


def lock_generation(lock: bytes) -> str | None:
    try:
        return json.loads(lock).get("generation")
    except (ValueError, AttributeError):
        return None


class Scheduler:
    def __init__(self, repository, internal, public=None, packages=None, *, bot_login: str,
                 bot_email: str | None = None, notes: list[str] | None = None) -> None:
        self.repository = repository
        self.internal = internal
        self.public = public
        self.packages = packages
        self.bot_login = bot_login
        self.bot_email = bot_email
        self.notes = [] if notes is None else notes
        self.notices: list[tuple[str, str, str | None]] = []
        self.recipe_cache: dict[str, dict[str, str] | None] = {}

    def beta_line(self) -> Line:
        lines = []
        for branch, tip in sorted(self.repository.release_heads().items()):
            if not policy.RELEASE_BRANCH_RE.fullmatch(branch):
                continue
            raw = self.repository.read(tip, STATE_FILE)
            try:
                state = policy.parse_state(raw.decode("utf-8"))
            except (AttributeError, UnicodeDecodeError, ValueError) as error:
                if raw is not None and b"channel=BETA" in raw.splitlines():
                    raise Stop("line", f"{branch}'s {STATE_FILE} says channel=BETA but does not validate: "
                                       f"{error}") from error
                continue
            if state["channel"] != "BETA":
                continue
            if policy.classify(state["version"])["release_branch"] != branch:
                raise Stop("line", f"{branch} is in BETA state for version {state['version']}, "
                                   "which belongs to another release line.")
            lines.append(Line(branch, tip, state["version"], int(state["prerelease"])))
        if not lines:
            raise Skip("No release/X.Y branch is in BETA state, as between a freeze and the next line's start; "
                       "nothing to do.")
        if len(lines) > 1:
            found = ", ".join(line.branch for line in lines)
            raise Stop("line", f"Exactly one release/X.Y branch may be in BETA state; found {found}.")
        line = lines[0]
        self.notes.append(f"Beta line: {line.branch} at {line.tip}, state BETA {line.number}.")
        return line

    def cut(self) -> str:
        self.repository.sync()
        line = self.beta_line()
        tag_sha = self.repository.tag(candidate_tag(line.prerelease()))
        if tag_sha is None:
            return self.retry(line)
        public_tag = f"v{line.prerelease()}"
        if self.public.release_published(public_tag):
            self.notes.append(f"`{public_tag}` is published, so the next cut is BETA {line.number + 1}.")
            return self.advance(line)
        outcome = self.unpublished(line, tag_sha)
        if not isinstance(outcome, Spent):
            raise outcome
        self.notes.append(str(outcome))
        result = self.advance(line, spent=outcome)
        if outcome.stage == "publication":
            # The continuation never sees the public repository's runs, so this spend alerts by itself.
            self.notices.append((outcome.stage, f"{outcome} {result} Do not re-run its public build: it would publish "
                                                f"{line.prerelease()} after the line has moved on.", outcome.link))
        return result

    def retry(self, line: Line) -> str:
        version = line.prerelease()
        self.notes.append(f"`{candidate_tag(version)}` does not exist, so {version} is retried under the same number.")
        self.check_fixes_on_develop(line)
        runs = self.internal.runs(CI, head_sha=line.tip)
        if any(in_flight(run) for run in runs):
            raise self.wait("ci", runs, f"CI for {line.branch} at {line.tip} is still running; {version} waits for it.")
        ci = newest(runs)
        if ci is None:
            raise Stop("ci", f"{line.branch} at {line.tip} has no CI run, so {version} cannot be cut.")
        if ci.get("conclusion") != "success":
            return self.recut(line, ci)
        candidates = self.candidate_runs(line)
        if any(in_flight(run) for run in candidates):
            raise self.wait("candidate", candidates, f"A candidate run for {version} is still running.")
        return self.dispatch_candidate(line)

    def wait(self, stage: str, runs: list[dict], reason: str) -> Exception:
        running = [run for run in runs if in_flight(run)]
        # The continuation follows only runs the App started, and the weekly cut would otherwise wait a week.
        if any((run.get("actor") or {}).get("login") == self.bot_login for run in running):
            return Skip(reason)
        return Stop(stage, f"{reason} Nothing resumes the cut when that run finishes, so dispatch Scheduled Beta Cut "
                           "after it does.", running[0].get("html_url"))

    def recut(self, line: Line, ci: dict) -> str:
        version = line.prerelease()
        failed = f"CI for {line.branch} at {line.tip} concluded {ci.get('conclusion')}"
        target = self.newest_green_develop()
        if target is None or self.repository.is_ancestor(target, line.tip):
            raise Stop("ci", f"{failed}, and develop has no newer green commit to cut {version} from. Re-run the CI "
                             "if the failure is transient.", ci.get("html_url"))
        self.notes.append(f"{failed}, so {version} is cut again from develop {target}.")
        lock, lock_note = self.lock_for(line, target)
        commit = self.commit_cut(line, line.number, target, lock, lock_note)
        return f"Pushed {commit} to {line.branch}: develop {target} as {version} again."

    def candidate_runs(self, line: Line) -> list[dict]:
        return [run for run in self.internal.runs(CANDIDATE, branch=line.branch, event="workflow_dispatch")
                if candidate_of(run) == (line.prerelease(), "create")]

    def dispatch_candidate(self, line: Line) -> str:
        version = line.prerelease()
        self.internal.dispatch(CANDIDATE, line.branch,
                               {"version": version, "expected_sha": line.tip, "operation": "create"})
        return f"Dispatched Prepare Release Candidate for `{candidate_tag(version)}` at {line.tip}."

    def unpublished(self, line: Line, tag_sha: str) -> Exception:
        version = line.prerelease()
        public_tag = f"v{version}"
        stages = {
            "CI": self.internal.runs(CI, head_sha=tag_sha),
            "candidate": [run for run in self.candidate_runs(line) if run.get("head_sha") == tag_sha],
            "promotion": self.internal.runs(PROMOTION, branch=line.branch, event="workflow_dispatch", head_sha=tag_sha),
            "public release": self.public.runs(PUBLICATION, branch=public_tag, event="push"),
        }
        for stage, runs in stages.items():
            if any(in_flight(run) for run in runs):
                reason = f"`{public_tag}` is not published yet; its {stage} run is still in progress."
                # The public release completes on its own; nothing is due from the cut until it has.
                return Skip(reason) if stage == "public release" else self.wait(stage.lower(), runs, reason)
        failed = newest(stages["candidate"])
        if failed is None:
            return Stop("candidate", f"`{candidate_tag(version)}` exists, but no candidate run for it was found.")
        candidate = newest([run for run in stages["candidate"] if run.get("conclusion") == "success"])
        # A run reports only its latest attempt, so a later failed re-run can hide a promoted candidate.
        promoted = newest([run for run in stages["promotion"] if run.get("conclusion") == "success"])
        if candidate is None and promoted is None:
            return Spent("candidate",
                         f"The candidate run for `{candidate_tag(version)}` concluded {failed.get('conclusion')} after "
                         f"the tag was pushed. Build tags never move, so {version} is spent and the next cut is "
                         f"BETA {line.number + 1}.", failed.get("html_url"))
        promotion = promoted or newest(stages["promotion"])
        if promotion is None:
            return Stop("promotion", f"The candidate for {version} succeeded, but Promote Release Source was not "
                                     f"dispatched. Dispatch it with operation=promote and candidate run "
                                     f"{candidate.get('id')}.", candidate.get("html_url"))
        if promotion.get("conclusion") != "success":
            return Stop("promotion", f"Promote Release Source for {version} concluded {promotion.get('conclusion')}.",
                        promotion.get("html_url"))
        publication = newest(stages["public release"])
        if publication is None:
            return Stop("publication", f"`{public_tag}` was promoted, but no public release run started for it.",
                        promotion.get("html_url"))
        if publication.get("conclusion") != "success":
            return Spent("publication", f"The public release run for `{public_tag}` concluded "
                                        f"{publication.get('conclusion')}. Tags never move, so {version} is spent and "
                                        f"the next cut is BETA {line.number + 1}.", publication.get("html_url"))
        return Stop("publication", f"The public release run for `{public_tag}` succeeded, but the release is not "
                                   "published.", publication.get("html_url"))

    def advance(self, line: Line, spent: Spent | None = None) -> str:
        number = line.number + 1
        target = self.newest_green_develop()
        if target is None:
            raise Stop("develop", "No commit on develop has a successful push CI run.")
        if self.repository.is_ancestor(target, line.tip):
            if spent is not None:
                action = "; land the fix on develop."
                if spent.stage == "publication":
                    action = (f". Until a cut moves on, a re-run of the public build that succeeds still publishes "
                              f"{line.prerelease()}: re-run the public build if its failure was transient, otherwise "
                              "land the fix on develop.")
                raise Stop(spent.stage, f"{spent} develop has no newer green commit to cut "
                                        f"{line.prerelease(number)} from{action}", spent.link)
            raise Skip(f"develop has not moved: its newest green commit {target} is already in {line.branch}.")
        self.check_fixes_on_develop(line)
        lock, lock_note = self.lock_for(line, target)
        commit = self.commit_cut(line, number, target, lock, lock_note)
        return f"Pushed {commit} to {line.branch}: develop {target} as {line.prerelease(number)}."

    def newest_green_develop(self) -> str | None:
        develop = self.repository.develop()
        runs = self.internal.runs(CI, branch="develop", event="push", status="success")
        for run in sorted(runs, key=run_order, reverse=True):
            sha = run.get("head_sha", "")
            if self.repository.contains(sha) and self.repository.is_ancestor(sha, develop):
                self.notes.append(f"Newest green develop commit: {sha}.")
                return sha
        return None

    def check_fixes_on_develop(self, line: Line) -> None:
        fixes = [
            commit.sha for commit in self.repository.commits_missing_from_develop(line.tip)
            if commit.author_email != self.bot_email and not METADATA_FILES.issuperset(commit.files)
        ]
        if fixes:
            raise Stop("fixes", f"{line.branch} holds commits that are not on develop: {', '.join(fixes)}. "
                                "Fixes land on develop first.")

    def recipes(self, commit: str) -> dict[str, str] | None:
        if commit not in self.recipe_cache:
            self.recipe_cache[commit] = self.repository.fingerprints(commit)
        return self.recipe_cache[commit]

    def lock_for(self, line: Line, target: str) -> tuple[bytes, str]:
        wanted = self.recipes(target)
        if wanted is None:
            raise RuntimeError(f"develop commit {target} is missing from the checkout")
        if set(wanted) != set(images.IMAGES):
            raise Skip(f"Newest green develop commit {target} predates dual-distro ARMHF images; "
                       "wait for CI to pass on a commit with Bookworm and Trixie support.")
        current = self.repository.read(line.tip, LOCK_FILE)
        if current is not None and lock_recipes(current) == wanted:
            self.notes.append(f"Linux image lock: kept; develop's recipe fingerprints match {line.branch}'s lock.")
            return current, ""
        generation = self.generation_for(wanted)
        lock, run = self.prepared_lock(generation, wanted)
        self.notes.append(f"Linux image lock: replaced with {generation} from {run.get('html_url')}.")
        return lock, f"The Linux image lock moves to {generation}, prepared by {run.get('html_url')}."

    def published_generations(self) -> dict[int, str]:
        shared: set[str] | None = None
        for package in IMAGE_PACKAGES:
            try:
                versions = self.packages.list_versions(image_tags.OWNER, package)
            except RuntimeError as error:
                if isinstance(error.__cause__, urllib.error.HTTPError) and error.__cause__.code == 404:
                    return {}
                raise
            tags = {tag for version in versions
                    for tag in version.tags if GENERATION_TAG.fullmatch(tag)}
            shared = tags if shared is None else shared & tags
        generations: dict[int, str] = {}
        for tag in sorted(shared or (), key=lambda item: int(GENERATION_TAG.fullmatch(item)["attempt"])):
            generations[int(GENERATION_TAG.fullmatch(tag)["run"])] = tag
        return generations

    def generation_for(self, wanted: dict[str, str]) -> str:
        published = self.published_generations()
        refreshing = None
        runs = (self.internal.runs(WARMER, branch="develop")
                + self.internal.runs(PUBLISHER, branch="develop", event="workflow_dispatch"))
        for run in sorted(runs, key=run_order, reverse=True):
            matches = self.recipes(run.get("head_sha", "")) == wanted
            # A generation is tagged only by the run's promote job, so it can be used before the run ends.
            if matches and run.get("id") in published:
                return published[run["id"]]
            if refreshing is None and in_flight(run) and matches and self.refreshes_images(run):
                refreshing = run
        if refreshing is not None:
            raise Stop("lock", "The Linux images for develop's recipe fingerprints are still being refreshed. Nothing "
                               "resumes the cut when that run finishes, so dispatch Scheduled Beta Cut after it does.",
                       refreshing.get("html_url"))
        raise Stop("lock", "No validated Linux image generation matches develop's recipe fingerprints. "
                           "Refresh the images with Refresh Build Caches (targets: linux-images) on develop.")

    def refreshes_images(self, run: dict) -> bool:
        for job in self.internal.jobs(run["id"]):
            name = job.get("name") or ""
            if name in (NORMAL_IMAGES_JOB, f"{IMAGE_REFRESH_JOB} / {NORMAL_IMAGES_JOB}"):
                return job.get("conclusion") != "skipped"
            if name == IMAGE_REFRESH_JOB:
                return job.get("status") != "completed"
        # The run has not yet decided whether it builds the normal images.
        return True

    def prepared_lock(self, generation: str, wanted: dict[str, str]) -> tuple[bytes, dict]:
        runs = [run for run in self.internal.runs(PREPARATION, branch="develop", event="workflow_dispatch")
                if preparation_of(run) == ("develop", generation)]
        if any(in_flight(run) for run in runs):
            raise self.wait("lock", runs, f"Prepare Release Dependencies for {generation} is still running; the cut "
                                          "waits for its lock.")
        for run in sorted(runs, key=run_order, reverse=True):
            if run.get("conclusion") != "success":
                continue
            lock = self.internal.artifact_file(run["id"], PREPARED_ARTIFACT, LOCK_FILE)
            if lock is not None and lock_generation(lock) == generation and lock_recipes(lock) == wanted:
                return lock, run
        latest = newest(runs)
        if latest is not None and latest.get("conclusion") != "success":
            raise Stop("lock", f"Prepare Release Dependencies for {generation} concluded {latest.get('conclusion')}. "
                               "Re-run it to resume the cut.", latest.get("html_url"))
        tip = self.repository.develop()
        if self.recipes(tip) != wanted:
            # The preparation verifies the images against develop's tip, not against the commit being cut.
            raise Stop("lock", f"develop's tip {tip} has other Linux image recipes than its newest green commit, so "
                               f"a preparation of {generation} would fail. Dispatch Scheduled Beta Cut once the tip's "
                               "CI has passed.")
        self.internal.dispatch(PREPARATION, "develop", {"source_branch": "develop", "generation": generation})
        raise Skip(f"Dispatched Prepare Release Dependencies for {generation}; a later run commits its lock "
                   "in the cut.")

    def commit_cut(self, line: Line, number: int, target: str, lock: bytes, lock_note: str) -> str:
        version = line.prerelease(number)
        worktree, conflicts = self.repository.start_merge(line.tip, target, self.bot_login, self.bot_email)
        try:
            unexpected = sorted(set(conflicts) - METADATA_FILES)
            if unexpected:
                raise Stop("merge", f"Merging develop {target} into {line.branch} conflicts in "
                                    f"{', '.join(unexpected)}.")
            self.repository.write(worktree, {STATE_FILE: render_state(line.version, number).encode(), LOCK_FILE: lock})
            try:
                self.repository.validate_lock(worktree)
            except RuntimeError as error:
                raise Stop("lock", f"The Linux image lock for {version} does not validate: {error}") from error
            body = textwrap.fill(
                f"Merge develop at {target} into {line.branch} with release state BETA {number}. {lock_note}".strip(),
                72, break_long_words=False, break_on_hyphens=False)
            commit = self.repository.commit(worktree, self.bot_login, self.bot_email,
                                            f"Cut {version} from develop\n\n{body}\n")
            try:
                self.repository.push(commit, line.branch)
            except RuntimeError as error:
                raise Stop("merge", f"Pushing the {version} cut to {line.branch} failed: {error}") from error
            return commit
        finally:
            self.repository.discard(worktree)

    def continue_after(self, run: dict) -> str:
        actor = (run.get("actor") or {}).get("login")
        if actor != self.bot_login:
            raise Skip(f"{run.get('html_url')} was started by {actor}, not {self.bot_login}; nothing to do.")
        workflow = (run.get("path"), run.get("event"))
        conclusion = run.get("conclusion")
        if workflow == (f".github/workflows/{PREPARATION}", "workflow_dispatch"):
            return self.after_preparation(run)
        self.repository.sync(shallow=True)
        line = self.beta_line()
        if (run.get("head_branch"), run.get("head_sha")) != (line.branch, line.tip):
            raise Skip(f"{run.get('html_url')} ran on {run.get('head_branch')} at {run.get('head_sha')}, not at the "
                       f"tip of {line.branch}; nothing to do.")
        version = line.prerelease()
        tag = candidate_tag(version)
        if workflow == (f".github/workflows/{CI}", "push"):
            if conclusion != "success":
                raise Stop("ci", f"CI for the {version} cut concluded {conclusion}. Re-run it if the failure is "
                                 "transient.", run.get("html_url"))
            if self.repository.tag(tag) is not None:
                raise Skip(f"`{tag}` already exists; nothing to do.")
            if any(in_flight(candidate) for candidate in self.candidate_runs(line)):
                raise Skip(f"A candidate run for {version} is already running; nothing to do.")
            return self.dispatch_candidate(line)
        candidate_run = workflow == (f".github/workflows/{CANDIDATE}", "workflow_dispatch")
        if candidate_run and candidate_of(run) == (version, "create"):
            if conclusion != "success":
                if self.repository.tag(tag) is None:
                    after = f"the next scheduled cut retries {version}"
                else:
                    after = f"the next scheduled cut moves on to BETA {line.number + 1}, because build tags never move"
                raise Stop("candidate", f"The candidate run for `{tag}` concluded {conclusion}. Re-run it if the "
                                        f"failure is transient; otherwise {after}.", run.get("html_url"))
            tag_sha = self.repository.tag(tag)
            if tag_sha != line.tip:
                raise Stop("candidate", f"The candidate run succeeded, but `{tag}` resolves to {tag_sha or 'nothing'} "
                                        f"instead of {line.tip}.", run.get("html_url"))
            self.internal.dispatch(PROMOTION, line.branch,
                                   {"version": version, "candidate_run_id": str(run["id"]), "operation": "promote"})
            return f"Dispatched Promote Release Source for `v{version}` from candidate run {run['id']}."
        if workflow == (f".github/workflows/{PROMOTION}", "workflow_dispatch"):
            if conclusion != "success":
                raise Stop("promotion", f"Promote Release Source for {version} concluded {conclusion}. Re-running it "
                                        f"resumes {version}.", run.get("html_url"))
            raise Skip(f"`v{version}` is promoted; the public repository's release run publishes it.")
        raise Skip(f"{run.get('html_url')} is not a CI, candidate or promotion run of the {version} cut; "
                   "nothing to do.")

    def after_preparation(self, run: dict) -> str:
        prepared = preparation_of(run)
        if run.get("head_branch") != "develop" or prepared is None or prepared[0] != "develop":
            raise Skip(f"{run.get('html_url')} is not a preparation from develop; nothing to do.")
        if run.get("conclusion") != "success":
            raise Stop("lock", f"Prepare Release Dependencies for {prepared[1]} concluded {run.get('conclusion')}. "
                               "Re-run it to resume the cut.", run.get("html_url"))
        self.internal.dispatch(BETA_CUT, "develop", {})
        return f"Dispatched the beta cut on develop: the lock for {prepared[1]} is prepared."


class Repository:
    """Git operations on the workflow checkout; remote access authenticates with the release-bot token."""

    def __init__(self, root: Path, remote: str, token: str | None = None, server: str = "https://github.com") -> None:
        self.root = root
        self.remote = remote
        self.auth: tuple[str, ...] = ()
        if token:
            credential = base64.b64encode(f"x-access-token:{token}".encode()).decode()
            self.auth = ("-c", f"http.{server}/.extraheader=AUTHORIZATION: basic {credential}")

    def git(self, *arguments: str, cwd: Path | None = None, check: bool = True,
            env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
        result = subprocess.run(["git", *self.auth, *arguments], cwd=cwd or self.root, capture_output=True,
                                env={**os.environ, "GIT_TERMINAL_PROMPT": "0", **(env or {})})
        if check and result.returncode:
            detail = (result.stderr or result.stdout).decode(errors="replace").strip()
            raise RuntimeError(f"git {' '.join(arguments[:2])} failed: {detail}")
        return result

    def output(self, *arguments: str, cwd: Path | None = None) -> str:
        return self.git(*arguments, cwd=cwd).stdout.decode().strip()

    def sync(self, *, shallow: bool = False) -> None:
        releases = "+refs/heads/release/*:refs/remotes/origin/release/*"
        if shallow:
            self.git("fetch", "--prune", "--no-tags", "--depth=1", self.remote, releases)
        else:
            self.git("fetch", "--prune", "--no-tags", self.remote,
                     "+refs/heads/develop:refs/remotes/origin/develop", releases)

    def release_heads(self) -> dict[str, str]:
        listing = self.output("for-each-ref", "--format=%(refname:strip=3) %(objectname)",
                              "refs/remotes/origin/release/")
        return dict(line.split(" ", 1) for line in listing.splitlines())

    def develop(self) -> str:
        return self.output("rev-parse", "--verify", "refs/remotes/origin/develop^{commit}")

    def tag(self, name: str) -> str | None:
        listing = self.output("ls-remote", self.remote, f"refs/tags/{name}", f"refs/tags/{name}^{{}}")
        refs = {ref: sha for sha, ref in (line.split("\t") for line in listing.splitlines())}
        return refs.get(f"refs/tags/{name}^{{}}") or refs.get(f"refs/tags/{name}")

    def contains(self, commit: str) -> bool:
        return bool(commit) and self.git("cat-file", "-e", f"{commit}^{{commit}}", check=False).returncode == 0

    def read(self, commit: str, path: str) -> bytes | None:
        self.git("cat-file", "-e", f"{commit}^{{commit}}")
        if self.git("cat-file", "-e", f"{commit}:{path}", check=False).returncode:
            return None
        return self.git("cat-file", "blob", f"{commit}:{path}").stdout

    def is_ancestor(self, ancestor: str, descendant: str) -> bool:
        result = self.git("merge-base", "--is-ancestor", ancestor, descendant, check=False)
        if result.returncode not in (0, 1):
            raise RuntimeError(f"git merge-base failed: {result.stderr.decode(errors='replace').strip()}")
        return result.returncode == 0

    def commits_missing_from_develop(self, tip: str) -> list[Commit]:
        commits = []
        for sha in self.output("rev-list", f"refs/remotes/origin/develop..{tip}").split():
            email, _, parents = self.output("show", "-s", "--format=%ae%n%P", sha).partition("\n")
            if parents:
                files = self.output("diff", "--no-renames", "--name-only", parents.split()[0], sha)
            else:
                files = self.output("ls-tree", "-r", "--name-only", sha)
            commits.append(Commit(sha, email, tuple(files.splitlines())))
        return commits

    def fingerprints(self, commit: str) -> dict[str, str] | None:
        if not self.contains(commit):
            return None
        archive = self.git("archive", "--format=tar", commit, ".github").stdout
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run(["tar", "-x", "-f", "-", "-C", directory], input=archive, check=True, capture_output=True)
            profiles = images.IMAGES
            if not (Path(directory) / ".github/images/linux-ci/Dockerfile.armhf-cross-trixie").exists():
                profiles = {key: spec for key, spec in profiles.items() if not key.startswith("armhf_trixie")}
            return {
                key: subprocess.run(["bash", ".github/scripts/linux-ci-image-fingerprint.sh", profile], cwd=directory,
                                    check=True, capture_output=True, text=True).stdout.strip()
                for key, (_package, profile, *_rest) in profiles.items()
            }

    def start_merge(self, tip: str, commit: str, name: str, email: str) -> tuple[Path, list[str]]:
        worktree = Path(tempfile.mkdtemp(prefix="beta-cut-")) / "tree"
        self.git("worktree", "add", "--detach", str(worktree), tip)
        merged = self.git("merge", "--no-ff", "--no-commit", commit, cwd=worktree, check=False,
                          env=git_identity(name, email))
        conflicts = self.output("diff", "--name-only", "--diff-filter=U", cwd=worktree).splitlines()
        if merged.returncode and not conflicts:
            detail = (merged.stderr or merged.stdout).decode(errors="replace").strip()
            self.discard(worktree)
            raise RuntimeError(f"git merge failed: {detail}")
        return worktree, conflicts

    def write(self, worktree: Path, files: dict[str, bytes]) -> None:
        for path, content in files.items():
            (worktree / path).write_bytes(content)
        self.git("add", "--", *files, cwd=worktree)

    def validate_lock(self, worktree: Path) -> None:
        result = subprocess.run([sys.executable, ".github/scripts/release-linux-images.py", "validate", LOCK_FILE,
                                 "--check-remote"], cwd=worktree, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError((result.stderr or result.stdout).strip())

    def commit(self, worktree: Path, name: str, email: str, message: str) -> str:
        self.git("commit", "--no-verify", "--quiet", "--message", message, cwd=worktree,
                 env=git_identity(name, email))
        return self.output("rev-parse", "HEAD", cwd=worktree)

    def push(self, commit: str, branch: str) -> None:
        self.git("push", self.remote, f"{commit}:refs/heads/{branch}")

    def discard(self, worktree: Path) -> None:
        self.git("worktree", "remove", "--force", str(worktree), check=False)
        shutil.rmtree(worktree.parent, ignore_errors=True)


class KeepRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *arguments, **keywords):
        return None


class GitHub:
    def __init__(self, token: str, repository: str, api_url: str = "https://api.github.com") -> None:
        self.token = token
        self.repository = repository
        self.api_url = api_url.rstrip("/")

    def request(self, method: str, path: str, *, params: dict | None = None,
                body: object = None) -> urllib.request.Request:
        url = self.api_url + path + ("?" + urllib.parse.urlencode(params) if params else "")
        data = None if body is None else json.dumps(body).encode()
        headers = {
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {self.token}",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "wsjtx-beta-scheduler",
        }
        if data is not None:
            headers["Content-Type"] = "application/json"
        return urllib.request.Request(url, data=data, method=method, headers=headers)

    def call(self, method: str, path: str, *, params: dict | None = None, body: object = None,
             missing_ok: bool = False):
        try:
            with urllib.request.urlopen(self.request(method, path, params=params, body=body), timeout=60) as response:
                payload = response.read()
        except urllib.error.HTTPError as error:
            if missing_ok and error.code == 404:
                return None
            raise RuntimeError(f"{method} {path} failed with HTTP {error.code}") from error
        return json.loads(payload) if payload else None

    def runs(self, workflow: str, **filters: str) -> list[dict]:
        return self.call("GET", f"/repos/{self.repository}/actions/workflows/{workflow}/runs",
                         params={"per_page": 100, **filters})["workflow_runs"]

    def jobs(self, run_id: int) -> list[dict]:
        return self.call("GET", f"/repos/{self.repository}/actions/runs/{run_id}/jobs",
                         params={"per_page": 100})["jobs"]

    def dispatch(self, workflow: str, ref: str, inputs: dict[str, str]) -> None:
        self.call("POST", f"/repos/{self.repository}/actions/workflows/{workflow}/dispatches",
                  body={"ref": ref, "inputs": inputs})

    def release_published(self, tag: str) -> bool:
        path = f"/repos/{self.repository}/releases/tags/{urllib.parse.quote(tag, safe='')}"
        return self.call("GET", path, missing_ok=True) is not None

    def user_id(self, login: str) -> int:
        return self.call("GET", f"/users/{urllib.parse.quote(login, safe='')}")["id"]

    def artifact_file(self, run_id: int, name: str, member: str) -> bytes | None:
        listing = self.call("GET", f"/repos/{self.repository}/actions/runs/{run_id}/artifacts",
                            params={"name": name, "per_page": 100})
        live = [item for item in listing["artifacts"] if item.get("name") == name and not item.get("expired")]
        if not live:
            return None
        archive = self.download(f"/repos/{self.repository}/actions/artifacts/{live[0]['id']}/zip")
        with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
            return bundle.read(member) if member in bundle.namelist() else None

    def download(self, path: str) -> bytes:
        try:
            with urllib.request.build_opener(KeepRedirect).open(self.request("GET", path), timeout=60) as response:
                return response.read()
        except urllib.error.HTTPError as redirect:
            location = redirect.headers.get("Location")
            if redirect.code not in (301, 302, 303, 307, 308) or not location:
                raise RuntimeError(f"GET {path} failed with HTTP {redirect.code}") from redirect
        # The signed storage URL must not receive the API token.
        with urllib.request.urlopen(location, timeout=120) as response:
            return response.read()


class Alerts:
    def __init__(self, github, run_url: str) -> None:
        self.github = github
        self.run_url = run_url

    def post(self, stage: str, reason: str, link: str | None = None) -> None:
        lines = [f"**Stage:** {stage}", f"**Reason:** {reason}", f"**Run:** {link or self.run_url}"]
        if link and link != self.run_url:
            lines.append(f"**Reported by:** {self.run_url}")
        body = "\n\n".join(lines)
        issues_path = f"/repos/{self.github.repository}/issues"
        issues = self.github.call("GET", issues_path,
                                  params={"state": "open", "creator": ALERT_CREATOR, "per_page": 100})
        tracking = sorted(
            issue["number"] for issue in issues
            if issue.get("title") == ALERT_TITLE and "pull_request" not in issue
            and (issue.get("user") or {}).get("login") == ALERT_CREATOR
        )
        if tracking:
            self.github.call("POST", f"{issues_path}/{tracking[0]}/comments", body={"body": body})
        else:
            self.github.call("POST", issues_path, body={"title": ALERT_TITLE, "body": body})


def run_step(command: str, action: Callable[[], str],
             notes: list[str]) -> tuple[int, str, tuple[str, str, str | None] | None]:
    alert = None
    try:
        result, status = action(), 0
    except Skip as skip:
        result, status = str(skip), 0
    except Stop as stop:
        result, status, alert = f"Stopped at {stop.stage}: {stop}", 1, (stop.stage, str(stop), stop.link)
    except Exception as error:  # Every unexpected failure also reaches the tracking issue.
        traceback.print_exc()
        result, status, alert = f"Failed: {error}", 1, (command, f"Unexpected error: {error}", None)
    summary = "\n".join([f"## Scheduled beta: {command}", "", *(f"- {note}" for note in notes), "", result, ""])
    return status, summary, alert


def build_scheduler(command: str, env, notes: list[str]) -> Scheduler:
    repository = env["GITHUB_REPOSITORY"]
    server = env.get("GITHUB_SERVER_URL", "https://github.com")
    api_url = env.get("GITHUB_API_URL", "https://api.github.com")
    token = env["RELEASE_BOT_TOKEN"]
    bot_login = f"{env['RELEASE_BOT_SLUG']}[bot]"
    internal = GitHub(token, repository, api_url)
    git = Repository(Path.cwd(), f"{server}/{repository}.git", token, server)
    if command == "continue":
        return Scheduler(git, internal, bot_login=bot_login, notes=notes)
    return Scheduler(
        git, internal,
        GitHub(env["PUBLIC_READ_TOKEN"], PUBLIC_REPOSITORY, api_url),
        image_tags.GitHubPackagesClient(env["GITHUB_TOKEN"], api_url),
        bot_login=bot_login,
        bot_email=f"{internal.user_id(bot_login)}+{bot_login}@users.noreply.github.com",
        notes=notes,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("cut", help="apply one step of the beta state machine")
    commands.add_parser("continue", help="continue after the workflow run in GITHUB_EVENT_PATH")
    alert_parser = commands.add_parser("alert", help="record a failure on the tracking issue")
    alert_parser.add_argument("--stage", required=True)
    alert_parser.add_argument("--reason", required=True)
    args = parser.parse_args(argv)
    env = os.environ
    repository = env["GITHUB_REPOSITORY"]
    run_url = f"{env.get('GITHUB_SERVER_URL', 'https://github.com')}/{repository}/actions/runs/{env['GITHUB_RUN_ID']}"
    alerts = Alerts(GitHub(env["GITHUB_TOKEN"], repository, env.get("GITHUB_API_URL", "https://api.github.com")),
                    run_url)
    if args.command == "alert":
        alerts.post(args.stage, args.reason)
        return 0
    notes: list[str] = []
    schedulers = []

    def step() -> str:
        scheduler = build_scheduler(args.command, env, notes)
        schedulers.append(scheduler)
        if args.command == "cut":
            return scheduler.cut()
        event = json.loads(Path(env["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
        return scheduler.continue_after(event["workflow_run"])

    status, summary, alert = run_step(args.command, step, notes)
    print(summary)
    if env.get("GITHUB_STEP_SUMMARY"):
        with open(env["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
            stream.write(summary)
    posts = [notice for scheduler in schedulers for notice in scheduler.notices] + ([alert] if alert else [])
    try:
        for post in posts:
            alerts.post(*post)
    except Exception as error:  # The step's own result stands; keep what could not be posted in the summary.
        traceback.print_exc()
        unposted = "".join(f"- Not posted to the tracking issue ({stage}): {reason} {link or ''}\n"
                           for stage, reason, link in posts) + f"- Posting failed: {error}\n"
        print(unposted)
        if env.get("GITHUB_STEP_SUMMARY"):
            with open(env["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
                stream.write(unposted)
        return 1
    if posts:
        if env.get("GITHUB_OUTPUT"):
            with open(env["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
                stream.write("alerted=true\n")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
