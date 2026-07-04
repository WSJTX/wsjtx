# Build the shared helper module once so parallel test targets do not race
# while writing the same Fortran .mod interface file.
add_library (packjt77_test_helpers STATIC packjt77_test_helpers.f90)
set_target_properties (packjt77_test_helpers PROPERTIES
  Fortran_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packjt77_test_helpers")
target_include_directories (packjt77_test_helpers PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}/packjt77_test_helpers")
wsjt_link_common_fortran (packjt77_test_helpers)

add_executable (test_packjt77_invariants
  test_packjt77_invariants.f90)
target_include_directories (test_packjt77_invariants PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77_invariants PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77_invariants)

add_test (NAME test_packjt77_invariants COMMAND test_packjt77_invariants)

add_executable (test_packjt77var_invariants
  test_packjt77var_invariants.f90)
target_include_directories (test_packjt77var_invariants PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77var_invariants PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77var_invariants)

add_test (NAME test_packjt77var_invariants COMMAND test_packjt77var_invariants)

add_executable (test_packjt77sdvar_invariants
  test_packjt77sdvar_invariants.f90)
target_include_directories (test_packjt77sdvar_invariants PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_packjt77sdvar_invariants)

add_test (NAME test_packjt77sdvar_invariants COMMAND test_packjt77sdvar_invariants)

add_executable (test_packjt77_protocol_vectors
  test_packjt77_protocol_vectors.f90)
target_include_directories (test_packjt77_protocol_vectors PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77_protocol_vectors PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77_protocol_vectors)

add_test (NAME test_packjt77_protocol_vectors COMMAND test_packjt77_protocol_vectors)

add_executable (test_packjt77_hash_state
  test_packjt77_hash_state.f90)
target_include_directories (test_packjt77_hash_state PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77_hash_state PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77_hash_state)

add_test (NAME test_packjt77_hash_state COMMAND test_packjt77_hash_state)
