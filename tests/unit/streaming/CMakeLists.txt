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

# A producer's writes while a multithreaded decode runs.
set (streaming_read_environment "WSJT_STREAM_READ_TESTS=1")
set (streaming_read_timeout 300)
if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
  set (streaming_read_timeout 1200)
  list (APPEND streaming_read_environment "QTEST_FUNCTION_TIMEOUT=1200000")
endif ()
if (WSJT_ENABLE_TSAN)
  list (APPEND streaming_read_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
add_test (NAME test_streaming_reads_while_decoding
  COMMAND test_streaming_consumers wsjtCodecReadsWhileItDecodes)
set_tests_properties (test_streaming_reads_while_decoding PROPERTIES
  LABELS "streaming;runtime"
  ENVIRONMENT "${streaming_read_environment}"
  FIXTURES_REQUIRED "${streaming_consumer_fixtures}"
  PROCESSORS 2
  TIMEOUT ${streaming_read_timeout})

# The codec's stdin reader over a pipe on fd 0, which these platforms redirect in-process.
if (UNIX)
  find_package (Threads REQUIRED)
  add_executable (test_stream_stdin test_stream_stdin.c ${CMAKE_SOURCE_DIR}/lib/stream_stdin.c)
  target_link_libraries (test_stream_stdin Threads::Threads)
  add_test (NAME test_stream_stdin COMMAND test_stream_stdin)
  set_tests_properties (test_stream_stdin PROPERTIES LABELS "streaming" TIMEOUT 60)
  # The reader with its allocations or its thread start failing.
  add_executable (test_stream_stdin_failures test_stream_stdin_failures.c)
  target_include_directories (test_stream_stdin_failures PRIVATE ${CMAKE_SOURCE_DIR}/lib)
  target_link_libraries (test_stream_stdin_failures Threads::Threads)
  foreach (failure memory thread drained)
    add_test (NAME test_stream_stdin_${failure} COMMAND test_stream_stdin_failures ${failure})
    set_tests_properties (test_stream_stdin_${failure} PROPERTIES LABELS "streaming" TIMEOUT 60)
  endforeach ()
endif ()

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

# Live audio that falls behind: each row stalls until its audio is late (the
# longest row runs about 110 s), then sends audio while a decode runs, and
# checks the passes against the GUI: its scheduler for multithreaded FT8, its
# drop for the other modes, and no refusal for MSK144.
add_executable (test_streaming_backlog test_streaming_backlog.cpp
  ${CMAKE_SOURCE_DIR}/Ft8MtdDecodeScheduler.cpp)
target_include_directories (test_streaming_backlog PRIVATE ${CMAKE_SOURCE_DIR})
add_dependencies (test_streaming_backlog wsjt-codec test_decoder_engine)
target_link_libraries (test_streaming_backlog Qt5::Test)
target_compile_definitions (test_streaming_backlog PRIVATE
  WSJT_CODEC_EXECUTABLE="$<TARGET_FILE:wsjt-codec>")
if (EXISTS "${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav"
    AND EXISTS "${CMAKE_SOURCE_DIR}/samples/FT4/000000_000002.wav"
    AND EXISTS "${CMAKE_SOURCE_DIR}/samples/MSK144/181211_120800.wav")
  set (streaming_backlog_environment
    "WSJT_FT8_SAMPLE_WAV=${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav"
    "WSJT_FT8_ENGINE_WAV=${CMAKE_BINARY_DIR}/tests/unit/decoder_engine/ft8-engine.wav"
    "WSJT_FT4_SAMPLE_WAV=${CMAKE_SOURCE_DIR}/samples/FT4/000000_000002.wav"
    "WSJT_MSK144_SAMPLE_WAV=${CMAKE_SOURCE_DIR}/samples/MSK144/181211_120800.wav"
    "OMP_STACKSIZE=16M")
  set (streaming_backlog_rows
    mtdBehindOnePeriodSkipsAndDefers mtdRunsEveryPassWhenFree mtdFromAFileNeverSkips
    mtdBehindTwoPeriodsReplacesTheFinal mtdFinalDuringItsProbeBacksOff mtdRecoveryRestartsTheBackoff
    mtdCancelDropsTheDeferredFinal houndDropsTheFinalWhileBusy
    standardFt8DropsPassesWhileBusy standardFt8RunsPassesWhenFree
    ft4DropsTheDecodeWhileBusy ft4DecodesWhenFree
    decodeAgainRefusedWhileBusy decodeAgainRunsWhenFree msk144NeverRefused)
  set (streaming_backlog_timeout 300)
  # The rows wait in real time; sanitizer builds, with slower decodes, run one row (ASan) or two (TSan).
  if (WSJT_ENABLE_TSAN)
    set (streaming_backlog_rows ft4DropsTheDecodeWhileBusy mtdBehindTwoPeriodsReplacesTheFinal)
  elseif (WSJT_ENABLE_ASAN_UBSAN)
    set (streaming_backlog_rows ft4DropsTheDecodeWhileBusy)
  endif ()
  if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
    set (streaming_backlog_timeout 1200)
    list (APPEND streaming_backlog_environment "QTEST_FUNCTION_TIMEOUT=1200000")
  endif ()
  if (WSJT_ENABLE_TSAN)
    list (APPEND streaming_backlog_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
  endif ()
  foreach (row IN LISTS streaming_backlog_rows)
    add_test (NAME test_streaming_backlog_${row} COMMAND test_streaming_backlog ${row})
    set_tests_properties (test_streaming_backlog_${row} PROPERTIES
      LABELS "streaming;runtime"
      ENVIRONMENT "${streaming_backlog_environment}"
      FIXTURES_REQUIRED ft8_engine_wav
      TIMEOUT ${streaming_backlog_timeout})
    # The multithreaded decoder runs two threads.
    if (row MATCHES "^mtd")
      set_tests_properties (test_streaming_backlog_${row} PROPERTIES PROCESSORS 2)
    endif ()
  endforeach ()
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

# MSK144 against the GUI's receive path: msk144_gui_model runs the GUI's
# hspec on the same audio, the oracle each row compares the stream with.
add_executable (msk144_gui_model msk144_gui_model.f90)
wsjt_link_common_fortran (msk144_gui_model)
add_executable (test_streaming_msk144 test_streaming_msk144.cpp)
add_dependencies (test_streaming_msk144 wsjt-codec msk144_gui_model)
target_link_libraries (test_streaming_msk144 Qt5::Test)
target_compile_definitions (test_streaming_msk144 PRIVATE
  WSJT_CODEC_EXECUTABLE="$<TARGET_FILE:wsjt-codec>"
  MSK144_GUI_MODEL="$<TARGET_FILE:msk144_gui_model>")
set (streaming_msk144_rows)
set (streaming_msk144_fixtures)
# Source archives omit samples/.
set (have_msk144_samples FALSE)
if (EXISTS "${CMAKE_SOURCE_DIR}/samples/MSK144/181211_120500.wav"
    AND EXISTS "${CMAKE_SOURCE_DIR}/samples/MSK144/181211_120800.wav")
  set (have_msk144_samples TRUE)
  target_compile_definitions (test_streaming_msk144 PRIVATE
    MSK144_SAMPLES="${CMAKE_SOURCE_DIR}/samples/MSK144")
  list (APPEND streaming_msk144_rows recording120500MatchesTheGui recording120800MatchesTheGui
    samplesDecodeAtTheGuiBlocks phaseEqErrorsKeepTheValueInForce)
endif ()
set (have_ft8_sample FALSE)
if (EXISTS "${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav")
  set (have_ft8_sample TRUE)
  target_compile_definitions (test_streaming_msk144 PRIVATE
    FT8_SAMPLE_WAV="${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav")
endif ()
if (WSJT_BUILD_UTILS)
  # msk144sim writes 000000_000001.wav into its working directory: message, width, SNR.
  set (msk144_sim_dir "${CMAKE_CURRENT_BINARY_DIR}/msk144")
  set (msk144_sim_short "<K1ABC W9XYZ> R+06;0.12;10")
  set (msk144_sim_train "K1ABC W9XYZ R-04;0.5;10")
  set (msk144_sim_depth "K1ABC W9XYZ R-04;2.5;0")
  set (msk144_sim_hash "<PA3XYZ/P> G4ABCD/P RR73;0.5;15")
  set (msk144_sim_long "<PA3XYZ/P> <G4ABCD/P> R 590003 IO91NP;0.5;15")
  set (msk144_sim_rover "K1ABC W9XYZ/R R-04;0.5;10")
  target_compile_definitions (test_streaming_msk144 PRIVATE MSK144_SIM_DIR="${msk144_sim_dir}")
  add_dependencies (test_streaming_msk144 msk144sim)
  foreach (fixture short train depth hash long rover)
    list (GET msk144_sim_${fixture} 0 message)
    list (GET msk144_sim_${fixture} 1 width)
    list (GET msk144_sim_${fixture} 2 snr)
    file (MAKE_DIRECTORY "${msk144_sim_dir}/${fixture}")
    add_test (NAME prepare_msk144_${fixture}_wav
      COMMAND msk144sim "${message}" 15 1500 ${width} ${snr} 1
      WORKING_DIRECTORY "${msk144_sim_dir}/${fixture}")
    set_tests_properties (prepare_msk144_${fixture}_wav PROPERTIES FIXTURES_SETUP msk144_sim_wavs TIMEOUT 30)
  endforeach ()
  list (APPEND streaming_msk144_fixtures msk144_sim_wavs)
  list (APPEND streaming_msk144_rows
    simulatedSignalsMatchTheGui stationIdentityComesFromConfigures trainingMatchesTheGui)
  if (have_msk144_samples)
    list (APPEND streaming_msk144_rows trainedCoefficientsGoBack)
  endif ()
  if (have_ft8_sample)
    list (APPEND streaming_msk144_rows modeChangeKeepsTheDecoderState)
  endif ()
endif ()
set (streaming_msk144_environment)
set (streaming_msk144_timeout 300)
# Sanitizer builds decode several times slower and leave out the parity sweeps; CI's
# sanitizer legs also build without WSJT_BUILD_UTILS, so there only placement and the
# phase_eq errors run.
if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
  list (REMOVE_ITEM streaming_msk144_rows recording120500MatchesTheGui recording120800MatchesTheGui
    simulatedSignalsMatchTheGui modeChangeKeepsTheDecoderState)
  set (streaming_msk144_timeout 1200)
  list (APPEND streaming_msk144_environment "QTEST_FUNCTION_TIMEOUT=1200000")
endif ()
if (WSJT_ENABLE_TSAN)
  list (APPEND streaming_msk144_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
foreach (row IN LISTS streaming_msk144_rows)
  add_test (NAME test_streaming_msk144_${row} COMMAND test_streaming_msk144 ${row})
  set_tests_properties (test_streaming_msk144_${row} PROPERTIES
    LABELS "streaming;runtime"
    ENVIRONMENT "${streaming_msk144_environment}"
    FIXTURES_REQUIRED "${streaming_msk144_fixtures}"
    TIMEOUT ${streaming_msk144_timeout})
endforeach ()
