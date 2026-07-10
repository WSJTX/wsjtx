# Build the shared helper module once so parallel test targets do not race
# while writing the same Fortran .mod interface file.
add_library (packjt77_test_helpers STATIC packjt77_test_helpers.f90)
set_target_properties (packjt77_test_helpers PROPERTIES
  Fortran_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packjt77_test_helpers")
target_include_directories (packjt77_test_helpers PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}/packjt77_test_helpers")
wsjt_link_common_fortran (packjt77_test_helpers)

add_executable (test_packjt77_grammar_gate
  test_packjt77_grammar_gate.f90)
target_include_directories (test_packjt77_grammar_gate PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_packjt77_grammar_gate)

add_test (NAME test_packjt77_grammar_gate COMMAND test_packjt77_grammar_gate)

add_executable (test_packjt77_invariants
  test_packjt77_invariants.f90
  ${CMAKE_SOURCE_DIR}/lib/ft2/genft2.f90)
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
target_link_libraries (test_packjt77sdvar_invariants PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77sdvar_invariants)

add_test (NAME test_packjt77sdvar_invariants COMMAND test_packjt77sdvar_invariants)

add_executable (test_packjt77_protocol_vectors
  test_packjt77_protocol_vectors.f90)
target_include_directories (test_packjt77_protocol_vectors PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77_protocol_vectors PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77_protocol_vectors)

add_test (NAME test_packjt77_protocol_vectors COMMAND test_packjt77_protocol_vectors)

add_executable (test_packjt77_schema_encoder
  test_packjt77_schema_encoder.f90)
target_include_directories (test_packjt77_schema_encoder PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77_schema_encoder PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77_schema_encoder)

add_test (NAME test_packjt77_schema_encoder COMMAND test_packjt77_schema_encoder)

add_executable (test_packjt77_schema_decoder
  test_packjt77_schema_decoder.f90)
target_include_directories (test_packjt77_schema_decoder PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77_schema_decoder PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77_schema_decoder)

add_test (NAME test_packjt77_schema_decoder COMMAND test_packjt77_schema_decoder)

add_executable (test_packjt77_hash_state
  test_packjt77_hash_state.f90)
target_include_directories (test_packjt77_hash_state PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77_hash_state PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77_hash_state)

add_test (NAME test_packjt77_hash_state COMMAND test_packjt77_hash_state)

add_executable (test_packjt77_pack28_sweep
  test_packjt77_pack28_sweep.f90)
target_include_directories (test_packjt77_pack28_sweep PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_packjt77_pack28_sweep)

add_test (NAME test_packjt77_pack28_sweep COMMAND test_packjt77_pack28_sweep)

add_executable (test_packjt77_domain_sweeps
  test_packjt77_domain_sweeps.f90)
target_include_directories (test_packjt77_domain_sweeps PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77_domain_sweeps PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77_domain_sweeps)

add_test (NAME test_packjt77_domain_sweeps COMMAND test_packjt77_domain_sweeps)

add_executable (test_packjt77_mutation_sweep
  test_packjt77_mutation_sweep.f90)
target_include_directories (test_packjt77_mutation_sweep PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_packjt77_mutation_sweep PRIVATE packjt77_test_helpers)
wsjt_link_common_fortran (test_packjt77_mutation_sweep)

add_test (NAME test_packjt77_mutation_sweep COMMAND test_packjt77_mutation_sweep)

add_test (NAME lint_packjt77_idioms
  COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/lint_packjt77_idioms.sh
          ${CMAKE_SOURCE_DIR})

add_executable (test_packjt77_silent_rewrite_corpus
  test_packjt77_silent_rewrite_corpus.f90)
target_include_directories (test_packjt77_silent_rewrite_corpus PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_packjt77_silent_rewrite_corpus)

add_test (NAME test_packjt77_silent_rewrite_corpus
  COMMAND test_packjt77_silent_rewrite_corpus
          ${CMAKE_CURRENT_SOURCE_DIR}/silent_rewrite_corpus.txt --assert-zero-silent)
