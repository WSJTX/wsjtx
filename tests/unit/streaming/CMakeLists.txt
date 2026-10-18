add_executable (test_streaming_consumers test_streaming_consumers.cpp)
add_dependencies (test_streaming_consumers jt9 test_decoder_engine test_decoder_engine_q65 test_decoder_engine_fst4 test_decoder_engine_jt4 test_decoder_engine_jt65)
target_link_libraries (test_streaming_consumers Qt5::Test)
target_compile_definitions (test_streaming_consumers PRIVATE
  JT4_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/jt4-engine.wav"
  JT65_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/jt65-engine.wav"
  JT9_EXECUTABLE="$<TARGET_FILE:jt9>"
  WSJT_CODEC_EXECUTABLE="$<TARGET_FILE:wsjt-codec>"
  WSPRD_EXECUTABLE="$<TARGET_FILE:wsprd>"
  FST4_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/fst4-engine.wav"
  FST4W_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/fst4w-engine.wav"
  FT8_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/ft8-engine.wav"
  Q65_ENGINE_WAV="${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/q65-engine.wav"
)
set (streaming_consumer_fixtures ft8_engine_wav jt4_engine_wav q65_engine_wav fst4_engine_wav fst4w_engine_wav jt65_engine_wav)
if (WSJT_BUILD_UTILS)
  # sfoxsim writes 000000_000000.wav into its working directory.
  target_compile_definitions (test_streaming_consumers PRIVATE
    SUPERFOX_WAV="${CMAKE_CURRENT_BINARY_DIR}/superfox/000000_000000.wav")
  add_dependencies (test_streaming_consumers sfoxsim)
  file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/superfox")
  add_test (NAME prepare_superfox_wav
    COMMAND sfoxsim 750 0.0 AW K1JT 5 1 0 0 1 -5
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/superfox")
  set_tests_properties (prepare_superfox_wav PROPERTIES FIXTURES_SETUP superfox_wav TIMEOUT 30)
  list (APPEND streaming_consumer_fixtures superfox_wav)
endif ()

add_test (NAME test_streaming_consumers COMMAND test_streaming_consumers)
set_tests_properties (test_streaming_consumers PROPERTIES
  LABELS "streaming;runtime"
  TIMEOUT 360
  FIXTURES_REQUIRED "${streaming_consumer_fixtures}"
)
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_streaming_consumers PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()

# The GUI's decode schedule, apart from the rest of the binary: its sessions
# wait for the decoder, which the sanitizers slow several times over.
set (streaming_schedule_environment "WSJT_STREAM_SCHEDULE_TESTS=1")
set (streaming_schedule_timeout 300)
if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
  set (streaming_schedule_timeout 1200)
  list (APPEND streaming_schedule_environment "QTEST_FUNCTION_TIMEOUT=1200000")
endif ()
if (WSJT_ENABLE_TSAN)
  list (APPEND streaming_schedule_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
add_test (NAME test_streaming_gui_schedule
  COMMAND test_streaming_consumers decodesAtTheGuiStopPoint fileSharesFollowTheEngine decodesInsideALongFrame
    aConfigureMovesTheStopPoint ft8PassesFollowTheGui decodeAfterEmeDelaySetsTheDelay
    decodeAgainLeavesThePeriodInFlight)
set_tests_properties (test_streaming_gui_schedule PROPERTIES
  LABELS "streaming;runtime"
  ENVIRONMENT "${streaming_schedule_environment}"
  FIXTURES_REQUIRED "${streaming_consumer_fixtures}"
  TIMEOUT ${streaming_schedule_timeout})

# Configures the decoder would refuse, one decode each.
set (streaming_range_environment "WSJT_STREAM_RANGE_TESTS=1")
set (streaming_range_timeout 300)
if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
  set (streaming_range_timeout 1200)
  list (APPEND streaming_range_environment "QTEST_FUNCTION_TIMEOUT=1200000")
endif ()
if (WSJT_ENABLE_TSAN)
  list (APPEND streaming_range_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
add_test (NAME test_streaming_configure_ranges
  COMMAND test_streaming_consumers wsjtCodecDeclinesValuesTheEngineRefuses
    wsjtCodecAcceptsValuesAtTheEngineLimits wsjtCodecReportsRefusedDecodes)
set_tests_properties (test_streaming_configure_ranges PROPERTIES
  LABELS "streaming;runtime"
  ENVIRONMENT "${streaming_range_environment}"
  FIXTURES_REQUIRED "${streaming_consumer_fixtures}"
  TIMEOUT ${streaming_range_timeout})

# Source archives omit samples/.
if (EXISTS "${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav")
  set (streaming_mtd_environment
    "WSJT_FT8_SAMPLE_WAV=${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav"
    "WSJT_SHARE_DIR=${CMAKE_SOURCE_DIR}"
    "OMP_STACKSIZE=16M")
  # Multithreaded FT8 runs about 6 times slower under ASan and 15 times under TSan,
  # so sanitizer builds run two depth-1 rows and raise QtTest's 300 s function limit.
  set (streaming_mtd_rows multithreadedFt8MatchesWavMode)
  set (streaming_mtd_timeout 300)
  if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
    set (streaming_mtd_rows
      "multithreadedFt8MatchesWavMode:start 2, delayed"
      "multithreadedFt8MatchesWavMode:start 3")
    set (streaming_mtd_timeout 1200)
    list (APPEND streaming_mtd_environment "QTEST_FUNCTION_TIMEOUT=1200000")
  endif ()
  if (WSJT_ENABLE_TSAN)
    list (APPEND streaming_mtd_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
  endif ()
  add_test (NAME test_streaming_mtd_parity
    COMMAND test_streaming_consumers ${streaming_mtd_rows})
  set_tests_properties (test_streaming_mtd_parity PROPERTIES
    LABELS "streaming;runtime;mtd"
    ENVIRONMENT "${streaming_mtd_environment}"
    PROCESSORS 4
    TIMEOUT ${streaming_mtd_timeout})
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
