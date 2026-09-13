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

            library = runtime / "libgcc_s.so.1"
            library.write_bytes(b"fixture GCC runtime\n")
            library_sha = hashlib.sha256(library.read_bytes()).hexdigest()
            runtime_records = f"libgcc_s.so.1={library_sha}\n".encode()
            runtime_sha = hashlib.sha256(runtime_records).hexdigest()

            (config / "armhf-ci-image-config.sh").write_text(
                "ARMHF_CI_IMAGE_SCHEMA=1\n"
                f"ARMHF_RUNTIME_PREFIX={runtime}\n"
                "ARMHF_GCC_RUNTIME_LIBRARIES=(libgcc_s.so.1)\n"
                "ARMHF_GLIBC_VERSION=2.36\n",
                encoding="utf-8",
            )
            (config / "cross-builder.env").write_text(
                "role=cross-builder\n"
                "generation=build-20260901-10-1\n"
                f"recipe_sha256={'1' * 64}\n"
                "toolchain_id=gcc13.4.0-armhf-fixture\n"
                f"runtime_sha256={runtime_sha}\n"
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
            subprocess.run(
                [
                    str(WRITER),
                    "runtime",
                    "armhf",
                    "build-20260901-20-1",
                    "3" * 64,
                ],
                cwd=ROOT,
                env=environment,
                check=True,
            )

            fields = dict(
                line.split("=", 1)
                for line in (output / "image.env").read_text().splitlines()
            )
            self.assertEqual(fields["role"], "runtime")
            self.assertEqual(fields["architecture"], "armhf")
            self.assertEqual(fields["generation"], "build-20260901-20-1")
            self.assertEqual(fields["recipe_sha256"], "3" * 64)
            self.assertEqual(fields["cross_generation"], "build-20260901-10-1")
            self.assertEqual(fields["cross_recipe_sha256"], "1" * 64)
            self.assertEqual(fields["runtime_sha256"], runtime_sha)


if __name__ == "__main__":
    unittest.main()
