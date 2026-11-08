#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import shlex
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


def verify_boost_links(build_dir, boost_prefix=None, instrumented=True):
    boost_links = set()
    verified_archives = set()
    for link_file in build_dir.rglob("link.txt"):
        boost_archives = []
        for argument in shlex.split(link_file.read_text(encoding="utf-8")):
            if not re.search(r"(?:^|[/,])(?:lib)?boost_|^-lboost_", argument):
                continue
            archive = Path(argument)
            if not archive.is_absolute() or archive.suffix != ".a" or argument.endswith(".dll.a"):
                fail(f"non-static or unresolved Boost link input in {link_file}: {argument}")
            require_file(archive)
            if boost_prefix and not archive.resolve().is_relative_to(boost_prefix.resolve()):
                fail(f"Boost link input is outside the pinned prefix: {archive}")
            if archive not in verified_archives:
                with archive.open("rb") as stream:
                    if stream.read(8) != b"!<arch>\n":
                        fail(f"Boost link input is not a static archive: {archive}")
                if boost_prefix and instrumented:
                    verify_archive(archive, "__tsan_")
                verified_archives.add(archive)
                print(f"  Boost archive SHA-256: {hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive}")
            boost_archives.append(archive)
        if boost_archives:
            boost_links.add(link_file.parent.name.removesuffix(".dir"))
    if "wsjtx" not in boost_links:
        fail("wsjtx link command has no static Boost archives")
    print(f"  static Boost links: {len(boost_links)} targets, {len(verified_archives)} archives")


def verify_no_shared_boost(build_dir):
    for path in build_dir.rglob("*"):
        if not path.is_file():
            continue
        with path.open("rb") as stream:
            header = stream.read(18)
        if len(header) < 18 or header[:4] != b"\x7fELF":
            continue
        byteorder = "little" if header[5] == 1 else "big"
        if int.from_bytes(header[16:18], byteorder) not in (2, 3):
            continue
        dynamic_section = tool_output("readelf", "-d", str(path))
        if re.search(r"\(NEEDED\).*libboost_", dynamic_section):
            fail(f"shared Boost dependency in {path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    parser.add_argument(
        "--sanitizer", choices=SANITIZERS, default="asan-ubsan"
    )
    parser.add_argument("--boost-prefix", type=Path)
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        fail(f"build directory does not exist: {build_dir}")

    sanitizer = SANITIZERS[args.sanitizer]
    if args.sanitizer == "tsan" and not args.boost_prefix:
        fail("TSan verification requires --boost-prefix")
    language_counts = verify_compile_commands(build_dir, sanitizer["flag"])
    for symbol in sanitizer["symbols"]:
        verify_archive(build_dir / "libwsjt_cxx.a", symbol)
        verify_archive(build_dir / "libwsjt_fort_omp.a", symbol)
    verify_link(build_dir, "jt9", sanitizer["flag"], sanitizer["runtimes"])
    verify_link(build_dir, "wsjtx", sanitizer["flag"], sanitizer["runtimes"])
    verify_boost_links(build_dir, args.boost_prefix, instrumented=args.sanitizer == "tsan")
    verify_no_shared_boost(build_dir)

    print("Sanitizer instrumentation verified")
    compile_summary = ", ".join(
        f"{language}={count}" for language, count in language_counts.items()
    )
    print(f"  compile commands: {compile_summary}")
    print("  static archives: libwsjt_cxx.a, libwsjt_fort_omp.a")
    print("  final executables: jt9, wsjtx")


if __name__ == "__main__":
    main()
