import pathlib
import re
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[3]


class LinuxCcachePolicyTests(unittest.TestCase):
    def read(self, relative_path):
        return (REPOSITORY / relative_path).read_text(encoding="utf-8")

    def sanitizer_step(self, name):
        action = self.read(".github/actions/build-linux-sanitizers/action.yml")
        match = re.search(
            rf"^    - name: {re.escape(name)}\n(?P<body>.*?)(?=^    - |\Z)",
            action,
            re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(match, name)
        return match.group("body")

    def armhf_job(self, name):
        workflow = self.read(".github/workflows/build-linux.yml")
        body = workflow.split(f"\n  {name}:\n", 1)[1]
        following = re.search(r"\n  [A-Za-z0-9_-]+:\n", body)
        return body[:following.start() + 1] if following else body

    def armhf_step(self, name, job="build-armhf"):
        workflow = self.armhf_job(job)
        match = re.search(
            rf"^      - name: {re.escape(name)}\n(?P<body>.*?)(?=^      - |\Z)",
            workflow,
            re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(match, name)
        return match.group("body")

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

    def test_sanitizer_save_requires_trusted_successful_build(self):
        action = self.read(".github/actions/build-linux-sanitizers/action.yml")
        save = self.sanitizer_step("Save sanitizer objects")
        for condition in (
            "inputs.save_ccache == 'true'",
            "github.ref == 'refs/heads/develop'",
            "steps.image.outputs.recipe_match == 'true'",
            "steps.ccache.outputs.cache-hit != 'true'",
        ):
            self.assertIn(condition, save)
        self.assertNotIn("always()", save)
        self.assertNotIn("!cancelled()", save)
        self.assertIn("key: ${{ steps.ccache.outputs.cache-primary-key }}", save)
        self.assertIn("path: ${{ steps.sanitizer.outputs.ccache_dir }}", save)
        self.assertLess(
            action.index("- name: Require sanitizer tests to pass"),
            action.index("- name: Save sanitizer objects"),
        )

    def test_sanitizer_tests_remain_enabled_by_default(self):
        workflow = self.read(".github/workflows/build-linux-sanitizers.yml")
        action = self.read(".github/actions/build-linux-sanitizers/action.yml")
        self.assertRegex(
            workflow,
            r"      run_tests:\n(?:        .*\n)*?        type: boolean\n        default: true",
        )
        self.assertRegex(
            action,
            r'  run_tests:\n(?:    .*\n)*?    default: "true"',
        )
        self.assertIn("run_tests: ${{ inputs.run_tests }}", workflow)
        self.assertIn("inputs.sanitizer == 'tsan' && 180 || 60", workflow)

    def test_build_only_sanitizer_keeps_test_targets_and_instrumentation(self):
        configure = self.sanitizer_step("Configure sanitizer build")
        self.assertIn("-DWSJT_ENABLE_TESTS=ON", configure)
        self.assertIn("-DWSJT_ENABLE_GUI_SMOKE_TESTS=ON", configure)
        for name in (
            "Configure sanitizer build",
            "Build sanitizer targets",
            "Verify sanitizer instrumentation",
        ):
            self.assertNotIn("inputs.run_tests", self.sanitizer_step(name))
        for name in (
            "Prepare test scratch directory",
            "Run tests",
            "Publish test summary",
            "Upload test results",
            "Require sanitizer tests to pass",
        ):
            with self.subTest(step=name):
                self.assertIn("inputs.run_tests == 'true'", self.sanitizer_step(name))
        enforce = self.sanitizer_step("Require sanitizer tests to pass")
        self.assertIn("always()", enforce)
        self.assertIn('steps.ctest.outcome }}" != "success"', enforce)
        self.assertIn("exit 1", enforce)

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

    def test_armhf_verifies_and_forwards_cache_identity(self):
        workflow = self.read(".github/workflows/build-linux.yml")
        self.assert_cache_tiers(workflow, "ccache-linux-armhf-${{ inputs.armhf_distro }}-")
        self.assertIn(
            "verify-armhf-ci-image.sh cross-builder armhf", workflow
        )
        self.assertIn("verify-armhf-ci-image.sh runtime armhf", workflow)
        self.assertIn(
            "CROSS_BUILDER_IMAGE: ${{ needs.resolve-image.outputs.cross_image_reference }}",
            workflow,
        )
        self.assertEqual(
            workflow.count(
                "RUNTIME_IMAGE: ${{ needs.resolve-image.outputs.image_reference }}"
            ),
            4,
        )
        self.assertIn("build-linux-armhf-cross.sh", workflow)
        self.assertIn("validate-linux-armhf-runtime.sh", workflow)
        self.assertIn("HAMLIB_BRANCH: ${{ inputs.hamlib_branch }}", workflow)
        self.assertIn("CCACHE_COMPILERCHECK: ${{ steps.image.outputs.ccache_compiler_check }}", workflow)
        self.assertIn("-e CCACHE_COMPILERCHECK", workflow)
        self.assertIn("-e WSJTX_CI_IMAGE_ALLOW_RECIPE_MISMATCH", workflow)
        self.assertIn("cross_recipe_match", workflow)
        self.assertIn("runtime_recipe_match", workflow)
        self.assertIn(
            "if: inputs.save_ccache && (!inputs.armhf_build_only || github.ref == 'refs/heads/develop') && steps.image.outputs.recipe_match == 'true' && steps.ccache.outputs.cache-hit != 'true'",
            workflow,
        )

    def test_armhf_build_only_preserves_compilation_audits_and_pair_verification(self):
        workflow = self.read(".github/workflows/build-linux.yml")
        self.assertEqual(len(re.findall(
            r"      armhf_build_only:\n(?:        .*\n)*?        type: boolean\n        default: false",
            workflow,
        )), 2)
        self.assertIn('if [ "$ARMHF_BUILD_ONLY" = true ] && [ "$ARCH" != armhf ]; then', workflow)
        for name in ("Inspect ARMHF image pair", "Cross-build ARMHF binaries"):
            self.assertNotIn("if:", self.armhf_step(name))
        compile_step = self.armhf_step("Cross-build ARMHF binaries")
        self.assertIn("ARMHF_BUILD_ONLY: ${{ inputs.armhf_build_only }}", compile_step)
        self.assertIn("-e ARMHF_BUILD_ONLY", compile_step)
        script = self.read(".github/scripts/build-linux-armhf-cross.sh")
        self.assertIn("-DWSJT_ENABLE_TESTS=ON", script)
        skip_install = script.index('if [ "$ARMHF_BUILD_ONLY" = true ]; then', script.index("cmake --build"))
        for audit in ("validate_linux_build_executables", ".github/scripts/audit-armhf-cross-build.sh"):
            self.assertLess(script.index(audit), skip_install)
        self.assertLess(skip_install, script.index("cmake --install"))
        self.assertIn("exit 0", script[skip_install:script.index("cmake --install")])

    def test_armhf_build_only_skips_runtime_and_artifact_steps(self):
        test_job = self.armhf_job("test-armhf")
        self.assertIn("    if: inputs.arch == 'armhf' && !inputs.armhf_build_only\n", test_job)
        for name in (
            "Test and package on native ARMv7", "Publish armhf test summary",
            "Upload armhf AppImage startup diagnostics", "Upload .deb", "Upload RPM",
            "Upload AppImage", "Upload build artifacts", "Upload test results",
            "Upload release tarballs",
        ):
            with self.subTest(step=name):
                self.assertIn(f"      - name: {name}\n", test_job)
                self.assertNotIn(f"      - name: {name}\n", self.armhf_job("build-armhf"))
        for name in ("Pack ARMHF build tree", "Upload ARMHF build tree"):
            with self.subTest(step=name):
                self.assertIn("if: ${{ !inputs.armhf_build_only }}", self.armhf_step(name))

    def test_armhf_runtime_runs_natively_on_an_arm_runner_from_the_cross_build_tree(self):
        build_job = self.armhf_job("build-armhf")
        test_job = self.armhf_job("test-armhf")
        self.assertIn("    runs-on: ubuntu-24.04\n", build_job)
        self.assertIn("    runs-on: ubuntu-24.04-arm\n", test_job)
        self.assertIn("    needs: [resolve-image, build-armhf]\n", test_job)
        self.assertNotIn("setup-qemu-action", test_job)
        self.assertNotIn("validate-linux-armhf-runtime.sh", build_job)
        self.assertIn("bash .github/scripts/validate-linux-armhf-runtime.sh", self.armhf_step("Test and package on native ARMv7", "test-armhf"))
        pack = self.armhf_step("Pack ARMHF build tree")
        self.assertIn(
            "sudo tar --create --file armhf-build-tree-${{ inputs.armhf_distro }}.tar --exclude='wsjtx-build/*.o' --exclude='wsjtx-build/*.a' wsjtx-build AppDir\n",
            pack,
        )
        self.assertLess(build_job.index("- name: Save armhf ccache"), build_job.index("- name: Pack ARMHF build tree"))
        for step in ("actions/checkout@", "docker/login-action@"):
            self.assertIn(step, test_job)
        self.assertLess(test_job.index("docker/login-action@"), test_job.index("- name: Require native 32-bit ARM execution"))
        self.assertLess(test_job.index("- name: Require native 32-bit ARM execution"), test_job.index("- name: Download ARMHF build tree"))
        preflight = self.armhf_step("Require native 32-bit ARM execution", "test-armhf")
        self.assertIn("grep -Eq '^CPU op-mode\\(s\\):.*32-bit'", preflight)
        self.assertIn("/proc/sys/fs/binfmt_misc/qemu-arm", preflight)
        self.assertIn('docker run --rm --platform linux/arm/v7 "$RUNTIME_IMAGE" getconf LONG_BIT', preflight)
        runtime_step = self.armhf_step("Test and package on native ARMv7", "test-armhf")
        for fragment in ("docker run --rm --platform linux/arm/v7", '-v "${GITHUB_WORKSPACE}:/work"', "-w /work", "-e ARMHF_RUNTIME_PHASE"):
            self.assertIn(fragment, runtime_step)
        for name in ("Publish armhf test summary", "Upload test results"):
            self.assertIn("        if: always()\n", self.armhf_step(name, "test-armhf"))
        for name, artifact in (
            ("Upload .deb", "wsjtx-${{ inputs.version }}-linux-armhf-${{ inputs.armhf_distro }}-deb"),
            ("Upload RPM", "wsjtx-${{ inputs.version }}-linux-armhf-${{ inputs.armhf_distro }}-rpm"),
            ("Upload AppImage", "wsjtx-${{ inputs.version }}-linux-armhf-${{ inputs.armhf_distro }}-AppImage"),
        ):
            self.assertIn(f"          name: {artifact}\n", self.armhf_step(name, "test-armhf"))
        self.assertIn("name: armhf-build-tree-${{ inputs.armhf_distro }}\n", self.armhf_step("Upload ARMHF build tree"))
        self.assertIn("name: armhf-build-tree-${{ inputs.armhf_distro }}\n", self.armhf_step("Download ARMHF build tree", "test-armhf"))
        self.assertIn("tar --extract --file armhf-build-tree-${{ inputs.armhf_distro }}.tar", self.armhf_step("Unpack ARMHF build tree", "test-armhf"))
        self.assertLess(test_job.index("- name: Unpack ARMHF build tree"), test_job.index("- name: Test and package on native ARMv7"))
        runtime = self.read(".github/scripts/validate-linux-armhf-runtime.sh")
        self.assertIn("ctest --parallel 3 --output-on-failure", runtime)
        self.assertNotIn("--exclude-regex", runtime)

    def test_clean_deb_install_gates_uploads_without_ci_libraries(self):
        runtime = self.armhf_job("test-armhf")
        install = self.armhf_step("Install and start the Debian package in a clean target OS", "test-armhf")
        self.assertIn('"arm32v7/debian:${ARMHF_DISTRO}"', install)
        self.assertIn('bash /validate.sh /packages/wsjtx.deb "$ARMHF_DISTRO" /logs', install)
        self.assertIn('mkdir -p deb-install-check', install)
        self.assertIn('${GITHUB_WORKSPACE}/deb-install-check:/logs', install)
        self.assertNotIn('wsjtx-build/deb-install-check', runtime)
        mounts = [line.strip() for line in install.splitlines() if line.strip().startswith("-v ")]
        self.assertEqual(len(mounts), 3)
        for destination in ("/packages/wsjtx.deb:ro", "/validate.sh:ro", "/logs"):
            self.assertTrue(any(destination in mount for mount in mounts))
        self.assertNotIn("LD_LIBRARY_PATH", install)
        self.assertNotIn("continue-on-error", install)
        for upload in ("Upload .deb", "Upload RPM", "Upload AppImage", "Upload release tarballs"):
            self.assertLess(runtime.index("id: deb-install"), runtime.index(f"- name: {upload}"))
            self.assertIn("if: inputs.build_packages", self.armhf_step(upload, "test-armhf"))
            self.assertNotIn("always()", self.armhf_step(upload, "test-armhf"))

    def test_armhf_build_only_save_requires_trusted_successful_audits(self):
        save = self.armhf_step("Save armhf ccache")
        self.assertIn("(!inputs.armhf_build_only || github.ref == 'refs/heads/develop')", save)
        self.assertIn("inputs.save_ccache", save)
        self.assertIn("steps.image.outputs.recipe_match == 'true'", save)
        self.assertIn("steps.ccache.outputs.cache-hit != 'true'", save)
        self.assertIn("key: ${{ steps.ccache.outputs.cache-primary-key }}", save)
        self.assertNotIn("always()", save)
        self.assertNotIn("!cancelled()", save)
        build_job = self.armhf_job("build-armhf")
        self.assertLess(build_job.index("- name: Cross-build ARMHF binaries"), build_job.index("- name: Save armhf ccache"))

    def test_release_requires_pinned_armhf_pair_without_stale_recipe_fallback(self):
        workflow = self.read(".github/workflows/release.yml")
        armhf_job = workflow.split("  linux-armhf:\n", 1)[1].split(
            "\n  windows:\n", 1
        )[0]
        self.assertNotIn("allow_stale_image: true", armhf_job)
        self.assertIn("image_source: public-release", armhf_job)
        self.assertIn("image_digest: ${{ matrix.armhf_distro == 'trixie' && needs.prepare.outputs.armhf_trixie_digest || needs.prepare.outputs.armhf_digest }}", armhf_job)
        self.assertIn("armhf_cross_image_digest: ${{ matrix.armhf_distro == 'trixie' && needs.prepare.outputs.armhf_trixie_cross_digest || needs.prepare.outputs.armhf_cross_digest }}", armhf_job)


if __name__ == "__main__":
    unittest.main()
