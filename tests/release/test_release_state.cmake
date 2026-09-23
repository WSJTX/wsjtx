cmake_minimum_required (VERSION 3.12)

if (NOT SOURCE_DIR)
  message (FATAL_ERROR "SOURCE_DIR is required")
endif ()

include ("${SOURCE_DIR}/CMake/Modules/read_release_state.cmake")

function (assert_equal actual expected description)
  if (NOT "${actual}" STREQUAL "${expected}")
    message (FATAL_ERROR "${description}: expected '${expected}', got '${actual}'")
  endif ()
endfunction ()

function (expect_invalid name contents)
  set (_fixture "${_fixture_dir}/${name}.txt")
  file (WRITE "${_fixture}" "${contents}")
  execute_process (
    COMMAND "${CMAKE_COMMAND}"
      -D RELEASE_STATE_READER=${SOURCE_DIR}/CMake/Modules/read_release_state.cmake
      -D RELEASE_STATE_FILE=${_fixture}
      -P ${SOURCE_DIR}/tests/release/validate_release_state_fixture.cmake
    RESULT_VARIABLE invalid_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if (invalid_result EQUAL 0)
    message (FATAL_ERROR "Invalid release state was accepted: ${name}")
  endif ()
endfunction ()

wsjt_read_release_state (
  "${SOURCE_DIR}/release-state.txt"
  version channel rc revision)

if (EXPECT_ARCHIVE_REVISION)
  assert_equal ("${revision}" "${EXPECT_ARCHIVE_REVISION}" "archive revision")
else ()
  assert_equal ("${revision}" "" "working-tree archive placeholder")
endif ()

if (TEST_BINARY_DIR)
  set (_fixture_dir "${TEST_BINARY_DIR}")
else ()
  set (_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/release-state-test")
endif ()
file (MAKE_DIRECTORY "${_fixture_dir}")
file (WRITE "${_fixture_dir}/rc.txt"
  "version=3.2.0\nchannel=RC\nrc=7\nrevision=0123456789abcdef0123456789abcdef01234567\n")
wsjt_read_release_state (
  "${_fixture_dir}/rc.txt"
  version channel rc revision)
assert_equal ("${channel}" "RC" "RC channel")
assert_equal ("${rc}" "7" "RC number")
assert_equal ("${revision}" "0123456789abcdef0123456789abcdef01234567" "expanded revision")

file (WRITE "${_fixture_dir}/unsigned.txt"
  "version=3.2.0\nchannel=RC\nrc=1\nrevision=$Format:%H$\nwindows_signing=unsigned\n")
wsjt_read_release_state (
  "${_fixture_dir}/unsigned.txt"
  version channel rc revision)
assert_equal ("${channel}" "RC" "unsigned release channel")
assert_equal ("${rc}" "1" "unsigned release number")

file (WRITE "${_fixture_dir}/ga.txt"
  "version=3.2.0\nchannel=GA\nrc=\nrevision=$Format:%H$\n")
wsjt_read_release_state (
  "${_fixture_dir}/ga.txt"
  version channel rc revision)
assert_equal ("${channel}" "GA" "GA channel")
assert_equal ("${rc}" "" "GA RC number")

expect_invalid (ga_with_rc
  "version=3.2.0\nchannel=GA\nrc=1\nrevision=$Format:%H$\n")
expect_invalid (rc_without_number
  "version=3.2.0\nchannel=RC\nrc=\nrevision=$Format:%H$\n")
expect_invalid (invalid_version
  "version=3.2\nchannel=DEVEL\nrc=\nrevision=$Format:%H$\n")
expect_invalid (invalid_revision
  "version=3.2.0\nchannel=DEVEL\nrc=\nrevision=not-a-git-object-id\n")
expect_invalid (unknown_key
  "version=3.2.0\nchannel=DEVEL\nrc=\ncommit=$Format:%H$\n")
expect_invalid (invalid_windows_signing
  "version=3.2.0\nchannel=RC\nrc=1\nrevision=$Format:%H$\nwindows_signing=ephemeral\n")
expect_invalid (duplicate_windows_signing
  "version=3.2.0\nchannel=RC\nrc=1\nrevision=$Format:%H$\nwindows_signing=unsigned\nwindows_signing=unsigned\n")

set (_cache_source "${_fixture_dir}/cache-source")
set (_cache_build "${_fixture_dir}/cache-build")
file (MAKE_DIRECTORY "${_cache_source}")
file (WRITE "${_cache_source}/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.12)\n"
  "project(release_cache NONE)\n"
  "include(\"${SOURCE_DIR}/CMake/Modules/read_release_state.cmake\")\n"
  "wsjt_read_release_state(\"${_cache_source}/state.txt\" version channel rc revision)\n"
  "wsjt_set_source_default_cache(WSJT_RELEASE_CHANNEL \"\${channel}\" \"channel\")\n"
  "wsjt_set_source_default_cache(WSJT_RC_NUMBER \"\${rc}\" \"rc\")\n"
  "file(WRITE \"\${CMAKE_BINARY_DIR}/result.txt\" \"\${WSJT_RELEASE_CHANNEL}:\${WSJT_RC_NUMBER}\")\n")
file (WRITE "${_cache_source}/state.txt"
  "version=3.2.0\nchannel=DEVEL\nrc=\nrevision=$Format:%H$\n")
execute_process (
  COMMAND "${CMAKE_COMMAND}" -S "${_cache_source}" -B "${_cache_build}"
  RESULT_VARIABLE cache_result OUTPUT_QUIET ERROR_QUIET)
if (NOT cache_result EQUAL 0)
  message (FATAL_ERROR "Initial release cache configure failed")
endif ()
file (WRITE "${_cache_source}/state.txt"
  "version=3.2.0\nchannel=RC\nrc=2\nrevision=$Format:%H$\n")
execute_process (
  COMMAND "${CMAKE_COMMAND}" -S "${_cache_source}" -B "${_cache_build}"
  RESULT_VARIABLE cache_result OUTPUT_QUIET ERROR_QUIET)
file (READ "${_cache_build}/result.txt" cache_result_value)
assert_equal ("${cache_result_value}" "RC:2" "tracked metadata refresh")

set (_override_build "${_fixture_dir}/override-build")
execute_process (
  COMMAND "${CMAKE_COMMAND}" -S "${_cache_source}" -B "${_override_build}"
    -D WSJT_RELEASE_CHANNEL=GA -D WSJT_RC_NUMBER=
  RESULT_VARIABLE override_result OUTPUT_QUIET ERROR_QUIET)
if (NOT override_result EQUAL 0)
  message (FATAL_ERROR "Explicit release cache override configure failed")
endif ()
file (READ "${_override_build}/result.txt" override_result_value)
assert_equal ("${override_result_value}" "GA:" "explicit cache override")
