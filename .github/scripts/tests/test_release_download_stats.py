import contextlib
import importlib.util
import io
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[3] / "ci-cd" / "release_download_stats.py"
SPEC = importlib.util.spec_from_file_location("release_download_stats", SCRIPT)
stats = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(stats)


def release(tag, assets, prerelease=False):
    return {
        "tag_name": tag,
        "prerelease": prerelease,
        "draft": False,
        "published_at": "2026-10-01T00:00:00Z",
        "assets": [{"name": n, "download_count": c} for n, c in assets.items()],
    }


class ClassifyTest(unittest.TestCase):
    # Asset names taken from published WSJTX/wsjtx releases v3.0.0 through v3.3.0-beta4.
    CASES = {
        "wsjtx-3.0.0-win64.exe": ("installer", "Windows"),
        "wsjtx-3.0.0-win32.exe": ("installer", "Win32"),
        "wsjtx-3.0.0-ARM-Darwin.dmg": ("installer", "macOS arm64"),
        "wsjtx-3.0.0-Darwin.dmg": ("installer", "macOS x86_64"),
        "wsjtx_3.0.0_amd64.deb": ("installer", "Linux x86_64"),
        "wsjtx-3.0.2-arm64-macOS.pkg": ("installer", "macOS arm64"),
        "wsjtx-3.0.2-x86_64-macOS.pkg": ("installer", "macOS x86_64"),
        "wsjtx-3.0.2-linux-aarch64.AppImage": ("installer", "Linux aarch64"),
        "wsjtx-3.0.2.aarch64.rpm": ("installer", "Linux aarch64"),
        "wsjtx-3.0.2.x86_64.rpm": ("installer", "Linux x86_64"),
        "wsjtx-3.2.0-rc1.armv7l.rpm": ("installer", "Linux armhf"),
        "wsjtx-3.3.0-beta4-linux-armhf-trixie.deb": ("installer", "Linux armhf"),
        "wsjtx-3.3.0-beta4-linux-armhf-bookworm.AppImage": ("installer", "Linux armhf"),
        "wsjtx-3.3.0-beta4-src.tar.gz": ("source", None),
        "wsjtx-3.3.0-beta4-windows-x86_64-jt9.tar.gz": ("tools", None),
        "wsjtx-3.3.0-beta4-arm64-macOS-codec.tar.gz": ("tools", None),
        "wsjtx.exe": ("tools", None),
        "jt9.exe": ("tools", None),
        "cwsim-arm64.tar.gz": ("tools", None),
        "SHA256SUMS": ("docs", None),
        "release-manifest.json": ("docs", None),
        "Release_Notes.txt": ("docs", None),
        "wsjtx-main_en.html": ("docs", None),
    }

    def test_published_asset_names(self):
        for name, expected in self.CASES.items():
            with self.subTest(name=name):
                self.assertEqual(stats.classify(name), expected)


class SummaryTest(unittest.TestCase):
    def test_platform_columns_add_up_to_installer_total(self):
        rel = release("v3.3.0", {
            "wsjtx-3.3.0-win64.exe": 100,
            "wsjtx-3.3.0-arm64-macOS.pkg": 20,
            "wsjtx-3.3.0-x86_64-macOS.pkg": 10,
            "wsjtx-3.3.0-linux-x86_64.deb": 7,
            "wsjtx-3.3.0-linux-armhf-trixie.rpm": 3,
            "wsjtx-3.3.0-src.tar.gz": 5,
            "wsjtx-3.3.0-linux-x86_64-wsprd.tar.gz": 4,
            "SHA256SUMS": 2,
        })
        s = stats.summarize(rel)
        self.assertEqual(s["installer"], 140)
        self.assertEqual(sum(s[p] for p in stats.PLATFORMS), s["installer"])
        self.assertEqual((s["source"], s["tools"], s["docs"]), (5, 4, 2))

    def test_diff_reports_only_downloads_since_snapshot(self):
        old = {"taken": "2026-10-20T00:00:00+00:00",
               "counts": {"v3.3.0": {"wsjtx-3.3.0-win64.exe": 90,
                                     "wsjtx-3.3.0-linux-x86_64.deb": 7}}}
        releases = [
            release("v3.3.1", {"wsjtx-3.3.1-win64.exe": 12}),
            release("v3.3.0", {"wsjtx-3.3.0-win64.exe": 100,
                               "wsjtx-3.3.0-linux-x86_64.deb": 7}),
        ]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            stats.print_diff(old, releases)
        lines = out.getvalue().splitlines()
        self.assertIn("v3.3.1", lines[1])
        self.assertIn("12 installers  (Windows 12)", lines[1])
        self.assertIn("v3.3.0", lines[2])
        self.assertIn("10 installers  (Windows 10)", lines[2])
        self.assertEqual(len(lines), 3)


if __name__ == "__main__":
    unittest.main()
