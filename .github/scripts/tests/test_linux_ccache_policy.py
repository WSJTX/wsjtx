import pathlib
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]


class LinuxCcachePolicyTests(unittest.TestCase):
    def read(self, relative_path):
        return (REPOSITORY / relative_path).read_text(encoding="utf-8")

    def assert_cache_tiers(self, text, prefix):
        primary = (
            f"{prefix}${{{{ steps.image.outputs.ccache_compatibility_id }}}}-"
            "${{ steps.image.outputs.generation }}-${{ github.sha }}"
            "${{ steps.ccache-key.outputs.suffix }}"
        )
        generation = (
            f"{prefix}${{{{ steps.image.outputs.ccache_compatibility_id }}}}-"
            "${{ steps.image.outputs.generation }}-"
        )
        compatible = (
            f"{prefix}${{{{ steps.image.outputs.ccache_compatibility_id }}}}-"
        )
        self.assertIn(f"key: {primary}", text)
        generation_index = text.index(f"          {generation}")
        compatible_index = text.index(f"          {compatible}", generation_index + 1)
        self.assertLess(generation_index, compatible_index)

    def test_normal_cache_prefers_current_image_generation(self):
        action = self.read(".github/actions/build-linux-payload/action.yml")
        self.assert_cache_tiers(action, "ccache-linux-${{ inputs.arch }}-")
        self.assertIn(
            'export CCACHE_COMPILERCHECK="${{ steps.image.outputs.ccache_compiler_check }}"',
            action,
        )
        self.assertIn('if: inputs.runtime_base == \'host\'', action)
        self.assertIn('echo "CC=gcc"', action)
        self.assertIn('echo "CXX=g++"', action)
        self.assertIn(
            "if: inputs.save_ccache == 'true' && steps.image.outputs.recipe_match == 'true' && steps.ccache.outputs.cache-hit != 'true'",
            action,
        )

    def test_sanitizer_cache_keeps_profile_and_generation(self):
        action = self.read(".github/actions/build-linux-sanitizers/action.yml")
        self.assert_cache_tiers(
            action,
            "ccache-linux-x86_64-${{ steps.sanitizer.outputs.cache_key }}-",
        )
        self.assertIn(
            'export CCACHE_COMPILERCHECK="${{ steps.image.outputs.ccache_compiler_check }}"',
            action,
        )
        self.assertIn(
            "if: inputs.save_ccache == 'true' && steps.image.outputs.recipe_match == 'true' && steps.ccache.outputs.cache-hit != 'true'",
            action,
        )

    def test_armhf_verifies_and_forwards_cache_identity(self):
        workflow = self.read(".github/workflows/build-linux.yml")
        self.assert_cache_tiers(workflow, "ccache-linux-armhf-")
        self.assertIn("verify-linux-ci-image.sh normal armhf", workflow)
        self.assertIn("HAMLIB_BRANCH: ${{ inputs.hamlib_branch }}", workflow)
        self.assertIn("CCACHE_COMPILERCHECK: ${{ steps.image.outputs.ccache_compiler_check }}", workflow)
        self.assertIn("-e CCACHE_COMPILERCHECK", workflow)
        self.assertIn("-e WSJTX_CI_IMAGE_ALLOW_RECIPE_MISMATCH", workflow)
        self.assertIn(
            "if: inputs.save_ccache && steps.image.outputs.recipe_match == 'true' && steps.ccache.outputs.cache-hit != 'true'",
            workflow,
        )

    def test_release_accepts_separately_promoted_armhf_recipe(self):
        workflow = self.read(".github/workflows/release.yml")
        armhf_job = workflow.split("  linux-armhf:\n", 1)[1].split(
            "\n  windows:\n", 1
        )[0]
        self.assertIn("allow_stale_image: true", armhf_job)


if __name__ == "__main__":
    unittest.main()
