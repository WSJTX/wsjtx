set (packjt77_test_helper_sources packjt77_test_helpers.f90)

add_executable (test_packjt77_invariants
  test_packjt77_invariants.f90
  ${packjt77_test_helper_sources})
target_include_directories (test_packjt77_invariants PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_packjt77_invariants)

add_test (NAME test_packjt77_invariants COMMAND test_packjt77_invariants)

add_executable (test_packjt77var_invariants
  test_packjt77var_invariants.f90
  ${packjt77_test_helper_sources})
target_include_directories (test_packjt77var_invariants PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_packjt77var_invariants)

add_test (NAME test_packjt77var_invariants COMMAND test_packjt77var_invariants)
