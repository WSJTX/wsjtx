import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
VALIDATOR = ROOT / ".github/scripts/armhf-static-ldd.sh"


class ArmhfStaticLddTests(unittest.TestCase):
    def run_validator(self, needed, libraries, runpath=None, transitive=None):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = pathlib.Path(temporary)
            fixture_bin = fixture / "bin"
            fixture_lib = fixture / "lib"
            fixture_bin.mkdir()
            fixture_lib.mkdir()
            target = fixture / "program"
            target.write_text("target\n", encoding="utf-8")

            transitive = transitive or {}
            dynamic_output = "".join(
                f"printf '%s\\n' ' 0x00000001 (NEEDED)             Shared library: [{library}]'\n"
                for library in needed
            )
            if runpath is not None:
                dynamic_output += (
                    "printf '%s\\n' ' 0x0000001d (RUNPATH)            "
                    f"Library runpath: [{runpath}]'\n"
                )
            transitive_cases = "".join(
                "  *" + parent + "*)\n"
                + "".join(
                    f"    printf '%s\\n' ' 0x00000001 (NEEDED)             Shared library: [{library}]'\n"
                    for library in dependencies
                )
                + "    ;;\n"
                for parent, dependencies in transitive.items()
            )
            (fixture_bin / "readelf").write_text(
                "#!/bin/sh\n"
                "case \"$*\" in\n"
                + transitive_cases
                + "  *)\n"
                + "".join(f"    {line}\n" for line in dynamic_output.splitlines())
                + "    ;;\n"
                "esac\n",
                encoding="utf-8",
            )
            (fixture_bin / "file").write_text(
                "#!/bin/sh\n"
                "case \"$*\" in\n"
                "  *libhost.so*) echo \"$*: ELF 64-bit LSB shared object, x86-64\" ;;\n"
                "  *) echo \"$*: ELF 32-bit LSB shared object, ARM, EABI5\" ;;\n"
                "esac\n",
                encoding="utf-8",
            )
            for tool in (fixture_bin / "readelf", fixture_bin / "file"):
                tool.chmod(0o755)
            for library in libraries:
                library_path = fixture_lib / library
                library_path.parent.mkdir(parents=True, exist_ok=True)
                library_path.write_text("fixture\n", encoding="utf-8")

            environment = os.environ.copy()
            environment.update(
                PATH=f"{fixture_bin}:{environment['PATH']}",
                WSJT_ARMHF_RUNTIME_PREFIX=str(fixture / "empty-runtime"),
                ARMHF_STATIC_LDD_LIBRARY_PATH=str(fixture_lib),
            )
            return subprocess.run(
                ["bash", str(VALIDATOR), str(target)],
                cwd=ROOT,
                env=environment,
                text=True,
                capture_output=True,
                check=False,
            )

    def test_resolves_armhf_dependency_without_executing_target(self):
        result = self.run_validator(["libfixture.so.1"], ["libfixture.so.1"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("libfixture.so.1 => ", result.stdout)

    def test_rejects_missing_dependency(self):
        result = self.run_validator(["libmissing.so.1"], [])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("libmissing.so.1 => not found", result.stdout)

    def test_rejects_quadmath_dependency(self):
        result = self.run_validator(["libquadmath.so.0"], ["libquadmath.so.0"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unavailable ARMHF runtime", result.stderr)

    def test_rejects_host_library(self):
        result = self.run_validator(["libhost.so.1"], ["libhost.so.1"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("non-ARMHF ELF", result.stderr)

    def test_resolves_origin_relative_runpath(self):
        result = self.run_validator(
            ["libpulsecommon-fixture.so"],
            ["pulseaudio/libpulsecommon-fixture.so"],
            runpath="$ORIGIN/lib/pulseaudio",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("pulseaudio/libpulsecommon-fixture.so", result.stdout)

    def test_resolves_transitive_dependency_closure(self):
        result = self.run_validator(
            ["libparent.so.1"],
            ["libparent.so.1", "libchild.so.1"],
            transitive={"libparent.so.1": ["libchild.so.1"]},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("libparent.so.1 => ", result.stdout)
        self.assertIn("libchild.so.1 => ", result.stdout)


if __name__ == "__main__":
    unittest.main()
