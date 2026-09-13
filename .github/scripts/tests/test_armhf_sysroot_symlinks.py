import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
NORMALIZER = ROOT / ".github/scripts/normalize-armhf-sysroot-symlinks.sh"


class ArmhfSysrootSymlinkTests(unittest.TestCase):
    def test_absolute_links_are_rebased_inside_sysroot(self):
        with tempfile.TemporaryDirectory() as temporary:
            sysroot = pathlib.Path(temporary)
            target = sysroot / "lib/arm-linux-gnueabihf/libm.so.6"
            target.parent.mkdir(parents=True)
            target.write_text("fixture\n", encoding="utf-8")
            link = sysroot / "usr/lib/arm-linux-gnueabihf/libm.so"
            link.parent.mkdir(parents=True)
            link.symlink_to("/lib/arm-linux-gnueabihf/libm.so.6")

            subprocess.run(["bash", str(NORMALIZER), str(sysroot)], check=True)

            self.assertFalse(os.readlink(link).startswith("/"))
            self.assertEqual(link.resolve(), target)

    def test_relative_links_remain_unchanged(self):
        with tempfile.TemporaryDirectory() as temporary:
            sysroot = pathlib.Path(temporary)
            directory = sysroot / "usr/lib"
            directory.mkdir(parents=True)
            link = directory / "libfixture.so"
            link.symlink_to("libfixture.so.1")

            subprocess.run(["bash", str(NORMALIZER), str(sysroot)], check=True)

            self.assertEqual(os.readlink(link), "libfixture.so.1")


if __name__ == "__main__":
    unittest.main()
