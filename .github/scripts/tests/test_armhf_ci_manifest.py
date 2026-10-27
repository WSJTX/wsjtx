import hashlib
import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
WRITER = ROOT / ".github/scripts/write-armhf-ci-manifest.sh"


class ArmhfCiManifestTests(unittest.TestCase):
    def test_runtime_manifest_preserves_requested_image_identity(self):
        for distro, time_bits, glibc in (("bookworm", 32, "2.36"), ("trixie", 64, "2.41")):
            with self.subTest(distro=distro):
                self.check_runtime_manifest(distro, time_bits, glibc)

    def test_runtime_manifest_rejects_mixed_distro_pair(self):
        self.check_runtime_manifest("trixie", 64, "2.41", cross_distro="bookworm")

    def test_runtime_manifest_rejects_mixed_time_abi(self):
        self.check_runtime_manifest("trixie", 64, "2.41", cross_time_bits=32)

    def check_runtime_manifest(self, distro, time_bits, glibc, cross_distro=None, cross_time_bits=None):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = pathlib.Path(temporary)
            config = fixture / "config"
            output = fixture / "output"
            runtime = fixture / "runtime"
            fixture_bin = fixture / "bin"
            config.mkdir()
            output.mkdir()
            runtime.mkdir()
            fixture_bin.mkdir()

            dpkg_query = fixture_bin / "dpkg-query"
            dpkg_query.write_text(
                "#!/bin/sh\nprintf '%s\\n' fixture-package=1.0\n",
                encoding="utf-8",
            )
            dpkg_query.chmod(0o755)

            runtime_records = b""
            for name in ("libgcc_s.so.1", "libfftw3f.so.3", "libfftw3f_threads.so.3"):
                library = runtime / name
                library.write_bytes(f"fixture {name}\n".encode())
                library_sha = hashlib.sha256(library.read_bytes()).hexdigest()
                runtime_records += f"{name}={library_sha}\n".encode()
            runtime_sha = hashlib.sha256(runtime_records).hexdigest()

            (config / "armhf-ci-image-config.sh").write_text(
                "ARMHF_CI_IMAGE_SCHEMA=2\n"
                f"ARMHF_DISTRO={distro}\n"
                f"ARMHF_TIME_BITS={time_bits}\n"
                f"ARMHF_RUNTIME_PREFIX={runtime}\n"
                "ARMHF_GCC_RUNTIME_LIBRARIES=(libgcc_s.so.1)\n"
                "FFTW_VERSION=3.3.11\n"
                f"FFTW_SHA256={'5' * 64}\n"
                f"ARMHF_GLIBC_VERSION={glibc}\n",
                encoding="utf-8",
            )
            (config / "cross-builder.env").write_text(
                "role=cross-builder\n"
                f"distro={cross_distro or distro}\n"
                f"time_bits={cross_time_bits or time_bits}\n"
                f"glibc_version={glibc}\n"
                "compiler_version=14.2.0\n"
                "compiler_target=arm-linux-gnueabihf\n"
                f"sysroot_package_sha256={'4' * 64}\n"
                "generation=build-20260901-10-1\n"
                f"recipe_sha256={'1' * 64}\n"
                "toolchain_id=gcc13.4.0-armhf-fixture\n"
                f"runtime_sha256={runtime_sha}\n"
                "fftw_version=3.3.11\n"
                f"fftw_source_sha256={'5' * 64}\n"
                "hamlib_ref=4.7.2\n"
                f"hamlib_commit={'2' * 40}\n",
                encoding="utf-8",
            )

            environment = os.environ.copy()
            environment.update(
                PATH=f"{fixture_bin}:{environment['PATH']}",
                WSJTX_ARMHF_CONFIG_DIR=str(config),
                WSJTX_CI_MANIFEST_DIR=str(output),
            )
            result = subprocess.run(
                [
                    str(WRITER),
                    "runtime",
                    "armhf",
                    "build-20260901-20-1",
                    "3" * 64,
                ],
                cwd=ROOT,
                env=environment,
                capture_output=True,
                text=True,
            )
            if cross_distro or cross_time_bits:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("ABI profiles differ", result.stderr)
                self.assertFalse((output / "image.env").exists())
                return
            self.assertEqual(result.returncode, 0, result.stderr)

            fields = dict(
                line.split("=", 1)
                for line in (output / "image.env").read_text().splitlines()
            )
            self.assertEqual(fields["role"], "runtime")
            self.assertEqual(fields["schema"], "2")
            self.assertEqual(fields["distro"], distro)
            self.assertEqual(fields["time_bits"], str(time_bits))
            self.assertEqual(fields["glibc_version"], glibc)
            self.assertEqual(fields["sysroot_package_sha256"], "4" * 64)
            self.assertEqual(fields["architecture"], "armhf")
            self.assertEqual(fields["generation"], "build-20260901-20-1")
            self.assertEqual(fields["recipe_sha256"], "3" * 64)
            self.assertEqual(fields["cross_generation"], "build-20260901-10-1")
            self.assertEqual(fields["cross_recipe_sha256"], "1" * 64)
            self.assertEqual(fields["runtime_sha256"], runtime_sha)
            self.assertEqual(fields["fftw_version"], "3.3.11")
            self.assertEqual(fields["fftw_source_sha256"], "5" * 64)


if __name__ == "__main__":
    unittest.main()
