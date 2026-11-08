import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
HELPER = REPO_ROOT / ".github/scripts/linux-artifact-validation.sh"


class LinuxArtifactValidationTests(unittest.TestCase):
    def run_helper(self, command, **environment):
        env = os.environ.copy()
        env["VALIDATION_HELPER"] = str(HELPER)
        env.update({name: str(value) for name, value in environment.items()})
        return subprocess.run(
            ["bash", "-c", f'source "$VALIDATION_HELPER"; {command}'],
            check=False,
            capture_output=True,
            env=env,
            text=True,
        )

    def make_application_tree(self, kind="appimage"):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        executables = ["wsjtx", "jt9", "qmap", "map65", "ft8code"]
        if kind == "appimage":
            executables.append("wsprd")
        for executable in executables:
            path = root / "usr/bin" / executable
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch(mode=0o755)
        for relative in (
            "usr/share/wsjtx/ALLCALL7.TXT",
            "usr/share/wsjtx/CALL3.TXT",
            "usr/share/wsjtx/sounds/Message.wav",
            "usr/share/wsjtx/sounds/Testing123.wav",
        ):
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()

        if kind == "appdir":
            for relative in (
                "usr/share/applications/wsjtx.desktop",
                "usr/share/pixmaps/wsjtx_icon.png",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
        else:
            qt_conf = root / "usr/bin/qt.conf"
            qt_conf.write_text("[Paths]\nPlugins = plugins\n", encoding="utf-8")
            for relative in (
                "usr/plugins/platforms/libqxcb.so",
                "usr/plugins/sqldrivers/libqsqlite.so",
                "usr/plugins/audio/libqtaudio_alsa.so",
                "usr/lib/libQt5Core.so.5",
                "usr/lib/libQt5Widgets.so.5",
                "usr/lib/libQt5Multimedia.so.5",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
        return temporary, root

    def test_package_manifest_accepts_expected_payload(self):
        manifest = "\n".join(
            (
                "/usr/bin/wsjtx",
                "/usr/bin/jt9",
                "/usr/bin/qmap",
                "/usr/bin/map65",
                "/usr/share/wsjtx/ALLCALL7.TXT",
                "/usr/share/wsjtx/CALL3.TXT",
                "/usr/share/wsjtx/sounds/Message.wav",
                "/usr/share/wsjtx/sounds/Testing123.wav",
            )
        )
        result = self.run_helper(
            'validate_linux_package_manifest "$MANIFEST" fixture', MANIFEST=manifest
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_package_manifest_rejects_missing_executable_data_or_sound(self):
        required = [
            "/usr/bin/wsjtx",
            "/usr/bin/jt9",
            "/usr/bin/qmap",
            "/usr/bin/map65",
            "/usr/share/wsjtx/ALLCALL7.TXT",
            "/usr/share/wsjtx/CALL3.TXT",
            "/usr/share/wsjtx/sounds/Message.wav",
            "/usr/share/wsjtx/sounds/Testing123.wav",
        ]
        for missing in (
            "/usr/bin/map65",
            "/usr/share/wsjtx/ALLCALL7.TXT",
            "/usr/share/wsjtx/sounds/Message.wav",
        ):
            with self.subTest(missing=missing):
                manifest = "\n".join(path for path in required if path != missing)
                result = self.run_helper(
                    'validate_linux_package_manifest "$MANIFEST" fixture',
                    MANIFEST=manifest,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(missing, result.stderr)

    def test_package_manifest_rejects_executable_adjacent_sounds(self):
        manifest = "\n".join(
            (
                "/usr/bin/wsjtx",
                "/usr/bin/jt9",
                "/usr/bin/qmap",
                "/usr/bin/map65",
                "/usr/bin/sounds/Message.wav",
                "/usr/share/wsjtx/ALLCALL7.TXT",
                "/usr/share/wsjtx/CALL3.TXT",
                "/usr/share/wsjtx/sounds/Message.wav",
                "/usr/share/wsjtx/sounds/Testing123.wav",
            )
        )
        result = self.run_helper(
            'validate_linux_package_manifest "$MANIFEST" fixture', MANIFEST=manifest
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("/usr/bin/sounds", result.stderr)

    def test_application_layout_rejects_missing_executable_data_or_sound(self):
        for relative in (
            "usr/bin/map65",
            "usr/share/wsjtx/ALLCALL7.TXT",
            "usr/share/wsjtx/sounds/Message.wav",
        ):
            with self.subTest(relative=relative):
                temporary, root = self.make_application_tree("appdir")
                self.addCleanup(temporary.cleanup)
                (root / relative).unlink()
                result = self.run_helper(
                    'validate_linux_application_layout "$TREE" appdir', TREE=root
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(str(root / relative), result.stderr)

    def test_application_layout_rejects_executable_adjacent_sounds(self):
        temporary, root = self.make_application_tree("appdir")
        self.addCleanup(temporary.cleanup)
        (root / "usr/bin/sounds").mkdir()
        result = self.run_helper(
            'validate_linux_application_layout "$TREE" appdir', TREE=root
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("usr/bin/sounds", result.stderr)

    def test_appimage_layout_requires_qt_runtime_payload(self):
        for relative in (
            "usr/bin/qt.conf",
            "usr/plugins/platforms/libqxcb.so",
            "usr/plugins/sqldrivers/libqsqlite.so",
            "usr/plugins/audio/libqtaudio_alsa.so",
            "usr/lib/libQt5Core.so.5",
        ):
            with self.subTest(relative=relative):
                temporary, root = self.make_application_tree()
                self.addCleanup(temporary.cleanup)
                (root / relative).unlink()
                result = self.run_helper(
                    'validate_linux_application_layout "$TREE" appimage', TREE=root
                )
                self.assertNotEqual(result.returncode, 0)

    def test_armhf_elf_metadata_accepts_eabi5_hard_float_loader(self):
        result = self.run_helper(
            'validate_elf_metadata armhf fixture "$FILE_OUT" "$HEADER_OUT" "$PROGRAM_OUT"',
            FILE_OUT=(
                "fixture: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 "
                "(SYSV), dynamically linked"
            ),
            HEADER_OUT=(
                "Class: ELF32\nMachine: ARM\n"
                "Flags: 0x5000400, Version5 EABI, hard-float ABI"
            ),
            PROGRAM_OUT=(
                "INTERP 0x000174 [Requesting program interpreter: "
                "/lib/ld-linux-armhf.so.3]"
            ),
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_appimage_bundled_gcrypt_requires_gpg_error_and_copyright(self):
        temporary, root = self.make_application_tree()
        self.addCleanup(temporary.cleanup)
        (root / "usr/lib/libgcrypt.so.20").touch()
        dependency = root / "usr/lib/libgpg-error.so.0"
        copyright_file = root / "usr/share/doc/libgpg-error0/copyright"
        command = 'validate_linux_application_layout "$TREE" appimage'

        result = self.run_helper(command, TREE=root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(dependency), result.stderr)

        dependency.touch()
        result = self.run_helper(command, TREE=root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(copyright_file), result.stderr)

        copyright_file.parent.mkdir(parents=True)
        copyright_file.write_text("libgpg-error copyright and license notice\n")
        result = self.run_helper(command, TREE=root)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_armhf_elf_metadata_rejects_wrong_abi_properties(self):
        good = {
            "FILE_OUT": (
                "fixture: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 "
                "(SYSV), dynamically linked"
            ),
            "HEADER_OUT": (
                "Class: ELF32\nMachine: ARM\n"
                "Flags: 0x5000400, Version5 EABI, hard-float ABI"
            ),
            "PROGRAM_OUT": (
                "INTERP 0x000174 [Requesting program interpreter: "
                "/lib/ld-linux-armhf.so.3]"
            ),
        }
        mutations = {
            "class": {"HEADER_OUT": good["HEADER_OUT"].replace("ELF32", "ELF64")},
            "machine": {
                "HEADER_OUT": good["HEADER_OUT"].replace("Machine: ARM", "Machine: X86-64")
            },
            "eabi": {"HEADER_OUT": good["HEADER_OUT"].replace("Version5", "Version4")},
            "hard_float": {
                "HEADER_OUT": good["HEADER_OUT"].replace("hard-float", "soft-float")
            },
            "loader": {"PROGRAM_OUT": good["PROGRAM_OUT"].replace("armhf", "arm")},
        }
        for fault, mutation in mutations.items():
            with self.subTest(fault=fault):
                environment = good | mutation
                result = self.run_helper(
                    'validate_elf_metadata armhf fixture "$FILE_OUT" "$HEADER_OUT" "$PROGRAM_OUT"',
                    **environment,
                )
                self.assertNotEqual(result.returncode, 0)

    def test_armhf_bundle_checks_required_glibc_versions_in_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libdependency.so.1"
            library.touch()
            command = r'''
file() { printf '%s\n' 'ELF 32-bit LSB shared object, ARM, EABI5 version 1, dynamically linked'; }
readelf() {
  case "$1" in
    -hW) printf '%s\n' 'Class: ELF32' 'Machine: ARM' 'Flags: Version5 EABI, hard-float ABI' ;;
    -VW) printf '%s\n' "$VERSIONS" ;;
  esac
}
dpkg() { python3 -c 'import sys; sys.exit(not (tuple(map(int, sys.argv[1].split("."))) > tuple(map(int, sys.argv[2].split(".")))))' "$2" "$4"; }
validate_armhf_elf_tree "$TREE" "$DISTRO"
'''
            for distro, version, success in (
                ("bookworm", "2.36", True),
                ("bookworm", "2.38", False),
                ("trixie", "2.38", True),
                ("trixie", "2.42", False),
                ("bookworm", "PRIVATE", False),
            ):
                with self.subTest(distro=distro, version=version):
                    result = self.run_helper(
                        command, TREE=directory, DISTRO=distro,
                        VERSIONS=(
                            "Version symbols section '.gnu.version':\n GLIBC_9.99\n"
                            "Version needs section '.gnu.version_r':\n"
                            f" Name: GLIBC_{version}\n"
                        ),
                    )
                    self.assertEqual(result.returncode == 0, success, result.stderr)
                    if not success:
                        self.assertIn(str(library), result.stderr)


    def test_tree_checks_executables_and_transitive_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("utility", "libdependency.so.1"):
                (root / name).touch()
            command = r'''
file() { echo 'ELF 64-bit LSB shared object, x86-64'; }
readelf() {
  case "$1:$3" in
    -dW:*/"$BAD_FILE") echo ' (NEEDED) Shared library: [libboost_log.so.1.83.0]' ;;
    -VW:*) echo "Version needs section '.gnu.version_r':"; echo 'Name: GLIBCXX_3.4.30'; echo 'Name: GFORTRAN_8' ;;
  esac
}
validate_linux_static_boost_tree "$TREE"
'''
            for bad_file in ("", "utility", "libdependency.so.1"):
                with self.subTest(bad_file=bad_file):
                    result = self.run_helper(command, TREE=root, BAD_FILE=bad_file)
                    self.assertEqual(result.returncode == 0, not bad_file, result.stderr)
                    if bad_file:
                        self.assertIn(f"{bad_file} imports a Boost shared library", result.stderr)
                    else:
                        self.assertIn("GLIBCXX_3.4.30", result.stdout)
                        self.assertIn("GFORTRAN_8", result.stdout)

    def test_tree_rejects_leftover_libraries_and_broken_symlinks(self):
        for name in ("libboost_log.a", "libboost_log.so.1.83.0", "libboost_log.so"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / name
                if name.endswith(".so"):
                    path.symlink_to("missing-library")
                else:
                    path.touch()
                result = self.run_helper('validate_linux_static_boost_tree "$TREE"', TREE=directory)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"Unexpected bundled Boost library: {path}", result.stderr)

    def test_package_dependencies_reject_boost_for_deb_and_rpm(self):
        for dependencies in (
            "libc6 (>= 2.36), libboost-log1.74.0",
            "libboost_log.so.1.83.0()(64bit)\nlibc.so.6(GLIBC_2.34)(64bit)",
        ):
            result = self.run_helper(
                'validate_linux_boost_dependencies package "$DEPENDENCIES"',
                DEPENDENCIES=dependencies,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Boost runtime dependency", result.stderr)
        result = self.run_helper(
            'validate_linux_boost_dependencies package "libc6 (>= 2.36), libqt5core5"'
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def make_boost_prefix(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        root = directory / "prefix"
        scripts = directory / "scripts"
        scripts.mkdir()
        for name in ("linux-boost-validation.sh", "linux-boost-config.sh", "boost-version.sh", "build-boost-linux.sh"):
            shutil.copyfile(HELPER.parent / name, scripts / name)
        header = "fixture patched queue header\n"
        header_hash = hashlib.sha256(header.encode()).hexdigest()
        source_files = {"boost/log/detail/threadsafe_queue.hpp": header_hash,
                        "libs/log/src/threadsafe_queue.cpp": "a" * 64}
        # Fixtures isolate provenance validation from Boost source downloads.
        helper = scripts / "patch-boost-log-queue.py"
        helper.write_text(
            "import hashlib\n"
            "def digest(data): return hashlib.sha256(data).hexdigest()\n"
            "SUPPORTED = {109200: '1.92.0'}\n"
            "HEADER = 'boost/log/detail/threadsafe_queue.hpp'\n"
            f"SOURCE_HASHES = {source_files!r}\n"
            f"PATCHED_HASHES = {source_files!r}\n"
        )
        (root / "include/boost/log/detail").mkdir(parents=True)
        (root / "include/boost/log/detail/threadsafe_queue.hpp").write_text(header)
        version_header = root / "include/boost/version.hpp"
        version_header.write_text("#define BOOST_VERSION 109200\n")
        (root / "lib").mkdir()
        archives = {}
        member = (b"fixture.o/".ljust(16) + b"0".ljust(12) + b"0".ljust(6) +
                  b"0".ljust(6) + b"100644".ljust(8) + b"2".ljust(10) + b"`\nx\n")
        for component in ("log_setup", "log", "filesystem", "thread", "atomic", "chrono", "container"):
            archive = root / f"lib/libboost_{component}.a"
            archive.write_bytes(b"!<arch>\n" + member)
            archives[archive.name] = hashlib.sha256(archive.read_bytes()).hexdigest()
        provenance = root / "share/wsjtx"
        provenance.mkdir(parents=True)
        (provenance / "boost-log-queue-patch.json").write_text(json.dumps({
            "boost_version": "1.92.0", "patch": "boost-log-queue-release-acquire-v1",
            "patch_helper_sha256": hashlib.sha256(helper.read_bytes()).hexdigest(),
            "files": {name: {"original_sha256": value, "patched_sha256": value}
                      for name, value in source_files.items()},
        }))
        recipe = "".join(hashlib.sha256((scripts / name).read_bytes()).hexdigest() + "\n"
                         for name in ("build-boost-linux.sh", "linux-boost-config.sh", "boost-version.sh", "patch-boost-log-queue.py"))
        manifest = {
            "version": "1.92.0",
            "source_url": "https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.bz2",
            "source_sha256": "5c1d40cb8e19adbf740a4ec2da35b3e58f3f5804b1dce44deb53df72193cbc6c",
            "recipe_sha256": hashlib.sha256(recipe.encode()).hexdigest(),
            "link": "static", "runtime_link": "shared", "threading": "multi",
            "patch_manifest": "boost-log-queue-patch.json",
            "compiler": {"path": "/usr/bin/arm-linux-gnueabihf-g++", "version": "12.2.0", "target": "arm-linux-gnueabihf"},
            "archives": archives,
            "version_header_sha256": hashlib.sha256(version_header.read_bytes()).hexdigest(),
        }
        (provenance / "boost-build.json").write_text(json.dumps(manifest))
        return root, scripts / "linux-boost-validation.sh"

    def test_archives_require_real_archives_and_target_architecture(self):
        root, helper = self.make_boost_prefix()
        command = r'''
readelf() { printf 'Machine: %s\n' "$MACHINE"; }
validate_linux_boost_archives "$TREE" armhf 1.92.0
'''
        for machine, success in (("ARM", True), ("AArch64", False), ("", False)):
            result = self.run_helper(command, TREE=root, MACHINE=machine, VALIDATION_HELPER=helper)
            self.assertEqual(result.returncode == 0, success, result.stderr)
        (root / "lib/libboost_log.a").write_bytes(b"INPUT(libboost_log.so)")
        result = self.run_helper(command, TREE=root, MACHINE="ARM", VALIDATION_HELPER=helper)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a genuine static archive", result.stderr)

    def test_archives_reject_stale_or_modified_provenance(self):
        for mutation, diagnostic in (
            ("archive", "archive hash mismatch"),
            ("recipe", "recipe_sha256 mismatch"),
            ("source", "source_sha256 mismatch"),
            ("patch", "queue patch hash mismatch"),
            ("header", "installed queue header differs"),
            ("shared", "shared Boost libraries"),
            ("escape", "archive escapes Boost prefix"),
        ):
            with self.subTest(mutation=mutation):
                root, helper = self.make_boost_prefix()
                manifest_path = root / "share/wsjtx/boost-build.json"
                manifest = json.loads(manifest_path.read_text())
                if mutation == "archive":
                    (root / "lib/libboost_log.a").write_bytes(b"!<arch>\nchanged")
                elif mutation in ("recipe", "source"):
                    manifest[mutation + "_sha256"] = "0" * 64
                    manifest_path.write_text(json.dumps(manifest))
                elif mutation == "patch":
                    path = root / "share/wsjtx/boost-log-queue-patch.json"
                    patch = json.loads(path.read_text())
                    patch["files"]["libs/log/src/threadsafe_queue.cpp"]["patched_sha256"] = "0" * 64
                    path.write_text(json.dumps(patch))
                elif mutation == "header":
                    (root / "include/boost/log/detail/threadsafe_queue.hpp").write_text("unpatched")
                elif mutation == "shared":
                    (root / "lib/libboost_log.so.1.92.0").touch()
                else:
                    archive = root / "lib/libboost_log.a"
                    outside = root.parent / "libboost_log.a"
                    archive.rename(outside)
                    archive.symlink_to(outside)
                result = self.run_helper(
                    'readelf() { echo "Machine: ARM"; }; validate_linux_boost_archives "$TREE" armhf 1.92.0',
                    TREE=root, VALIDATION_HELPER=helper,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)

    def test_archives_reject_empty_libraries(self):
        for component in ("system", "date_time", "log", "thread"):
            with self.subTest(component=component):
                root, helper = self.make_boost_prefix()
                archive = root / f"lib/libboost_{component}.a"
                archive.write_bytes(b"!<arch>\n")
                path = root / "share/wsjtx/boost-build.json"
                manifest = json.loads(path.read_text())
                manifest["archives"][archive.name] = hashlib.sha256(archive.read_bytes()).hexdigest()
                path.write_text(json.dumps(manifest))
                result = self.run_helper(
                    'readelf() { echo "Machine: ARM"; }; validate_linux_boost_archives "$TREE" armhf 1.92.0',
                    TREE=root, VALIDATION_HELPER=helper,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("has no object members", result.stderr)

    def test_cmake_selection_rejects_distro_or_shared_boost_fallback(self):
        root, helper = self.make_boost_prefix()
        build = root.parent / "build"
        link = build / "CMakeFiles/wsjtx.dir/link.txt"
        link.parent.mkdir(parents=True)
        flags = link.with_name("flags.make")
        cache_lines = ["Boost_USE_STATIC_LIBS:BOOL=ON", "Boost_NO_BOOST_CMAKE:BOOL=OFF"]
        for package in ("Boost", "boost_headers", "boost_log", "boost_log_setup", "boost_container"):
            config = root / f"lib/cmake/{package}-1.92.0"
            config.mkdir(parents=True)
            cache_lines.append(f"{package}_DIR:PATH={config}")
        cache = "\n".join(cache_lines)
        inputs = f"c++ main.o {root}/lib/libboost_log.a {root}/lib/libboost_log_setup.a"
        includes = f"CXX_INCLUDES = -isystem {root}/include\n"
        for mutation in (None, "shared", "distro", "link", "flag", "module", "include", "missing"):
            with self.subTest(mutation=mutation):
                changed_cache, changed_inputs, changed_includes = cache, inputs, includes
                if mutation == "shared":
                    changed_inputs = inputs.replace("libboost_log.a", "libboost_log.so")
                elif mutation == "distro":
                    changed_cache = cache.replace(f"boost_container_DIR:PATH={root}", "boost_container_DIR:PATH=/usr")
                elif mutation == "link":
                    changed_inputs += " -lboost_log"
                elif mutation == "flag":
                    changed_cache = cache.replace("Boost_USE_STATIC_LIBS:BOOL=ON", "Boost_USE_STATIC_LIBS:BOOL=OFF")
                elif mutation == "module":
                    changed_cache = cache.replace("Boost_NO_BOOST_CMAKE:BOOL=OFF", "Boost_NO_BOOST_CMAKE:BOOL=ON")
                elif mutation == "include":
                    changed_includes = includes.replace(f"{root}/include", "/usr/include")
                elif mutation == "missing":
                    changed_cache = "\n".join(line for line in cache_lines if not line.startswith("boost_log_DIR:"))
                (build / "CMakeCache.txt").write_text(changed_cache)
                link.write_text(changed_inputs)
                flags.write_text(changed_includes)
                result = self.run_helper('validate_linux_boost_selection "$BUILD" "$TREE"',
                                         BUILD=build, TREE=root, VALIDATION_HELPER=helper)
                self.assertEqual(result.returncode == 0, mutation is None, result.stderr)

    def test_package_validators_inspect_dependency_metadata(self):
        command = r'''
validate_linux_package_manifest() { :; }
dpkg-deb() {
  case "$1" in
    --fsys-tarfile) : ;;
    -f) echo 'libboost-log1.74.0' ;;
    *) exit 9 ;;
  esac
}
tar() { :; }
rpm() {
  case "$1" in
    -qpl) : ;;
    -qpR) echo 'libboost_log.so.1.83.0()(64bit)' ;;
    *) exit 9 ;;
  esac
}
"$VALIDATOR" fixture
'''
        for validator in ("validate_deb_package", "validate_rpm_package"):
            with self.subTest(validator=validator):
                result = self.run_helper(command, VALIDATOR=validator)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Boost runtime dependency", result.stderr)


if __name__ == "__main__":
    unittest.main()
