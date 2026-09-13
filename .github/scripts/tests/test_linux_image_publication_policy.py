import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
PUBLISH = (ROOT / ".github/workflows/publish-linux-ci-images.yml").read_text()
BUILD = (ROOT / ".github/workflows/build-linux.yml").read_text()
WARM = (ROOT / ".github/workflows/warm-dependency-caches.yml").read_text()
CLASSIFIER = (ROOT / ".github/scripts/is-linux-ci-image-input.sh").read_text()


def job(name: str) -> str:
    match = re.search(
        rf"^  {re.escape(name)}:\n(?P<body>.*?)(?=^  [a-z][a-z0-9-]*:\n|\Z)",
        PUBLISH,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise AssertionError(f"job not found: {name}")
    return match.group("body")


class LinuxImagePublicationPolicyTests(unittest.TestCase):
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
            5,
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
            "packages+=(linux-armhf-cross-bookworm linux-armv7-bookworm)",
            rollback,
        )
        self.assertIn("packages+=(linux-tsan-noble)", rollback)
        self.assertIn("stable_packages+=(linux-noble)", rollback)


if __name__ == "__main__":
    unittest.main()
