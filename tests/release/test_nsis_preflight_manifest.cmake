# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required (VERSION 3.12)

if (NOT SOURCE_DIR OR NOT TEST_BINARY_DIR)
  message (FATAL_ERROR "SOURCE_DIR and TEST_BINARY_DIR are required")
endif ()
set (stage "${TEST_BINARY_DIR}/staged package")
set (manifest "${TEST_BINARY_DIR}/destination.nsh")
file (REMOVE_RECURSE "${stage}")
file (MAKE_DIRECTORY "${stage}/bin" "${stage}/data/café")
file (WRITE "${stage}/bin/wsjtx.exe" "executable")
file (WRITE "${stage}/data/café/dollar $ payload.txt" "payload")
file (WRITE "${TEST_BINARY_DIR}/unrelated.txt" "must not be checked")

function (generate output)
  execute_process (COMMAND "${CMAKE_COMMAND}"
    "-DSTAGING_DIR=${stage}" "-DOUTPUT_FILE=${output}"
    -P "${SOURCE_DIR}/CMake/NSIS/GeneratePreflight.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
  if (NOT status EQUAL 0)
    message (FATAL_ERROR "Preflight generation failed: ${stdout}${stderr}")
  endif ()
endfunction ()

generate ("${manifest}")
file (READ "${manifest}" content)
foreach (expected IN ITEMS
    "Push \"Uninstall.exe\"" "Push \"bin\\jt9stream.exe\"" "Push \"bin\\wsjtx.exe\""
    "Push \"data\\café\\dollar $$ payload.txt\"" "Call CheckDestinationDirectory")
  string (FIND "${content}" "${expected}" found)
  if (found EQUAL -1)
    message (FATAL_ERROR "Missing preflight instruction: ${expected}\n${content}")
  endif ()
endforeach ()
string (FIND "${content}" "unrelated.txt" unrelated)
if (NOT unrelated EQUAL -1)
  message (FATAL_ERROR "Preflight includes a file outside the staged package")
endif ()
generate ("${TEST_BINARY_DIR}/second.nsh")
file (READ "${TEST_BINARY_DIR}/second.nsh" second)
if (NOT content STREQUAL second)
  message (FATAL_ERROR "Preflight generation is not deterministic")
endif ()

execute_process (COMMAND "${CMAKE_COMMAND}"
  "-DSTAGING_DIR=${TEST_BINARY_DIR}/absent" "-DOUTPUT_FILE=${manifest}"
  -P "${SOURCE_DIR}/CMake/NSIS/GeneratePreflight.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if (status EQUAL 0)
  message (FATAL_ERROR "Missing staging directory was accepted")
endif ()
execute_process (COMMAND "${CMAKE_COMMAND}"
  "-DSTAGING_DIR=${stage}" "-DOUTPUT_FILE=${stage}"
  -P "${SOURCE_DIR}/CMake/NSIS/GeneratePreflight.cmake"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if (status EQUAL 0)
  message (FATAL_ERROR "Unwritable manifest output was accepted")
endif ()
