import fnmatch
import os
import pathlib
import re
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
PUBLISH = (ROOT / ".github/workflows/publish-linux-ci-images.yml").read_text()
BUILD = (ROOT / ".github/workflows/build-linux.yml").read_text()
WARM = (ROOT / ".github/workflows/warm-dependency-caches.yml").read_text()
CLASSIFIER = (ROOT / ".github/scripts/is-linux-ci-image-input.sh").read_text()


def job(name: str, workflow: str = PUBLISH) -> str:
    match = re.search(
        rf"^  {re.escape(name)}:\n(?P<body>.*?)(?=^  [a-z][a-z0-9-]*:\n|\Z)",
        workflow,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise AssertionError(f"job not found: {name}")
    return match.group("body")


class LinuxImagePublicationPolicyTests(unittest.TestCase):
    def test_armhf_cross_cache_import_and_export_share_registry_destination(self):
        metadata = job("metadata")
        for package_root in ("ghcr.io/wsjtx/wsjtx", "ghcr.io/wsjtx/wsjtx-internal"):
            self.assertIn(
                f"armhf_cross_cache_from=type=registry,ref={package_root}/linux-armhf-cross-bookworm:buildcache",
                metadata,
            )
        cross = job("build-armhf-cross")
        self.assertIn("cache-from: ${{ needs.metadata.outputs.armhf_cross_cache_from }}", cross)
        self.assertIn(
            "cache-to: type=registry,ref=${{ needs.metadata.outputs.package_root }}/linux-armhf-cross-bookworm:buildcache,mode=max",
            cross,
        )

    def select_warmers(self, event, *, target="", schedule="", paths=()):
        selector = re.search(
            r"        run: \|\n(?P<body>.*?)(?=^  refresh-linux-images:)",
            WARM,
            re.MULTILINE | re.DOTALL,
        ).group("body")
        git_stub = """
git() {
  case "$1" in
    cat-file) return 0 ;;
    diff) printf '%s\\n' "$CHANGED_PATHS" ;;
    *) return 2 ;;
  esac
}
"""
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "output"
            result = subprocess.run(
                ["bash", "-c", git_stub + textwrap.dedent(selector)],
                cwd=ROOT,
                env={
                    **os.environ,
                    "EVENT_NAME": event,
                    "TARGETS": target,
                    "SCHEDULE": schedule,
                    "BEFORE_SHA": "previous",
                    "CURRENT_SHA": "current",
                    "CHANGED_PATHS": "\n".join(paths),
                    "HAMLIB_BRANCH": "4.7.2",
                    "GITHUB_OUTPUT": str(output),
                    "GITHUB_STEP_SUMMARY": str(pathlib.Path(directory) / "summary"),
                },
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return dict(line.split("=", 1) for line in output.read_text().splitlines())

    def test_macos_warmer_selection_routes_both_architectures(self):
        cases = [
            ({"event": "workflow_dispatch", "target": "macos-x86_64"}, "false", "true"),
            ({"event": "workflow_dispatch", "target": "macos-arm64"}, "true", "false"),
            ({"event": "workflow_dispatch", "target": "macos"}, "true", "true"),
            ({"event": "workflow_dispatch", "target": "all"}, "true", "true"),
            ({"event": "workflow_dispatch", "target": "linux"}, "false", "false"),
            ({"event": "schedule", "schedule": "23 10 * * 1"}, "true", "true"),
        ]
        for path in (
            ".github/workflows/build-macos.yml", ".github/actions/warm-macos-deps/action.yml",
            ".github/scripts/install-xpack-gfortran-macos.sh", ".github/scripts/build-qt-macos.sh",
            ".github/scripts/macos-qt-cache-key.sh", ".github/scripts/verify-macos-static-archives.sh",
            ".github/workflows/ci.yml", ".github/workflows/release.yml",
        ):
            cases.append(({"event": "push", "paths": [path]}, "true", "true"))
        for inputs, arm, intel in cases:
            with self.subTest(inputs=inputs):
                outputs = self.select_warmers(**inputs)
                self.assertEqual(outputs["macos_arm64"], arm)
                self.assertEqual(outputs["macos_x86_64"], intel)

    def test_asan_warmer_selection(self):
        cases = [
            ({"event": "schedule", "schedule": "23 10 * * 1"}, "true", "none"),
            ({"event": "workflow_dispatch", "target": "linux-asan-ubsan"}, "true", "none"),
            ({"event": "workflow_dispatch", "target": "all"}, "true", "normal"),
            ({"event": "workflow_dispatch", "target": "linux-tsan"}, "false", "tsan"),
            ({"event": "push", "paths": [".github/scripts/build-hamlib-linux.sh"]}, "false", "normal"),
            ({"event": "push", "paths": [".github/scripts/build-qt-tsan-linux.sh"]}, "false", "tsan"),
            ({"event": "push", "paths": ["CMakeLists.txt", ".github/scripts/verify-linux-ci-image.sh"]}, "true", "normal-and-tsan"),
            ({"event": "push", "paths": ["mainwindow.cpp"]}, "false", "none"),
        ]
        for arguments, selected, image_set in cases:
            with self.subTest(arguments=arguments):
                outputs = self.select_warmers(**arguments)
                self.assertEqual(outputs["linux_asan_ubsan"], selected)
                self.assertEqual(outputs["image_set"], image_set)

    def test_armhf_warmer_selection(self):
        cases = [
            ({"event": "schedule", "schedule": "23 10 * * 1"}, "true", "none"),
            ({"event": "workflow_dispatch", "target": "linux-armhf"}, "true", "none"),
            ({"event": "workflow_dispatch", "target": "all"}, "true", "normal"),
            ({"event": "workflow_dispatch", "target": "linux-tsan"}, "false", "tsan"),
            ({"event": "push", "paths": [".github/images/linux-ci/Dockerfile.armhf-cross"]}, "false", "normal"),
            ({"event": "push", "paths": ["mainwindow.cpp"]}, "false", "none"),
        ]
        for arguments, selected, image_set in cases:
            with self.subTest(arguments=arguments):
                outputs = self.select_warmers(**arguments)
                self.assertEqual(outputs["linux_armhf"], selected)
                self.assertEqual(outputs["image_set"], image_set)

    def test_armhf_build_inputs_select_warming(self):
        push_filters = re.findall(r'^      - "([^"]+)"$', WARM.split("  schedule:", 1)[0], re.MULTILINE)
        for path in (
            ".github/workflows/build-linux.yml",
            ".github/scripts/build-linux-armhf-cross.sh",
            ".github/scripts/audit-armhf-cross-build.sh",
            ".github/cmake/armhf-toolchain.cmake",
            ".github/scripts/linux-artifact-validation.sh",
            "CMakeLists.txt", "CMake/Sources.cmake", "tests/unit/CMakeLists.txt",
        ):
            with self.subTest(path=path):
                self.assertTrue(any(fnmatch.fnmatchcase(path, pattern) for pattern in push_filters))
                outputs = self.select_warmers("push", paths=[path])
                self.assertEqual(outputs["linux_armhf"], "true")
                self.assertEqual(outputs["image_set"], "none")

    def test_armhf_warmers_share_configuration_and_serialize(self):
        standalone = job("warm-linux-armhf-ccache", WARM)
        published = job("warm-armhf-ccache")
        for warmer in (standalone, published):
            for required in (
                "github.ref == 'refs/heads/develop'",
                "uses: ./.github/workflows/build-linux.yml", "arch: armhf",
                "armhf_build_only: true", "allow_stale_image: false",
                "recache: false", "save_ccache: true",
                "group: armhf-ccache-develop", "cancel-in-progress: false",
                "contents: read", "packages: read",
            ):
                self.assertIn(required, warmer)
            self.assertNotIn("packages: write", warmer)
        for condition in (
            "needs.detect.outputs.linux_armhf == 'true'",
            "needs.detect.outputs.image_set != 'normal'",
            "needs.detect.outputs.image_set != 'normal-and-tsan'",
        ):
            self.assertIn(condition, standalone)
        self.assertIn("- warm-linux-armhf-ccache", job("summary", WARM))
        for condition in (
            "needs.promote.result == 'success'", "inputs.publication_target == 'internal'",
            "inputs.mode == 'refresh'", "inputs.promote", "inputs.include_armhf",
            "(inputs.image_set == 'normal' || inputs.image_set == 'normal-and-tsan')",
            "image_tag: ${{ needs.metadata.outputs.build_tag }}",
        ):
            self.assertIn(condition, published)
        self.assertIn("- metadata", published)
        self.assertIn("- promote", published)
        self.assertNotIn("warm-armhf-ccache", job("promote"))

    def test_sanitizer_build_inputs_select_warming(self):
        push_filters = re.findall(
            r'^      - "([^"]+)"$',
            WARM.split("  schedule:", 1)[0],
            re.MULTILINE,
        )
        for path in (
            ".github/workflows/build-linux-sanitizers.yml",
            ".github/actions/build-linux-sanitizers/action.yml",
            ".github/scripts/verify-sanitizer-instrumentation.py",
            "CMakeLists.txt",
            "CMake/Sources.cmake",
            "tests/CMakeLists.txt",
            "tests/unit/CMakeLists.txt",
        ):
            with self.subTest(path=path):
                self.assertTrue(any(fnmatch.fnmatchcase(path, pattern) for pattern in push_filters))
                outputs = self.select_warmers("push", paths=[path])
                self.assertEqual(outputs["linux_asan_ubsan"], "true")
                self.assertEqual(outputs["image_set"], "none")

    def test_asan_warmers_share_configuration_and_serialize(self):
        standalone = job("warm-linux-asan-ubsan-ccache", WARM)
        published = job("warm-asan-ubsan-ccache")
        for warmer in (standalone, published):
            with self.subTest(warmer=warmer):
                for required in (
                    "github.ref == 'refs/heads/develop'",
                    "uses: ./.github/workflows/build-linux-sanitizers.yml",
                    "sanitizer: asan-ubsan",
                    "run_tests: false",
                    "recache: false",
                    "save_ccache: true",
                    "allow_stale_image: false",
                    "group: asan-ubsan-ccache-develop",
                    "cancel-in-progress: false",
                    "contents: read",
                    "packages: read",
                ):
                    self.assertIn(required, warmer)
                self.assertNotIn("packages: write", warmer)
        self.assertIn("needs.detect.outputs.linux_asan_ubsan == 'true'", standalone)
        self.assertIn("needs.detect.outputs.image_set != 'normal'", standalone)
        self.assertIn("needs.detect.outputs.image_set != 'normal-and-tsan'", standalone)
        self.assertIn("- warm-linux-asan-ubsan-ccache", job("summary", WARM))

    def test_asan_warming_follows_internal_promotion_without_gating_it(self):
        warm = job("warm-asan-ubsan-ccache")
        self.assertIn("metadata", warm)
        self.assertIn("promote", warm)
        self.assertIn("needs.promote.result == 'success'", warm)
        self.assertIn("inputs.publication_target == 'internal'", warm)
        self.assertIn("inputs.mode == 'refresh'", warm)
        self.assertIn("inputs.promote", warm)
        self.assertIn("(inputs.image_set == 'normal' || inputs.image_set == 'normal-and-tsan')", warm)
        self.assertIn("image_tag: ${{ needs.metadata.outputs.build_tag }}", warm)
        self.assertNotIn("warm-asan-ubsan-ccache", job("promote"))

    def test_public_release_publication_is_tag_scoped_and_complete(self):
        validate = job("validate-inputs")
        self.assertIn('[ "$REQUEST_REPOSITORY" != WSJTX/wsjtx ]', validate)
        self.assertIn("^refs/tags/v[0-9]+\\.[0-9]+\\.[0-9]+", validate)
        self.assertIn('[ "$PROMOTE" != false ]', validate)
        self.assertIn('[ "$IMAGE_SET" != normal ]', validate)
        self.assertIn('[ "$INCLUDE_ARMHF" != true ]', validate)

    def test_public_release_images_use_release_tag_and_public_package_root(self):
        metadata = job("metadata")
        public_block = re.search(
            r'if \[ "\$PUBLICATION_TARGET" = public-release \]; then(?P<body>.*?)else',
            metadata,
            re.DOTALL,
        ).group("body")
        self.assertIn(
            'release-${GITHUB_REF_NAME}-${GITHUB_SHA}-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}',
            public_block,
        )
        self.assertIn("package_root=ghcr.io/wsjtx/wsjtx", public_block)
        self.assertNotIn(":stable", public_block)
        self.assertIn("type=gha", public_block)
        self.assertEqual(
            PUBLISH.count(
                "labels: ${{ inputs.publication_target == 'public-release' && 'org.opencontainers.image.source=https://github.com/WSJTX/wsjtx' || '' }}"
            ),
            7,
        )

    def test_reusable_publisher_exports_all_release_image_digests(self):
        self.assertIn("x86_64_digest:", PUBLISH)
        self.assertIn("value: ${{ jobs.build-normal.outputs.digest }}", PUBLISH)
        self.assertIn("aarch64_digest:", PUBLISH)
        self.assertIn("value: ${{ jobs.build-arm64.outputs.digest }}", PUBLISH)
        self.assertIn("armhf_digest:", PUBLISH)
        self.assertIn("value: ${{ jobs.build-armhf-runtime.outputs.digest }}", PUBLISH)
        self.assertIn("armhf_cross_digest:", PUBLISH)
        self.assertIn("value: ${{ jobs.build-armhf-cross.outputs.digest }}", PUBLISH)
        self.assertIn("armhf_trixie_digest:", PUBLISH)
        self.assertIn("value: ${{ jobs.build-armhf-trixie-runtime.outputs.digest }}", PUBLISH)
        self.assertIn("armhf_trixie_cross_digest:", PUBLISH)
        self.assertIn("value: ${{ jobs.build-armhf-trixie-cross.outputs.digest }}", PUBLISH)

    def test_public_release_builds_require_and_consume_digest_references(self):
        self.assertIn("^sha256:[0-9a-f]{64}$", BUILD)
        self.assertIn("public-release images require an immutable sha256 digest", BUILD)
        self.assertIn(
            'image_reference=${package_root}/${package}@${IMAGE_DIGEST}', BUILD
        )
        self.assertIn(
            'cross_image_reference=${package_root}/${cross_package}@${ARMHF_CROSS_IMAGE_DIGEST}',
            BUILD,
        )
        self.assertEqual(
            BUILD.count("image: ${{ needs.resolve-image.outputs.image_reference }}"), 2
        )
        self.assertIn(
            "CROSS_BUILDER_IMAGE: ${{ needs.resolve-image.outputs.cross_image_reference }}",
            BUILD,
        )
        self.assertIn(
            "RUNTIME_IMAGE: ${{ needs.resolve-image.outputs.image_reference }}", BUILD
        )

    def test_manual_target_mapping_keeps_tsan_independent(self):
        self.assertRegex(WARM, r"linux\|linux-images\)\n\s+normal_images=true")
        self.assertRegex(WARM, r"linux-tsan\)\n\s+tsan_images=true")
        all_case = re.search(r"all\)\n(?P<body>.*?);;", WARM, re.DOTALL).group("body")
        self.assertIn("enable_all", all_case)
        self.assertNotIn("tsan_images=true", all_case)
        self.assertIn("image_set: ${{ needs.detect.outputs.image_set }}", WARM)
        self.assertRegex(
            WARM,
            r'if \[ "\$SCHEDULE" = "17 9 1 \* \*" \][\s\S]*?normal_images=true\s+tsan_images=true',
        )

    def test_push_selection_uses_tested_profile_classifier(self):
        self.assertIn('is-linux-ci-image-input.sh --classify "$path"', WARM)
        self.assertIn("expect_image_set normal ", CLASSIFIER)
        self.assertIn("expect_image_set tsan ", CLASSIFIER)
        self.assertIn("expect_image_set normal-and-tsan ", CLASSIFIER)
        self.assertIn('expect_image_set none "CMake/Sources.cmake"', CLASSIFIER)
        self.assertIn("armhf-cross-bookworm", CLASSIFIER)
        self.assertIn("armhf-runtime-bookworm", CLASSIFIER)
        self.assertIn('include_armhf: true', WARM)

    def test_candidate_warm_uses_exact_tag_and_publishes_without_recache(self):
        warm = job("warm-normal-ccache")
        self.assertIn("- build-normal", warm)
        self.assertIn("uses: ./.github/workflows/build-linux.yml", warm)
        self.assertIn("image_tag: ${{ needs.metadata.outputs.build_tag }}", warm)
        self.assertIn("allow_stale_image: false", warm)
        self.assertIn("recache: false", warm)
        self.assertIn("save_ccache: true", warm)

        promote = job("promote")
        self.assertIn("needs.warm-normal-ccache.result == 'success'", promote)
        self.assertIn("- warm-normal-ccache", promote)

    def test_metadata_loads_only_the_selected_image_cohort(self):
        metadata = job("metadata")
        self.assertIn('if [ "$IMAGE_SET" != tsan ]; then', metadata)
        self.assertIn(". .github/scripts/linux-ci-image-config.sh", metadata)
        self.assertIn("linux-ci-image-fingerprint.sh armhf-cross-bookworm", metadata)
        self.assertIn("linux-ci-image-fingerprint.sh armhf-runtime-bookworm", metadata)
        self.assertIn('if [ "$IMAGE_SET" != normal ]; then', metadata)
        self.assertIn(". .github/scripts/tsan-linux-deps-config.sh", metadata)

    def test_armhf_images_are_built_and_promoted_as_one_generation(self):
        cross = job("build-armhf-cross")
        runtime = job("build-armhf-runtime")
        promote = job("promote")

        self.assertIn("platforms: linux/amd64", cross)
        self.assertIn("Dockerfile.armhf-cross", cross)
        self.assertIn("/usr/local/lib/android /usr/share/dotnet /opt/ghc", cross)
        self.assertNotIn("/usr/local/lib/android", job("build-normal"))
        self.assertIn("- build-armhf-cross", runtime)
        self.assertIn("platforms: linux/arm/v7", runtime)
        self.assertIn("Dockerfile.armhf-runtime", runtime)
        self.assertIn(
            "CROSS_BUILDER_IMAGE=${{ needs.metadata.outputs.package_root }}/"
            "linux-armhf-cross-bookworm:${{ needs.metadata.outputs.build_tag }}",
            runtime,
        )
        self.assertIn("linux-armhf-cross-bookworm:${VALIDATED_TAG}", promote)
        self.assertIn("linux-armv7-bookworm:${VALIDATED_TAG}", promote)
        self.assertIn("linux-armhf-cross-bookworm:stable", promote)
        self.assertIn("linux-armv7-bookworm:stable", promote)

    def test_publication_mutations_require_develop(self):
        validate = job("validate-inputs")
        self.assertIn('[ "$MODE" = rollback ] || [ "$PROMOTE" = true ]', validate)
        self.assertIn('[ "$REQUEST_REF" != refs/heads/develop ]', validate)

    def test_tsan_only_jobs_and_tag_moves_exclude_normal_images(self):
        validate = job("validate-inputs")
        self.assertIn("normal|tsan|normal-and-tsan", validate)
        self.assertIn('[ "$IMAGE_SET" = tsan ] && [ "$INCLUDE_ARMHF" = true ]', validate)
        self.assertIn("inputs.image_set == 'tsan'", job("build-tsan"))
        self.assertIn("inputs.image_set == 'normal'", job("build-normal"))
        self.assertIn('if [ "$IMAGE_SET" != tsan ]; then', job("promote"))
        self.assertIn('if [ "$IMAGE_SET" != normal ]; then', job("promote"))
        self.assertIn('if [ "$IMAGE_SET" != tsan ]; then', job("rollback"))
        self.assertIn('if [ "$IMAGE_SET" != normal ]; then', job("rollback"))

    def test_rollback_uses_only_explicitly_selected_packages(self):
        rollback = job("rollback")
        self.assertNotIn("tsan_available", rollback)
        self.assertNotIn("armhf_available", rollback)
        self.assertIn("packages+=(linux-noble linux-arm64-bookworm)", rollback)
        self.assertIn(
            "packages+=(linux-armhf-cross-bookworm linux-armv7-bookworm linux-armhf-cross-trixie linux-armv7-trixie)",
            rollback,
        )
        self.assertIn("packages+=(linux-tsan-noble)", rollback)
        self.assertIn("stable_packages+=(linux-noble)", rollback)

    def test_trixie_pair_and_both_cache_warmers_are_distro_specific(self):
        cross = job("build-armhf-trixie-cross")
        runtime = job("build-armhf-trixie-runtime")
        self.assertIn("Dockerfile.armhf-cross-trixie", cross)
        self.assertIn("linux-armhf-cross-trixie:buildcache", cross)
        self.assertIn("- build-armhf-trixie-cross", runtime)
        self.assertIn("ARMHF_DISTRO=trixie", runtime)
        self.assertIn("linux-armhf-cross-trixie:${{ needs.metadata.outputs.build_tag }}", runtime)
        for warmer in (job("warm-armhf-ccache"), job("warm-linux-armhf-ccache", WARM)):
            self.assertIn("distro: [bookworm, trixie]", warmer)
            self.assertIn("armhf_distro: ${{ matrix.distro }}", warmer)
            self.assertIn("group: armhf-ccache-develop-${{ matrix.distro }}", warmer)
        promote = job("promote")
        for member in ("cross", "runtime"):
            self.assertIn(f"needs.build-armhf-trixie-{member}.result == 'success'", promote)
        self.assertLess(promote.index("imagetools inspect"), promote.index("imagetools create"))
        rollback = job("rollback")
        self.assertLess(rollback.index("imagetools inspect"), rollback.index("imagetools create"))

    def test_preparation_uses_the_selected_sources_own_schema_contract(self):
        preparation = (ROOT / ".github/workflows/prepare-release-dependencies.yml").read_text()
        self.assertIn("working-directory: source", preparation)
        self.assertIn("python3 .github/scripts/release-linux-images.py prepare", preparation)
        self.assertNotIn("python3 ../.github/scripts/release-linux-images.py prepare", preparation)


    def test_release_paths_consume_the_same_committed_selection(self):
        candidate = (ROOT / ".github/workflows/release.yml").read_text()
        public = (ROOT / ".github/workflows/public-release.yml").read_text()
        tagging = (ROOT / ".github/workflows/release-tag-helper.yml").read_text()
        promotion = (ROOT / ".github/workflows/promote-release.yml").read_text()
        for workflow in (candidate, public):
            self.assertIn("release-linux-images.py validate release-linux-images.json", workflow)
            self.assertIn('--check-remote --github-output "$GITHUB_OUTPUT"', workflow)
            self.assertEqual(workflow.count("image_source: public-release"), 3)
            for output in ("x86_64_digest", "aarch64_digest", "armhf_digest", "armhf_cross_digest"):
                self.assertIn("needs.prepare.outputs." + output, workflow)
            self.assertNotIn("allow_stale_image: true", workflow)
            self.assertNotIn("resolve-linux-ci-image-tag.py", workflow)
        self.assertNotIn("publish-linux-ci-images.yml", public)
        self.assertNotIn("packages: write", public)
        self.assertIn(
            "environment: ${{ needs.prepare.outputs.release_channel == 'BETA' && 'beta-release' || 'public-release' }}",
            public,
        )
        self.assertIn("linux_images: $linux_images", candidate)
        self.assertIn("release-linux-images.py provenance release-linux-images.json", candidate)
        self.assertIn("release-linux-images.py verify-provenance", promotion)
        self.assertLess(tagging.index("release-linux-images.py validate"), tagging.index("- name: Create candidate tag"))

    def test_prepared_image_consumption_keeps_source_and_recipe_guards(self):
        self.assertIn("WSJTX/wsjtx:refs/tags/v*|WSJTX/wsjtx-internal:refs/heads/release/*", BUILD)
        self.assertIn('if [ "$ALLOW_STALE_IMAGE" = true ]; then', BUILD)
        self.assertIn("Prepared public images require strict recipe verification", BUILD)


if __name__ == "__main__":
    unittest.main()
