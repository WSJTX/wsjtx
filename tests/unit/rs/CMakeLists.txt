cmake_minimum_required (VERSION 3.7.2)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project (test_rs_codec LANGUAGES C)
  enable_testing ()
endif ()

get_filename_component (wsjt_source_dir "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

foreach (codec wsjtx map65)
  if (codec STREQUAL "wsjtx")
    set (codec_dir "${wsjt_source_dir}/lib/ftrsd")
    set (wrapper "${wsjt_source_dir}/lib/wrapkarn.c")
  else ()
    set (codec_dir "${wsjt_source_dir}/map65/libm65")
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
endforeach ()
