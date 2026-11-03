cmake_minimum_required (VERSION 3.7.2)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project (test_interleave63 LANGUAGES Fortran)
  enable_testing ()
endif ()

get_filename_component (wsjt_source_dir "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

add_executable (test_interleave63
  test_interleave63.f90
  ${wsjt_source_dir}/lib/interleave63.f90
  ${wsjt_source_dir}/lib/move.f90)
set_target_properties (test_interleave63 PROPERTIES
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test (NAME test_interleave63 COMMAND test_interleave63)
