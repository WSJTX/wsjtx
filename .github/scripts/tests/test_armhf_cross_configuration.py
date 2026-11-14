import pathlib
import os
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]


class ArmhfCrossConfigurationTests(unittest.TestCase):
    def read(self, relative_path):
        return (ROOT / relative_path).read_text(encoding="utf-8")

    def test_cross_toolchain_preserves_bookworm_abi(self):
        image_config = self.read(".github/scripts/armhf-toolchain-config.sh")
        self.assertIn("ARMHF_GCC_VERSION=12", image_config)
        self.assertIn("ARMHF_GLIBC_VERSION=2.36", image_config)
        self.assertIn("ARMHF_MIN_KERNEL_VERSION=3.2.0", image_config)
        self.assertIn("ARMHF_TARGET_TRIPLET=arm-linux-gnueabihf", image_config)

        verifier = self.read(".github/scripts/verify-armhf-ci-image.sh")
        audit = self.read(".github/scripts/audit-armhf-cross-build.sh")
        self.assertIn("reject_quadmath_dependency", verifier)
        self.assertIn("validate_armhf_elf_tree", audit)
        self.assertIn("reject_private_glibc_dependency", verifier)
        self.assertIn('READELF="$readelf"', audit)

    def test_distro_profiles_select_compiler_and_time_abi(self):
        for distro, expected in (("bookworm", "12:2.36:32:"),
                                ("trixie", "14:2.41:64:-D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64")):
            with self.subTest(distro=distro):
                result = subprocess.run(
                    ["bash", "-c", '. .github/scripts/armhf-toolchain-config.sh; '
                     'printf "%s:%s:%s:%s" "$ARMHF_GCC_VERSION" "$ARMHF_GLIBC_VERSION" '
                     '"$ARMHF_TIME_BITS" "${ARMHF_ABI_FLAGS[*]}"'],
                    cwd=ROOT, env={**os.environ, "ARMHF_DISTRO": distro},
                    capture_output=True, text=True, check=True,
                )
                self.assertEqual(result.stdout, expected)
        result = subprocess.run(
            ["bash", "-c", '. .github/scripts/armhf-toolchain-config.sh'],
            cwd=ROOT, env={**os.environ, "ARMHF_DISTRO": "invalid"},
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unsupported ARMHF_DISTRO", result.stderr)

    def test_trixie_uses_native_gcc14_and_explicit_time64(self):
        builder = self.read(".github/images/linux-ci/Dockerfile.armhf-cross-trixie")
        self.assertIn("FROM debian:trixie", builder)
        self.assertIn("ARMHF_DISTRO=trixie", builder)
        for compiler in ("gcc", "g++", "gfortran"):
            self.assertIn(f"{compiler}-14-arm-linux-gnueabihf", builder)
        self.assertNotIn("crosstool-ng", builder)
        self.assertIn("install-armhf-sysroot", builder)
        smoke = self.read(".github/scripts/build-armhf-toolchain-smoke.sh")
        self.assertEqual(smoke.count("sizeof(time_t) * 8 == WSJT_TIME_BITS"), 2)
        self.assertIn("clock_gettime(CLOCK_REALTIME", smoke)
        self.assertIn("std::time(nullptr)", smoke)
        hamlib = self.read(".github/scripts/build-hamlib-armhf-cross.sh")
        self.assertEqual(hamlib.count("${ARMHF_ABI_FLAGS[*]}"), 2)
        toolchain = self.read(".github/cmake/armhf-toolchain.cmake")
        self.assertIn('WSJT_ARMHF_DISTRO STREQUAL "trixie"', toolchain)
        self.assertEqual(toolchain.count("-D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64"), 2)

    def test_cross_builders_install_foreign_architecture_probe(self):
        for name in ("Dockerfile.armhf-cross", "Dockerfile.armhf-cross-trixie"):
            with self.subTest(dockerfile=name):
                builder = self.read(f".github/images/linux-ci/{name}")
                image = builder.split("AS cross_builder", 1)[1]
                install = image.split("run-apt-get install", 1)[1].split("&&", 1)[0]
                self.assertIn("arch-test", install.split())
                self.assertIn("qemu-user-static", install.split())

    def test_runtime_package_names_match_each_distro_time_abi(self):
        installer = self.read(".github/images/linux-ci/install-armhf-runtime-packages.sh")
        selection = installer.split("/usr/local/bin/run-apt-get update", 1)[0]
        for distro, suffix in (("bookworm", ""), ("trixie", "t64")):
            with self.subTest(distro=distro):
                result = subprocess.run(
                    ["bash", "-c", selection + '\nprintf "%s\\n" "${packages[@]}"'],
                    cwd=ROOT, env={**os.environ, "ARMHF_DISTRO": distro},
                    capture_output=True, text=True, check=True,
                )
                packages = result.stdout.splitlines()
                self.assertFalse(any(name.startswith("libboost-") for name in packages))
                for name in ("widgets", "network", "printsupport", "test", "concurrent"):
                    self.assertIn(f"libqt5{name}5{suffix}", packages)

    def test_bookworm_uses_distribution_compilers_and_matching_target_runtime(self):
        builder = self.read(".github/images/linux-ci/Dockerfile.armhf-cross")
        for compiler in ("gcc", "g++", "gfortran"):
            self.assertIn(f"{compiler}-12-arm-linux-gnueabihf", builder)
        self.assertIn('arm-linux-gnueabihf-$tool-12', builder)
        self.assertNotIn("crosstool-ng", builder)
        self.assertNotIn("COPY --from=toolchain", builder)
        self.assertFalse((ROOT / ".github/images/linux-ci/armhf-cross-toolchain.config").exists())
        installer = self.read(".github/images/linux-ci/install-armhf-sysroot.sh")
        self.assertIn('source_path="$(armhf_sysroot_runtime_path "$library")"', installer)
        self.assertIn('-print-file-name=libquadmath.so.0', installer)
        verifier = self.read(".github/scripts/verify-armhf-ci-image.sh")
        self.assertIn('cmp -s "$ARMHF_RUNTIME_PREFIX/$library" "$(armhf_sysroot_runtime_path "$library")"', verifier)
        self.assertIn('OS: Linux, ABI: $ARMHF_MIN_KERNEL_VERSION', verifier)

    def test_compiler_policy_rejects_other_major_versions(self):
        for distro, accepted, rejected in (("bookworm", "12.2.0", "13.4.0"),
                                          ("trixie", "14.2.0", "12.2.0")):
            for version, expected in ((accepted, 0), (rejected, 1)):
                with self.subTest(distro=distro, version=version):
                    result = subprocess.run(
                        ["bash", "-c", '. .github/scripts/armhf-toolchain-config.sh; '
                         'armhf_gcc_version_matches "$1"', "bash", version],
                        cwd=ROOT, env={**os.environ, "ARMHF_DISTRO": distro},
                        capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, expected)

    def test_runtime_lookup_uses_only_target_sysroot(self):
        with tempfile.TemporaryDirectory() as temporary:
            sysroot = pathlib.Path(temporary)
            for directory in ("usr/lib", "lib"):
                path = sysroot / directory / "arm-linux-gnueabihf" / "libstdc++.so.6"
                path.parent.mkdir(parents=True)
                path.write_bytes(b"target runtime")
                result = subprocess.run(
                    ["bash", "-c", '. .github/scripts/armhf-ci-image-config.sh; '
                     'ARMHF_SYSROOT=$1; armhf_sysroot_runtime_path libstdc++.so.6',
                     "bash", str(sysroot)], cwd=ROOT, capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), str(path))
                path.unlink()
            result = subprocess.run(
                ["bash", "-c", '. .github/scripts/armhf-ci-image-config.sh; '
                 'ARMHF_SYSROOT=$1; armhf_sysroot_runtime_path libstdc++.so.6',
                 "bash", str(sysroot)], cwd=ROOT, capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("target runtime library is missing", result.stderr)

    def test_shared_toolchain_config_is_copied_to_both_image_roles(self):
        config = self.read(".github/scripts/armhf-ci-image-config.sh")
        self.assertIn('. "$_armhf_config_dir/armhf-toolchain-config.sh"', config)
        for role in ("cross", "runtime"):
            dockerfile = self.read(f".github/images/linux-ci/Dockerfile.armhf-{role}")
            self.assertIn(".github/scripts/armhf-toolchain-config.sh", dockerfile)

    def test_fftw_uses_the_same_cross_built_runtime_in_both_abi_profiles(self):
        installer = self.read(".github/images/linux-ci/install-armhf-sysroot.sh")
        self.assertIn('DESTDIR="$ARMHF_SYSROOT"', installer)
        self.assertIn('CFLAGS="$fftw_cflags"', installer)
        self.assertIn('fftw_cc="$ARMHF_TARGET_TRIPLET-gcc -B$ARMHF_SYSROOT/usr/lib/$ARMHF_TARGET_TRIPLET/"', installer)
        self.assertIn('FFTW_ARCH=armhf CC="$fftw_cc"', installer)
        self.assertIn('${ARMHF_ABI_FLAGS[*]} -O3', installer)
        self.assertIn('cp -L "$ARMHF_SYSROOT$LINUX_FFTW_PREFIX"/lib/libfftw3f*.so*', installer)
        self.assertIn('build-fftw-smoke-linux.sh', installer)
        build = self.read(".github/scripts/build-linux-armhf-cross.sh")
        self.assertIn('-DFFTW3_ROOT_DIR="$ARMHF_SYSROOT$LINUX_FFTW_PREFIX"', build)
        self.assertIn('-DWSJT_FFTW_VERSION="$FFTW_VERSION"', build)
        for dockerfile in ("Dockerfile.armhf-cross", "Dockerfile.armhf-cross-trixie",
                           "Dockerfile.armhf-runtime"):
            with self.subTest(dockerfile=dockerfile):
                recipe = self.read(f".github/images/linux-ci/{dockerfile}")
                self.assertIn(".github/scripts/fftw-version.sh", recipe)
                self.assertIn("tests/dependencies/test_fftw_runtime.c", recipe)

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
        self.assertIn("FROM debian:bookworm AS cross_builder\n", builder)
        sysroot = self.read(".github/images/linux-ci/install-armhf-sysroot.sh")
        self.assertIn("--architectures=armhf", sysroot)
        self.assertIn("qemu-user-static", builder)
        self.assertIn("normalize-armhf-sysroot-symlinks.sh", builder)
        self.assertIn("ARG CROSS_BUILDER_PLATFORM=linux/amd64", runtime)
        self.assertIn(
            "FROM --platform=${CROSS_BUILDER_PLATFORM} ${CROSS_BUILDER_IMAGE}",
            runtime,
        )
        self.assertIn("ARG ARMHF_DISTRO=bookworm", runtime)
        self.assertIn("FROM arm32v7/debian:${ARMHF_DISTRO}", runtime)
        self.assertIn("install-armhf-runtime-packages", runtime)
        self.assertIn(
            "COPY --from=cross_builder /usr/local/bin/qemu-arm-static", runtime
        )
        self.assertIn('cp -L /usr/bin/qemu-arm-static "$ARMHF_QEMU_EXECUTABLE"', sysroot)

        cross_build = self.read(".github/scripts/build-linux-armhf-cross.sh")
        runtime_package = self.read(".github/scripts/package-linux-armhf.sh")
        self.assertNotIn("qemu", cross_build.lower())
        self.assertIn("cmake --install wsjtx-build", cross_build)
        self.assertIn("cpack -G DEB", runtime_package)
        self.assertIn("cpack -G RPM", runtime_package)
        self.assertIn("CPACK_INSTALL_CMAKE_PROJECTS=", runtime_package)
        self.assertIn("CPACK_INSTALLED_DIRECTORIES=/work/AppDir;/", runtime_package)
        self.assertNotIn("qemu", runtime_package.lower())
        self.assertNotIn("LINUX_APPIMAGE_RUNNER", runtime_package)
        self.assertIn("  ./linuxdeploy.AppImage --appimage-extract-and-run \\\n", runtime_package)
        self.assertIn('./"$OUTPUT" --appimage-extract >/dev/null', runtime_package)
        self.assertIn("-o linuxdeploy-plugin-qt.AppImage", runtime_package)
        self.assertIn("armhf-static-ldd.sh", runtime_package)
        self.assertIn('PATH="/work/wsjtx-build/appimage-tools:$PATH"', runtime_package)
        packaging = runtime_package[runtime_package.index("Package release tarballs"):]
        self.assertLess(runtime_package.index("--output appimage"), runtime_package.index("package-cli-tools.sh"))
        self.assertIn('PATH="/work/wsjtx-build/appimage-tools:$PATH" .github/scripts/package-cli-tools.sh', packaging)
        self.assertIn("--linuxdeploy /work/linuxdeploy.AppImage", packaging)
        self.assertIn(".github/scripts/smoke-release-tarballs.sh", packaging)
        self.assertNotIn("--ldd", packaging)

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
        self.assertIn("TIMEOUT 360", properties)


if __name__ == "__main__":
    unittest.main()
