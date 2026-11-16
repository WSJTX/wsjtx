add_executable (test_wsprd_utils
  test_wsprd_utils.c
  ${CMAKE_SOURCE_DIR}/lib/lookup3.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/wsprd_utils.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/nhash.c)

add_test (NAME test_wsprd_utils COMMAND test_wsprd_utils)

# wsprd_decode.h's calls over the decoder, in one process. Three rows decode
# the in-tree sample, which source archives omit.
add_executable (test_wsprd_decode test_wsprd_decode.c
  $<TARGET_OBJECTS:wsprd_decode>
  $<TARGET_OBJECTS:wsjt_wspr>
  ${CMAKE_SOURCE_DIR}/lib/lookup3.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/nhash.c
  ${CMAKE_SOURCE_DIR}/lib/indexx.f90
  ${CMAKE_SOURCE_DIR}/lib/wsprd/osdwspr.f90)
target_include_directories (test_wsprd_decode PRIVATE ${CMAKE_SOURCE_DIR} ${FFTW3_INCLUDE_DIRS})
target_link_libraries (test_wsprd_decode ${FFTW3_LIBRARIES} ${LIBM_LIBRARIES})
set (wsprd_decode_sample "${CMAKE_SOURCE_DIR}/samples/WSPR/150426_0918.wav")
set (wsprd_decode_scratch "${CMAKE_CURRENT_BINARY_DIR}/test_wsprd_decode_hash")
file (MAKE_DIRECTORY "${wsprd_decode_scratch}")
add_test (NAME test_wsprd_decode_repeat
  COMMAND test_wsprd_decode repeat "${wsprd_decode_sample}")
add_test (NAME test_wsprd_decode_hash_roundtrip
  COMMAND test_wsprd_decode hash_roundtrip "${wsprd_decode_sample}" "${wsprd_decode_scratch}")
add_test (NAME test_wsprd_decode_run_file
  COMMAND test_wsprd_decode run_file "${wsprd_decode_sample}" "${wsprd_decode_scratch}")
add_test (NAME test_wsprd_decode_short_name
  COMMAND test_wsprd_decode short_name "${wsprd_decode_sample}" "${wsprd_decode_scratch}")
set (wsprd_decode_tests
  test_wsprd_decode_repeat test_wsprd_decode_hash_roundtrip test_wsprd_decode_run_file
  test_wsprd_decode_short_name)
if (NOT EXISTS "${wsprd_decode_sample}")
  set_tests_properties (${wsprd_decode_tests} PROPERTIES DISABLED TRUE)
endif ()
add_test (NAME test_wsprd_decode_unresolved_type3 COMMAND test_wsprd_decode unresolved_type3)
add_test (NAME test_wsprd_decode_type2_grid
  COMMAND test_wsprd_decode type2_grid "${wsprd_decode_scratch}")
add_test (NAME test_wsprd_decode_unencodable_text COMMAND test_wsprd_decode unencodable_text)
add_test (NAME test_wsprd_decode_decoder_api COMMAND test_wsprd_decode decoder_api)
list (APPEND wsprd_decode_tests test_wsprd_decode_unresolved_type3 test_wsprd_decode_type2_grid
  test_wsprd_decode_unencodable_text test_wsprd_decode_decoder_api)
set_tests_properties (${wsprd_decode_tests} PROPERTIES LABELS "decoder;wsprd" TIMEOUT 120)
