add_executable (test_fst4_generator
  test_fst4_generator.f90)
target_include_directories (test_fst4_generator PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_fst4_generator)

add_test (NAME test_fst4_generator COMMAND test_fst4_generator)
