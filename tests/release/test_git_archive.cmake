cmake_minimum_required (VERSION 3.12)

if (NOT SOURCE_DIR OR NOT TEST_BINARY_DIR OR NOT GIT_EXECUTABLE)
  message (FATAL_ERROR "SOURCE_DIR, TEST_BINARY_DIR, and GIT_EXECUTABLE are required")
endif ()

set (archive "${TEST_BINARY_DIR}/source.tar")
set (extract_dir "${TEST_BINARY_DIR}/archive-source")
file (REMOVE_RECURSE "${extract_dir}")
file (MAKE_DIRECTORY "${extract_dir}")
# Job containers may see the runner-mounted checkout as owned by another user.
set (git_source_args -c "safe.directory=${SOURCE_DIR}" -C "${SOURCE_DIR}")
execute_process (
  COMMAND "${GIT_EXECUTABLE}" ${git_source_args} rev-parse HEAD
  RESULT_VARIABLE revision_result
  OUTPUT_VARIABLE revision
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process (
  COMMAND "${GIT_EXECUTABLE}" ${git_source_args} archive --format=tar --output=${archive} HEAD
  RESULT_VARIABLE archive_result)
execute_process (
  COMMAND "${CMAKE_COMMAND}" -E tar xf "${archive}"
  WORKING_DIRECTORY "${extract_dir}"
  RESULT_VARIABLE extract_result)
if (NOT revision_result EQUAL 0 OR NOT archive_result EQUAL 0 OR NOT extract_result EQUAL 0)
  message (FATAL_ERROR "Could not create and extract the Git source archive")
endif ()

execute_process (
  COMMAND "${CMAKE_COMMAND}"
    -D SOURCE_DIR=${extract_dir}
    -D TEST_BINARY_DIR=${TEST_BINARY_DIR}/archive-fixtures
    -D EXPECT_ARCHIVE_REVISION=${revision}
    -P ${extract_dir}/tests/release/test_release_state.cmake
  RESULT_VARIABLE state_result)
if (NOT state_result EQUAL 0)
  message (FATAL_ERROR "Extracted Git archive did not preserve release identity")
endif ()

include ("${extract_dir}/CMake/Modules/read_release_state.cmake")
wsjt_read_release_state (
  "${extract_dir}/release-state.txt"
  archive_version archive_channel archive_rc archive_revision)
include ("${extract_dir}/CMake/Modules/set_build_type.cmake")
set (PROJECT_NAME wsjtx)
string (REPLACE "." ";" archive_version_parts "${archive_version}")
list (GET archive_version_parts 0 PROJECT_VERSION_MAJOR)
list (GET archive_version_parts 1 PROJECT_VERSION_MINOR)
list (GET archive_version_parts 2 PROJECT_VERSION_PATCH)
set (WSJT_RELEASE_CHANNEL "${archive_channel}" CACHE STRING "" FORCE)
set (WSJT_RC_NUMBER "${archive_rc}" CACHE STRING "" FORCE)
set_build_type ()
if (archive_channel STREQUAL "DEVEL")
  set (expected_version "${archive_version}-devel")
elseif (archive_channel STREQUAL "RC")
  set (expected_version "${archive_version}-rc${archive_rc}")
else ()
  set (expected_version "${archive_version}")
endif ()
set (actual_version
  "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}${BUILD_TYPE_REVISION}")
if (NOT actual_version STREQUAL expected_version)
  message (FATAL_ERROR
    "Archived source produced version '${actual_version}', expected '${expected_version}'")
endif ()

set (version_output_dir "${TEST_BINARY_DIR}/archive-version")
file (MAKE_DIRECTORY "${version_output_dir}")
execute_process (
  COMMAND "${CMAKE_COMMAND}"
    -D SOURCE_DIR=${extract_dir}
    -D BINARY_DIR=${version_output_dir}
    -D OUTPUT_DIR=${version_output_dir}
    -D SOURCE_REVISION=${archive_revision}
    -P ${extract_dir}/CMake/getsvn.cmake
  RESULT_VARIABLE version_result)
if (NOT version_result EQUAL 0)
  message (FATAL_ERROR "Could not generate revision header from archived source")
endif ()
string (SUBSTRING "${archive_revision}" 0 6 short_revision)
file (READ "${version_output_dir}/scs_version.h" revision_header)
if (NOT revision_header MATCHES "#define SCS_VERSION_STR \"${short_revision}\"")
  message (FATAL_ERROR "Archived source revision was not written to scs_version.h")
endif ()
