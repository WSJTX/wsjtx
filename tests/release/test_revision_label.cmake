# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required (VERSION 3.12)

if (NOT SOURCE_DIR OR NOT TEST_BINARY_DIR OR NOT GIT_EXECUTABLE)
  message (FATAL_ERROR "SOURCE_DIR, TEST_BINARY_DIR and GIT_EXECUTABLE are required")
endif ()

set (GIT_FOUND TRUE)
set (CMAKE_CURRENT_BINARY_DIR "${TEST_BINARY_DIR}")
include ("${SOURCE_DIR}/CMake/Modules/GetGitRevisionDescription.cmake")

set (checkout "${TEST_BINARY_DIR}/checkout")
file (REMOVE_RECURSE "${checkout}")
file (MAKE_DIRECTORY "${checkout}")
set (git "${GIT_EXECUTABLE}" -C "${checkout}")

function (run)
  execute_process (COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
  if (NOT result EQUAL 0)
    message (FATAL_ERROR "${ARGN} failed: ${error}")
  endif ()
endfunction ()

function (expect_label expected description)
  git_local_changes ("${checkout}" label)
  if (NOT label STREQUAL expected)
    message (FATAL_ERROR "${description}: expected ${expected}, got ${label}")
  endif ()
endfunction ()

run (${git} init -q)
foreach (setting "user.name;Release" "user.email;release@example.invalid" "commit.gpgsign;false"
    "core.autocrlf;false" "maintenance.auto;false" "gc.auto;0")
  run (${git} config ${setting})
endforeach ()
file (WRITE "${checkout}/source.txt" "one\n")
run (${git} add source.txt)
run (${git} commit -q --no-verify -m label)
expect_label (CLEAN "fresh checkout")

run (${CMAKE_COMMAND} -E sleep 2)
file (TOUCH "${checkout}/source.txt")
file (SHA256 "${checkout}/.git/index" index_before)
expect_label (CLEAN "file touched, content unchanged")
file (SHA256 "${checkout}/.git/index" index_after)
if (NOT index_after STREQUAL index_before)
  message (FATAL_ERROR "the check wrote the index")
endif ()

# Windows CI has no ownership failure, and MSYS2's git may spell the path differently.
if (NOT CMAKE_HOST_WIN32)
  set (ENV{GIT_TEST_ASSUME_DIFFERENT_OWNER} 1)
  expect_label (CLEAN "checkout owned by another user")
  unset (ENV{GIT_TEST_ASSUME_DIFFERENT_OWNER})
endif ()

file (APPEND "${checkout}/source.txt" "two\n")
expect_label (DIRTY "content changed")

run (${git} add source.txt)
expect_label (DIRTY "change staged")
