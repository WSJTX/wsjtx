#!/usr/bin/env python3
"""Render ctest-results.xml (JUnit) as a GitHub Actions step summary.

Usage: publish-ctest-summary.py <label> [path-to-xml]

Output goes to stdout; callers redirect into $GITHUB_STEP_SUMMARY.
Missing or malformed XML produces a legible notice rather than a hard fail
so the summary step stays non-blocking (the ctest step is the fail gate).
"""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def main() -> int:
    label = sys.argv[1] if len(sys.argv) > 1 else ""
    xml_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("ctest-results.xml")

    if not xml_path.exists():
        print(f"## Test Results — {label}")
        print()
        print(f"No `{xml_path.name}` produced (ctest step did not run or was skipped).")
        return 0

    try:
        root = ET.parse(xml_path).getroot()
    except ET.ParseError as exc:
        print(f"## Test Results — {label}")
        print()
        print(f"Malformed `{xml_path.name}`: {exc}")
        return 0

    suite = root if root.tag == "testsuite" else root.find("testsuite")
    if suite is None:
        print(f"## Test Results — {label}")
        print()
        print("Malformed ctest-results.xml (no `testsuite` element).")
        return 0

    total = int(suite.get("tests", "0"))
    failures = int(suite.get("failures", "0"))
    errors = int(suite.get("errors", "0"))
    skipped = int(suite.get("skipped", "0"))
    passed = max(0, total - failures - errors - skipped)
    verdict = "PASS" if failures == 0 and errors == 0 else "FAIL"

    print(f"## Test Results — {label} — {verdict}")
    print()
    print(f"**{passed}/{total} passed** ({failures} failed, {errors} errors, {skipped} skipped)")
    print()
    print("| Result | Test | Time (s) |")
    print("|--------|------|----------|")
    for case in suite.findall("testcase"):
        name = case.get("name", "?")
        raw_time = case.get("time", "")
        try:
            display_time = f"{float(raw_time):.2f}"
        except ValueError:
            display_time = raw_time
        if case.find("failure") is not None:
            result = "FAIL"
        elif case.find("error") is not None:
            result = "ERROR"
        elif case.find("skipped") is not None:
            result = "SKIP"
        else:
            result = "PASS"
        print(f"| {result} | `{name}` | {display_time} |")

    return 0


if __name__ == "__main__":
    sys.exit(main())
