import importlib.util
import pathlib
import os
import subprocess
import sys
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "summary", ROOT / ".github/scripts/summarize-native-caches.py")
summary = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(summary)


class NativeCachePolicyTests(unittest.TestCase):
    def test_compiler_only_command(self):
        result = subprocess.run(
            [sys.executable, str(SPEC.origin), "--compiler"],
            env={**os.environ, "CACHE_STEPS": "{}", "COLD_BUILD": "true"},
            capture_output=True, text=True, check=True)
        self.assertIn("| Compiler objects | bypassed (cold build) | not run |", result.stdout)

    def test_unknown_dependency_is_rejected(self):
        result = subprocess.run(
            [sys.executable, str(SPEC.origin), "unknown"],
            env={**os.environ, "CACHE_STEPS": "{}"},
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown dependency: unknown", result.stderr)

    def test_restore_outcomes(self):
        cases = [
            ({}, "not run"),
            ({"outcome": "failure"}, "failure"),
            ({"outcome": "cancelled"}, "cancelled"),
            ({"outcome": "success", "outputs": {"cache-hit": "true"}}, "exact hit"),
            ({"outcome": "success", "outputs": {"cache-hit": "false", "cache-matched-key": "older"}}, "fallback restore"),
            ({"outcome": "success", "outputs": {"cache-hit": ""}}, "miss"),
        ]
        for step, expected in cases:
            with self.subTest(expected=expected):
                self.assertEqual(summary.restore_status(step, False), expected)
        self.assertEqual(summary.restore_status({}, True), "bypassed (cold build)")

    def test_failed_rebuild_and_forced_recompilation_are_distinct(self):
        report = summary.render({
            "qt-cache": {"outcome": "success"},
            "qt-build": {"outcome": "failure"},
            "ccache": {"outcome": "success", "outputs": {"cache-hit": "true"}},
            "compile": {"outcome": "skipped"},
        }, ["qt"], recache=True, compiler=True)
        self.assertIn("| Qt | miss | failure |", report)
        self.assertIn("| Compiler objects | exact hit | not run; forced recompilation requested |", report)

    def test_cold_build_blocks_every_workflow_cache_restore_and_save(self):
        for platform in ["macos", "windows"]:
            text = (ROOT / f".github/workflows/build-{platform}.yml").read_text()
            self.assertEqual(len(re.findall(r"^      cold_build:", text, re.M)), 2)
            for step in re.split(r"^      - ", text, flags=re.M)[1:]:
                if "uses: actions/cache/" in step:
                    self.assertIn("!inputs.cold_build", step)
            self.assertIn("CCACHE_DIR=$(mktemp -d", text)
            self.assertIn("id: compile", text)
        windows = (ROOT / ".github/workflows/build-windows.yml").read_text()
        self.assertIn("cache: ${{ !inputs.cold_build }}", windows)
        self.assertIn("cold_build: ${{ inputs.cold_build }}", windows)
        action = (ROOT / ".github/actions/build-windows-deps/action.yml").read_text()
        for step in re.split(r"^    - ", action, flags=re.M)[1:]:
            if "uses: actions/cache/" in step:
                self.assertIn("inputs.cold_build != 'true'", step)

    def test_summaries_run_after_failures(self):
        paths = [".github/workflows/build-macos.yml", ".github/workflows/build-windows.yml",
                 ".github/actions/warm-macos-deps/action.yml", ".github/actions/build-windows-deps/action.yml"]
        for path in paths:
            text = (ROOT / path).read_text()
            for step in re.split(r"^\s+- name: ", text, flags=re.M)[1:]:
                if step.startswith("Summarize"):
                    self.assertIn("if: always()", step)
        warmer = (ROOT / ".github/workflows/warm-dependency-caches.yml").read_text()
        self.assertIn("  summary:\n    if: always()", warmer)

    def test_windows_boost_recipe_and_cache_use_the_common_pin(self):
        action = (ROOT / ".github/actions/build-windows-deps/action.yml").read_text()
        workflow = (ROOT / ".github/workflows/build-windows.yml").read_text()
        recipe = (ROOT / ".github/scripts/build-boost-windows.sh").read_text()
        self.assertLess(action.index(". .github/scripts/boost-version.sh"),
                        action.index("id: boost-cache"))
        cache = next(part for part in re.split(r"^\s+- name: ", action, flags=re.M)
                     if part.startswith("Restore Boost cache"))
        self.assertIn("${{ steps.paths.outputs.boost_version }}", cache)
        self.assertIn("${{ steps.paths.outputs.boost_source_sha256 }}", cache)
        self.assertIn("'.github/scripts/boost-version.sh'", cache)
        self.assertNotIn("inputs.boost_version", action)
        self.assertNotIn("inputs.boost_source_sha256", action)
        self.assertNotIn("BOOST_VERSION:", workflow)
        self.assertNotIn("BOOST_SOURCE_SHA256:", workflow)
        self.assertIn('. "$script_dir/boost-version.sh"', recipe)
        self.assertIn('"$BOOST_SOURCE_URL"', recipe)
        self.assertIn('"$actual_sha256" != "$BOOST_SOURCE_SHA256"', recipe)
        self.assertNotIn("VERSION SOURCE_SHA256", recipe)

    def test_windows_fftw_cache_and_linkage_use_the_selected_package(self):
        action = (ROOT / ".github/actions/build-windows-deps/action.yml").read_text()
        windows = (ROOT / ".github/workflows/build-windows.yml").read_text()
        self.assertIn("hashFiles('.github/scripts/fftw-version.sh', '.github/scripts/install-fftw-windows.sh')", action)
        self.assertIn('./fftw-runtime-check.exe "$FFTW_VERSION"', action)
        self.assertNotIn("mingw-w64-ucrt-x86_64-fftw", windows)
        self.assertIn('-DFFTW3F_LIBRARY="${FFTW_PREFIX}/lib/libfftw3f.dll.a"', windows)
        self.assertIn('-DFFTW3F_THREADS_LIBRARY="${FFTW_PREFIX}/lib/libfftw3f_threads.dll.a"', windows)
        self.assertIn('cmp "${{ steps.windows-deps.outputs.fftw_prefix }}/bin/$library"', windows)

    def test_macos_fftw_cache_tracks_shared_source_policy(self):
        for path in (".github/workflows/build-macos.yml",
                     ".github/actions/warm-macos-deps/action.yml"):
            with self.subTest(path=path):
                text = (ROOT / path).read_text()
                self.assertLess(text.index(". .github/scripts/fftw-version.sh"),
                                text.index("id: fftw-cache"))
                cache = next(part for part in re.split(r"^\s+- name: ", text, flags=re.M)
                             if part.startswith("Restore FFTW cache"))
                self.assertIn("${{ env.FFTW_VERSION }}", cache)
                self.assertIn("hashFiles('.github/scripts/fftw-version.sh',", cache)
                self.assertNotIn("3.3.10", cache)

    def test_macos_boost_cache_tracks_shared_source_policy(self):
        for path in (".github/workflows/build-macos.yml",
                     ".github/actions/warm-macos-deps/action.yml"):
            with self.subTest(path=path):
                text = (ROOT / path).read_text()
                self.assertLess(text.index(". .github/scripts/boost-version.sh"),
                                text.index("id: boost-cache"))
                cache = next(part for part in re.split(r"^\s+- name: ", text, flags=re.M)
                             if part.startswith("Restore Boost cache"))
                self.assertIn("${{ env.BOOST_VERSION }}", cache)
                self.assertIn("${{ env.BOOST_SOURCE_SHA256 }}", cache)
                self.assertIn("hashFiles('.github/scripts/boost-version.sh',", cache)
                self.assertNotIn("1.85.0", text)

    def test_macos_dependency_warmer_saves_what_the_build_restores(self):
        build = (ROOT / ".github/workflows/build-macos.yml").read_text()
        action = (ROOT / ".github/actions/warm-macos-deps/action.yml").read_text()
        self.assertNotIn("env.RUNNER", action)
        self.assertNotIn("env.DEPLOYMENT_TARGET", action)
        action = action.replace("inputs.runner", "env.RUNNER").replace("inputs.deployment_target", "env.DEPLOYMENT_TARGET")

        def caches(text):
            qt = re.search(r"macos-qt-cache-key\.sh \\\n((?:\s+--.*\n)+)", text).group(1).split()
            return qt, {match["id"]: (match["path"], match["key"]) for match in re.finditer(
                r"id: (?P<id>[a-z0-9-]+-cache)\n(?:.*\n){1,3}?\s+path: (?P<path>.+)\n\s+key: (?P<key>.+)\n", text)}

        self.assertEqual(set(caches(action)[1]),
                         {f"{name}-cache" for name in ("qt", "libusb", "portaudio", "fftw", "boost", "hamlib", "pfunit")})
        self.assertEqual(caches(action), caches(build))

    def test_boost_caches_track_the_queue_publication_patch(self):
        for path in (".github/workflows/build-macos.yml",
                     ".github/actions/warm-macos-deps/action.yml",
                     ".github/actions/build-windows-deps/action.yml"):
            with self.subTest(path=path):
                text = (ROOT / path).read_text()
                cache = next(part for part in re.split(r"^\s+- name: ", text, flags=re.M)
                             if part.startswith("Restore Boost cache"))
                recipe = re.search(r"hashFiles\(([^)]+)\)", cache).group(1)
                self.assertIn("'.github/scripts/boost-log-queue.patch'", recipe)

    def test_windows_declares_openmp_without_relying_on_fftw_package_dependencies(self):
        for path in (".github/workflows/build-windows.yml",
                     ".github/workflows/warm-dependency-caches.yml"):
            with self.subTest(path=path):
                text = (ROOT / path).read_text()
                self.assertIn("mingw-w64-ucrt-x86_64-libgomp", text)
        windows = (ROOT / ".github/workflows/build-windows.yml").read_text()
        self.assertIn("-DCMAKE_REQUIRE_FIND_PACKAGE_OpenMP=ON", windows)


if __name__ == "__main__":
    unittest.main()
