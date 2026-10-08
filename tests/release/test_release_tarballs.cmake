cmake_minimum_required (VERSION 3.12)

if (NOT SOURCE_DIR OR NOT TEST_BINARY_DIR)
  message (FATAL_ERROR "SOURCE_DIR and TEST_BINARY_DIR are required")
endif ()

include ("${SOURCE_DIR}/CMake/Modules/read_release_tarballs.cmake")
file (MAKE_DIRECTORY "${TEST_BINARY_DIR}")

function (assert_programs file expected description)
  wsjt_read_release_tarballs ("${file}" programs)
  if (NOT "${programs}" STREQUAL "${expected}")
    message (FATAL_ERROR "${description}: expected '${expected}', got '${programs}'")
  endif ()
endfunction ()

function (expect_invalid name contents)
  set (fixture "${TEST_BINARY_DIR}/${name}.txt")
  file (WRITE "${fixture}" "${contents}")
  execute_process (
    COMMAND "${CMAKE_COMMAND}"
      -D RELEASE_TARBALLS_READER=${SOURCE_DIR}/CMake/Modules/read_release_tarballs.cmake
      -D RELEASE_TARBALLS_FILE=${fixture}
      -P ${SOURCE_DIR}/tests/release/validate_release_tarballs_fixture.cmake
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error)
  if (result EQUAL 0)
    message (FATAL_ERROR "Invalid release tarball list was accepted: ${name}")
  endif ()
endfunction ()

assert_programs ("${SOURCE_DIR}/CMake/release-tarballs.txt"
  "jt9;wsjt-codec;wsprd;wsprcode;encode77;ft4sim;jt4sim;jt65sim;cwsim;sfoxsim"
  "committed list")

file (WRITE "${TEST_BINARY_DIR}/forms.txt"
  "# comment\r\n\r\n   \r\n  # indented comment\r\nengine\tjt9  wsjt-codec\r\nwsprd wsprd")
assert_programs ("${TEST_BINARY_DIR}/forms.txt" "jt9;wsjt-codec;wsprd"
  "blank, comment, tab and CRLF lines")

expect_invalid (no_programs "jt9 jt9\ntesting\n")
expect_invalid (named_twice "jt9 jt9\njt9 wsjt-codec\n")
expect_invalid (only_comments "# comment\n\n")
