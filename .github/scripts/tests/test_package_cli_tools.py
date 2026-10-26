import os
import pathlib
import subprocess
import tarfile
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = ROOT / ".github/scripts/package-cli-tools.sh"
SMOKE = ROOT / ".github/scripts/smoke-release-tarballs.sh"
BUILD_MACOS = ROOT / ".github/workflows/build-macos.yml"


OTOOL_STUB = """#!/bin/sh
case "$1" in -L) kind=refs ;; -l) kind=load ;; -D) kind=id ;; *) exit 2 ;; esac
echo "$2:"
fixture="$STUB_FIXTURES/otool.$kind.$(basename "$2")"
if [ -f "$fixture" ]; then cat "$fixture"; fi
"""

OBJDUMP_STUB = """#!/bin/sh
fixture="$STUB_FIXTURES/objdump.$(basename "$2")"
[ -f "$fixture.fail" ] && exit 1
if [ -f "$fixture" ]; then cat "$fixture"; fi
"""

READELF_STUB = """#!/bin/sh
case "$1" in
  -dW) kind=d ;;
  -VW) kind=V ;;
  -hW) printf '%s\\n' 'Class: ELF32' 'Machine: ARM' 'Flags: Version5 EABI, hard-float ABI'; exit 0 ;;
  -lW) echo '/lib/ld-linux-armhf.so.3'; exit 0 ;;
  *) exit 2 ;;
esac
shift
[ "$1" != -- ] || shift
fixture="$STUB_FIXTURES/readelf.$kind.$(basename "$1")"
if [ -f "$fixture" ]; then cat "$fixture"; fi
"""

# Copies each --executable into the AppDir, and into usr/lib the libraries
# $STUB_FIXTURES/deploys.<program> names, as linuxdeploy deploys them.
LINUXDEPLOY_STUB = """#!/bin/sh
printf '%s\\n' "$*" "token=${GITHUB_TOKEN-unset}" "extract=${APPIMAGE_EXTRACT_AND_RUN-unset}" >> "$STUB_FIXTURES/linuxdeploy.log"
[ -f "$STUB_FIXTURES/linuxdeploy.fail" ] && exit 1
appdir=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --appdir) appdir="$2"; shift 2 ;;
    --executable)
      mkdir -p "$appdir/usr/bin" "$appdir/usr/lib"
      [ -f "$STUB_FIXTURES/linuxdeploy.skip-programs" ] || cp "$2" "$appdir/usr/bin/"
      while read -r library; do cp "$STUB_FIXTURES/appdir-lib/$library" "$appdir/usr/lib/"; done < "$STUB_FIXTURES/deploys.$(basename "$2")"
      shift 2 ;;
    *) shift ;;
  esac
done
mkdir -p "$appdir/usr/share/doc/libqt5core5t64"
echo copyright > "$appdir/usr/share/doc/libqt5core5t64/copyright"
"""


def needed(*libraries, runpath=None):
    lines = [f" 0x0000000000000001 (NEEDED)             Shared library: [{library}]" for library in libraries]
    if runpath is not None:
        lines.append(f" 0x000000000000001d (RUNPATH)            Library runpath: [{runpath}]")
    return lines


def versions(*names):
    return [
        "Version symbols section '.gnu.version' contains 9 entries:",
        "  000:   0 (*local*)       2 (GLIBC_9.99)",
        "Version needs section '.gnu.version_r' contains 1 entry:",
        "  000000: Version: 1  File: libc.so.6  Cnt: 1",
        *(f"  0x0010:   Name: {name}  Flags: none  Version: 2" for name in names),
    ]


def rpath(path):
    return ["Load command 12", "          cmd LC_RPATH", "      cmdsize 32", f"         path {path} (offset 12)"]


def minos(version):
    return ["Load command 9", "      cmd LC_BUILD_VERSION", "  cmdsize 32", " platform 1", f"    minos {version}"]


def version_min_macosx(version):
    return ["Load command 9", "      cmd LC_VERSION_MIN_MACOSX", "  cmdsize 16", f"  version {version}", "      sdk 15.0"]


MINOS = minos("11.0")


class PackageCliToolsTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.bin = self.root / "bin"
        self.fixtures = self.root / "fixtures"
        self.source = self.root / "build"
        self.out = self.root / "out"
        for directory in (self.bin, self.fixtures, self.source):
            directory.mkdir()
        for name, body in (
            ("otool", OTOOL_STUB), ("objdump", OBJDUMP_STUB), ("readelf", READELF_STUB),
            ("linuxdeploy", LINUXDEPLOY_STUB),
        ):
            (self.bin / name).write_text(body, encoding="utf-8")
            (self.bin / name).chmod(0o755)

    def fixture(self, name, lines):
        (self.fixtures / name).write_text("".join(f"{line}\n" for line in lines), encoding="utf-8")

    def files(self, directory, names):
        directory.mkdir(parents=True, exist_ok=True)
        for name in names:
            (directory / name).write_text(name, encoding="utf-8")

    def package(self, platform, groups, *arguments, newline="\n"):
        groups_file = self.root / "release-tarballs.txt"
        groups_file.write_bytes(("# comment" + newline + newline.join(groups) + newline).encode())
        return subprocess.run(
            [
                "bash", str(SCRIPT), "--platform", platform, "--version", "9.9.9", "--arch", "x86_64",
                "--groups", str(groups_file), "--source", str(self.source), "--out", str(self.out), *arguments,
            ],
            env=dict(
                os.environ, PATH=f"{self.bin}:{os.environ['PATH']}", TMPDIR=str(self.root),
                STUB_FIXTURES=str(self.fixtures), GITHUB_TOKEN="secret",
            ),
            text=True,
            capture_output=True,
            check=False,
        )

    def members(self, name):
        with tarfile.open(self.out / f"{name}.tar.gz") as archive:
            tops = {member.name.split("/", 1)[0] for member in archive.getmembers()}
            self.assertEqual(tops, {name})
            return sorted(member.name.split("/", 1)[1] for member in archive.getmembers() if member.isfile())

    def text(self, name, member):
        with tarfile.open(self.out / f"{name}.tar.gz") as archive:
            return archive.extractfile(f"{name}/{member}").read().decode()

    def assert_failed(self, result, *messages):
        self.assertEqual(result.returncode, 1, result.stderr)
        for message in messages:
            self.assertIn(message, result.stderr)
        self.assertFalse(self.out.exists() and any(self.out.iterdir()))

    # macOS: the CLI assets directory as deploy-macos-cli-tools.sh leaves it.
    # Fixture names differ by more than case: macOS file systems ignore it.
    def macos_assets(self):
        self.files(self.source, ["jt9", "wsjt-codec", "wsprd", "encode77"])
        self.files(self.source / "lib", ["QtCore", "libfftw3f.3.dylib", "libfftw3f_threads.3.dylib", "libmystery.dylib"])
        for program in ("jt9", "wsjt-codec", "encode77"):
            self.fixture(f"otool.load.{program}", rpath("@loader_path/lib") + MINOS)
        self.fixture("otool.load.wsprd", rpath("@loader_path") + rpath("@loader_path/lib") + MINOS)
        for library in ("QtCore", "libfftw3f.3.dylib", "libfftw3f_threads.3.dylib"):
            self.fixture(f"otool.load.{library}", MINOS)
            self.fixture(f"otool.id.{library}", [f"@rpath/{library}"])
        self.fixture("otool.id.libfftw3f.3.dylib", ["/Users/runner/work/fftw-prefix/lib/libfftw3f.3.dylib"])
        self.fixture("otool.refs.jt9", [
            "\t@rpath/libfftw3f_threads.3.dylib (compatibility version 9.0.0, current version 9.10.0)",
            "\t@rpath/QtCore (compatibility version 5.15.0, current version 5.15.18)",
            "\t/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 1900.180.0)",
        ])
        self.fixture("otool.refs.wsjt-codec", ["\t@rpath/libfftw3f_threads.3.dylib (compatibility version 9.0.0)"])
        self.fixture("otool.refs.wsprd", ["\t@rpath/libfftw3f.3.dylib (compatibility version 9.0.0)"])
        self.fixture("otool.refs.encode77", ["\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)"])
        self.fixture("otool.refs.QtCore", [
            "\t@rpath/QtCore (compatibility version 5.15.0)",
            "\t/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit (compatibility version 1.0.0)",
        ])
        self.fixture("otool.refs.libfftw3f_threads.3.dylib", [
            "\t@rpath/libfftw3f_threads.3.dylib (compatibility version 9.0.0)",
            "\t@rpath/libfftw3f.3.dylib (compatibility version 9.0.0)",
        ])
        self.fixture("otool.refs.libfftw3f.3.dylib", ["\t/Users/runner/work/fftw-prefix/lib/libfftw3f.3.dylib (compatibility version 9.0.0)"])

    MACOS_GROUPS = ["jt9 jt9", "codec wsjt-codec", "wsprd wsprd", "utilities encode77"]

    def test_macos_tarballs_hold_each_group_and_the_dylibs_it_reaches(self):
        self.macos_assets()
        result = self.package("macos", self.MACOS_GROUPS)
        self.assertEqual(result.returncode, 0, result.stderr)
        documents = ["COPYING", "README.txt", "THIRD-PARTY.txt"]
        self.assertEqual(
            self.members("wsjtx-9.9.9-x86_64-macOS-jt9"),
            sorted([*documents, "jt9", "lib/QtCore", "lib/libfftw3f.3.dylib", "lib/libfftw3f_threads.3.dylib"]),
        )
        self.assertEqual(
            self.members("wsjtx-9.9.9-x86_64-macOS-codec"),
            sorted([*documents, "wsjt-codec", "lib/libfftw3f.3.dylib", "lib/libfftw3f_threads.3.dylib"]),
        )
        self.assertEqual(
            self.members("wsjtx-9.9.9-x86_64-macOS-wsprd"), sorted([*documents, "wsprd", "lib/libfftw3f.3.dylib"])
        )
        self.assertEqual(self.members("wsjtx-9.9.9-x86_64-macOS-utilities"), sorted([*documents, "encode77"]))
        readme = self.text("wsjtx-9.9.9-x86_64-macOS-jt9", "README.txt")
        self.assertIn("They require macOS 11.0 or later.", readme)
        self.assertIn("xattr -d com.apple.quarantine", readme)
        self.assertIn("wsjtx-9.9.9-src.tar.gz", readme)
        self.assertIn("GNU General Public License version 3 (COPYING)", readme)
        self.assertIn("are Copyright (C) 2001-", readme)
        self.assertIn("Joseph Taylor, K1JT;", readme)
        self.assertTrue(readme.rstrip().endswith("other members of the WSJT Development Group."), readme)
        self.assertNotIn("_{prog}_", readme)
        self.assertIn("wsjt-codec, in its own tarball, is the stream codec", readme)
        self.assertIn("wsjt-codec is the stream codec, without jt9's shared-memory worker: it decodes the period modes "
                      "and JTTY, and encodes the slot modes, Echo, Tune and JTTY,", self.text("wsjtx-9.9.9-x86_64-macOS-codec", "README.txt"))
        self.assertNotIn("mingw-w64", self.text("wsjtx-9.9.9-x86_64-macOS-jt9", "THIRD-PARTY.txt"))
        notices = self.text("wsjtx-9.9.9-x86_64-macOS-jt9", "THIRD-PARTY.txt")
        self.assertIn("Files: QtCore", notices)
        self.assertIn("Files: libfftw3f.3.dylib libfftw3f_threads.3.dylib", notices)
        utilities = self.text("wsjtx-9.9.9-x86_64-macOS-utilities", "THIRD-PARTY.txt")
        self.assertIn("GCC runtime libraries\n  License: GPL-3.0-or-later WITH GCC-exception-3.1\n", utilities)
        self.assertIn("  Linked statically into the programs\n", utilities)
        self.assertNotIn("Files:", utilities)
        self.assertEqual(
            self.text("wsjtx-9.9.9-x86_64-macOS-wsprd", "COPYING"), (ROOT / "COPYING").read_text(encoding="utf-8")
        )

    def test_macos_unresolved_rpath_reference_fails_naming_it(self):
        self.macos_assets()
        with (self.fixtures / "otool.refs.wsprd").open("a", encoding="utf-8") as stream:
            stream.write("\t@rpath/libmissing.dylib (compatibility version 1.0.0)\n")
        self.assert_failed(self.package("macos", self.MACOS_GROUPS), "@rpath/libmissing.dylib(wsprd)")

    def test_macos_build_tree_reference_fails(self):
        self.macos_assets()
        self.fixture("otool.refs.encode77", ["\t/opt/homebrew/lib/libgfortran.5.dylib (compatibility version 6.0.0)"])
        self.assert_failed(self.package("macos", self.MACOS_GROUPS), "/opt/homebrew/lib/libgfortran.5.dylib(encode77)")

    def test_macos_absolute_rpath_fails(self):
        self.macos_assets()
        self.fixture("otool.load.libfftw3f.3.dylib", rpath("/Users/runner/work/fftw-prefix/lib") + MINOS)
        self.assert_failed(
            self.package("macos", self.MACOS_GROUPS),
            "lib/libfftw3f.3.dylib has an absolute LC_RPATH: /Users/runner/work/fftw-prefix/lib",
        )

    def test_macos_program_with_absolute_rpath_fails(self):
        self.macos_assets()
        self.fixture("otool.load.wsprd", rpath("@loader_path/lib") + rpath("/opt/homebrew/lib") + MINOS)
        self.assert_failed(self.package("macos", self.MACOS_GROUPS), "wsprd has an absolute LC_RPATH: /opt/homebrew/lib")

    def test_macos_library_missing_from_notices_table_fails(self):
        self.macos_assets()
        self.fixture("otool.refs.encode77", ["\t@rpath/libmystery.dylib (compatibility version 1.0.0)"])
        self.assert_failed(
            self.package("macos", ["utilities encode77", "jt9 jt9"]),
            "bundled libraries missing from third-party-libraries.tsv: libmystery.dylib",
        )

    def test_macos_dylib_outside_lib_fails(self):
        self.macos_assets()
        self.files(self.source, ["libfftw3f_top.dylib"])
        self.fixture("otool.refs.wsprd", ["\t@rpath/libfftw3f_top.dylib (compatibility version 1.0.0)"])
        self.assert_failed(self.package("macos", self.MACOS_GROUPS), "@rpath/libfftw3f_top.dylib(wsprd)")

    def test_macos_rpath_references_resolve_through_their_own_program(self):
        self.macos_assets()
        self.files(self.source, ["wsprcode"])
        self.files(self.source / "lib/extra", ["libfftw3f_extra.dylib"])
        self.fixture("otool.load.wsprcode", rpath("@executable_path/lib/extra") + rpath("@loader_path/lib") + MINOS)
        self.fixture("otool.load.libfftw3f_extra.dylib", MINOS)
        self.fixture("otool.refs.wsprcode", ["\t@rpath/libfftw3f_extra.dylib (compatibility version 1.0.0)"])
        result = self.package("macos", ["utilities wsprcode"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("lib/extra/libfftw3f_extra.dylib", self.members("wsjtx-9.9.9-x86_64-macOS-utilities"))
        self.out.joinpath("wsjtx-9.9.9-x86_64-macOS-utilities.tar.gz").unlink()
        self.fixture("otool.refs.encode77", ["\t@rpath/libfftw3f_extra.dylib (compatibility version 1.0.0)"])
        self.assert_failed(self.package("macos", ["utilities wsprcode encode77"]), "@rpath/libfftw3f_extra.dylib(encode77)")

    def test_macos_loader_path_references_resolve_beside_the_dylib(self):
        self.macos_assets()
        self.fixture("otool.refs.libfftw3f_threads.3.dylib", ["\t@loader_path/libfftw3f.3.dylib (compatibility version 9.0.0)"])
        result = self.package("macos", self.MACOS_GROUPS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("lib/libfftw3f.3.dylib", self.members("wsjtx-9.9.9-x86_64-macOS-codec"))

    def test_macos_reference_escaping_the_source_fails(self):
        self.macos_assets()
        self.files(self.root, ["outside.dylib"])
        self.fixture("otool.refs.libfftw3f_threads.3.dylib", ["\t@loader_path/../../outside.dylib (compatibility version 1.0.0)"])
        self.assert_failed(self.package("macos", self.MACOS_GROUPS), "@loader_path/../../outside.dylib(lib/libfftw3f_threads.3.dylib)")

    def test_macos_program_rpath_outside_its_directory_fails(self):
        self.macos_assets()
        self.fixture("otool.load.jt9", rpath("@loader_path/../lib") + MINOS)
        self.assert_failed(self.package("macos", self.MACOS_GROUPS), "jt9 has an LC_RPATH outside its directory: @loader_path/../lib")

    def test_macos_floor_is_the_highest_minos(self):
        self.macos_assets()
        self.fixture("otool.load.QtCore", minos("13.0"))
        self.fixture("otool.load.libfftw3f.3.dylib", minos("9.5"))
        result = self.package("macos", self.MACOS_GROUPS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("They require macOS 13.0 or later.", self.text("wsjtx-9.9.9-x86_64-macOS-jt9", "README.txt"))
        self.assertIn("They require macOS 11.0 or later.", self.text("wsjtx-9.9.9-x86_64-macOS-wsprd", "README.txt"))

    def test_macos_legacy_minimum_version_is_recorded_for_every_tarball(self):
        self.macos_assets()
        source_version = ["Load command 10", "      cmd LC_SOURCE_VERSION", "  cmdsize 16", "  version 99.0"]
        for program in ("jt9", "wsjt-codec", "wsprd", "encode77"):
            self.fixture(
                f"otool.load.{program}",
                source_version + rpath("@loader_path/lib") + version_min_macosx("10.13") + source_version,
            )
        for library in ("QtCore", "libfftw3f.3.dylib", "libfftw3f_threads.3.dylib"):
            self.fixture(f"otool.load.{library}", version_min_macosx("10.13") + source_version)
        result = self.package("macos", self.MACOS_GROUPS)
        self.assertEqual(result.returncode, 0, result.stderr)
        for group in ("jt9", "codec", "wsprd", "utilities"):
            self.assertIn(
                "They require macOS 10.13 or later.",
                self.text(f"wsjtx-9.9.9-x86_64-macOS-{group}", "README.txt"),
            )

    def test_macos_floor_is_the_highest_across_both_version_commands(self):
        self.macos_assets()
        for modern, expected in (("10.12", "10.13"), ("13.0", "13.0")):
            with self.subTest(modern=modern):
                self.fixture("otool.load.jt9", rpath("@loader_path/lib") + minos(modern))
                self.fixture("otool.load.QtCore", version_min_macosx("10.13"))
                for library in ("libfftw3f.3.dylib", "libfftw3f_threads.3.dylib"):
                    self.fixture(f"otool.load.{library}", minos(modern))
                result = self.package("macos", ["jt9 jt9"])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(
                    f"They require macOS {expected} or later.",
                    self.text("wsjtx-9.9.9-x86_64-macOS-jt9", "README.txt"),
                )

    def test_macos_minimum_does_not_read_fields_from_other_load_commands(self):
        self.macos_assets()
        for command in ("LC_BUILD_VERSION", "LC_VERSION_MIN_MACOSX"):
            with self.subTest(command=command):
                self.fixture("otool.load.encode77", [
                    "Load command 9", f"      cmd {command}", "  cmdsize 16",
                    "Load command 10", "      cmd LC_SOURCE_VERSION", "  cmdsize 16", "  version 99.0",
                ])
                self.assert_failed(
                    self.package("macos", ["utilities encode77"]),
                    "no minimum macOS version recorded in wsjtx-9.9.9-x86_64-macOS-utilities",
                )

    def test_macos_without_a_minimum_version_fails(self):
        self.macos_assets()
        self.fixture("otool.load.encode77", rpath("@loader_path/lib"))
        self.assert_failed(self.package("macos", self.MACOS_GROUPS), "no minimum macOS version recorded in wsjtx-9.9.9-x86_64-macOS-utilities")

    def test_tarball_named_twice_fails(self):
        self.macos_assets()
        self.assert_failed(self.package("macos", ["wsprd wsprd", "jt9 jt9", "wsprd wsjt-codec"]), "release tarball wsprd is named twice")

    def test_tarball_without_readme_description_fails(self):
        self.macos_assets()
        self.assert_failed(self.package("macos", ["extra encode77"]), "no README description for release tarball extra")

    def test_missing_program_fails_naming_it(self):
        self.macos_assets()
        (self.source / "wsjt-codec").unlink()
        self.assert_failed(self.package("macos", self.MACOS_GROUPS), "missing under", "wsjt-codec")

    def test_group_without_programs_fails(self):
        self.macos_assets()
        self.assert_failed(self.package("macos", ["jt9 jt9", "utilities"]), "release tarball utilities names no programs")

    # Windows
    def windows_build(self, failing=()):
        self.files(self.source, ["jt9.exe", "encode77.exe", "wsjtx.exe"])
        self.files(self.root / "dlls", ["libfftw3f-3.dll", "LIBGFORTRAN-5.DLL", "unused.dll"])
        self.files(self.root / "system", ["KERNEL32.dll", "msvcrt.dll"])
        self.fixture("objdump.jt9.exe", ["\tDLL Name: libfftw3f-3.dll", "\tDLL Name: KERNEL32.dll"])
        self.fixture("objdump.libfftw3f-3.dll", ["\tDLL Name: libgfortran-5.dll"])
        self.fixture("objdump.LIBGFORTRAN-5.DLL", ["\tDLL Name: KERNEL32.dll"])
        self.fixture("objdump.encode77.exe", ["\tDLL Name: msvcrt.dll"])
        for name in failing:
            self.fixture(f"objdump.{name}.fail", [])
        return self.package(
            "windows", ["jt9 jt9", "utilities encode77"],
            "--dll-dir", str(self.root / "dlls"), "--system-dir", str(self.root / "system"), newline="\r\n",
        )

    def test_windows_tarballs_hold_each_group_and_its_dll_closure(self):
        result = self.windows_build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.members("wsjtx-9.9.9-windows-x86_64-jt9"),
            ["COPYING", "LIBGFORTRAN-5.DLL", "README.txt", "THIRD-PARTY.txt", "jt9.exe", "libfftw3f-3.dll"],
        )
        self.assertEqual(
            self.members("wsjtx-9.9.9-windows-x86_64-utilities"), ["COPYING", "README.txt", "THIRD-PARTY.txt", "encode77.exe"]
        )
        self.assertIn("no Authenticode signature", self.text("wsjtx-9.9.9-windows-x86_64-jt9", "README.txt"))
        notices = self.text("wsjtx-9.9.9-windows-x86_64-jt9", "THIRD-PARTY.txt")
        self.assertIn("FFTW 3\n  License: GPL-2.0-or-later\n", notices)
        self.assertIn("GCC runtime libraries\n  License: GPL-3.0-or-later WITH GCC-exception-3.1\n", notices)
        self.assertIn("  Files: LIBGFORTRAN-5.DLL\n", notices)
        self.assertIn("mingw-w64 runtime\n", notices)
        self.assertEqual(notices.count("  Linked statically into the programs"), 1)
        self.assertNotIn("libquadmath", notices)

    def test_windows_ucrt_api_set_imports_are_part_of_windows(self):
        (self.root / "system").mkdir()
        self.fixture("objdump.encode77.exe", ["\tDLL Name: api-ms-win-crt-runtime-l1-1-0.dll", "\tDLL Name: API-MS-WIN-CRT-HEAP-L1-1-0.dll"])
        self.files(self.source, ["jt9.exe", "encode77.exe"])
        self.files(self.root / "dlls", [])
        self.fixture("objdump.jt9.exe", [])
        result = self.package(
            "windows", ["jt9 jt9", "utilities encode77"],
            "--dll-dir", str(self.root / "dlls"), "--system-dir", str(self.root / "system"),
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.members("wsjtx-9.9.9-windows-x86_64-utilities"), ["COPYING", "README.txt", "THIRD-PARTY.txt", "encode77.exe"]
        )
        self.assertIn("Universal C Runtime, part of Windows 10 and later", self.text("wsjtx-9.9.9-windows-x86_64-utilities", "README.txt"))

    def test_windows_other_api_set_imports_need_a_file(self):
        (self.root / "system").mkdir()
        self.fixture("objdump.encode77.exe", ["\tDLL Name: api-ms-win-core-synch-l1-2-0.dll", "\tDLL Name: ext-ms-win-ntuser-window-l1-1-0.dll"])
        self.files(self.source, ["jt9.exe", "encode77.exe"])
        self.files(self.root / "dlls", [])
        self.fixture("objdump.jt9.exe", [])
        result = self.package(
            "windows", ["jt9 jt9", "utilities encode77"],
            "--dll-dir", str(self.root / "dlls"), "--system-dir", str(self.root / "system"),
        )
        self.assert_failed(result, "api-ms-win-core-synch-l1-2-0.dll(encode77.exe)", "ext-ms-win-ntuser-window-l1-1-0.dll(encode77.exe)")

    def test_windows_import_outside_dll_and_system_dirs_fails(self):
        (self.root / "system").mkdir()
        self.fixture("objdump.encode77.exe", ["\tDLL Name: libmissing.dll"])
        self.files(self.source, ["jt9.exe", "encode77.exe"])
        self.files(self.root / "dlls", [])
        self.fixture("objdump.jt9.exe", [])
        result = self.package(
            "windows", ["jt9 jt9", "utilities encode77"],
            "--dll-dir", str(self.root / "dlls"), "--system-dir", str(self.root / "system"),
        )
        self.assert_failed(result, "libmissing.dll(encode77.exe)")

    def test_windows_objdump_failure_fails_naming_the_file(self):
        self.assert_failed(self.windows_build(failing=["libfftw3f-3.dll"]), "objdump failed on libfftw3f-3.dll")

    # Linux: linuxdeploy and readelf are stubbed; the excludelist is the committed one.
    def linux_build(self):
        self.files(self.source, ["jt9", "wsjt-codec", "wsjtx"])
        self.files(self.fixtures / "appdir-lib", ["libQt5Core.so.5", "libicuuc.so.74", "libgfortran.so.5"])
        self.fixture("deploys.jt9", ["libQt5Core.so.5", "libicuuc.so.74", "libgfortran.so.5"])
        self.fixture("deploys.wsjt-codec", ["libgfortran.so.5"])
        self.fixture("readelf.d.jt9", needed("libQt5Core.so.5", "libgfortran.so.5", "libc.so.6", runpath="$ORIGIN/../lib"))
        self.fixture("readelf.V.jt9", versions("GLIBC_2.34", "GLIBC_2.2.5"))
        self.fixture("readelf.d.wsjt-codec", needed("libgfortran.so.5", "libm.so.6", runpath="${ORIGIN}/../lib"))
        self.fixture("readelf.V.wsjt-codec", versions("GLIBC_2.29"))
        self.fixture("readelf.d.libQt5Core.so.5", needed("libicuuc.so.74", "libstdc++.so.6", runpath="$ORIGIN"))
        self.fixture("readelf.V.libQt5Core.so.5", versions("GLIBC_2.38", "GLIBCXX_3.4.9", "GLIBCXX_3.4.30", "CXXABI_1.3.13"))
        self.fixture("readelf.d.libicuuc.so.74", needed("libstdc++.so.6", runpath="${ORIGIN}"))
        self.fixture("readelf.d.libgfortran.so.5", needed("libc.so.6", "ld-linux-aarch64.so.1", runpath="$ORIGIN"))

    def linux_package(self, groups=("jt9 jt9", "codec wsjt-codec")):
        return self.package("linux", list(groups), "--linuxdeploy", str(self.bin / "linuxdeploy"))

    def test_linux_tarballs_bundle_deployed_libraries_and_state_host_requirements(self):
        self.linux_build()
        result = self.linux_package()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.members("wsjtx-9.9.9-linux-x86_64-jt9"),
            [
                "COPYING", "README.txt", "THIRD-PARTY.txt", "bin/jt9", "lib/libQt5Core.so.5", "lib/libgfortran.so.5",
                "lib/libicuuc.so.74", "share/doc/libqt5core5t64/copyright",
            ],
        )
        readme = self.text("wsjtx-9.9.9-linux-x86_64-jt9", "README.txt")
        self.assertIn("glibc 2.38 or later, libstdc++ with GLIBCXX_3.4.30 or later", readme)
        self.assertIn("these libraries: ld-linux-aarch64.so.1 libc.so.6 libstdc++.so.6.", readme)
        self.assertIn("ICU 74\n", self.text("wsjtx-9.9.9-linux-x86_64-jt9", "THIRD-PARTY.txt"))
        self.assertIn("without it", readme)
        self.assertIn("glibc 2.29 or later, and these libraries", self.text("wsjtx-9.9.9-linux-x86_64-codec", "README.txt"))
        self.assertIn("share/doc/", self.text("wsjtx-9.9.9-linux-x86_64-jt9", "THIRD-PARTY.txt"))
        log = (self.fixtures / "linuxdeploy.log").read_text(encoding="utf-8").splitlines()
        self.assertTrue(log[0].startswith("--appimage-extract-and-run --appdir "), log[0])
        self.assertTrue(log[0].endswith(f"--executable {self.source}/jt9"), log[0])
        self.assertEqual(log[1:3], ["token=unset", "extract=1"])

    def test_armhf_distro_tarballs_have_distinct_names_and_top_directories(self):
        self.linux_build()
        (self.bin / "file").write_text(
            "#!/bin/sh\necho 'ELF 32-bit LSB shared object, ARM, EABI5 version 1'\n",
            encoding="utf-8",
        )
        (self.bin / "file").chmod(0o755)
        (self.bin / "dpkg").write_text(
            "#!/bin/sh\npython3 -c 'import sys; sys.exit(not (tuple(map(int, sys.argv[1].split(\".\"))) > tuple(map(int, sys.argv[2].split(\".\")))))' \"$2\" \"$4\"\n",
            encoding="utf-8",
        )
        (self.bin / "dpkg").chmod(0o755)
        for distro in ("bookworm", "trixie"):
            with self.subTest(distro=distro):
                result = self.package(
                    "linux", ["codec wsjt-codec"], "--arch", "armhf",
                    "--distro", distro, "--linuxdeploy", str(self.bin / "linuxdeploy"),
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                name = f"wsjtx-9.9.9-linux-armhf-{distro}-codec"
                self.assertIn("bin/wsjt-codec", self.members(name))
                self.assertIn(f"for linux-armhf-{distro}", self.text(name, "README.txt"))
        self.assertEqual(len(list(self.out.glob("*.tar.gz"))), 2)

    def test_distro_is_restricted_to_supported_armhf_linux_targets(self):
        for platform, arch, distro in (
            ("linux", "x86_64", "bookworm"),
            ("macos", "armhf", "bookworm"),
            ("linux", "armhf", "sid"),
        ):
            with self.subTest(platform=platform, arch=arch, distro=distro):
                result = self.package(
                    platform, ["codec wsjt-codec"], "--arch", arch, "--distro", distro,
                )
                self.assertEqual(result.returncode, 2)
                self.assertIn("--distro requires linux armhf", result.stderr)

    def test_linux_library_neither_bundled_nor_excluded_fails(self):
        self.linux_build()
        self.fixture("readelf.d.wsjt-codec", needed("libfftw3f.so.3", runpath="$ORIGIN/../lib"))
        self.assert_failed(self.linux_package(), "libfftw3f.so.3(bin/wsjt-codec)")

    def test_linux_runpath_is_not_inherited_by_bundled_libraries(self):
        self.linux_build()
        self.fixture("readelf.d.libQt5Core.so.5", needed("libicuuc.so.74"))
        self.assert_failed(self.linux_package(), "libicuuc.so.74(lib/libQt5Core.so.5)")

    def test_linux_runpath_outside_origin_fails(self):
        self.linux_build()
        self.fixture("readelf.d.jt9", needed("libc.so.6", runpath="$ORIGIN/../lib:/work/wsjtx-build"))
        self.assert_failed(self.linux_package(), "bin/jt9 has a RUNPATH entry not relative to $ORIGIN: /work/wsjtx-build")

    def test_linux_empty_runpath_entry_fails(self):
        self.linux_build()
        self.fixture("readelf.d.jt9", needed("libc.so.6", runpath="$ORIGIN/../lib:"))
        self.assert_failed(self.linux_package(), "bin/jt9 has an empty RUNPATH entry: $ORIGIN/../lib:")

    def test_linux_program_linuxdeploy_did_not_deploy_fails(self):
        self.linux_build()
        self.fixture("linuxdeploy.skip-programs", [])
        self.assert_failed(self.linux_package(), "linuxdeploy did not deploy jt9")

    def test_linux_linuxdeploy_failure_fails(self):
        self.linux_build()
        self.fixture("linuxdeploy.fail", [])
        self.assert_failed(self.linux_package(), "linuxdeploy failed for wsjtx-9.9.9-linux-x86_64-jt9")

    def test_linux_requires_linuxdeploy(self):
        self.linux_build()
        result = self.package("linux", ["jt9 jt9"])
        self.assertEqual(result.returncode, 2)
        self.assertIn("--linuxdeploy is required", result.stderr)


class LinuxdeployExcludelistTests(unittest.TestCase):
    def test_excludelist_belongs_to_the_pinned_linuxdeploy(self):
        excludelist = (ROOT / ".github/scripts/linuxdeploy-excludelist.txt").read_text(encoding="utf-8")
        tag = excludelist.split("compiled into linuxdeploy ", 1)[1].split(",", 1)[0]
        self.assertIn(
            f'LINUXDEPLOY_TAG: "{tag}"', (ROOT / ".github/actions/build-linux-payload/action.yml").read_text(encoding="utf-8")
        )
        self.assertIn(f"LINUXDEPLOY_TAG={tag}\n", (ROOT / ".github/scripts/package-linux-armhf.sh").read_text(encoding="utf-8"))
        names = [line for line in excludelist.splitlines() if line and not line.startswith("#")]
        self.assertEqual(len(names), 52)
        for name in ("libc.so.6", "libstdc++.so.6", "libgcc_s.so.1", "libz.so.1", "libm.so.6"):
            self.assertIn(name, names)
        for name in ("libgfortran.so.5", "libgomp.so.1", "libQt5Core.so.5", "libfftw3f.so.3"):
            self.assertNotIn(name, names)


class SmokeReleaseTarballsTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.dir = self.root / "tarballs"
        self.bin = self.root / "bin"
        self.log = self.root / "smoke.log"
        self.dir.mkdir()
        self.bin.mkdir()
        (self.bin / "timeout").write_text(
            f'#!/bin/sh\necho "$1" >> "{self.root}/timeout.log"\nshift\nexec "$@"\n', encoding="utf-8"
        )
        (self.bin / "timeout").chmod(0o755)

    def tarball(self, group, programs, layout="bin/", failing=(), libraries=(), target="linux-x86_64", top=None):
        name = f"wsjtx-9.9.9-{target}-{group}"
        tree = self.root / "trees" / name
        for program in programs:
            path = tree / f"{layout}{program}"
            path.parent.mkdir(parents=True, exist_ok=True)
            status = 3 if program in failing else 0
            path.write_text(
                f'#!/bin/sh\necho "{program} $* lib=${{LD_LIBRARY_PATH-unset}} cwd=$(pwd)" >> "{self.log}"\nexit {status}\n',
                encoding="utf-8",
            )
            path.chmod(0o755)
        for library in libraries:
            (tree / "lib").mkdir(parents=True, exist_ok=True)
            (tree / "lib" / library).write_text(library, encoding="utf-8")
        with tarfile.open(self.dir / f"{name}.tar.gz", "w:gz") as archive:
            archive.add(tree, arcname=top or name)

    def smoke(self, groups, *arguments, platform="linux", target="linux-x86_64", script=SMOKE):
        groups_file = self.root / "release-tarballs.txt"
        groups_file.write_text("# comment\n" + "".join(f"{line}\n" for line in groups), encoding="utf-8")
        return subprocess.run(
            [
                "bash", str(script), "--platform", platform, "--version", "9.9.9", "--target", target,
                "--groups", str(groups_file), "--dir", str(self.dir), *arguments,
            ],
            env=dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}", TMPDIR=str(self.root), LD_LIBRARY_PATH="/bogus"),
            text=True, capture_output=True, check=False,
        )

    def test_every_program_runs_from_its_own_tarball_without_library_path(self):
        self.tarball("jt9", ["jt9"])
        self.tarball("wsprd", ["wsprd"])
        self.tarball("utilities", ["encode77", "ft4sim"])
        result = self.smoke(["jt9 jt9", "wsprd wsprd", "utilities encode77 ft4sim"])
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = self.log.read_text(encoding="utf-8").splitlines()
        self.assertEqual([line.split(" ", 1)[0] for line in lines], ["jt9", "wsprd", "encode77", "ft4sim"])
        self.assertTrue(lines[0].startswith("jt9 --help lib=unset"), lines[0])
        self.assertRegex(lines[1], r"^wsprd -a \S+/run-wsprd \S+/samples/WSPR/150426_0918\.wav lib=unset cwd=\S+/run-wsprd$")
        self.assertTrue(lines[2].startswith("encode77 CQ K1ABC FN42 lib=unset"), lines[2])
        self.assertTrue(lines[3].startswith("ft4sim  lib=unset"), lines[3])
        self.assertEqual((self.root / "timeout.log").read_text(encoding="utf-8").split(), ["120"] * 4)

    def test_every_committed_group_has_a_smoke_invocation(self):
        script = self.scripts_copy()
        lines = [
            line.strip()
            for line in (ROOT / "CMake/release-tarballs.txt").read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        for line in lines:
            group, *programs = line.split()
            self.tarball(group, programs)
        result = self.smoke(lines, script=script)
        self.assertEqual(result.returncode, 0, result.stderr)
        ran = [entry.split(" ", 1)[0] for entry in self.log.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(ran, [program for line in lines for program in line.split()[1:]])

    def scripts_copy(self):
        scripts = self.root / "repo/.github/scripts"
        scripts.mkdir(parents=True)
        (scripts / SMOKE.name).write_bytes(SMOKE.read_bytes())
        (scripts / "smoke-launch-windows.sh").write_text(
            f'#!/bin/sh\necho "launch $(basename "$1") cwd=$(pwd) args=$*" >> "{self.log}"\n'
            'case "$1" in *ft4sim.exe) exit 1 ;; esac\n',
            encoding="utf-8",
        )
        (scripts / "smoke-launch-windows.sh").chmod(0o755)
        sample = self.root / "repo/samples/WSPR/150426_0918.wav"
        sample.parent.mkdir(parents=True)
        sample.write_bytes(b"RIFF")
        return scripts / SMOKE.name

    def test_windows_programs_launch_through_the_clean_path_launcher(self):
        script = self.scripts_copy()
        self.tarball("jt9", ["jt9.exe"], layout="", target="windows-x86_64")
        self.tarball("utilities", ["encode77.exe"], layout="", target="windows-x86_64")
        result = self.smoke(["jt9 jt9", "utilities encode77"], platform="windows", target="windows-x86_64", script=script)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = self.log.read_text(encoding="utf-8").splitlines()
        self.assertRegex(lines[0], r"^launch jt9\.exe cwd=\S+/run-jt9 args=\S+/wsjtx-9\.9\.9-windows-x86_64-jt9/jt9\.exe --help$")
        self.assertRegex(lines[1], r"^launch encode77\.exe cwd=\S+/run-encode77 args=\S+ CQ K1ABC FN42$")

    def test_windows_launch_failure_fails(self):
        script = self.scripts_copy()
        self.tarball("utilities", ["ft4sim.exe"], layout="", target="windows-x86_64")
        result = self.smoke(["utilities ft4sim"], platform="windows", target="windows-x86_64", script=script)
        self.assertEqual(result.returncode, 1)
        self.assertIn("ft4sim from wsjtx-9.9.9-windows-x86_64-utilities failed", result.stderr)

    def test_macos_programs_run_from_the_top_of_their_tarball(self):
        script = self.scripts_copy()
        self.tarball("wsprd", ["wsprd"], layout="", target="arm64-macOS")
        result = self.smoke(["wsprd wsprd"], platform="macos", target="arm64-macOS", script=script)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(self.log.read_text(encoding="utf-8"), r"wsprd -a \S+/run-wsprd \S+/repo/\.github/scripts/\.\./\.\./samples/WSPR/150426_0918\.wav ")
        self.assertFalse((self.root / "timeout.log").exists())

    def test_tarball_with_another_top_directory_fails(self):
        self.tarball("jt9", ["jt9"], top="jt9")
        result = self.smoke(["jt9 jt9"])
        self.assertEqual(result.returncode, 1)
        self.assertIn("wsjtx-9.9.9-linux-x86_64-jt9.tar.gz does not extract to wsjtx-9.9.9-linux-x86_64-jt9/", result.stderr)

    def test_failing_program_fails_naming_it(self):
        self.tarball("utilities", ["encode77", "ft4sim"], failing=["ft4sim"])
        result = self.smoke(["utilities encode77 ft4sim"])
        self.assertEqual(result.returncode, 1)
        self.assertIn("ft4sim from wsjtx-9.9.9-linux-x86_64-utilities failed", result.stderr)

    def test_program_missing_from_its_tarball_fails(self):
        self.tarball("jt9", ["wsjt-codec"])
        result = self.smoke(["jt9 jt9"])
        self.assertEqual(result.returncode, 1)
        self.assertIn("jt9 is not in wsjtx-9.9.9-linux-x86_64-jt9", result.stderr)

    def test_program_without_a_smoke_invocation_fails(self):
        self.tarball("extra", ["newtool"])
        result = self.smoke(["extra newtool"])
        self.assertEqual(result.returncode, 1)
        self.assertIn("no smoke invocation for newtool", result.stderr)

    def ldd_stub(self, resolution, status=0):
        (self.bin / "ldd").write_text(
            "#!/bin/sh\n"
            f'[ "${{LD_LIBRARY_PATH-unset}}" = unset ] || exit 9\n'
            'tree=$(dirname "$(dirname "$1")")\n'
            "printf '\\tlinux-vdso.so.1 (0x1)\\n'\n"
            f"printf '\\tlibQt5Core.so.5 => %s (0x2)\\n' \"{resolution}\"\n"
            "printf '\\tlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x3)\\n'\n"
            f"exit {status}\n",
            encoding="utf-8",
        )
        (self.bin / "ldd").chmod(0o755)

    def test_ldd_reports_a_library_not_found(self):
        self.tarball("jt9", ["jt9"], libraries=["libQt5Core.so.5"])
        self.ldd_stub("not found")
        result = self.smoke(["jt9 jt9"], "--ldd")
        self.assertEqual(result.returncode, 1)
        self.assertIn("libQt5Core.so.5(bin/jt9): not found", result.stderr)

    def test_ldd_failure_fails(self):
        self.tarball("jt9", ["jt9"], libraries=["libQt5Core.so.5"])
        self.ldd_stub("$tree/lib/libQt5Core.so.5", status=1)
        result = self.smoke(["jt9 jt9"], "--ldd")
        self.assertEqual(result.returncode, 1)
        self.assertIn("ldd failed on bin/jt9", result.stderr)

    def test_ldd_accepts_a_library_without_dependencies(self):
        # ICU's data library needs nothing; ldd says so and exits 0.
        self.tarball("jt9", ["jt9"], libraries=["libicudata.so.74"])
        (self.bin / "ldd").write_text("#!/bin/sh\necho '\tstatically linked'\n", encoding="utf-8")
        (self.bin / "ldd").chmod(0o755)
        result = self.smoke(["jt9 jt9"], "--ldd")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ldd_accepts_bundled_copies_through_origin(self):
        self.tarball("jt9", ["jt9"], libraries=["libQt5Core.so.5"])
        self.ldd_stub("$tree/bin/../lib/libQt5Core.so.5")
        result = self.smoke(["jt9 jt9"], "--ldd")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ldd_rejects_a_system_copy_of_a_bundled_library(self):
        self.tarball("jt9", ["jt9"], libraries=["libQt5Core.so.5"])
        self.ldd_stub("/usr/lib/x86_64-linux-gnu/libQt5Core.so.5")
        result = self.smoke(["jt9 jt9"], "--ldd")
        self.assertEqual(result.returncode, 1)
        self.assertIn("libQt5Core.so.5(bin/jt9): /usr/lib/x86_64-linux-gnu/libQt5Core.so.5", result.stderr)


class MacosPostinstallTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name) / "volume"
        self.scripts = pathlib.Path(temporary.name) / "scripts"
        self.scripts.mkdir()
        lines = BUILD_MACOS.read_text(encoding="utf-8").splitlines()
        start = next(i for i, line in enumerate(lines) if line.strip() == 'cat > "${SCRIPTS}/postinstall" << \'SCRIPT\'')
        indent = len(lines[start]) - len(lines[start].lstrip())
        end = next(i for i in range(start + 1, len(lines)) if lines[i] == " " * indent + "SCRIPT")
        body = [line[indent:] for line in lines[start + 1:end]]
        self.assertFalse(any("launchctl" in line or "sysctl" in line for line in body))
        self.assertTrue(any(line.startswith('root="${3%/}"') for line in body))
        for line in body:
            for prefix in line.split("/usr/local")[:-1]:
                self.assertTrue(prefix.endswith(("$root", '"$root"')), line)
        self.postinstall = self.scripts / "postinstall"
        self.postinstall.write_text("\n".join(body) + "\n", encoding="utf-8")
        (self.scripts / "installed-cli-tools.txt").write_text("jt9\nwsprd\n", encoding="utf-8")

    def executable(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#!/bin/sh\n", encoding="utf-8")
        path.chmod(0o755)

    def run_postinstall(self):
        return subprocess.run(
            ["bash", str(self.postinstall), "pkg", "/", str(self.root), "/"],
            stdin=subprocess.DEVNULL, text=True, capture_output=True, check=False,
        )

    def test_upgrade_removes_unshipped_programs_and_their_links(self):
        programs = self.root / "usr/local/wsjtx"
        links = self.root / "usr/local/bin"
        for name in ("jt9", "wsprd", "wsprcode"):
            self.executable(programs / name)
        links.mkdir(parents=True)
        (links / "wsprcode").symlink_to(programs / "wsprcode")
        result = self.run_postinstall()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(path.name for path in programs.iterdir()), ["jt9", "wsprd"])
        self.assertEqual(sorted(path.name for path in links.iterdir()), ["jt9", "wsprd"])

    def test_symlinked_program_directory_is_not_cleaned(self):
        elsewhere = self.root / "Users/someone/bin"
        for name in ("jt9", "backup.sh"):
            self.executable(elsewhere / name)
        (self.root / "usr/local").mkdir(parents=True)
        (self.root / "usr/local/wsjtx").symlink_to(elsewhere)
        result = self.run_postinstall()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((elsewhere / "backup.sh").exists())

    def test_trailing_non_executable_entry_does_not_fail_the_install(self):
        programs = self.root / "usr/local/wsjtx"
        self.executable(programs / "jt9")
        (programs / "zz-notes.txt").write_text("notes", encoding="utf-8")
        result = self.run_postinstall()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / "usr/local/bin/jt9").is_symlink())


if __name__ == "__main__":
    unittest.main()
