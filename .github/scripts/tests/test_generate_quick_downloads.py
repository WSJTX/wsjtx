import contextlib
import importlib.util
import io
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "generate-quick-downloads.py"
SPEC = importlib.util.spec_from_file_location("generate_quick_downloads", SCRIPT)
downloads = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(downloads)


class QuickDownloadsTest(unittest.TestCase):
    def render(self, *names):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(downloads.main([str(SCRIPT), "WSJTX/wsjtx", "v3.3.0", *names]), 0)
        return output.getvalue()

    def test_both_armhf_baselines_have_distinct_links_in_every_format(self):
        assets = [
            f"wsjtx-3.3.0-linux-armhf-{distro}.{kind}"
            for distro in ("bookworm", "trixie") for kind in ("deb", "rpm", "AppImage")
        ]
        rendered = self.render(*reversed(assets), "wsjtx-3.3.0-linux-x86_64.deb")
        for name in assets:
            distro = name.split("-armhf-", 1)[1].split(".", 1)[0]
            self.assertIn(f"[armhf ({distro.title()})](https://github.com/WSJTX/wsjtx/releases/download/v3.3.0/{name})", rendered)
        for line in rendered.splitlines():
            if "[armhf (Bookworm)]" in line:
                self.assertLess(line.index("[armhf (Bookworm)]"), line.index("[armhf (Trixie)]"))
        self.assertIn("Use .deb packages on Debian-based systems.", rendered)
        self.assertNotIn("universal", rendered)

    def test_historical_assets_and_other_architectures_remain_visible(self):
        names = (
            "wsjtx-3.3.0-linux-armhf.deb", "wsjtx-3.3.0-linux-armhf.AppImage",
            "wsjtx-3.3.0.armv7l.rpm", "wsjtx-3.3.0-linux-aarch64.deb",
            "wsjtx-3.3.0-linux-x86_64.AppImage", "wsjtx-3.3.0-arm64-macOS.pkg",
            "wsjtx-3.3.0-win64.exe", "wsjtx-3.3.0-src.tar.gz",
        )
        rendered = self.render(*names)
        for name in names:
            self.assertIn(f"/v3.3.0/{name}", rendered)
        self.assertIn("[armhf]", rendered)
        self.assertIn("[armv7l]", rendered)
        self.assertNotIn("choose Bookworm", rendered)

    def test_cli_archives_are_excluded_and_asset_paths_are_normalized(self):
        name = "wsjtx-3.3.0-linux-armhf-trixie.deb"
        rendered = self.render(f"artifacts/{name}", name, "wsjtx-3.3.0-linux-armhf-trixie-codec.tar.gz")
        self.assertEqual(rendered.count(name), 1)
        self.assertNotIn("codec.tar.gz", rendered)
        self.assertEqual(self.render("wsjtx-3.3.0-linux-armhf-bookworm-jt9.tar.gz"), "")


if __name__ == "__main__":
    unittest.main()
