# SPDX-License-Identifier: GPL-3.0-only

"""Regression tests against the real application, isolated INI and UI controls.

The Linux-only Qt preload probe observes the actual control and status label,
optionally edits it or returns accepted from unchanged Settings, then closes
the application normally. The Settings fixture bypasses physical audio-device
validation and exercises MainWindow's accepted-settings return path only.
No test hook is compiled into WSJT-X and no station profile is used.
"""
import argparse
import configparser
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--application", required=True, type=Path)
parser.add_argument("--probe", required=True, type=Path)
args, unittest_args = parser.parse_known_args()


class SubmodeStartup(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wsjtx-submode-")
        self.profile = Path(self.temporary.name)
        (self.profile / "config").mkdir()
        (self.profile / "runtime").mkdir(mode=0o700)
        self.ini = self.profile / "config/WSJT-X - SubmodeRegression.ini"

    def tearDown(self):
        self.temporary.cleanup()

    def write_ini(self, mode="Q65", key="SubMode_Q65", value=2, period=30, vhf=False):
        setting = f"{key}={value}\n" if value is not None else ""
        self.ini.write_text(
            f"[General]\n{setting}TRPeriod_Q65={period}\nTRPeriod=15\n"
            f"[Common]\nMode={mode}\n[Configuration]\nVHFUHF={str(vhf).lower()}\n"
            "MyCall=N0CALL\nMyGrid=AA00\nPSKReporter=false\n"
        )

    def launch(self, expected, label=None, edit=None, mode=None, accept_settings=False):
        output = self.profile / "state.json"
        if output.exists():
            output.unlink()
        env = dict(os.environ)
        for name in list(env):
            if name.startswith("WSJT_SUBMODE_PROBE_"):
                del env[name]
        env.update(QT_QPA_PLATFORM="offscreen",
                   XDG_CONFIG_HOME=str(self.profile / "config"),
                   XDG_DATA_HOME=str(self.profile / "data"),
                   XDG_CACHE_HOME=str(self.profile / "cache"),
                   XDG_RUNTIME_DIR=str(self.profile / "runtime"),
                   LD_PRELOAD=str(args.probe.resolve()),
                   WSJT_SUBMODE_PROBE_OUTPUT=str(output))
        if edit is not None:
            env["WSJT_SUBMODE_PROBE_EDIT"] = str(edit)
        if mode:
            env["WSJT_SUBMODE_PROBE_MODE"] = mode
        if accept_settings:
            env["WSJT_SUBMODE_PROBE_ACCEPT_SETTINGS"] = "1"
        proc = subprocess.Popen([str(args.application.resolve()), "--rig-name=SubmodeRegression"],
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                start_new_session=True)
        try:
            log = proc.communicate(timeout=20)[0].decode(errors="replace")
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            log = proc.communicate()[0].decode(errors="replace")
            self.fail(f"Application did not finish startup/normal close: {log}")
        self.assertEqual(proc.returncode, 0, log)
        self.assertTrue(output.exists(), log)
        state = json.loads(output.read_text())
        self.assertEqual(state["value"], expected, state)
        if label:
            self.assertIn(label, state["status_labels"], state)
        return state

    def saved(self, key):
        ini = configparser.RawConfigParser(interpolation=None, strict=False)
        ini.optionxform = str
        ini.read(self.ini)
        return ini.get("General", key)

    def test_q65_without_vhf_and_period_limits(self):
        for period, value in [(15, 2), (30, 3), (60, 4), (120, 5), (300, 4)]:
            with self.subTest(period=period):
                self.write_ini(period=period, value=value)
                self.launch(value, f"Q65-{period}{chr(65 + value)}")
                self.assertEqual(self.saved("SubMode_Q65"), str(value))

    def test_q65_vhf_enabled(self):
        self.write_ini(vhf=True)
        self.launch(2, "Q65-30C")

    def test_restart_and_manual_prelaunch_edit(self):
        self.write_ini(period=60, value=1)
        self.launch(1, "Q65-60B")
        self.launch(1, "Q65-60B")  # normal saved-settings restart
        text = self.ini.read_text().replace("SubMode_Q65=1", "SubMode_Q65=4")
        self.ini.write_text(text)
        self.launch(4, "Q65-60E")  # edit the application's saved file while closed
        self.assertEqual(self.saved("SubMode_Q65"), "4")
        self.launch(4, "Q65-60E")

    def test_ui_selection_is_saved_and_restored(self):
        self.write_ini(period=60, value=1)
        self.launch(4, "Q65-60E", edit=4)
        self.assertEqual(self.saved("SubMode_Q65"), "4")
        self.launch(4, "Q65-60E")

    def test_accept_settings_does_not_reset_q65(self):
        self.write_ini()
        self.launch(2, "Q65-30C", accept_settings=True)
        self.assertEqual(self.saved("SubMode_Q65"), "2")

    def test_other_modes_keep_vhf_constraints(self):
        for mode, key, value in [("JT65", "SubMode_JT65", 2),
                                 ("JT4", "SubMode_JT4", 6), ("JT9", "SubMode", 7)]:
            for vhf in [True, False]:
                with self.subTest(mode=mode, vhf=vhf):
                    self.write_ini(mode=mode, key=key, value=value, vhf=vhf)
                    self.launch(value if vhf else 0,
                                f"{mode} {chr(65 + value)}" if vhf else None)

    def test_missing_invalid_and_out_of_range_submode(self):
        for value, expected in [(None, 0), ("invalid", 0), ("", 0), (-1, 0), (999, 3)]:
            with self.subTest(value=value):
                self.write_ini(value=value)
                self.launch(expected, f"Q65-30{chr(65 + expected)}")

    def test_first_run_keeps_default_mode(self):
        self.launch(0, "FT8")

    def test_mode_switch_uses_its_own_key_and_limits(self):
        self.write_ini(mode="JT65", key="SubMode_JT65", value=2, period=120, vhf=True)
        self.ini.write_text(self.ini.read_text().replace("[General]\n", "[General]\nSubMode_Q65=5\n"))
        self.launch(5, "Q65-120F", mode="Q65")
        self.launch(2, "JT65 C", mode="JT65")


if __name__ == "__main__":
    unittest.main(argv=[__file__] + unittest_args, verbosity=2)
