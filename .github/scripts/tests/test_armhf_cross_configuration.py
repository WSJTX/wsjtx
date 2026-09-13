import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]


class ArmhfCrossConfigurationTests(unittest.TestCase):
    def read(self, relative_path):
        return (ROOT / relative_path).read_text(encoding="utf-8")

    def test_cross_toolchain_pins_armv7_hard_float_and_bookworm_abi(self):
        config = self.read(".github/images/linux-ci/armhf-cross-toolchain.config")
        for setting in (
            "CT_ARCH_ARM=y",
            "CT_ARCH_ARM_MODE_THUMB=y",
            'CT_ARCH_ARCH="armv7-a+fp"',
            "CT_ARCH_FLOAT_HW=y",
            "CT_LINUX_V_6_1=y",
            "CT_BINUTILS_V_2_40=y",
            "CT_GLIBC_V_2_36=y",
            'CT_GLIBC_MIN_KERNEL_VERSION="3.2.0"',
            "CT_GCC_V_13=y",
            "CT_CC_GCC_LIBGOMP=y",
            "CT_CC_LANG_CXX=y",
            "CT_CC_LANG_FORTRAN=y",
            "CT_PARALLEL_JOBS=1",
            'CT_TARGET_CFLAGS="-g0"',
        ):
            self.assertIn(setting, config)
        self.assertNotIn("CT_CC_GCC_LIBQUADMATH=y", config)

        image_config = self.read(".github/scripts/armhf-ci-image-config.sh")
        self.assertIn("ARMHF_GCC_VERSION=13.4.0", image_config)
        self.assertIn("ARMHF_GLIBC_VERSION=2.36", image_config)
        self.assertIn("ARMHF_TARGET_TRIPLET=arm-linux-gnueabihf", image_config)

        verifier = self.read(".github/scripts/verify-armhf-ci-image.sh")
        audit = self.read(".github/scripts/audit-armhf-cross-build.sh")
        self.assertIn("reject_quadmath_dependency", verifier)
        self.assertIn("unexpectedly depends on libquadmath", audit)
        self.assertIn("reject_private_glibc_dependency", verifier)
        self.assertIn("private glibc symbol", audit)

    def test_armhf_cache_identity_parsing_fails_closed(self):
        workflow = self.read(".github/workflows/build-linux.yml")
        self.assertIn(
            '''ccache_compatibility_id=$(awk -F= '$1 == "cross_ccache_compatibility_id" { print $2 }' "$output_file")''',
            workflow,
        )
        self.assertIn(
            'if [ -z "$ccache_compatibility_id" ] || '
            '[ -z "$ccache_compiler_check" ]; then',
            workflow,
        )
        self.assertNotIn('\\"cross_ccache_compatibility_id\\"', workflow)

    def test_armhf_qemu_registration_uses_one_immutable_release(self):
        expected = (
            "image: tonistiigi/binfmt:qemu-v10.2.3-68@sha256:"
            "400a4873b838d1b89194d982c45e5fb3cda4593fbfd7e08a02e76b03b21166f0"
        )
        for path in (
            ".github/workflows/build-linux.yml",
            ".github/workflows/publish-linux-ci-images.yml",
        ):
            self.assertIn(expected, self.read(path))

    def test_cmake_separates_host_programs_from_target_dependencies(self):
        toolchain = self.read(".github/cmake/armhf-toolchain.cmake")
        self.assertIn("set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)", toolchain)
        self.assertIn("set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)", toolchain)
        self.assertIn("set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)", toolchain)
        self.assertIn("set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)", toolchain)
        self.assertIn("set(_wsjt_armhf_triplet arm-linux-gnueabihf)", toolchain)
        self.assertIn("${_wsjt_armhf_triplet}-gfortran", toolchain)
        self.assertIn("set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE armhf", toolchain)
        self.assertIn("set(CPACK_RPM_PACKAGE_ARCHITECTURE armv7l", toolchain)

        dependencies = self.read("CMake/Dependencies.cmake")
        self.assertIn("WSJT_QT_HOST_PATH", dependencies)
        self.assertIn("IMPORTED_LOCATION", dependencies)

    def test_builder_and_runtime_keep_distinct_native_architectures(self):
        builder = self.read(".github/images/linux-ci/Dockerfile.armhf-cross")
        runtime = self.read(".github/images/linux-ci/Dockerfile.armhf-runtime")
        self.assertTrue(builder.startswith("FROM debian:bookworm\n"))
        self.assertIn("--architectures=armhf", builder)
        self.assertIn("qemu-user-static", builder)
        self.assertIn("normalize-armhf-sysroot-symlinks.sh", builder)
        self.assertIn("tail -n 300 build.log", builder)
        self.assertIn("ARG CROSS_BUILDER_PLATFORM=linux/amd64", runtime)
        self.assertIn(
            "FROM --platform=${CROSS_BUILDER_PLATFORM} ${CROSS_BUILDER_IMAGE}",
            runtime,
        )
        self.assertIn("FROM arm32v7/debian:bookworm", runtime)
        self.assertIn("install-armhf-runtime-packages", runtime)
        self.assertIn(
            "COPY --from=cross_builder /usr/bin/qemu-arm-static", runtime
        )

        cross_build = self.read(".github/scripts/build-linux-armhf-cross.sh")
        runtime_package = self.read(".github/scripts/package-linux-armhf.sh")
        self.assertNotIn("qemu", cross_build.lower())
        self.assertIn("cmake --install wsjtx-build", cross_build)
        self.assertIn("cpack -G DEB", runtime_package)
        self.assertIn("cpack -G RPM", runtime_package)
        self.assertIn("CPACK_INSTALL_CMAKE_PROJECTS=", runtime_package)
        self.assertIn("CPACK_INSTALLED_DIRECTORIES=/work/AppDir;/", runtime_package)
        self.assertIn('"$ARMHF_QEMU_EXECUTABLE"', runtime_package)
        self.assertIn(
            'LINUX_APPIMAGE_RUNNER="$ARMHF_QEMU_EXECUTABLE"', runtime_package
        )
        self.assertIn("armhf-static-ldd.sh", runtime_package)
        self.assertIn('PATH="/work/wsjtx-build/appimage-tools:$PATH"', runtime_package)

    def test_hybrid_is_the_only_armhf_build_implementation(self):
        workflow = self.read(".github/workflows/build-linux.yml")
        native_action = self.read(
            ".github/actions/build-linux-payload/action.yml"
        )
        self.assertFalse(
            (ROOT / ".github/scripts/build-linux-payload.sh").exists()
        )
        self.assertNotIn("build-linux-payload.sh", workflow)
        self.assertEqual(workflow.count("build-linux-armhf-cross.sh"), 1)
        self.assertIn("docker run --rm --platform linux/amd64", workflow)
        self.assertNotIn("inputs.arch != 'armhf'", native_action)
        self.assertNotIn("linuxdeploy-armhf", native_action)

    def test_armhf_joins_only_the_opt_in_full_ci_matrix(self):
        workflow = self.read(".github/workflows/ci.yml")
        prepare = workflow.split("  prepare:\n", 1)[1].split("\n  macos:\n", 1)[0]
        armhf_job = workflow.split("  linux-armhf:\n", 1)[1].split(
            "\n  windows:\n", 1
        )[0]

        self.assertIn("RESOLVE_ARMHF=true", prepare)
        self.assertIn("inputs.armhf_image_tag", prepare)
        self.assertIn(
            "contains(github.event.pull_request.labels.*.name, 'full-ci')",
            armhf_job,
        )
        self.assertIn("github.event_name == 'workflow_dispatch'", armhf_job)
        self.assertIn('arch: \"armhf\"', armhf_job)
        self.assertIn(
            "image_tag: ${{ needs.prepare.outputs.armhf_image_tag }}",
            armhf_job,
        )

    def test_slow_decoder_timeouts_are_at_most_four_minutes(self):
        jtty = self.read("tests/unit/jtty/CMakeLists.txt")
        for test_name, timeout in (
            ("test_jtty_adjacent_decode", 180),
            ("test_jtty_structured_decode", 240),
            ("test_jtty_overlap_decode", 180),
            ("test_jtty_overlap_decode_480", 240),
            ("test_jtty_windowed_decode", 180),
            ("test_jtty_gap_merge", 180),
            ("test_jtty_surface_ranges", 180),
        ):
            properties = jtty.split(
                f"set_tests_properties ({test_name}", 1
            )[1].split(")", 1)[0]
            self.assertIn(f"TIMEOUT {timeout}", properties)

        q65 = self.read("tests/unit/q65/CMakeLists.txt")
        properties = q65.split(
            "set_tests_properties (test_q65_decode_pipeline", 1
        )[1].split(")", 1)[0]
        self.assertIn("TIMEOUT 180", properties)


if __name__ == "__main__":
    unittest.main()
