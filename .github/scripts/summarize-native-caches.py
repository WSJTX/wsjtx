#!/usr/bin/env python3
"""Report cache restores and build outcomes without claiming cache readiness."""

import argparse
import json
import os


LABELS = {
    "qt": "Qt", "libusb": "libusb", "portaudio": "PortAudio", "fftw": "FFTW",
    "boost": "Boost", "hamlib": "Hamlib",
}


def restore_status(step, cold):
    if cold:
        return "bypassed (cold build)"
    outcome = step.get("outcome", "skipped")
    if outcome != "success":
        return "not run" if outcome == "skipped" else outcome
    outputs = step.get("outputs", {})
    if outputs.get("cache-hit") == "true":
        return "exact hit"
    if outputs.get("cache-matched-key"):
        return "fallback restore"
    return "miss"


def build_status(step):
    outcome = step.get("outcome", "skipped")
    return {"success": "succeeded", "skipped": "not run"}.get(outcome, outcome)


def render(steps, dependencies, cold=False, recache=False, compiler=False):
    lines = ["### Native caches", "", "| Cache | Restore | Build |",
             "| --- | --- | --- |"]
    for name in dependencies:
        restored = restore_status(steps.get(f"{name}-cache", {}), cold)
        built = build_status(steps.get(f"{name}-build", {}))
        lines.append(f"| {LABELS[name]} | {restored} | {built} |")
    if compiler:
        restored = restore_status(steps.get("ccache", {}), cold)
        built = build_status(steps.get("compile", {}))
        if recache:
            built += "; forced recompilation requested"
        lines.append(f"| Compiler objects | {restored} | {built} |")
    lines.extend(["", "Build outcomes describe compilation only; verification and job results are reported separately."])
    if cold:
        lines.extend(["", "Workflow cache restores and saves are disabled. Runner-installed software remains available."])
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dependencies", nargs="*", metavar="dependency")
    parser.add_argument("--compiler", action="store_true")
    args = parser.parse_args()
    for name in args.dependencies:
        if name not in LABELS:
            parser.error(f"unknown dependency: {name}")
    print(render(json.loads(os.environ["CACHE_STEPS"]), args.dependencies,
                 os.environ.get("COLD_BUILD") == "true",
                 os.environ.get("RECACHE") == "true", args.compiler), end="")


if __name__ == "__main__":
    main()
