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
  COST 18
  TIMEOUT 180)

foreach (codec wsjtx map65)
  if (codec STREQUAL "wsjtx")
    set (codec_dir "${CMAKE_SOURCE_DIR}/lib/ftrsd")
    set (wrapper "${CMAKE_SOURCE_DIR}/lib/wrapkarn.c")
  else ()
    set (codec_dir "${CMAKE_SOURCE_DIR}/map65/libm65")
    set (wrapper "${codec_dir}/wrapkarn.c")
  endif ()

  add_executable (test_rs_${codec}
    test_rs_codec.c
    ${codec_dir}/init_rs.c
    ${codec_dir}/encode_rs.c
    ${codec_dir}/decode_rs.c
    ${wrapper})
  target_compile_definitions (test_rs_${codec} PRIVATE BIGSYM=1)
  target_include_directories (test_rs_${codec} PRIVATE ${codec_dir})
  target_compile_features (test_rs_${codec} PRIVATE c_std_99)
  set_target_properties (test_rs_${codec} PROPERTIES AUTOMOC OFF)
  add_test (NAME test_rs_${codec} COMMAND test_rs_${codec})
  set_tests_properties (test_rs_${codec} PROPERTIES TIMEOUT 15)
endforeach ()

add_executable (test_interleave63
  test_interleave63.f90
  ${CMAKE_SOURCE_DIR}/lib/interleave63.f90
  ${CMAKE_SOURCE_DIR}/lib/move.f90)
set_target_properties (test_interleave63 PROPERTIES
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test (NAME test_interleave63 COMMAND test_interleave63)

add_executable (test_jt65_xcor_silence
  test_jt65_xcor_silence.f90)
wsjt_link_common_fortran (test_jt65_xcor_silence)

add_test (NAME test_jt65_xcor_silence COMMAND test_jt65_xcor_silence)
