# SPDX-License-Identifier: GPL-3.0-or-later
import os
import pathlib
import subprocess
import tempfile
import unittest


SCRIPTS = pathlib.Path(__file__).resolve().parents[1]


class MacOSArtifactValidationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.stage = self.root / "stage"
        self.stage.mkdir()
        self.tools = self.root / "bin"
        self.tools.mkdir()
        self.env = dict(os.environ, PATH=f"{self.tools}:{os.environ['PATH']}")
        self.tool("file", 'echo "$1: Mach-O 64-bit executable arm64"')
        self.tool("lipo", 'echo "${TEST_ARCH:-arm64}"')
        self.tool("otool", '''
case "$3" in
  -l)
    if [ "${TEST_ARCHIVE:-0}" = 1 ]; then
      echo "Archive : $4"
      echo "$4(member.o):"
    fi
    printf '      cmd LC_BUILD_VERSION\\n    minos %s\\n' "${TEST_MINOS:-11.0}"
    ;;
  -D) echo "$4:" ;;
  -L)
    echo "$4:"
    echo ' /usr/lib/libSystem.B.dylib (compatibility version 1.0.0)'
    case "$4" in
      *"${TEST_IMPORTER:-no-importer}")
        echo ' @rpath/libboost_log.dylib (compatibility version 1.85.0)' ;;
    esac
    ;;
esac
''')

    def tool(self, name, script):
        path = self.tools / name
        path.write_text("#!/bin/sh\n" + script + "\n", encoding="utf-8")
        path.chmod(0o755)

    def run_check(self, script="verify-macos-artifacts.sh", *arguments):
        return subprocess.run(
            ["bash", str(SCRIPTS / script), "--target", "11.0", "--arch", "arm64",
             *arguments, str(self.stage)],
            env=self.env, text=True, capture_output=True, check=False,
        )

    def test_static_linked_payload_passes(self):
        (self.stage / "wsjtx").write_bytes(b"executable")
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_direct_and_transitive_boost_imports_fail(self):
        for name in ("wsjtx", "libdependency.dylib", "plugin-without-extension"):
            with self.subTest(name=name):
                (self.stage / name).write_bytes(b"image")
                self.env["TEST_IMPORTER"] = name
                result = self.run_check()
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("Boost must be linked statically", result.stdout)

    def test_unreferenced_boost_libraries_fail(self):
        for name in ("libboost_log.dylib", "libboost_log.1.85.0.dylib", "libboost_log.a"):
            with self.subTest(name=name):
                path = self.stage / name
                path.write_bytes(b"unused library")
                result = self.run_check()
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("Bundled Boost library is not allowed", result.stdout)
                path.unlink()

    def test_dangling_boost_symlink_fails(self):
        (self.stage / "libboost_log.dylib").symlink_to("missing.dylib")
        result = self.run_check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Bundled Boost library is not allowed", result.stdout)

    def run_archive_check(self, *arguments):
        archive = self.root / "libboost_log.a"
        archive.write_bytes(b"archive")
        return subprocess.run(
            ["bash", str(SCRIPTS / "verify-macos-static-archives.sh"), "--target",
             "11.0", "--arch", "arm64", *arguments, str(archive)],
            env=self.env, text=True, capture_output=True, check=False,
        )

    def test_archive_architecture_and_target_pass(self):
        self.env["TEST_ARCHIVE"] = "1"
        result = self.run_archive_check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_shared_image_named_as_archive_fails(self):
        result = self.run_archive_check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("not a readable static archive", result.stdout)

    def test_archive_inspection_failure_is_fatal_in_warn_mode(self):
        self.tool("otool", "exit 1")
        result = self.run_archive_check("--mode", "warn")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Could not inspect static archive members", result.stdout)

    def test_archive_architecture_failure_is_fatal_in_warn_mode(self):
        self.tool("lipo", "exit 1")
        result = self.run_archive_check("--mode", "warn")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Could not determine static archive architecture", result.stdout)

    def test_wrong_architecture_fails(self):
        self.env.update(TEST_ARCHIVE="1", TEST_ARCH="x86_64")
        result = self.run_archive_check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Expected architecture arm64", result.stdout)

    def test_archive_deployment_target_is_enforced(self):
        self.env.update(TEST_ARCHIVE="1", TEST_MINOS="12.0")
        result = self.run_archive_check()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("requires macOS 12.0", result.stdout)


if __name__ == "__main__":
    unittest.main()
