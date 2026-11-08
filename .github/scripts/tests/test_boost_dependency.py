import hashlib
import io
import json
import os
import pathlib
import sys
import tarfile
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
VERIFIER = ROOT / "CMake/Modules/verify_static_boost.cmake"


class StaticBoostSelectionTests(unittest.TestCase):
    def configure(self, *, library="log.a", kind="UNKNOWN", magic=b"!<arch>\n",
                  transitive=False, missing=False):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory)
            (source / library).write_bytes(magic)
            (source / "setup.a").write_bytes(b"!<arch>\n")
            location = "" if missing else (
                f'set_property(TARGET Boost::log PROPERTY IMPORTED_LOCATION "{source / library}")')
            (source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.12)
project(BoostSelection NONE)
include("{VERIFIER}")
add_library(Boost::log {kind} IMPORTED)
{location}
add_library(Boost::log_setup STATIC IMPORTED)
set_target_properties(Boost::log_setup PROPERTIES
  IMPORTED_CONFIGURATIONS RELEASE
  IMPORTED_LOCATION_RELEASE "{source / 'setup.a'}"
  INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:Boost::log>")
wsjt_verify_static_boost({'Boost::log_setup' if transitive else 'Boost::log'})
''')
            return subprocess.run(
                ["cmake", "-S", str(source), "-B", str(source / "build")],
                text=True, capture_output=True)

    def test_accepts_static_target_closure(self):
        result = self.configure(transitive=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_direct_shared_target(self):
        result = self.configure(kind="SHARED")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is SHARED_LIBRARY", result.stderr)

    def test_rejects_transitive_shared_target(self):
        result = self.configure(kind="SHARED", transitive=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is SHARED_LIBRARY", result.stderr)

    def test_rejects_windows_import_archive(self):
        result = self.configure(library="libboost_log.dll.a")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("libboost_log.dll.a", result.stderr)

    def test_rejects_shared_location_for_unknown_target(self):
        result = self.configure(library="libboost_log.so")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("libboost_log.so", result.stderr)

    def test_rejects_linker_script_disguised_as_archive(self):
        result = self.configure(magic=b"INPUT(libboost_log.so)\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Not a Boost static archive", result.stderr)

    def test_rejects_missing_location(self):
        result = self.configure(missing=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No static archive location", result.stderr)


class LinuxBoostBuildFlagsTests(unittest.TestCase):
    def test_target_hardening_and_caller_flags_reach_static_b2_build(self):
        for triplet, architecture, cross in (("x86_64-linux-gnu", "amd64", False),
                                             ("arm-linux-gnueabihf", "armhf", True)):
            with self.subTest(target=triplet), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                scripts = root / "scripts"
                scripts.mkdir()
                builder = scripts / "build-boost-linux.sh"
                builder.write_bytes((ROOT / ".github/scripts/build-boost-linux.sh").read_bytes())
                (scripts / "patch-boost-log-queue.py").write_text("")
                archive = root / "source.tar.bz2"
                b2 = (f"#!{sys.executable}\nimport json, os, pathlib, sys\n"
                      "pathlib.Path(os.environ['BOOST_TEST_B2']).write_text(json.dumps(sys.argv[1:]))\n"
                      "sys.exit(77)\n")
                with tarfile.open(archive, "w:bz2") as output:
                    for name, content in (("bootstrap.sh", "#!/bin/sh\nexit 0\n"), ("b2", b2)):
                        data = content.encode()
                        member = tarfile.TarInfo(f"boost_1_92_0/{name}")
                        member.size, member.mode = len(data), 0o755
                        output.addfile(member, io.BytesIO(data))
                (scripts / "linux-boost-config.sh").write_text(
                    "linux_boost_source_config() { :; }\n"
                    "LINUX_BOOST_VERSION=1.92.0\nLINUX_BOOST_SOURCE_URL=fixture\n"
                    f"LINUX_BOOST_SOURCE_SHA256={hashlib.sha256(archive.read_bytes()).hexdigest()}\n")
                tools = root / "bin"
                tools.mkdir()
                mock = f"#!{sys.executable}\n" + r'''
import json, os, pathlib, shutil, sys
name = pathlib.Path(sys.argv[0]).name
if name == 'fixture-g++':
    assert sys.argv[1:] == ['-dumpmachine']
    print(os.environ['BOOST_TEST_TRIPLET'])
elif name == 'dpkg-architecture':
    assert sys.argv[1:] == ['-t', os.environ['BOOST_TEST_TRIPLET'], '-qDEB_HOST_ARCH']
    print(os.environ['BOOST_TEST_ARCH'])
elif name == 'dpkg-buildflags':
    assert os.environ['DEB_HOST_ARCH'] == os.environ['BOOST_TEST_ARCH']
    assert os.environ['DEB_HOST_GNU_TYPE'] == os.environ['BOOST_TEST_TRIPLET']
    assert 'hardening=+all' in os.environ['DEB_BUILD_MAINT_OPTIONS'].split()
    flags = {'CPPFLAGS': '-D_FORTIFY_SOURCE=2',
             'CXXFLAGS': '-O2 -fstack-protector-strong -Wformat -Werror=format-security'}
    assert sys.argv[1] == '--get'
    print(flags[sys.argv[2]])
elif name == 'curl':
    shutil.copyfile(os.environ['BOOST_TEST_SOURCE'], sys.argv[sys.argv.index('-o') + 1])
else:
    raise AssertionError(name)
'''
                for name in ("fixture-g++", "dpkg-architecture", "dpkg-buildflags", "curl"):
                    tool = tools / name
                    tool.write_text(mock)
                    tool.chmod(0o755)
                capture = root / "b2.json"
                clean_environment = {key: value for key, value in os.environ.items()
                                     if not key.startswith("DEB_")}
                environment = {**clean_environment, "PATH": f"{tools}:{os.environ['PATH']}",
                               "CXX": "fixture-g++", "CMAKE_BUILD_PARALLEL_LEVEL": "1",
                               "RUNNER_TEMP": str(root), "BOOST_TEST_SOURCE": str(archive),
                               "BOOST_TEST_B2": str(capture), "BOOST_TEST_TRIPLET": triplet,
                               "BOOST_TEST_ARCH": architecture, "CPPFLAGS": "-DCALLER_CPP=1",
                               "CXXFLAGS": "-O1 -DCALLER_CXX=1 -fPIE", "LDFLAGS": "-Wl,--as-needed"}
                environment.pop("BOOST_SYSROOT", None)
                if cross:
                    environment["BOOST_SYSROOT"] = str(root / "sysroot")
                result = subprocess.run(["bash", str(builder), "1.92.0", str(root / "prefix")],
                                        env=environment, text=True, capture_output=True)
                self.assertEqual(result.returncode, 77, result.stdout + result.stderr)
                arguments = json.loads(capture.read_text())
                flags = next(arg.removeprefix("cxxflags=") for arg in arguments if arg.startswith("cxxflags="))
                self.assertIn("-D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong -Wformat -Werror=format-security", flags)
                self.assertIn("-DCALLER_CPP=1 -O1 -DCALLER_CXX=1 -fPIE -fPIC", flags)
                linkflags = next(arg for arg in arguments if arg.startswith("linkflags="))
                self.assertTrue(linkflags.startswith("linkflags=-Wl,--as-needed"), linkflags)
                self.assertNotIn("-pie", linkflags)
                self.assertIn("link=static", arguments)
                if cross:
                    self.assertIn(f"--sysroot={root}/sysroot", flags)
                    self.assertIn("architecture=arm", arguments)


if __name__ == "__main__":
    unittest.main()
