import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
HELPER = REPO_ROOT / ".github/scripts/linux-artifact-validation.sh"


class LinuxArtifactValidationTests(unittest.TestCase):
    def run_helper(self, command, **environment):
        env = os.environ.copy()
        env["VALIDATION_HELPER"] = str(HELPER)
        env.update({name: str(value) for name, value in environment.items()})
        return subprocess.run(
            ["bash", "-c", f'source "$VALIDATION_HELPER"; {command}'],
            check=False,
            capture_output=True,
            env=env,
            text=True,
        )

    def make_application_tree(self, kind="appimage"):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        executables = ["wsjtx", "jt9", "qmap", "map65", "ft8code"]
        if kind == "appimage":
            executables.append("wsprd")
        for executable in executables:
            path = root / "usr/bin" / executable
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch(mode=0o755)
        for relative in (
            "usr/share/wsjtx/ALLCALL7.TXT",
            "usr/share/wsjtx/CALL3.TXT",
            "usr/share/wsjtx/sounds/Message.wav",
            "usr/share/wsjtx/sounds/Testing123.wav",
        ):
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()

        if kind == "appdir":
            for relative in (
                "usr/share/applications/wsjtx.desktop",
                "usr/share/pixmaps/wsjtx_icon.png",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
        else:
            qt_conf = root / "usr/bin/qt.conf"
            qt_conf.write_text("[Paths]\nPlugins = plugins\n", encoding="utf-8")
            for relative in (
                "usr/plugins/platforms/libqxcb.so",
                "usr/plugins/sqldrivers/libqsqlite.so",
                "usr/plugins/audio/libqtaudio_alsa.so",
                "usr/lib/libQt5Core.so.5",
                "usr/lib/libQt5Widgets.so.5",
                "usr/lib/libQt5Multimedia.so.5",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
        return temporary, root

    def test_package_manifest_accepts_expected_payload(self):
        manifest = "\n".join(
            (
                "/usr/bin/wsjtx",
                "/usr/bin/jt9",
                "/usr/bin/qmap",
                "/usr/bin/map65",
                "/usr/share/wsjtx/ALLCALL7.TXT",
                "/usr/share/wsjtx/CALL3.TXT",
                "/usr/share/wsjtx/sounds/Message.wav",
                "/usr/share/wsjtx/sounds/Testing123.wav",
            )
        )
        result = self.run_helper(
            'validate_linux_package_manifest "$MANIFEST" fixture', MANIFEST=manifest
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_package_manifest_rejects_missing_executable_data_or_sound(self):
        required = [
            "/usr/bin/wsjtx",
            "/usr/bin/jt9",
            "/usr/bin/qmap",
            "/usr/bin/map65",
            "/usr/share/wsjtx/ALLCALL7.TXT",
            "/usr/share/wsjtx/CALL3.TXT",
            "/usr/share/wsjtx/sounds/Message.wav",
            "/usr/share/wsjtx/sounds/Testing123.wav",
        ]
        for missing in (
            "/usr/bin/map65",
            "/usr/share/wsjtx/ALLCALL7.TXT",
            "/usr/share/wsjtx/sounds/Message.wav",
        ):
            with self.subTest(missing=missing):
                manifest = "\n".join(path for path in required if path != missing)
                result = self.run_helper(
                    'validate_linux_package_manifest "$MANIFEST" fixture',
                    MANIFEST=manifest,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(missing, result.stderr)

    def test_package_manifest_rejects_executable_adjacent_sounds(self):
        manifest = "\n".join(
            (
                "/usr/bin/wsjtx",
                "/usr/bin/jt9",
                "/usr/bin/qmap",
                "/usr/bin/map65",
                "/usr/bin/sounds/Message.wav",
                "/usr/share/wsjtx/ALLCALL7.TXT",
                "/usr/share/wsjtx/CALL3.TXT",
                "/usr/share/wsjtx/sounds/Message.wav",
                "/usr/share/wsjtx/sounds/Testing123.wav",
            )
        )
        result = self.run_helper(
            'validate_linux_package_manifest "$MANIFEST" fixture', MANIFEST=manifest
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("/usr/bin/sounds", result.stderr)

    def test_application_layout_rejects_missing_executable_data_or_sound(self):
        for relative in (
            "usr/bin/map65",
            "usr/share/wsjtx/ALLCALL7.TXT",
            "usr/share/wsjtx/sounds/Message.wav",
        ):
            with self.subTest(relative=relative):
                temporary, root = self.make_application_tree("appdir")
                self.addCleanup(temporary.cleanup)
                (root / relative).unlink()
                result = self.run_helper(
                    'validate_linux_application_layout "$TREE" appdir', TREE=root
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(str(root / relative), result.stderr)

    def test_application_layout_rejects_executable_adjacent_sounds(self):
        temporary, root = self.make_application_tree("appdir")
        self.addCleanup(temporary.cleanup)
        (root / "usr/bin/sounds").mkdir()
        result = self.run_helper(
            'validate_linux_application_layout "$TREE" appdir', TREE=root
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("usr/bin/sounds", result.stderr)

    def test_appimage_layout_requires_qt_runtime_payload(self):
        for relative in (
            "usr/bin/qt.conf",
            "usr/plugins/platforms/libqxcb.so",
            "usr/plugins/sqldrivers/libqsqlite.so",
            "usr/plugins/audio/libqtaudio_alsa.so",
            "usr/lib/libQt5Core.so.5",
        ):
            with self.subTest(relative=relative):
                temporary, root = self.make_application_tree()
                self.addCleanup(temporary.cleanup)
                (root / relative).unlink()
                result = self.run_helper(
                    'validate_linux_application_layout "$TREE" appimage', TREE=root
                )
                self.assertNotEqual(result.returncode, 0)

    def test_armhf_elf_metadata_accepts_eabi5_hard_float_loader(self):
        result = self.run_helper(
            'validate_elf_metadata armhf fixture "$FILE_OUT" "$HEADER_OUT" "$PROGRAM_OUT"',
            FILE_OUT=(
                "fixture: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 "
                "(SYSV), dynamically linked"
            ),
            HEADER_OUT=(
                "Class: ELF32\nMachine: ARM\n"
                "Flags: 0x5000400, Version5 EABI, hard-float ABI"
            ),
            PROGRAM_OUT=(
                "INTERP 0x000174 [Requesting program interpreter: "
                "/lib/ld-linux-armhf.so.3]"
            ),
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_armhf_elf_metadata_rejects_wrong_abi_properties(self):
        good = {
            "FILE_OUT": (
                "fixture: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 "
                "(SYSV), dynamically linked"
            ),
            "HEADER_OUT": (
                "Class: ELF32\nMachine: ARM\n"
                "Flags: 0x5000400, Version5 EABI, hard-float ABI"
            ),
            "PROGRAM_OUT": (
                "INTERP 0x000174 [Requesting program interpreter: "
                "/lib/ld-linux-armhf.so.3]"
            ),
        }
        mutations = {
            "class": {"HEADER_OUT": good["HEADER_OUT"].replace("ELF32", "ELF64")},
            "machine": {
                "HEADER_OUT": good["HEADER_OUT"].replace("Machine: ARM", "Machine: X86-64")
            },
            "eabi": {"HEADER_OUT": good["HEADER_OUT"].replace("Version5", "Version4")},
            "hard_float": {
                "HEADER_OUT": good["HEADER_OUT"].replace("hard-float", "soft-float")
            },
            "loader": {"PROGRAM_OUT": good["PROGRAM_OUT"].replace("armhf", "arm")},
        }
        for fault, mutation in mutations.items():
            with self.subTest(fault=fault):
                environment = good | mutation
                result = self.run_helper(
                    'validate_elf_metadata armhf fixture "$FILE_OUT" "$HEADER_OUT" "$PROGRAM_OUT"',
                    **environment,
                )
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
