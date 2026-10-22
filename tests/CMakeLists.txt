find_package (Qt5Test 5 REQUIRED)

add_test (
  NAME test_release_state
  COMMAND ${CMAKE_COMMAND}
    -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -D TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-state
    -P ${CMAKE_SOURCE_DIR}/tests/release/test_release_state.cmake)

add_test (
  NAME test_release_tarballs
  COMMAND ${CMAKE_COMMAND}
    -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -D TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-tarballs
    -P ${CMAKE_SOURCE_DIR}/tests/release/test_release_tarballs.cmake)

get_property (release_configure_depends DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY CMAKE_CONFIGURE_DEPENDS)
string (REPLACE ";" "|" release_configure_depends "${release_configure_depends}")
add_test (
  NAME test_release_state_dependency
  COMMAND ${CMAKE_COMMAND}
    -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -D CONFIGURE_DEPENDS=${release_configure_depends}
    -P ${CMAKE_SOURCE_DIR}/tests/release/test_release_state_dependency.cmake)

find_package (Git QUIET)
if (GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  add_test (
    NAME test_release_git_archive
    COMMAND ${CMAKE_COMMAND}
      -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
      -D TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-git-archive
      -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
      -P ${CMAKE_SOURCE_DIR}/tests/release/test_git_archive.cmake)
endif ()
if (GIT_FOUND)
  add_test (
    NAME test_release_revision_label
    COMMAND ${CMAKE_COMMAND}
      -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
      -D TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-revision-label
      -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
      -P ${CMAKE_SOURCE_DIR}/tests/release/test_revision_label.cmake)
endif ()

add_test (
  NAME test_release_build_type
  COMMAND ${CMAKE_COMMAND}
    -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -D TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-build-type
    -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
    -P ${CMAKE_SOURCE_DIR}/tests/release/test_build_type.cmake)

# A cross-compiled tree runs its tests in another container than the one that built it.
if (NOT CMAKE_CROSSCOMPILING)
  add_test (
    NAME test_release_expiry_build_step
    COMMAND ${CMAKE_COMMAND}
      -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
      -D BINARY_DIR=${CMAKE_BINARY_DIR}
      -D TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/release-expiry-build-step
      -P ${CMAKE_SOURCE_DIR}/tests/release/test_expiry_build_step.cmake)
  set_tests_properties (test_release_expiry_build_step PROPERTIES RUN_SERIAL TRUE)
endif ()

foreach (package_tool dpkg rpm)
  add_test (
    NAME test_release_package_order_${package_tool}
    COMMAND ${CMAKE_COMMAND}
      -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
      -D TOOL=${package_tool}
      -P ${CMAKE_SOURCE_DIR}/tests/release/test_package_version_order.cmake)
  set_tests_properties (test_release_package_order_${package_tool} PROPERTIES
    SKIP_REGULAR_EXPRESSION "Skipped: ${package_tool} is not installed")
endforeach ()

add_compile_options ("$<$<COMPILE_LANGUAGE:Fortran>:-Wall;-Wno-conversion;-fno-second-underscore;-fno-f2c>")
add_compile_options ("$<$<AND:$<COMPILE_LANGUAGE:Fortran>,$<CONFIG:Debug>>:-fbounds-check>")
add_compile_options ("$<$<AND:$<COMPILE_LANGUAGE:Fortran>,$<NOT:$<CONFIG:Debug>>>:-funroll-all-loops>")
add_compile_options ("$<$<AND:$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>>:-Wall;-Wextra>")
add_compile_options ("$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU>>:-Wno-pragmas>")
add_compile_options ("$<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:GNU>>,$<NOT:$<CONFIG:Debug>>>:-fdata-sections;-ffunction-sections>")
if (WSJT_BUILD_FORTRAN_OPENMP)
  add_compile_options ("$<$<AND:$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>,$<C_COMPILER_ID:GNU>>:${OpenMP_C_FLAGS}>")
endif ()

set (CMAKE_AUTOMOC ON)
include_directories (${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR})

add_subdirectory (unit/audio)
add_subdirectory (unit/band_hopping)
add_subdirectory (unit/common)
add_subdirectory (unit/decoder_ipc)
add_subdirectory (unit/decoder_engine)
add_subdirectory (unit/hrd)
add_subdirectory (unit/hash)
add_subdirectory (unit/message_filter)
add_subdirectory (unit/msk)
add_subdirectory (unit/network)
add_subdirectory (unit/qmap)
add_subdirectory (unit/jtty)
add_subdirectory (unit/jt9)
add_subdirectory (unit/jpl)
add_subdirectory (unit/logbook)
add_subdirectory (unit/map65)
add_subdirectory (unit/echo)
add_subdirectory (unit/ft8)
add_subdirectory (unit/fst4)
add_subdirectory (unit/legacy72)
add_subdirectory (unit/packjt77)
add_subdirectory (unit/q65)
add_subdirectory (unit/jt65)
add_subdirectory (unit/superfox)
add_subdirectory (unit/streaming)
add_subdirectory (unit/tci)
add_subdirectory (unit/wsprd)
