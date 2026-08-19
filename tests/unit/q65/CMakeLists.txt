add_executable (test_q65_workspace
  test_q65_workspace.f90
  ${CMAKE_SOURCE_DIR}/lib/qra/q65/q65_workspace.f90)

add_test (NAME test_q65_workspace COMMAND test_q65_workspace)

add_executable (test_q65_source_encoding
  test_q65_source_encoding.f90)
wsjt_link_common_fortran (test_q65_source_encoding)

add_test (NAME test_q65_source_encoding COMMAND test_q65_source_encoding)

add_executable (test_q65_decode_pipeline
  q65_test_fixture.f90
  test_q65_decode_pipeline.f90)
wsjt_link_common_fortran (test_q65_decode_pipeline)

add_test (NAME test_q65_decode_pipeline COMMAND test_q65_decode_pipeline)
set_tests_properties (test_q65_decode_pipeline PROPERTIES
  TIMEOUT 180)
