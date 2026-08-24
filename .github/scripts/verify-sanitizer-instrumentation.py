#!/usr/bin/env python3

import argparse
import json
import subprocess
from pathlib import Path


SANITIZERS = {
    "asan-ubsan": {
        "flag": "-fsanitize=address,undefined",
        "symbols": ("__asan_", "__ubsan_"),
        "runtimes": ("libasan.so", "libubsan.so"),
    },
    "tsan": {
        "flag": "-fsanitize=thread",
        "symbols": ("__tsan_",),
        "runtimes": ("libtsan.so",),
    },
}
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


def verify_compile_commands(build_dir, sanitizer_flag):
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
            if sanitizer_flag not in command_text(entry):
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


def verify_link(build_dir, target, sanitizer_flag, runtimes):
    link_command_path = build_dir / "CMakeFiles" / f"{target}.dir" / "link.txt"
    executable_path = build_dir / target
    require_file(link_command_path)
    require_file(executable_path)

    if sanitizer_flag not in link_command_path.read_text(encoding="utf-8"):
        fail(f"{target} final link command does not contain {sanitizer_flag}")

    dynamic_section = tool_output("readelf", "-d", str(executable_path))
    for runtime in runtimes:
        if runtime not in dynamic_section:
            fail(f"{target} does not declare a dependency on {runtime}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    parser.add_argument(
        "--sanitizer", choices=SANITIZERS, default="asan-ubsan"
    )
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        fail(f"build directory does not exist: {build_dir}")

    sanitizer = SANITIZERS[args.sanitizer]
    language_counts = verify_compile_commands(build_dir, sanitizer["flag"])
    for symbol in sanitizer["symbols"]:
        verify_archive(build_dir / "libwsjt_cxx.a", symbol)
        verify_archive(build_dir / "libwsjt_fort_omp.a", symbol)
    verify_link(build_dir, "jt9", sanitizer["flag"], sanitizer["runtimes"])
    verify_link(build_dir, "wsjtx", sanitizer["flag"], sanitizer["runtimes"])

    print("Sanitizer instrumentation verified")
    compile_summary = ", ".join(
        f"{language}={count}" for language, count in language_counts.items()
    )
    print(f"  compile commands: {compile_summary}")
    print("  static archives: libwsjt_cxx.a, libwsjt_fort_omp.a")
    print("  final executables: jt9, wsjtx")


if __name__ == "__main__":
    main()
