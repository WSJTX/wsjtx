import hashlib
import io
import json
import os
import pathlib
import shutil
import subprocess
import tarfile
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]


class FftwBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = pathlib.Path(self.temp.name)
        self.bin = self.work / "bin"
        self.bin.mkdir()
        self.archive = self.work / "source.tar.gz"
        configure = b'#!/bin/sh\nprintf "%s\\n" "$@" > "$FFTW_TEST_CONFIGURE"\n'
        with tarfile.open(self.archive, "w:gz") as archive:
            info = tarfile.TarInfo("fftw-3.3.11/configure")
            info.mode = 0o755
            info.size = len(configure)
            archive.addfile(info, io.BytesIO(configure))
        self.command("curl", 'while [ "$#" -gt 0 ]; do\n'
                     '  if [ "$1" = -o ]; then cp "$FFTW_TEST_ARCHIVE" "$2"; exit; fi\n'
                     '  shift\ndone\nexit 1\n')
        self.command("make", 'printf "%s\\n" "$*" >> "$FFTW_TEST_MAKE"\n')
        self.command("sysctl", "echo 2\n")
        self.env = {
            **os.environ,
            "PATH": f"{self.bin}{os.pathsep}{os.environ['PATH']}",
            "FFTW_TEST_ARCHIVE": str(self.archive),
            "FFTW_TEST_CONFIGURE": str(self.work / "configure-args"),
            "FFTW_TEST_MAKE": str(self.work / "make-args"),
            "FFTW_ARCH": "aarch64",
            "ARCH": "arm64",
            "MAKE_JOBS": "2",
        }

    def command(self, name, body):
        path = self.bin / name
        path.write_text("#!/bin/sh\nset -eu\n" + body)
        path.chmod(0o755)

    def run_helper(self, helper, *, valid_checksum=True):
        scripts = self.work / helper
        scripts.mkdir()
        shutil.copy(ROOT / ".github/scripts" / helper, scripts / helper)
        digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        if not valid_checksum:
            digest = "0" * 64
        (scripts / "fftw-version.sh").write_text(
            f"FFTW_VERSION=3.3.11\nFFTW_SHA256={digest}\n")
        arguments = [str(self.work / "prefix")]
        if helper == "build-fftw-macos.sh":
            arguments.insert(0, "11.0")
        else:
            arguments += ["--host=arm-linux-gnueabihf"]
        return subprocess.run(
            ["bash", str(scripts / helper), *arguments], cwd=self.work,
            env={**self.env, "DESTDIR": str(self.work / "sysroot")},
            capture_output=True, text=True)

    def test_checksum_failure_stops_before_configure(self):
        for helper in ("build-fftw.sh", "build-fftw-macos.sh"):
            with self.subTest(helper=helper):
                result = self.run_helper(helper, valid_checksum=False)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse((self.work / "configure-args").exists())
                self.assertFalse((self.work / "make-args").exists())

    def test_linux_cross_build_preserves_configure_and_destdir(self):
        result = self.run_helper("build-fftw.sh")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        args = (self.work / "configure-args").read_text().splitlines()
        for argument in ("--enable-single", "--enable-threads", "--enable-shared",
                         "--disable-static", "--enable-neon", "--host=arm-linux-gnueabihf"):
            self.assertIn(argument, args)
        self.assertEqual((self.work / "make-args").read_text().splitlines(),
                         ["-j2", f"install DESTDIR={self.work / 'sysroot'}"])

    def test_macos_build_retains_deployment_target(self):
        result = self.run_helper("build-fftw-macos.sh")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        args = (self.work / "configure-args").read_text().splitlines()
        self.assertIn("--enable-neon", args)
        self.assertIn("CFLAGS=-O3 -mmacosx-version-min=11.0", args)
        self.assertIn("LDFLAGS=-mmacosx-version-min=11.0", args)

    def test_native_image_shell_can_source_shared_dependency_policy(self):
        for name in ("Dockerfile.noble", "Dockerfile.bookworm"):
            with self.subTest(recipe=name):
                shell = ["/bin/sh", "-c"]
                checks = 0
                for line in (ROOT / ".github/images/linux-ci" / name).read_text().splitlines():
                    if line.startswith("SHELL "):
                        shell = json.loads(line.removeprefix("SHELL "))
                    if ". /usr/local/share/wsjtx-ci/linux-ci-image-config.sh" in line:
                        self.assertEqual(pathlib.PurePosixPath(shell[0]).name, "bash")
                        result = subprocess.run(
                            [*shell, 'set -eu; . "$1"; printf "%s\\n" "$FFTW_VERSION"',
                             "policy-check", str(ROOT / ".github/scripts/linux-ci-image-config.sh")],
                            cwd=self.work, capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(result.stdout.strip(), "3.3.11")
                        checks += 1
                self.assertGreater(checks, 0)


if __name__ == "__main__":
    unittest.main()
