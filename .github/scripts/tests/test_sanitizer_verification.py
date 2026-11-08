import importlib.util
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SCRIPTS = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "sanitizer_verifier", SCRIPTS / "verify-sanitizer-instrumentation.py"
)
VERIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFIER)


class SanitizerLinkVerificationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / "boost"
        self.prefix.mkdir()
        self.archive = self.prefix / "libboost_log.a"
        self.archive.write_bytes(b"!<arch>\n")
        self.build = self.root / "build"
        self.link = self.build / "CMakeFiles/wsjtx.dir/link.txt"
        self.link.parent.mkdir(parents=True)
        self.link.write_text(f"c++ main.o {self.archive} -o wsjtx\n")

    def test_accepts_instrumented_archives_from_pinned_prefix(self):
        with patch.object(VERIFIER, "tool_output", return_value="U __tsan_read8"):
            VERIFIER.verify_boost_links(self.build, self.prefix)

    def test_rejects_uninstrumented_archive(self):
        with patch.object(VERIFIER, "tool_output", side_effect=["U malloc", "T boost_function"]):
            with self.assertRaisesRegex(SystemExit, "no reference to __tsan_"):
                VERIFIER.verify_boost_links(self.build, self.prefix)

    def test_rejects_empty_archive(self):
        with patch.object(VERIFIER, "tool_output", return_value=""):
            with self.assertRaisesRegex(SystemExit, "no reference to __tsan_"):
                VERIFIER.verify_boost_links(self.build, self.prefix)

    def test_asan_checks_prefix_without_requiring_tsan_instrumentation(self):
        with patch.object(VERIFIER, "tool_output") as command:
            VERIFIER.verify_boost_links(self.build, self.prefix, instrumented=False)
        command.assert_not_called()

    def test_rejects_transitive_archive_outside_prefix(self):
        other = self.root / "libboost_thread.a"
        other.write_bytes(b"!<arch>\n")
        with self.link.open("a") as stream:
            stream.write(f" {other}")
        with patch.object(VERIFIER, "tool_output", return_value="U __tsan_read8"):
            with self.assertRaisesRegex(SystemExit, "outside the pinned prefix"):
                VERIFIER.verify_boost_links(self.build, self.prefix)

    def test_rejects_shared_and_unresolved_link_inputs(self):
        for argument in ("/usr/lib/libboost_log.so", "-lboost_log", "/usr/lib/libboost_log.dll.a"):
            with self.subTest(argument=argument):
                self.link.write_text(f"c++ main.o {argument} -o wsjtx\n")
                with self.assertRaisesRegex(SystemExit, "non-static or unresolved"):
                    VERIFIER.verify_boost_links(self.build)

    def test_rejects_disguised_shared_library(self):
        self.archive.write_bytes(b"\x7fELF")
        with self.assertRaisesRegex(SystemExit, "not a static archive"):
            VERIFIER.verify_boost_links(self.build)

    def test_rejects_boost_import_in_secondary_shared_image(self):
        image = self.build / "plugin.so"
        image.write_bytes(b"\x7fELF\x02\x01" + b"\0" * 10 + b"\x03\0")
        with patch.object(VERIFIER, "tool_output", return_value="(NEEDED) [libboost_thread.so.1.83.0]"):
            with self.assertRaisesRegex(SystemExit, "shared Boost dependency.*plugin.so"):
                VERIFIER.verify_no_shared_boost(self.build)

    def test_skips_relocatable_objects(self):
        (self.build / "source.o").write_bytes(b"\x7fELF\x02\x01" + b"\0" * 10 + b"\x01\0")
        with patch.object(VERIFIER, "tool_output") as command:
            VERIFIER.verify_no_shared_boost(self.build)
        command.assert_not_called()


