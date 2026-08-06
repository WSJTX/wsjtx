add_executable (test_ft8_decode_ranges
  test_ft8_decode_ranges.f90)
target_include_directories (test_ft8_decode_ranges PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_ft8_decode_ranges)

add_test (NAME test_ft8_decode_ranges COMMAND test_ft8_decode_ranges)
