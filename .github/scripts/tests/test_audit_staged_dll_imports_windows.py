# SPDX-License-Identifier: GPL-3.0-or-later
import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = ROOT / ".github/scripts/audit-staged-dll-imports-windows.sh"

# Every executable imports Qt5Core.dll, which is staged.
OBJDUMP_STUB = """#!/bin/sh
echo "$2:     file format pei-x86-64"
printf '\\tDLL Name: KERNEL32.dll\\n'
printf '\\tDLL Name: Qt5Core.dll\\n'
"""


class QtImportAuditTests(unittest.TestCase):
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
        return subprocess.run(
            ["bash", str(SCRIPT), str(self.stage), str(executable)],
            env=self.env, text=True, capture_output=True, check=False,
        )

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


if __name__ == "__main__":
    unittest.main()
