# Compile the shared vectors once so parallel test builds have one producer
# for the Fortran module interface.
add_library (jt65_test_vectors STATIC jt65_test_vectors.f90)
set_target_properties (jt65_test_vectors PROPERTIES
  Fortran_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/jt65_test_vectors")
target_include_directories (jt65_test_vectors PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}/jt65_test_vectors")

add_executable (test_jt65_source_encoding
  test_jt65_source_encoding.f90)
wsjt_link_common_fortran (test_jt65_source_encoding)
target_link_libraries (test_jt65_source_encoding PRIVATE jt65_test_vectors)

add_test (NAME test_jt65_source_encoding COMMAND test_jt65_source_encoding)

add_executable (test_jt65_decode_pipeline
  jt65_test_fixture.f90
  test_jt65_decode_pipeline.f90)
set_source_files_properties (test_jt65_decode_pipeline.f90 PROPERTIES
  COMPILE_FLAGS "-Wno-unused-dummy-argument")
wsjt_link_common_fortran (test_jt65_decode_pipeline)
target_link_libraries (test_jt65_decode_pipeline PRIVATE jt65_test_vectors)

add_test (NAME test_jt65_decode_pipeline COMMAND test_jt65_decode_pipeline)
set_tests_properties (test_jt65_decode_pipeline PROPERTIES
  TIMEOUT 180)

add_executable (test_jt65_xcor_silence
  test_jt65_xcor_silence.f90)
wsjt_link_common_fortran (test_jt65_xcor_silence)

add_test (NAME test_jt65_xcor_silence COMMAND test_jt65_xcor_silence)
