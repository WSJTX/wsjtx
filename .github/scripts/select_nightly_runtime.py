#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Decide whether Nightly Tests tests this commit.

A scheduled run skips only when the newest completed scheduled run that
succeeded or failed tested the same commit and succeeded, so an unmoved
develop that passed is tested once. A night after a failed nightly retests the
same commit. Cancelled and skipped runs do not count. A dispatched run always
tests.
"""

from __future__ import annotations

import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass


API_VERSION = "2022-11-28"
WORKFLOW = "nightly-runtime-tests.yml"
COUNTED_CONCLUSIONS = ("success", "failure")


@dataclass(frozen=True)
class Decision:
    run: bool
    previous: dict | None
    reason: str


def run_order(run: dict) -> tuple[str, int]:
    return run.get("created_at") or "", run.get("id") or 0


def newest_counted_scheduled_run(runs: list[dict]) -> dict | None:
    counted = [
        run for run in runs
        if run.get("event") == "schedule"
        and run.get("status") == "completed"
        and run.get("conclusion") in COUNTED_CONCLUSIONS
    ]
    return max(counted, key=run_order, default=None)


def decide(event_name: str, head_sha: str, runs: list[dict]) -> Decision:
    previous = newest_counted_scheduled_run(runs)
    if event_name == "workflow_dispatch":
        return Decision(True, previous, "dispatched runs always test")
    if event_name != "schedule":
        raise ValueError(f"unsupported event: {event_name}")
    if previous is None:
        return Decision(True, None, "no earlier scheduled run succeeded or failed")
    if previous.get("head_sha") != head_sha:
        return Decision(True, previous, "develop has moved since the newest scheduled run")
    if previous.get("conclusion") == "success":
        return Decision(False, previous, f"the newest scheduled run already passed at {head_sha}")
    return Decision(True, previous, "the newest scheduled run failed at this commit, so it is tested again")


class GitHub:
    def __init__(self, token: str, repository: str, api_url: str = "https://api.github.com") -> None:
        self.token = token
        self.repository = repository
        self.api_url = api_url.rstrip("/")

    def call(self, path: str, params: dict) -> dict:
        url = self.api_url + path + "?" + urllib.parse.urlencode(params)
        request = urllib.request.Request(url, method="GET", headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {self.token}",
            "X-GitHub-Api-Version": API_VERSION,
            "User-Agent": "wsjtx-nightly-runtime-tests",
        })
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            raise RuntimeError(f"GET {path} failed with HTTP {error.code}") from error

    def scheduled_runs(self) -> list[dict]:
        path = f"/repos/{self.repository}/actions/workflows/{WORKFLOW}/runs"
        return self.call(path, {"event": "schedule", "status": "completed", "per_page": 100})["workflow_runs"]


def summary(decision: Decision, head_sha: str, server: str, repository: str) -> str:
    lines = ["## Nightly tests", ""]
    lines.append(f"{'Testing' if decision.run else 'Skipping'} `{head_sha}`: {decision.reason}.")
    if decision.previous is not None:
        previous_sha = decision.previous.get("head_sha", "")
        lines += ["", f"Newest scheduled run: {decision.previous.get('html_url', '')} at `{previous_sha}`."]
        if previous_sha and previous_sha != head_sha:
            lines.append(f"Changes since then: {server}/{repository}/compare/{previous_sha}...{head_sha}")
    return "\n".join(lines) + "\n"


def main() -> int:
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GITHUB_TOKEN is required", file=sys.stderr)
        return 2
    repository = os.environ["GITHUB_REPOSITORY"]
    head_sha = os.environ["GITHUB_SHA"]
    event_name = os.environ["GITHUB_EVENT_NAME"]
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")

    decision = decide(event_name, head_sha, GitHub(token, repository).scheduled_runs())
    print(f"{'Testing' if decision.run else 'Skipping'} {head_sha}: {decision.reason}")
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"run={'true' if decision.run else 'false'}\n")
    step_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if step_summary:
        with open(step_summary, "a", encoding="utf-8") as stream:
            stream.write(summary(decision, head_sha, server, repository))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
