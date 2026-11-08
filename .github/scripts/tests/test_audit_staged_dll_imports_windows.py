# SPDX-License-Identifier: GPL-3.0-or-later
import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = ROOT / ".github/scripts/audit-staged-dll-imports-windows.sh"

# Images import staged Qt5Core.dll unless a sidecar supplies their import table.
OBJDUMP_STUB = """#!/bin/sh
if [ -f "$2.imports" ]; then
    cat "$2.imports"
    exit 0
fi
echo "$2:     file format pei-x86-64"
printf '\\tDLL Name: KERNEL32.dll\\n'
printf '\\tDLL Name: Qt5Core.dll\\n'
"""


class StagedImportAuditTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = pathlib.Path(temporary.name)
        self.stage = root / "stage"
        self.stage.mkdir()
        (self.stage / "Qt5Core.dll").write_bytes(b"")
        tools = root / "bin"
        tools.mkdir()
        (tools / "objdump").write_text(OBJDUMP_STUB, encoding="utf-8")
        (tools / "objdump").chmod(0o755)
        self.env = dict(os.environ, PATH=f"{tools}:{os.environ['PATH']}")

    def audit(self, name):
        executable = self.stage / name
        executable.write_bytes(b"MZ")
        result = subprocess.run(
            ["bash", str(SCRIPT), str(self.stage), str(executable)],
            env=self.env, text=True, capture_output=True, check=False,
        )
        executable.unlink()
        return result

    def set_imports(self, name, metadata):
        image = self.stage / name
        image.parent.mkdir(parents=True, exist_ok=True)
        image.write_bytes(b"MZ")
        image.with_name(image.name + ".imports").write_text(metadata, encoding="utf-8")

    def test_decoders_must_not_import_qt(self):
        for name in ("jt9.exe", "wsjt-codec.exe", "WSJT-CODEC.EXE"):
            with self.subTest(name=name):
                result = self.audit(name)
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn(f"{name} must not depend on Qt: Qt5Core.dll", result.stdout)

    def test_other_programs_may_import_qt(self):
        result = self.audit("jt9code.exe")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotIn("must not depend on Qt", result.stdout)

    def test_direct_boost_import_is_rejected(self):
        self.set_imports("wsjtx.exe", "DLL Name: libboost_log-mt-x64-1_91.dll\n")
        result = self.audit("wsjtx.exe")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("wsjtx.exe imports shared Boost", result.stdout)

    def test_transitive_boost_import_in_nested_dll_is_rejected(self):
        self.set_imports("plugins/helper.DLL", "DLL Name: BOOST_THREAD.DLL\n")
        result = self.audit("wsjtx.exe")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("helper.DLL imports shared Boost", result.stdout)

    def test_unrequested_utility_is_audited(self):
        self.set_imports("tools/utility.EXE", "DLL Name: libboost_log.dll\n")
        result = self.audit("wsjtx.exe")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("utility.EXE imports shared Boost", result.stdout)

    def test_unreferenced_boost_libraries_are_rejected(self):
        for name in ("libboost_log.dll", "BOOST_THREAD.DLL", "libboost_log.dll.a", "libboost_log.a", "boost_log.lib"):
            with self.subTest(name=name):
                library = self.stage / name
                library.write_bytes(b"")
                result = self.audit("wsjtx.exe")
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn(f"Boost library must not be bundled in static release: {library}", result.stdout)
                library.unlink()

    def test_windows_api_floor_applies_to_every_linked_image(self):
        for api in ("WaitOnAddress", "WakeByAddressSingle", "WakeByAddressAll", "api-ms-win-core-synch-l1-2-0.dll"):
            with self.subTest(api=api):
                self.set_imports("plugins/helper.dll", f"DLL Name: KERNEL32.dll\n  1234 {api}\n")
                result = self.audit("wsjtx.exe")
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("helper.dll imports Windows 8+ synchronization APIs", result.stdout)
                self.assertIn(api, result.stdout)

    def test_static_boost_image_with_no_shared_imports_passes(self):
        self.set_imports("wsjtx.exe", "DLL Name: KERNEL32.dll\n")
        (self.stage / "Qt5Core.dll").unlink()
        result = self.audit("wsjtx.exe")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
