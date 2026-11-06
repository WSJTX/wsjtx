import pathlib
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


if __name__ == "__main__":
    unittest.main()
