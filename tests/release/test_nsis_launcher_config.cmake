# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required (VERSION 3.12)

if (NOT SOURCE_DIR OR NOT TEST_BINARY_DIR)
  message (FATAL_ERROR "SOURCE_DIR and TEST_BINARY_DIR are required")
endif ()

function (check_launcher_config name build_type configurations package_config expected)
  set (PROJECT_SOURCE_DIR "${SOURCE_DIR}")
  set (PROJECT_BINARY_DIR "${TEST_BINARY_DIR}/${name}")
  set (CMAKE_BUILD_TYPE "${build_type}")
  set (CMAKE_CONFIGURATION_TYPES "${configurations}")
  file (MAKE_DIRECTORY "${PROJECT_BINARY_DIR}")
  set (launcher "${PROJECT_BINARY_DIR}/${expected}/wsjtx-launch-user.exe")
  file (WRITE "${PROJECT_BINARY_DIR}/NSIS-launcher-${expected}.cmake"
    "set(WSJTX_NSIS_LAUNCHER \"${launcher}\")\n")
  configure_file ("${SOURCE_DIR}/CMakeCPackOptions.cmake.in"
    "${PROJECT_BINARY_DIR}/CMakeCPackOptions.cmake" @ONLY)

  set (CPACK_GENERATOR NSIS)
  set (CPACK_BUILD_CONFIG "${package_config}")
  include ("${PROJECT_BINARY_DIR}/CMakeCPackOptions.cmake")
  string (REPLACE "/" "\\" native_launcher "${launcher}")
  if (NOT WSJTX_NSIS_LAUNCHER STREQUAL native_launcher)
    message (FATAL_ERROR "${name}: selected the wrong installer launcher")
  endif ()
endfunction ()

check_launcher_config (single_release Release "" "" Release)
check_launcher_config (single_uppercase RELEASE "" Release RELEASE)
check_launcher_config (single_debug Debug "" Release Debug)
check_launcher_config (multi_debug "" "Debug;Release" Debug Debug)
check_launcher_config (multi_default "" "Debug;Release" "" Release)
