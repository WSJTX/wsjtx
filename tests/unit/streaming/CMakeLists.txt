add_executable (test_streaming_consumers test_streaming_consumers.cpp)
add_dependencies (test_streaming_consumers test_decoder_engine test_decoder_engine_q65 test_decoder_engine_fst4 test_decoder_engine_jt4)
target_link_libraries (test_streaming_consumers Qt5::Test)
target_compile_definitions (test_streaming_consumers PRIVATE
  JT4_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/jt4-engine.wav"
  WSJT_CODEC_EXECUTABLE="$<TARGET_FILE:wsjt-codec>"
  WSPRD_EXECUTABLE="$<TARGET_FILE:wsprd>"
  FST4_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/fst4-engine.wav"
  FST4W_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/fst4w-engine.wav"
  FT8_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/ft8-engine.wav"
  Q65_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/q65-engine.wav"
)

add_test (NAME test_streaming_consumers COMMAND test_streaming_consumers)
set_tests_properties (test_streaming_consumers PROPERTIES
  LABELS "streaming;runtime"
  TIMEOUT 360
  FIXTURES_REQUIRED "ft8_engine_wav;jt4_engine_wav;q65_engine_wav;fst4_engine_wav;fst4w_engine_wav"
)
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_streaming_consumers PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()

add_executable (test_streaming_period test_streaming_period.f90)
wsjt_link_common_fortran (test_streaming_period)
add_test (NAME test_streaming_period COMMAND test_streaming_period)
set_tests_properties (test_streaming_period PROPERTIES LABELS "streaming")

add_executable (test_streaming_control test_streaming_control.f90)
wsjt_link_common_fortran (test_streaming_control)
add_test (NAME test_streaming_control COMMAND test_streaming_control)
set_tests_properties (test_streaming_control PROPERTIES LABELS "streaming")

add_executable (test_streaming_apply ${PROJECT_SOURCE_DIR}/test/apply_routing_test.f90)
wsjt_link_common_fortran (test_streaming_apply)
add_test (NAME test_streaming_apply COMMAND test_streaming_apply)
set_tests_properties (test_streaming_apply PROPERTIES LABELS "streaming")
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_streaming_apply PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
