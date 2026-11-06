# SPDX-License-Identifier: GPL-3.0-or-later
"""Where the long decode-thread rows (label decode-thread-long) are left out.

ci-cd/DEVELOPMENT_WORKFLOW.md, Test Failure Policy: armhf and macOS Intel always
leave them out, and every leg does in a release build, a run from a tag or a
release/ branch other than its push CI. Each test pins a ctest command whole,
so an added exclusion fails it as surely as a dropped one.
"""

import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
RELEASE = ("startsWith(github.ref, 'refs/tags/') || "
           "(startsWith(github.ref, 'refs/heads/release/') && github.event_name != 'push')")
EXCLUDE = " && '-LE decode-thread-long' || '' }}"
OUTPUTS = "--output-on-failure --output-junit ctest-results.xml"


class DecodeThreadLongPolicyTests(unittest.TestCase):
    def read(self, relative_path):
        return (REPOSITORY / relative_path).read_text(encoding="utf-8")

    def assertOneCtest(self, relative_path, command):
        text = self.read(relative_path)
        self.assertIn(command, text)
        self.assertEqual(text.count("ctest "), 1, "one ctest command")
        self.assertEqual(text.count("decode-thread-long"), 1, "one use of the label")

    def test_linux_leaves_them_out_in_release_builds(self):
        self.assertOneCtest(".github/actions/build-linux-payload/action.yml",
                            '      run: >-\n'
                            '        xvfb-run -a -s "-screen 0 1280x1024x24" ctest --parallel 3\n'
                            '        ${{ (' + RELEASE + ')' + EXCLUDE + '\n'
                            '        ' + OUTPUTS + '\n')

    def test_macos_intel_always_and_both_macos_in_release_builds(self):
        self.assertOneCtest(".github/workflows/build-macos.yml",
                            '        run: >-\n'
                            '          ctest --parallel 3\n'
                            "          ${{ (inputs.arch == 'x86_64' || " + RELEASE + ')' + EXCLUDE + '\n'
                            '          ' + OUTPUTS + '\n')

    def test_windows_leaves_them_out_in_release_builds(self):
        self.assertOneCtest(".github/workflows/build-windows.yml",
                            '          ctest ${{ (' + RELEASE + ')' + EXCLUDE + ' ' + OUTPUTS + '\n')

    def test_armhf_always_leaves_them_out(self):
        self.assertOneCtest(".github/scripts/validate-linux-armhf-runtime.sh",
                            '      ctest --parallel 3 -LE decode-thread-long ' + OUTPUTS + '\n')


if __name__ == "__main__":
    unittest.main()
