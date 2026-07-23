#!/usr/bin/env python3

import json
import subprocess
import sys
from pathlib import Path


SANITIZER_FLAG = "-fsanitize=address,undefined"
LANGUAGE_SUFFIXES = {
    "C": {".c"},
    "C++": {".cc", ".cpp", ".cxx"},
    "Fortran": {".f", ".f03", ".f08", ".f90", ".f95", ".for", ".ftn"},
}


def fail(message):
    raise SystemExit(f"ERROR: {message}")


def command_text(entry):
    if "command" in entry:
        return entry["command"]
    return " ".join(entry.get("arguments", []))


def require_file(path):
    if not path.is_file():
        fail(f"expected build artifact is missing: {path}")


def tool_output(*arguments):
    result = subprocess.run(
        arguments,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return result.stdout


def verify_compile_commands(build_dir):
    commands_path = build_dir / "compile_commands.json"
    require_file(commands_path)
    commands = json.loads(commands_path.read_text(encoding="utf-8"))

    language_counts = {language: 0 for language in LANGUAGE_SUFFIXES}
    uninstrumented = []
    for entry in commands:
        suffix = Path(entry.get("file", "")).suffix.lower()
        for language, suffixes in LANGUAGE_SUFFIXES.items():
            if suffix not in suffixes:
                continue
            language_counts[language] += 1
            if SANITIZER_FLAG not in command_text(entry):
                uninstrumented.append(entry.get("file", "<unknown>"))

    missing_languages = [
        language for language, count in language_counts.items() if count == 0
    ]
    if missing_languages:
        fail(
            "no compile commands were found for: " + ", ".join(missing_languages)
        )
    if uninstrumented:
        examples = ", ".join(uninstrumented[:10])
        suffix = "" if len(uninstrumented) <= 10 else ", ..."
        fail(f"compile commands without sanitizer instrumentation: {examples}{suffix}")
    return language_counts


def verify_archive(path, symbol):
    require_file(path)
    symbols = tool_output("nm", "-A", "--undefined-only", str(path))
    if symbol not in symbols:
        fail(f"{path.name} has no reference to {symbol}")


def verify_link(build_dir, target):
    link_command_path = build_dir / "CMakeFiles" / f"{target}.dir" / "link.txt"
    executable_path = build_dir / target
    require_file(link_command_path)
    require_file(executable_path)

    if SANITIZER_FLAG not in link_command_path.read_text(encoding="utf-8"):
        fail(f"{target} final link command does not contain {SANITIZER_FLAG}")

    dynamic_section = tool_output("readelf", "-d", str(executable_path))
    for runtime in ("libasan.so", "libubsan.so"):
        if runtime not in dynamic_section:
            fail(f"{target} does not declare a dependency on {runtime}")


def main():
    if len(sys.argv) != 2:
        fail(f"usage: {Path(sys.argv[0]).name} BUILD_DIR")

    build_dir = Path(sys.argv[1]).resolve()
    if not build_dir.is_dir():
        fail(f"build directory does not exist: {build_dir}")

    language_counts = verify_compile_commands(build_dir)
    verify_archive(build_dir / "libwsjt_cxx.a", "__asan_")
    verify_archive(build_dir / "libwsjt_fort_omp.a", "__asan_")
    verify_archive(build_dir / "libwsjt_cxx.a", "__ubsan_")
    verify_archive(build_dir / "libwsjt_fort_omp.a", "__ubsan_")
    verify_link(build_dir, "jt9")
    verify_link(build_dir, "wsjtx")

    print("Sanitizer instrumentation verified")
    compile_summary = ", ".join(
        f"{language}={count}" for language, count in language_counts.items()
    )
    print(f"  compile commands: {compile_summary}")
    print("  static archives: libwsjt_cxx.a, libwsjt_fort_omp.a")
    print("  final executables: jt9, wsjtx")


if __name__ == "__main__":
    main()
