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
# Without a console (a Windows runner) QtTest writes to the debugger, not to ctest.
set (streaming_consumers_environment "QT_FORCE_STDERR_LOGGING=1")
if (WSJT_ENABLE_TSAN)
  list (APPEND streaming_consumers_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
set_tests_properties (test_streaming_consumers PROPERTIES
  LABELS "streaming;runtime"
  TIMEOUT 360
  FIXTURES_REQUIRED "${streaming_consumer_fixtures}"
  ENVIRONMENT "${streaming_consumers_environment}"
)

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
  COMMAND test_streaming_consumers wsjtCodecDeclinesValuesTheEngineRefuses wsjtCodecDeclinesBadLabels
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

# One FT8 period from a recording with no flag, --stream and -0.
if (EXISTS "${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav")
  set (streaming_flag_environment "WSJT_FT8_SAMPLE_WAV=${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav"
    "QT_FORCE_STDERR_LOGGING=1")
  set (streaming_flag_timeout 300)
  if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
    set (streaming_flag_timeout 1200)
    list (APPEND streaming_flag_environment "QTEST_FUNCTION_TIMEOUT=1200000")
  endif ()
  if (WSJT_ENABLE_TSAN)
    list (APPEND streaming_flag_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
  endif ()
  add_test (NAME test_streaming_stream_flag COMMAND test_streaming_consumers wsjtCodecIgnoresTheStreamFlag)
  set_tests_properties (test_streaming_stream_flag PROPERTIES
    LABELS "streaming;runtime"
    ENVIRONMENT "${streaming_flag_environment}"
    TIMEOUT ${streaming_flag_timeout})
endif ()

# JT9 periods on a JT9 recording; source archives omit samples/.
if (EXISTS "${CMAKE_SOURCE_DIR}/samples/JT9/130418_1742.wav")
  set (streaming_jt9_environment "WSJT_JT9_SAMPLE_WAV=${CMAKE_SOURCE_DIR}/samples/JT9/130418_1742.wav"
    "QT_FORCE_STDERR_LOGGING=1")
  set (streaming_jt9_timeout 300)
  if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
    set (streaming_jt9_timeout 1200)
    list (APPEND streaming_jt9_environment "QTEST_FUNCTION_TIMEOUT=1200000")
  endif ()
  if (WSJT_ENABLE_TSAN)
    list (APPEND streaming_jt9_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
  endif ()
  add_test (NAME test_streaming_jt9_periods
    COMMAND test_streaming_consumers wsjtCodecKeepsJt9AtSixtySeconds)
  set_tests_properties (test_streaming_jt9_periods PROPERTIES
    LABELS "streaming;runtime"
    ENVIRONMENT "${streaming_jt9_environment}"
    TIMEOUT ${streaming_jt9_timeout})
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
    decodeAgainRefusedWhileBusy decodeAgainRunsWhenFree msk144NeverRefused
    mtdDeferredFinalOutlivesADiscontinuity)
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

# The GUI's lines from the stream's events (DecodeEventLine): each rendered
# line equals jt9's for the same audio and options, before and after
# DecodeOutputPlan::prepareLine.
add_executable (test_streaming_render test_streaming_render.cpp)
add_dependencies (test_streaming_render jt9 wsjt-codec)
target_include_directories (test_streaming_render PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_streaming_render wsjt_qt wsjt_fort_common Qt5::Test)
target_compile_definitions (test_streaming_render PRIVATE
  JT9_EXECUTABLE="$<TARGET_FILE:jt9>"
  WSJT_CODEC_EXECUTABLE="$<TARGET_FILE:wsjt-codec>")
add_test (NAME test_streaming_render COMMAND test_streaming_render rendersJt9Lines rendersDecodeFinished)
set_tests_properties (test_streaming_render PROPERTIES LABELS "streaming" TIMEOUT 60)
if (WSJT_BUILD_UTILS)
  # The worker's path, for settings jt9's command line cannot give.
  add_executable (ft8_worker_model ft8_worker_model.f90)
  target_include_directories (ft8_worker_model PRIVATE ${CMAKE_SOURCE_DIR}/lib)
  wsjt_link_common_fortran (ft8_worker_model)
  # The decoder's stack, as jt9's.
  if (WIN32)
    set_property (TARGET ft8_worker_model APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--stack,16777216")
  endif ()
  add_dependencies (test_streaming_render ft8_worker_model)
  target_compile_definitions (test_streaming_render PRIVATE
    SUPERFOX_WAV="${CMAKE_CURRENT_BINARY_DIR}/superfox/000000_000000.wav"
    FT8_WORKER_MODEL="$<TARGET_FILE:ft8_worker_model>")
endif ()
# Without a console (a Windows runner) QtTest writes to the debugger, not to
# ctest, unless QT_FORCE_STDERR_LOGGING is set; the rows require their PASS line.
set (streaming_render_environment "WSJT_RENDER_FIXTURES=${CMAKE_BINARY_DIR}/tests/unit"
  "WSJT_SHARE_DIR=${CMAKE_SOURCE_DIR}" "QT_FORCE_STDERR_LOGGING=1")
set (streaming_render_timeout 300)
# Each row runs jt9 and the stream; the sanitizers slow both several times over.
if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
  set (streaming_render_timeout 1200)
  list (APPEND streaming_render_environment "QTEST_FUNCTION_TIMEOUT=1200000")
endif ()
if (WSJT_ENABLE_TSAN)
  list (APPEND streaming_render_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
# Source archives omit samples/.
if (EXISTS "${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav")
  list (APPEND streaming_render_environment "WSJT_SAMPLES_DIR=${CMAKE_SOURCE_DIR}/samples")
endif ()
function (wsjt_add_streaming_render_test name function fixtures)
  add_test (NAME test_streaming_render_${name} COMMAND test_streaming_render "${function}")
  # QtTest exits 0 for a data tag it does not have: require the row's PASS.
  string (REPLACE ":" "\\(" pattern "${function}")
  if (function MATCHES ":")
    string (APPEND pattern "\\)")
  else ()
    string (APPEND pattern "\\(\\)")
  endif ()
  set_tests_properties (test_streaming_render_${name} PROPERTIES
    LABELS "streaming;runtime;decode"
    ENVIRONMENT "${streaming_render_environment}"
    TIMEOUT ${streaming_render_timeout}
    PASS_REGULAR_EXPRESSION "PASS   : TestStreamingRender::${pattern}"
    FAIL_REGULAR_EXPRESSION "FAIL!")
  if (fixtures)
    set_tests_properties (test_streaming_render_${name} PROPERTIES FIXTURES_REQUIRED "${fixtures}")
  endif ()
endfunction ()
set (streaming_render_rows fst4-15 fst4-15-doppler q65-15)
set (streaming_render_sample_rows ft8 ft8-d3 ft8-mtd ft4 jt9 jt9-d3 jt65 jt65-seq jt65-seq-avg
  jt65-seq-avg-utc jt65-seq-vhf-avg jt65jt9 jt4f jt4a jt4a-avg q65-30a q65-30a-ap q65-30a-seq
  q65-30a-seq-avg q65-60a q65-60b-seq-avg q65-60d q65-120d q65-120e-seq-avg-counts q65-300a fst4
  fst4-doppler fst4w)
set (streaming_render_ap_rows ft8-ap ft4-ap q65-pileup-ap q65-pileup-off-counts)
if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
  set (streaming_render_rows fst4-15 q65-15)
  set (streaming_render_sample_rows ft8 ft4 jt9 jt65-seq-vhf-avg jt4f q65-30a fst4)
  set (streaming_render_ap_rows ft8-ap)
endif ()
foreach (row IN LISTS streaming_render_rows)
  wsjt_add_streaming_render_test (${row} "matchesJt9:${row}" "fst4_engine_wav;q65_engine_wav")
endforeach ()
if (EXISTS "${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav")
  foreach (row IN LISTS streaming_render_sample_rows)
    wsjt_add_streaming_render_test (${row} "matchesJt9:${row}" "")
  endforeach ()
  if (NOT WSJT_ENABLE_TSAN AND NOT WSJT_ENABLE_ASAN_UBSAN)
    wsjt_add_streaming_render_test (clear_average clearAverageClearsOnce "")
    wsjt_add_streaming_render_test (clear_average_q65 q65ClearAverageClearsOnce "")
    wsjt_add_streaming_render_test (pass decodesCarryTheirPass "")
  endif ()
endif ()
if (WSJT_BUILD_UTILS)
  # Weak signals, which need a priori decoding: the lines carry aN. The noise
  # differs from run to run, so eight periods each, nearly all of them aN.
  foreach (sim ft8 ft4)
    file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/render/${sim}ap")
  endforeach ()
  add_dependencies (test_streaming_render ft8sim ft4sim q65sim)
  add_test (NAME prepare_render_ft8ap_wavs COMMAND ft8sim "K1ABC W9XYZ RR73" 1500.0 0.0 0.1 1.0 8 -22
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/render/ft8ap")
  add_test (NAME prepare_render_ft4ap_wavs COMMAND ft4sim "K1ABC W9XYZ RR73" 1500 0.0 0.1 1.0 8 -19
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/render/ft4ap")
  # Q65 messages with the copied bit set, decodable only with a priori help.
  file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/render/q65pileup")
  add_test (NAME prepare_render_q65pileup_wavs COMMAND q65sim "K1ABC W9XYZ R-15" A 1500 0.0 0.0 0.0 1 30 1 4 -26 1
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/render/q65pileup")
  # SuperFox with its CQ, and with its free text.
  foreach (variant cq text)
    file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/superfox-${variant}")
  endforeach ()
  add_test (NAME prepare_render_superfox_cq_wav COMMAND sfoxsim 750 0.0 AW K1JT 3 1 1 0 1 -5
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/superfox-cq")
  add_test (NAME prepare_render_superfox_text_wav COMMAND sfoxsim 750 0.0 AW K1JT 3 1 0 1 1 -5
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/superfox-text")
  set_tests_properties (prepare_render_ft8ap_wavs prepare_render_ft4ap_wavs prepare_render_q65pileup_wavs
    prepare_render_superfox_cq_wav prepare_render_superfox_text_wav PROPERTIES FIXTURES_SETUP render_sim_wavs
    TIMEOUT 30)
  foreach (row superfox superfox-cq superfox-text mtd-ap)
    wsjt_add_streaming_render_test (worker-${row} "matchesTheWorker:${row}" "superfox_wav;render_sim_wavs")
  endforeach ()
  foreach (row IN LISTS streaming_render_ap_rows)
    wsjt_add_streaming_render_test (${row} "matchesJt9:${row}" render_sim_wavs)
  endforeach ()
endif ()
foreach (row ft8-mtd worker-mtd-ap)
  if (TEST test_streaming_render_${row})
    set_tests_properties (test_streaming_render_${row} PROPERTIES PROCESSORS 4)
  endif ()
endforeach ()

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
set (streaming_msk144_rows trainingComparisonAllowsSignalRoundoff)
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
