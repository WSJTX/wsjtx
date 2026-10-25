import hashlib
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]


class WindowsFftwInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.prefix = self.root / "prefix"
        self.archive = self.root / "package"
        self.archive.write_bytes(b"pinned Windows FFTW package\n")
        self.digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.script = self.root / "install-fftw-windows.sh"
        shutil.copyfile(ROOT / ".github/scripts/install-fftw-windows.sh", self.script)
        self.command("curl", '#!/bin/bash\nwhile [ "$1" != -o ]; do shift; done\ncp "$FFTW_TEST_ARCHIVE" "$2"\n')
        self.command("tar", '''#!/bin/bash
while [ "$1" != -C ]; do shift; done
mkdir -p "$2/ucrt64/include" "$2/ucrt64/lib" "$2/ucrt64/bin"
touch "$2/ucrt64/include/fftw3.h" \
  "$2/ucrt64/lib/libfftw3f.dll.a" "$2/ucrt64/lib/libfftw3f_threads.dll.a" \
  "$2/ucrt64/bin/libfftw3f-3.dll" "$2/ucrt64/bin/libfftw3f_threads-3.dll"
''')

    def command(self, name, contents):
        path = self.bin / name
        path.write_text(contents)
        path.chmod(0o755)

    def install(self, digest):
        (self.root / "fftw-version.sh").write_text(
            f"FFTW_WINDOWS_PACKAGE_VERSION=3.3.11-2\nFFTW_WINDOWS_SHA256={digest}\n")
        return subprocess.run(
            ["bash", str(self.script), str(self.prefix)],
            env={**os.environ, "PATH": f"{self.bin}:{os.environ['PATH']}",
                 "FFTW_TEST_ARCHIVE": str(self.archive)},
            capture_output=True, text=True)

    def test_verified_package_is_installed_in_private_prefix(self):
        result = self.install(self.digest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.prefix / "bin/libfftw3f-3.dll").is_file())
        self.assertTrue((self.prefix / "lib/libfftw3f_threads.dll.a").is_file())
        self.assertFalse((self.prefix / "ucrt64").exists())

    def test_changed_archive_is_rejected_before_extraction(self):
        result = self.install("0" * 64)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.prefix.exists())


if __name__ == "__main__":
    unittest.main()
