import fnmatch
import os
import pathlib
import re
import shutil
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
        rf"^  {re.escape(name)}:\n(?P<body>.*?)(?=^  [a-z][a-z0-9_-]*:\n|\Z)",
        workflow,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise AssertionError(f"job not found: {name}")
    return match.group("body")


class LinuxImagePublicationPolicyTests(unittest.TestCase):
    def test_dependency_fingerprints_exclude_package_only_validation(self):
        profiles = ("normal-noble", "normal-jammy", "normal-bookworm", "tsan-noble",
                    "armhf-cross-bookworm", "armhf-cross-trixie",
                    "armhf-runtime-bookworm", "armhf-runtime-trixie")
        package_helper = ".github/scripts/linux-artifact-validation.sh"
        dependency_helper = ".github/scripts/linux-boost-validation.sh"
        inputs_script = ".github/scripts/linux-ci-image-inputs.sh"
        paths = {inputs_script, package_helper}
        for profile in profiles:
            result = subprocess.check_output(
                ["bash", "-c", 'source "$1"; linux_ci_image_inputs "$2"',
                 "test", str(ROOT / inputs_script), profile], text=True,
            )
            paths.update(result.splitlines())
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for relative in paths:
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / relative, target)

            def fingerprints():
                values = {profile: subprocess.check_output(
                    ["bash", ".github/scripts/linux-ci-image-fingerprint.sh", profile],
                    cwd=root, text=True,
                ) for profile in profiles}
                values["boost-tsan"] = subprocess.check_output(
                    ["bash", "-c", 'source .github/scripts/tsan-linux-deps-config.sh; '
                     'TSAN_TOOLCHAIN_ID=fixture; tsan_dependency_cache_key boost'],
                    cwd=root, text=True,
                )
                return values

            before = fingerprints()
            with (root / package_helper).open("a") as stream:
                stream.write("\n# Packaging validation policy changed.\n")
            self.assertEqual(fingerprints(), before)
            with (root / dependency_helper).open("a") as stream:
                stream.write("\n# Dependency validation policy changed.\n")
            after = fingerprints()
            for profile in (*profiles, "boost-tsan"):
                with self.subTest(profile=profile):
                    self.assertEqual(before[profile] == after[profile], "runtime" in profile)

        for filename in ("Dockerfile.noble", "Dockerfile.jammy", "Dockerfile.bookworm",
                         "Dockerfile.armhf-cross", "Dockerfile.armhf-cross-trixie"):
            recipe = (ROOT / ".github/images/linux-ci" / filename).read_text()
            self.assertIn(dependency_helper, recipe)
            self.assertNotIn(package_helper, recipe)
        outputs = self.select_warmers("push", paths=[dependency_helper])
        self.assertEqual(outputs["image_set"], "normal-and-tsan")

    def test_single_profile_candidates_cannot_promote_or_select_other_cohorts(self):
        validate = job("validate-inputs")
        body = textwrap.dedent(validate.split("        run: |\n", 1)[1])
        environment = {
            **os.environ, "MODE": "refresh", "ROLLBACK_TAG": "", "IMAGE_SET": "normal",
            "INCLUDE_ARMHF": "false", "CANDIDATE_PROFILE": "aarch64", "PROMOTE": "false",
            "REQUEST_REF": "refs/heads/build/fftw-3.3.11",
            "REQUEST_REPOSITORY": "WSJTX/wsjtx-internal", "PUBLICATION_TARGET": "internal",
        }
        for profile in ("x86_64", "aarch64", "armhf-bookworm", "armhf-trixie"):
            with self.subTest(profile=profile):
                result = subprocess.run(["bash", "-c", body],
                                        env={**environment, "CANDIDATE_PROFILE": profile},
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for overrides in (
                {"PROMOTE": "true"}, {"INCLUDE_ARMHF": "true"},
                {"IMAGE_SET": "tsan"}, {"PUBLICATION_TARGET": "public-release"},
                {"MODE": "rollback", "ROLLBACK_TAG": "validated-build-20261109-123-1"},
            ):
                with self.subTest(profile=profile, overrides=overrides):
                    result = subprocess.run(
                        ["bash", "-c", body],
                        env={**environment, "CANDIDATE_PROFILE": profile, **overrides},
                        capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, 2)
                    self.assertIn("Single-profile candidates require", result.stdout)

    def test_publisher_rejects_combined_cohort_requests(self):
        body = textwrap.dedent(job("validate-inputs").split("        run: |\n", 1)[1])
        environment = {
            **os.environ, "MODE": "refresh", "ROLLBACK_TAG": "", "IMAGE_SET": "normal-and-tsan",
            "INCLUDE_ARMHF": "true", "CANDIDATE_PROFILE": "all", "PROMOTE": "true",
            "REQUEST_REF": "refs/heads/develop", "REQUEST_REPOSITORY": "WSJTX/wsjtx-internal",
            "PUBLICATION_TARGET": "internal",
        }
        result = subprocess.run(["bash", "-c", body], env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("Publish normal and TSan cohorts separately", result.stdout)

    def test_candidate_profiles_select_only_their_image_jobs(self):
        names = {"build-asan-ubsan", "build-x64", "build-arm64", "build-tsan", "build-armhf-cross",
                 "build-armhf-runtime", "build-armhf-trixie-cross", "build-armhf-trixie-runtime"}
        cases = [
            ("x86_64", "normal", False, {"build-x64"}),
            ("aarch64", "normal", False, {"build-arm64"}),
            ("armhf-bookworm", "normal", False, {"build-armhf-cross", "build-armhf-runtime"}),
            ("armhf-trixie", "normal", False, {"build-armhf-trixie-cross", "build-armhf-trixie-runtime"}),
            ("all", "normal", False, {"build-asan-ubsan", "build-x64", "build-arm64"}),
            ("all", "normal", True, names - {"build-tsan"}),
            ("all", "tsan", False, {"build-tsan"}),
        ]
        cases = [(*case, "internal") for case in cases]
        cases.append(("all", "normal", True, names - {"build-tsan", "build-asan-ubsan"}, "public-release"))
        for profile, image_set, include_armhf, expected, publication_target in cases:
            with self.subTest(profile=profile, image_set=image_set, include_armhf=include_armhf,
                              publication_target=publication_target):
                inputs = {"candidate_profile": profile, "image_set": image_set,
                          "include_armhf": include_armhf, "publication_target": publication_target}
                selected = set()
                for name in names:
                    expression = job(name).split("    needs:", 1)[0].split("    if:", 1)[1]
                    expression = " ".join(expression.strip().removeprefix(">-").split())
                    expression = re.sub(r"inputs\.(\w+)", lambda match: repr(inputs[match[1]]), expression)
                    expression = expression.replace("&&", " and ").replace("||", " or ")
                    if eval(expression, {"__builtins__": {}}, {}):
                        selected.add(name)
                self.assertEqual(selected, expected)

    def test_jammy_build_uses_matching_package_recipe_and_cache(self):
        build = job("build-x64")
        self.assertIn("file: .github/images/linux-ci/Dockerfile.jammy", build)
        self.assertIn("platforms: linux/amd64", build)
        for tag in ("candidate_tag", "build_tag"):
            self.assertIn(
                "${{ needs.metadata.outputs.package_root }}/linux-jammy:${{ needs.metadata.outputs."
                + tag + " }}",
                build,
            )
        self.assertIn("IMAGE_RECIPE_SHA256=${{ needs.metadata.outputs.normal_jammy_recipe }}", build)
        cache = "type=registry,ref=${{ needs.metadata.outputs.package_root }}/linux-jammy:buildcache"
        self.assertIn(f"cache-from: {cache}\n", build)
        self.assertIn(f"cache-to: {cache},mode=max,oci-mediatypes=true,image-manifest=true", build)
        self.assertNotIn(":stable", build)
        metadata = job("metadata")
        self.assertIn("linux-ci-image-fingerprint.sh normal-jammy", metadata)
        for name in ("warm-normal-ccache", "warm-arm64-ccache", "promote"):
            self.assertIn("inputs.promote", job(name))

    def test_armhf_image_builds_register_emulation_before_buildx(self):
        for name in ("build-armhf-cross", "build-armhf-runtime",
                     "build-armhf-trixie-cross", "build-armhf-trixie-runtime"):
            with self.subTest(job=name):
                build = job(name)
                self.assertIn("docker/setup-qemu-action@v4", build)
                self.assertIn(
                    "tonistiigi/binfmt:qemu-v10.2.3-68@sha256:"
                    "400a4873b838d1b89194d982c45e5fb3cda4593fbfd7e08a02e76b03b21166f0",
                    build,
                )
                self.assertLess(build.index("docker/setup-qemu-action@v4"),
                                build.index("docker/setup-buildx-action@v4"))

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
            ".github/actions/warm-macos-deps/action.yml",
            ".github/scripts/build-qt-macos.sh",
            ".github/scripts/macos-qt-cache-key.sh",
        ):
            cases.append(({"event": "push", "paths": [path]}, "true", "true"))
        # Not dependency-cache key inputs: build-macos.yml's keys must equal
        # warm-macos-deps' (test_native_cache_policy.py), so a key change there
        # also changes the action.
        for path in (
            ".github/workflows/build-macos.yml", ".github/scripts/verify-macos-static-archives.sh",
            ".github/scripts/install-xpack-gfortran-macos.sh", ".github/scripts/install-r-macos-gfortran.sh",
            ".github/workflows/ci.yml", ".github/workflows/release.yml",
        ):
            cases.append(({"event": "push", "paths": [path]}, "false", "false"))
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
            ({"event": "push", "paths": [".github/scripts/build-qt-linux.sh"]}, "false", "normal"),
            ({"event": "push", "paths": [".github/scripts/qt5-source-config.sh"]}, "false", "normal-and-tsan"),
            ({"event": "push", "paths": ["CMakeLists.txt", ".github/scripts/verify-linux-ci-image.sh"]}, "false", "normal-and-tsan"),
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
            ({"event": "push", "paths": [".github/scripts/build-hamlib-armhf-cross.sh"]}, "false", "normal"),
            ({"event": "push", "paths": ["mainwindow.cpp"]}, "false", "none"),
        ]
        for arguments, selected, image_set in cases:
            with self.subTest(arguments=arguments):
                outputs = self.select_warmers(**arguments)
                self.assertEqual(outputs["linux_armhf"], selected)
                self.assertEqual(outputs["image_set"], image_set)

    def test_armhf_build_inputs_do_not_start_the_warmer(self):
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
                self.assertFalse(any(fnmatch.fnmatchcase(path, pattern) for pattern in push_filters))
                outputs = self.select_warmers("push", paths=[path])
                self.assertEqual(outputs["linux_armhf"], "false")
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
            "inputs.image_set == 'normal'",
            "image_tag: ${{ needs.metadata.outputs.build_tag }}",
        ):
            self.assertIn(condition, published)
        self.assertIn("- metadata", published)
        self.assertIn("- promote", published)
        self.assertNotIn("warm-armhf-ccache", job("promote"))

    def test_sanitizer_build_inputs_do_not_start_the_warmer(self):
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
                self.assertFalse(any(fnmatch.fnmatchcase(path, pattern) for pattern in push_filters))
                outputs = self.select_warmers("push", paths=[path])
                self.assertEqual(outputs["linux_asan_ubsan"], "false")
                self.assertEqual(outputs["image_set"], "none")

    @staticmethod
    def warmer_jobs():
        parts = re.split(r"^  ([a-z][a-z0-9_-]*):\n", WARM.split("\njobs:\n", 1)[1], flags=re.MULTILINE)
        return dict(zip(parts[1::2], parts[2::2]))

    def test_warmer_prunes_develop_linux_ccache_after_its_warmers(self):
        jobs = self.warmer_jobs()
        prune = jobs["prune-linux-ccache"]
        needs = set(re.findall(r"^      - ([a-z0-9_-]+)$", prune.split("    if:", 1)[0], re.MULTILINE))
        self.assertEqual(needs, {
            "detect", "refresh-linux-images", "refresh-tsan-image", "warm-linux-arm64-ccache",
            "warm-linux-armhf-ccache", "warm-linux-asan-ubsan-ccache",
        })
        self.assertIn("    if: always() && !cancelled() && github.ref == 'refs/heads/develop'\n", prune)
        self.assertIn("    continue-on-error: true\n", prune)
        self.assertIn("    permissions:\n      actions: write\n      contents: read\n", prune)
        self.assertEqual(WARM.count("actions: write"), 1)
        self.assertIn("GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}", prune)
        self.assertIn(
            "python3 .github/scripts/prune_linux_ccache.py\n"
            "          --repository ${{ github.repository }}\n"
            "          --ref refs/heads/develop\n"
            "          --apply\n"
            "          --skip-sha-check\n",
            prune,
        )
        # ci.yml, whose Linux x86_64 job saves the commit's entry, keeps the check.
        ci = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertIn("prune_linux_ccache.py", ci)
        self.assertNotIn("--skip-sha-check", ci)
        self.assertEqual(list(jobs)[-2:], ["prune-linux-ccache", "summary"])
        self.assertIn("      - prune-linux-ccache\n", jobs["summary"])

    def test_warmer_build_artifacts_expire_after_one_day(self):
        calls = 0
        for name, body in self.warmer_jobs().items():
            if re.search(r"^    uses: \./\.github/workflows/build-(?:linux|windows|macos)\.yml$", body, re.MULTILINE):
                calls += 1
                with self.subTest(job=name):
                    self.assertIn("      artifact_retention_days: 1\n", body)
        self.assertEqual(calls, 5)

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
        self.assertIn("inputs.image_set == 'normal'", warm)
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
            8,
        )

    def test_reusable_publisher_exports_all_release_image_digests(self):
        self.assertIn("x86_64_digest:", PUBLISH)
        self.assertIn("value: ${{ jobs.build-x64.outputs.digest }}", PUBLISH)
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
        self.assertIn("image_set: normal", job("refresh-linux-images", WARM))
        self.assertIn("image_set: tsan", job("refresh-tsan-image", WARM))
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

    def test_dependency_version_policies_refresh_images_and_native_caches(self):
        for dependency in ("fftw", "boost"):
            path = f".github/scripts/{dependency}-version.sh"
            with self.subTest(path=path):
                self.assertIn(f'      - "{path}"', WARM)
                outputs = self.select_warmers("push", paths=[path])
                self.assertEqual(outputs["image_set"], "normal-and-tsan")
                for cache in ("macos_arm64", "macos_x86_64", "windows", "linux_armhf"):
                    self.assertEqual(outputs[cache], "true")

    def test_boost_queue_patch_refreshes_each_compiled_dependency(self):
        path = ".github/scripts/boost-log-queue.patch"
        self.assertIn(f'      - "{path}"', WARM)
        case = re.search(r"\.github/scripts/boost-log-queue\.patch\)\n(?P<body>.*?);;",
                         WARM, re.DOTALL).group("body")
        self.assertIn("enable_cache_all", case)
        profiles = ("normal-noble", "normal-jammy", "normal-bookworm", "tsan-noble",
                    "armhf-cross-bookworm", "armhf-cross-trixie")
        for profile in profiles:
            with self.subTest(profile=profile):
                result = subprocess.run(
                    ["bash", ".github/scripts/is-linux-ci-image-input.sh", profile, path],
                    cwd=ROOT, text=True, capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
        outputs = self.select_warmers("push", paths=[path])
        self.assertEqual(outputs["image_set"], "normal-and-tsan")
        for cache in ("macos_arm64", "macos_x86_64", "windows", "linux_armhf"):
            self.assertEqual(outputs[cache], "true")

    def test_selected_dispatch_builds_x64_only_once(self):
        selected = (ROOT / ".github/workflows/build-selected-artifacts.yml").read_text()
        for target, mode, expected in (
            ("linux-x64", "artifact-now", ["linux-x64"]),
            ("linux-x64", "linux-smoke-first", ["linux-smoke-x64"]),
            ("linux-arm64", "artifact-now", []),
            ("windows-x64", "artifact-now", []),
            ("windows-x64", "linux-smoke-first", ["linux-smoke-x64"]),
            ("all", "artifact-now", ["linux-x64"]),
            ("all", "linux-smoke-first", ["linux-smoke-x64"]),
        ):
            with self.subTest(target=target, mode=mode):
                chosen = []
                for name in ("linux-x64", "linux-smoke-x64"):
                    body = job(name, selected)
                    condition = re.search(r"    if: (.*?)(?=    uses:)", body, re.DOTALL).group(1)
                    condition = " ".join(condition.removeprefix(">-").split())
                    condition = condition.replace("inputs.targets", repr(target))
                    condition = condition.replace("inputs.mode", repr(mode))
                    condition = condition.replace("&&", " and ").replace("||", " or ")
                    if eval(condition, {"__builtins__": {}}, {}):
                        chosen.append(name)
                self.assertEqual(chosen, expected)

    def test_portable_runtime_follows_either_successful_x64_build(self):
        selected = (ROOT / ".github/workflows/build-selected-artifacts.yml").read_text()
        runtime = job("linux-portable-runtime", selected)
        condition = re.search(r"    if: >-\n(.*?)(?=    name:)", runtime, re.DOTALL).group(1)
        for prepare, smoke, normal, cancelled, expected in (
            ("success", "skipped", "success", False, True),
            ("success", "success", "skipped", False, True),
            ("success", "skipped", "failure", False, False),
            ("success", "failure", "skipped", False, False),
            ("success", "skipped", "skipped", False, False),
            ("failure", "skipped", "success", False, False),
            ("success", "success", "skipped", True, False),
        ):
            with self.subTest(prepare=prepare, smoke=smoke, normal=normal, cancelled=cancelled):
                expression = " ".join(condition.split())
                expression = expression.replace("!cancelled()", repr(not cancelled))
                for name, result in (("prepare", prepare), ("linux-smoke-x64", smoke), ("linux-x64", normal)):
                    expression = expression.replace(f"needs.{name}.result", repr(result))
                expression = expression.replace("&&", " and ").replace("||", " or ")
                self.assertEqual(eval(expression, {"__builtins__": {}}, {}), expected)
        self.assertIn('ubuntu: ["22.04", "24.04"]', runtime)
        self.assertIn('merge-multiple: true', runtime)
        self.assertIn('linux-x86_64-@(AppImage|jt9|codec|wsprd|utilities)', runtime)

    def test_jammy_provenance_preserves_dependency_cache_layers(self):
        recipe = (ROOT / ".github/images/linux-ci/Dockerfile.jammy").read_text()
        apt = recipe.index('/usr/local/bin/install-packages jammy')
        qt = recipe.index('RUN /usr/local/share/wsjtx-ci/build-qt-linux.sh')
        manifest = recipe.index('RUN /usr/local/bin/write-wsjtx-ci-manifest')
        self.assertLess(recipe.index('ARG IMAGE_REFRESH_EPOCH'), apt)
        for argument in ("IMAGE_CREATED", "IMAGE_GENERATION", "IMAGE_REVISION", "IMAGE_RECIPE_SHA256"):
            with self.subTest(argument=argument):
                declaration = recipe.index(f'ARG {argument}')
                self.assertLess(qt, declaration)
                self.assertLess(declaration, manifest)
        self.assertLess(qt, recipe.index('LABEL org.opencontainers.image.created='))

    def test_selected_artifacts_use_immutable_candidate_generation_before_promotion(self):
        workflow = (ROOT / ".github/workflows/build-selected-artifacts.yml").read_text()
        prepare = job("prepare", workflow)
        body = re.search(
            r"      - id: linux-image\n.*?        run: \|\n(?P<body>.*?)(?=^      - |\Z)",
            prepare, re.MULTILINE | re.DOTALL,
        ).group("body")
        body = textwrap.dedent(body)
        generation = "build-20261111-17-1"
        for tag, accepted in (
            ("", True), (generation, True), ("validated-" + generation, True),
            ("candidate-17-1", True), ("stable", False), ("candidate-invalid", False),
        ):
            with self.subTest(tag=tag), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                scripts = root / ".github/scripts"
                scripts.mkdir(parents=True)
                shutil.copy(ROOT / ".github/scripts/validate-ci-inputs.sh", scripts)
                resolver = scripts / "resolve-linux-ci-image-tag.py"
                resolver.write_text(
                    "#!/usr/bin/env bash\nprintf '%s\\n' 'image_tag=" + generation +
                    "' >> \"$GITHUB_OUTPUT\"\n"
                )
                resolver.chmod(0o755)
                output = root / "output"
                result = subprocess.run(
                    ["bash", "-e", "-o", "pipefail", "-c", body], cwd=root,
                    env={**os.environ, "REQUESTED_TAG": tag, "GITHUB_OUTPUT": str(output)},
                    text=True, capture_output=True,
                )
                self.assertEqual(result.returncode == 0, accepted, result.stdout + result.stderr)
                if accepted:
                    self.assertEqual(output.read_text().strip(), "image_tag=" + (tag or generation))

    def test_candidate_warm_uses_exact_tag_and_publishes_without_recache(self):
        warm = job("warm-normal-ccache")
        self.assertIn("- build-x64", warm)
        self.assertIn("uses: ./.github/workflows/build-linux.yml", warm)
        self.assertIn("image_tag: ${{ needs.metadata.outputs.build_tag }}", warm)
        self.assertIn("allow_stale_image: false", warm)
        self.assertIn("recache: false", warm)
        self.assertIn("save_ccache: true", warm)

    def test_jammy_image_resolution_requires_immutable_images(self):
        body = textwrap.dedent(job("resolve-image", BUILD).split("        run: |\n", 1)[1])
        environment = {
            **os.environ, "ARMHF_DISTRO": "bookworm", "ARCH": "x86_64",
            "ARMHF_BUILD": "false", "ARMHF_BUILD_ONLY": "false",
            "IMAGE_SOURCE": "internal", "IMAGE_DIGEST": "",
            "ARMHF_CROSS_IMAGE_DIGEST": "",
            "REQUEST_REPOSITORY": "WSJTX/wsjtx", "GITHUB_REF": "refs/tags/v4.7.2",
        }
        tag = "build-20261109-42580943971-1"
        for requested, stale, source, arch, accepted in (
            (tag, "false", "internal", "x86_64", True),
            ("candidate-123-1", "false", "internal", "x86_64", True),
            (tag, "true", "internal", "x86_64", True),
            ("stable", "false", "internal", "x86_64", False),
            ("", "false", "internal", "x86_64", True),
            (tag, "false", "public-release", "x86_64", False),
            (tag, "false", "internal", "aarch64", True),
        ):
            with self.subTest(tag=requested, stale=stale, source=source, arch=arch), \
                 tempfile.TemporaryDirectory() as directory:
                output = pathlib.Path(directory) / "output"
                scripts = pathlib.Path(directory) / ".github/scripts"
                scripts.mkdir(parents=True)
                shutil.copy(ROOT / ".github/scripts/validate-ci-inputs.sh", scripts)
                resolver = scripts / "resolve-linux-ci-image-tag.py"
                resolver.write_text("#!/usr/bin/env bash\nprintf 'image_tag=" + tag + "\\n' >> \"$GITHUB_OUTPUT\"\n")
                resolver.chmod(0o755)
                result = subprocess.run(
                    ["bash", "-e", "-o", "pipefail", "-c", body], cwd=directory,
                    env={**environment, "REQUESTED_TAG": requested,
                         "ALLOW_STALE_IMAGE": stale, "IMAGE_SOURCE": source,
                         "ARCH": arch, "GITHUB_OUTPUT": str(output)},
                    text=True, capture_output=True,
                )
                self.assertEqual(result.returncode == 0, accepted, result.stdout + result.stderr)
                if accepted:
                    package = "linux-jammy" if arch == "x86_64" else "linux-arm64-bookworm"
                    self.assertIn(
                        f"image_reference=ghcr.io/wsjtx/wsjtx-internal/{package}:{requested or tag}",
                        output.read_text(),
                    )

        digest = "sha256:" + "a" * 64
        for requested in ("validated-" + tag, "release-v4.7.2-" + "b" * 40 + "-123-1"):
            for stale in ("false", "true"):
                with self.subTest(tag=requested, stale=stale), \
                     tempfile.TemporaryDirectory() as directory:
                    output = pathlib.Path(directory) / "output"
                    result = subprocess.run(
                        ["bash", "-e", "-o", "pipefail", "-c", body], cwd=ROOT,
                        env={**environment, "REQUESTED_TAG": requested,
                             "ALLOW_STALE_IMAGE": stale, "IMAGE_SOURCE": "public-release",
                             "IMAGE_DIGEST": digest,
                             "GITHUB_OUTPUT": str(output)}, text=True, capture_output=True,
                    )
                    self.assertEqual(result.returncode == 0, stale == "false", result.stdout + result.stderr)
                    if stale == "false":
                        self.assertIn(f"image_reference=ghcr.io/wsjtx/wsjtx/linux-jammy@{digest}",
                                      output.read_text())

        promote = job("promote")
        self.assertIn("needs.warm-normal-ccache.result == 'success'", promote)
        self.assertIn("- warm-normal-ccache", promote)


    def test_refresh_requests_select_independent_cohorts(self):
        for image_set, expected in (("none", set()), ("normal", {"refresh-linux-images"}),
                                    ("tsan", {"refresh-tsan-image"}),
                                    ("normal-and-tsan", {"refresh-linux-images", "refresh-tsan-image"})):
            with self.subTest(image_set=image_set):
                selected = set()
                for name in ("refresh-linux-images", "refresh-tsan-image"):
                    body = job(name, WARM)
                    condition = re.search(r"    if: (.*?)(?=    uses:)", body, re.DOTALL).group(1)
                    condition = " ".join(condition.removeprefix(">-").split())
                    condition = condition.replace("needs.detect.outputs.image_set", repr(image_set))
                    condition = condition.replace("&&", " and ").replace("||", " or ")
                    if eval(condition, {"__builtins__": {}}, {}):
                        selected.add(name)
                    self.assertIn("    needs: detect\n", body)
                    self.assertNotIn("needs.refresh-", body)
                self.assertEqual(selected, expected)
        self.assertIn("include_armhf: true", job("refresh-linux-images", WARM))
        self.assertIn("include_armhf: false", job("refresh-tsan-image", WARM))
        self.assertIn("- refresh-tsan-image", job("summary", WARM))

    def test_cohort_locks_do_not_serialize_normal_refresh_behind_tsan(self):
        self.assertNotRegex(WARM, r"(?m)^concurrency:")
        self.assertIn("group: linux-ci-image-publishing-${{ inputs.image_set }}", PUBLISH)
        self.assertIn("cancel-in-progress: false", PUBLISH)
        for name, cohort in (("retain-linux-images", "normal"), ("retain-tsan-image", "tsan")):
            retention = job(name, WARM)
            self.assertIn(f"group: linux-ci-image-publishing-{cohort}", retention)
            self.assertIn("cancel-in-progress: false", retention)
            if cohort == "normal":
                self.assertIn("--package wsjtx-internal/linux-jammy", retention)
                self.assertNotIn("linux-tsan-noble", retention)
                self.assertNotIn("refresh-tsan-image", retention)
            else:
                self.assertNotIn("wsjtx-internal/linux-jammy", retention)
                self.assertNotIn("wsjtx-internal/linux-noble", retention)
                self.assertNotIn("needs.refresh-linux-images", retention)
        self.assertNotIn("continue-on-error", job("refresh-tsan-image", WARM))

    def test_tsan_promotion_requires_its_own_success(self):
        promote = job("promote")
        expression = promote.split("    if:", 1)[1].split("    needs:", 1)[0]
        expression = " ".join(expression.strip().removeprefix(">-").split())
        for result in ("success", "failure", "cancelled", "skipped"):
            with self.subTest(result=result):
                condition = expression.replace("always()", "True").replace("cancelled()", "False")
                condition = re.sub(r"inputs\.(\w+)", lambda m: repr({
                    "mode": "refresh", "promote": True, "image_set": "tsan", "include_armhf": False,
                }[m[1]]), condition)
                condition = re.sub(r"needs\.([a-z0-9-]+)\.result", lambda m: repr(
                    result if m[1] == "build-tsan" else "skipped"), condition)
                condition = condition.replace("&&", " and ").replace("||", " or ")
                self.assertEqual(bool(eval(condition, {"__builtins__": {}}, {})), result == "success")

    def test_normal_promotion_requires_jammy_and_sanitizer_images_and_application_warmers(self):
        promote = job("promote")
        expression = promote.split("    if:", 1)[1].split("    needs:", 1)[0]
        expression = " ".join(expression.strip().removeprefix(">-").split())
        required = ("build-x64", "build-asan-ubsan", "build-arm64",
                    "warm-normal-ccache", "warm-arm64-ccache")
        for failed in (None, *required):
            with self.subTest(failed=failed):
                condition = expression.replace("always()", "True")
                condition = re.sub(r"inputs\.(\w+)", lambda m: repr({
                    "mode": "refresh", "promote": True, "image_set": "normal", "include_armhf": False,
                }[m[1]]), condition)
                condition = re.sub(r"needs\.([a-z0-9-]+)\.result", lambda m: repr(
                    "failure" if m[1] == failed else "success"), condition)
                condition = condition.replace("&&", " and ").replace("||", " or ").replace("false", "False")
                self.assertEqual(bool(eval(condition, {"__builtins__": {}}, {})), failed is None)
        moves = re.findall(r"--tag ghcr.io/wsjtx/wsjtx-internal/([^ :]+):stable", promote)
        self.assertEqual(moves[-1], "linux-jammy")

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
        self.assertIn("normal|tsan) ;;", validate)
        self.assertIn('[ "$IMAGE_SET" = tsan ] && [ "$INCLUDE_ARMHF" = true ]', validate)
        self.assertIn("inputs.image_set == 'tsan'", job("build-tsan"))
        self.assertIn("inputs.image_set == 'normal'", job("build-asan-ubsan"))
        self.assertIn('if [ "$IMAGE_SET" != tsan ]; then', job("promote"))
        self.assertIn('if [ "$IMAGE_SET" != normal ]; then', job("promote"))
        self.assertIn('if [ "$IMAGE_SET" != tsan ]; then', job("rollback"))
        self.assertIn('if [ "$IMAGE_SET" != normal ]; then', job("rollback"))

    def test_rollback_uses_only_explicitly_selected_packages(self):
        rollback = job("rollback")
        self.assertNotIn("tsan_available", rollback)
        self.assertNotIn("armhf_available", rollback)
        self.assertIn("packages+=(linux-jammy linux-noble linux-arm64-bookworm)", rollback)
        self.assertIn(
            "packages+=(linux-armhf-cross-bookworm linux-armv7-bookworm linux-armhf-cross-trixie linux-armv7-trixie)",
            rollback,
        )
        self.assertIn("packages+=(linux-tsan-noble)", rollback)
        self.assertIn("stable_packages+=(linux-jammy)", rollback)

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
