add_executable (test_jt65_source_encoding
  jt65_test_vectors.f90
  test_jt65_source_encoding.f90)
wsjt_link_common_fortran (test_jt65_source_encoding)

add_test (NAME test_jt65_source_encoding COMMAND test_jt65_source_encoding)
