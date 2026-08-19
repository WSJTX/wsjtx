add_executable (test_jt65_source_encoding
  jt65_test_vectors.f90
  test_jt65_source_encoding.f90)
wsjt_link_common_fortran (test_jt65_source_encoding)

add_test (NAME test_jt65_source_encoding COMMAND test_jt65_source_encoding)

add_executable (test_jt65_decode_pipeline
  jt65_test_vectors.f90
  jt65_test_fixture.f90
  test_jt65_decode_pipeline.f90)
wsjt_link_common_fortran (test_jt65_decode_pipeline)

add_test (NAME test_jt65_decode_pipeline COMMAND test_jt65_decode_pipeline)
set_tests_properties (test_jt65_decode_pipeline PROPERTIES
  TIMEOUT 180)
