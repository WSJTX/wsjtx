#!/usr/bin/env python3

import re
import sys
from pathlib import Path


JOB_PATTERN = re.compile(
    r"^  (?P<quote>[\"']?)(?P<name>[A-Za-z_][A-Za-z0-9_-]*)(?P=quote):"
    r"(?:\s+&[A-Za-z_][A-Za-z0-9_-]*)?(?:\s*#.*)?$"
)
TIMEOUT_PATTERN = re.compile(r"^    timeout-minutes:\s*(\d+)\s*(?:#.*)?$")
MINIMUM_JOB_TIMEOUTS = {
    ("build-macos.yml", "build"): 180,
    ("build-windows.yml", "build"): 150,
    ("public-release.yml", "macos-intel-deps"): 300,
    ("warm-dependency-caches.yml", "warm-macos-x86_64-deps"): 300,
}


def unbounded_jobs(path: Path, seen: "set[tuple[str, str]] | None" = None) -> list[str]:
    failures: list[str] = []
    in_jobs = False
    job_name: str | None = None
    job_line = 0
    has_runner = False
    has_timeout = False
    timeout_minutes: int | None = None

    def finish_job() -> None:
        if job_name is not None and has_runner and seen is not None:
            seen.add((path.name, job_name))
        if job_name is not None and has_runner and not has_timeout:
            failures.append(f"{path}:{job_line}: job '{job_name}' has runs-on but no timeout-minutes")
        minimum = MINIMUM_JOB_TIMEOUTS.get((path.name, job_name or ""))
        if job_name is not None and has_runner and minimum is not None and (
            timeout_minutes is None or timeout_minutes < minimum
        ):
            failures.append(
                f"{path}:{job_line}: job '{job_name}' timeout-minutes must be at least {minimum}"
            )

    for line_number, line in enumerate(path.read_text().splitlines(), start=1):
        if line == "jobs:":
            in_jobs = True
            continue
        if not in_jobs:
            continue
        if line and not line[0].isspace() and not line.startswith("#"):
            finish_job()
            break

        match = JOB_PATTERN.match(line)
        if match:
            finish_job()
            job_name = match.group("name")
            job_line = line_number
            has_runner = False
            has_timeout = False
            timeout_minutes = None
        elif (
            line.startswith("  ")
            and not line.startswith("    ")
            and line.strip()
            and not line.lstrip().startswith("#")
        ):
            finish_job()
            failures.append(f"{path}:{line_number}: unsupported job declaration; timeout cannot be verified")
            job_name = None
            has_runner = False
            has_timeout = False
        elif job_name is not None:
            has_runner = has_runner or line.startswith("    runs-on:")
            has_timeout = has_timeout or line.startswith("    timeout-minutes:")
            timeout_match = TIMEOUT_PATTERN.match(line)
            if timeout_match:
                timeout_minutes = int(timeout_match.group(1))
    else:
        finish_job()

    return failures


def main() -> int:
    workflow_dir = Path(".github/workflows")
    workflows = sorted((*workflow_dir.glob("*.yml"), *workflow_dir.glob("*.yaml")))
    seen: set[tuple[str, str]] = set()
    failures = [failure for path in workflows for failure in unbounded_jobs(path, seen)]
    failures += [f"{workflow_dir / name}: no runner-backed job '{job}' for its minimum timeout"
                 for name, job in sorted(set(MINIMUM_JOB_TIMEOUTS) - seen)]
    if failures:
        print("Every runner-backed GitHub Actions job must define timeout-minutes:", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"Validated explicit timeouts for runner-backed jobs in {len(workflows)} workflows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