class TSanDependencyVerificationTests(unittest.TestCase):
    def test_scoped_suppressions_preserve_baseline_without_hiding_logging_reports(self):
        source = SCRIPTS.parents[1]
        cmake = (source / "CMakeLists.txt").read_text()
        generation = cmake.split("include (CMake/Dependencies.cmake)", 1)[1].split(
            "include (CMake/CompilerFlags.cmake)", 1
        )[0]
        baseline = (source / "tests/sanitizers/tsan.supp").read_text()
        self.assertNotRegex(baseline, r"(?m)^race:.*boost::")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(source / "tests/sanitizers", root / "tests/sanitizers")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.16)\n"
                "project(SuppressionFixture NONE)\n" + generation
            )
            build = root / "build"
            subprocess.run(
                ["cmake", "-S", str(root), "-B", str(build), "-DWSJT_ENABLE_TSAN=ON"],
                text=True, capture_output=True, check=True,
            )
            output = build / "tsan-suppressions"
            self.assertEqual((output / "tsan.supp").read_text(), baseline)
            for scope in ("fortran", "network", "gui"):
                scoped = (source / f"tests/sanitizers/tsan-{scope}.supp").read_text()
                self.assertEqual(
                    (output / f"tsan-{scope}.supp").read_text(), baseline + "\n" + scoped,
                )

    def test_image_smoke_selects_static_instrumented_boost_prefix(self):
        verifier = (SCRIPTS / "verify-linux-ci-image.sh").read_text()
        arguments = verifier.split("  cmake_args=(\n", 1)[1].split("  export PKG_CONFIG_PATH=", 1)[0]
        result = subprocess.run(
            ["bash", "-c", "set -eu\n"
             'expected_flavor=tsan\nimage_prefix=/fixture\nBOOST_VERSION=1.92.0\n'
             'smoke_dir=/smoke\nhamlib_prefix=/fixture/tsan/hamlib\n'
             "cmake_args=(\n" + arguments + '\nprintf "%s\\n" "${cmake_args[@]}"'],
            text=True, capture_output=True, check=True,
        )
        options = result.stdout.splitlines()
        self.assertIn("-DBoost_DIR=/fixture/tsan/boost-static/lib/cmake/Boost-1.92.0", options)
        self.assertIn("-DBoost_NO_BOOST_CMAKE=OFF", options)
        self.assertIn("-DBoost_USE_STATIC_LIBS=ON", options)
        self.assertIn("-DBoost_NO_SYSTEM_PATHS=ON", options)
        self.assertIn(
            "-DCMAKE_PREFIX_PATH=/fixture/tsan/qt;/fixture/tsan/boost-static;/fixture/tsan/hamlib",
            options,
        )

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.script = self.root / "verify-tsan-deps-linux.sh"
        shutil.copyfile(SCRIPTS / self.script.name, self.script)
        for name in ("linux-artifact-validation.sh", "boost-version.sh"):
            shutil.copyfile(SCRIPTS / name, self.root / name)
        (self.root / "tsan-linux-deps-config.sh").write_text(
            'TSAN_BOOST_VERSION=1.92.0\n'
            'tsan_dependency_cache_key() { printf "fixture-%s\\n" "$1"; }\n'
        )
        self.qt = self.root / "qt"
        self.boost = self.root / "boost"
        self.hamlib = self.root / "hamlib"
        for name, prefix in (("qt", self.qt), ("boost", self.boost), ("hamlib", self.hamlib)):
            prefix.mkdir()
            (prefix / ".wsjtx-tsan-manifest").write_text(f"dependency={name}\ncache_key=fixture-{name}\n")
        for component in ("Core", "Test", "Multimedia", "SerialPort", "WebSockets"):
            self.file(self.qt / f"lib/cmake/Qt5{component}/Qt5{component}Config.cmake")
        self.file(self.qt / "lib/libQt5Core.so.5")
        self.file(self.boost / "include/boost/log/core.hpp")
        header = "boost/log/detail/threadsafe_queue.hpp"
        cpp = "libs/log/src/threadsafe_queue.cpp"
        header_data = b"fixture queue header\n"
        source_hashes = {header: hashlib.sha256(header_data).hexdigest(), cpp: "1" * 64}
        patched_hashes = {header: source_hashes[header], cpp: "2" * 64}
        helper = self.root / "patch-boost-log-queue.py"
        helper.write_text(
            "import hashlib\n"
            "def digest(data): return hashlib.sha256(data).hexdigest()\n"
            f"HEADER = {header!r}\nSUPPORTED = {{109200: '1.92.0'}}\n"
            f"SOURCE_HASHES = {source_hashes!r}\nPATCHED_HASHES = {patched_hashes!r}\n"
        )
        self.file(self.boost / "share/wsjtx/boost-log-queue-patch.json", json.dumps({
            "boost_version": "1.92.0", "patch": "boost-log-queue-release-acquire-v1",
            "patch_helper_sha256": hashlib.sha256(helper.read_bytes()).hexdigest(),
            "files": {name: {"original_sha256": source_hashes[name],
                             "patched_sha256": patched_hashes[name]} for name in (header, cpp)},
        }).encode())
        self.file(self.boost / "include" / header, header_data)
        for component in ("log", "log_setup", "filesystem", "thread", "container", "chrono", "atomic"):
            self.file(self.boost / f"lib/libboost_{component}.a", b"!<arch>\n")
        self.file(self.hamlib / "lib/libhamlib.a", b"!<arch>\n")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.file(self.bin / "nm", b'#!/bin/sh\nprintf "U __tsan_read8\\n"\n')
        (self.bin / "nm").chmod(0o755)
        self.cache = self.root / "CMakeCache.txt"
        for package in ("Boost", "boost_headers", "boost_log", "boost_log_setup"):
            self.file(self.boost / f"lib/cmake/{package}-1.92.0/{package}Config.cmake")
        self.file(self.root / "CMakeFiles/wsjtx.dir/flags.make",
                  f"CXX_INCLUDES = -isystem {self.boost}/include\n".encode())
        self.file(self.root / "CMakeFiles/wsjtx.dir/link.txt",
                  f"c++ main.o {self.boost}/lib/libboost_log.a {self.boost}/lib/libboost_log_setup.a\n".encode())
        self.cache.write_text("\n".join((
            "Boost_USE_STATIC_LIBS:BOOL=ON",
            "Boost_NO_BOOST_CMAKE:BOOL=OFF",
            f"Qt5Core_DIR:PATH={self.qt}/lib/cmake/Qt5Core",
            *(f"{package}_DIR:PATH={self.boost}/lib/cmake/{package}-1.92.0"
              for package in ("Boost", "boost_headers", "boost_log", "boost_log_setup")),
            f"Hamlib_INCLUDE_DIR:PATH={self.hamlib}/include",
            f"Hamlib_LIBRARY:FILEPATH={self.hamlib}/lib/libhamlib.a",
        )) + "\n")

    def file(self, path, data=b""):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def run_verifier(self):
        return subprocess.run(
            ["bash", str(self.script), str(self.qt), str(self.boost), str(self.hamlib), str(self.cache)],
            env={**os.environ, "PATH": str(self.bin) + os.pathsep + os.environ["PATH"]},
            text=True, capture_output=True, check=False,
        )

    def test_accepts_pinned_static_dependencies(self):
        result = self.run_verifier()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_missing_patch_provenance(self):
        (self.boost / "share/wsjtx/boost-log-queue-patch.json").unlink()
        result = self.run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Boost queue provenance validation failed", result.stderr)

    def test_patch_helper_change_invalidates_dependency_cache(self):
        for name in ("tsan-linux-deps-config.sh", "build-boost-tsan-linux.sh", "boost-version.sh"):
            shutil.copyfile(SCRIPTS / name, self.root / name)
        command = ["bash", "-c", 'source "$1"; TSAN_TOOLCHAIN_ID=fixture; '
                   'tsan_dependency_cache_key boost', "test", str(self.root / "tsan-linux-deps-config.sh")]
        before = subprocess.check_output(command, text=True)
        with (self.root / "patch-boost-log-queue.py").open("a") as stream:
            stream.write("\n# Changed patch recipe\n")
        after = subprocess.check_output(command, text=True)
        self.assertNotEqual(before, after)
        (self.root / "tsan-linux-deps-config.sh").write_text(
            'TSAN_BOOST_VERSION=1.92.0\n'
            'tsan_dependency_cache_key() { if [ "$1" = boost ]; then '
            f"printf '%s\\n' '{after.strip()}'; "
            'else printf "fixture-%s\\n" "$1"; fi; }\n'
        )
        (self.boost / ".wsjtx-tsan-manifest").write_text(f"dependency=boost\ncache_key={before.strip()}\n")
        result = self.run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("boost TSan cache manifest mismatch", result.stderr)

    def test_rejects_stale_shared_boost(self):
        self.file(self.boost / "lib/libboost_thread.so.1.83.0")
        result = self.run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Shared Boost library", result.stderr)

    def test_rejects_missing_archive(self):
        (self.boost / "lib/libboost_thread.a").unlink()
        result = self.run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing Boost.thread", result.stderr)

    def test_rejects_uninstrumented_additional_archive(self):
        self.file(self.boost / "lib/libboost_date_time.a", b"!<arch>\n")
        self.file(self.bin / "nm", b'#!/bin/sh\ncase "$*" in *libboost_date_time.a*) exit 0 ;; esac\nprintf "U __tsan_read8\\n"\n')
        result = self.run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("libboost_date_time.a does not reference the TSan runtime", result.stderr)

    def test_rejects_transitive_system_config_in_cache(self):
        with self.cache.open("a") as stream:
            stream.write("boost_thread_DIR:PATH=/usr/lib/cmake/boost_thread-1.92.0\n")
        result = self.run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Boost config outside pinned prefix", result.stderr)


if __name__ == "__main__":
    unittest.main()
