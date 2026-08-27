import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
PUBLISH = (ROOT / ".github/workflows/publish-linux-ci-images.yml").read_text()
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
        self.assertIn('if [ "$IMAGE_SET" != normal ]; then', metadata)
        self.assertIn(". .github/scripts/tsan-linux-deps-config.sh", metadata)

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
        self.assertIn("packages+=(linux-tsan-noble)", rollback)
        self.assertIn("stable_packages+=(linux-noble)", rollback)


if __name__ == "__main__":
    unittest.main()
