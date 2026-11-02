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

# four2a's per-thread patience override, read on an OpenMP worker too.
if (TARGET wsjt_fort_omp)
  add_executable (test_four2a_patience test_four2a_patience.f90)
  target_include_directories (test_four2a_patience PRIVATE
    ${WSJT_FORTRAN_OPENMP_MODULE_DIR})
  target_link_libraries (test_four2a_patience PRIVATE
    wsjt_fort_omp wsjt_fortran_openmp)
  add_test (NAME test_four2a_patience COMMAND test_four2a_patience)
  set_tests_properties (test_four2a_patience PROPERTIES
    LABELS "streaming" ENVIRONMENT "OMP_DYNAMIC=FALSE" TIMEOUT 30)
  if (WSJT_ENABLE_TSAN)
    set_property (TEST test_four2a_patience APPEND PROPERTY
      ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
  endif ()
endif ()

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

# Period-mode decodes on the codec's decode thread while the main thread
# reads, renders and decodes MSK144 and JTTY. The rows run real-time sessions;
# the timed ones run alone.
add_executable (test_streaming_decode_thread test_streaming_decode_thread.cpp
  ${CMAKE_SOURCE_DIR}/Ft8MtdDecodeScheduler.cpp)
target_include_directories (test_streaming_decode_thread PRIVATE ${CMAKE_SOURCE_DIR})
add_dependencies (test_streaming_decode_thread wsjt-codec jt9 test_decoder_engine test_decoder_engine_q65
  test_decoder_engine_fst4 test_decoder_engine_jt4)
target_link_libraries (test_streaming_decode_thread Qt5::Test)
target_compile_definitions (test_streaming_decode_thread PRIVATE
  WSJT_CODEC_EXECUTABLE="$<TARGET_FILE:wsjt-codec>"
  JT9_EXECUTABLE="$<TARGET_FILE:jt9>")
set (decode_thread_environment
  "WSJT_SAMPLES_DIR=${CMAKE_SOURCE_DIR}/samples"
  "WSJT_ENGINE_FIXTURES=${CMAKE_BINARY_DIR}/tests/unit/decoder_engine"
  "OMP_STACKSIZE=16M"
  "QT_FORCE_STDERR_LOGGING=1")
set (decode_thread_timeout 300)
if (WSJT_ENABLE_TSAN OR WSJT_ENABLE_ASAN_UBSAN)
  set (decode_thread_timeout 1200)
  list (APPEND decode_thread_environment "QTEST_FUNCTION_TIMEOUT=1200000")
endif ()
if (WSJT_ENABLE_TSAN)
  list (APPEND decode_thread_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
set (decode_thread_fixtures ft8_engine_wav fst4_engine_wav q65_engine_wav jt4_engine_wav)
# TIMING rows, the measurements and the rows whose time is mostly the slowest
# decodes or a cold FFTW_MEASURE plan, register only with
# WSJT_ENABLE_STREAM_TIMING_TESTS: routine CI runs the behaviour rows. Windows
# runs its suite serially within its job limit, so without the option it runs
# only the inline registrations below.
function (wsjt_add_decode_thread_test name function)
  cmake_parse_arguments (row "SERIAL;TIMING" "PROCESSORS;TIMEOUT" "" ${ARGN})
  if (NOT WSJT_ENABLE_STREAM_TIMING_TESTS AND (row_TIMING OR WIN32))
    return ()
  endif ()
  add_test (NAME test_streaming_decode_thread_${name} COMMAND test_streaming_decode_thread "${function}")
  # QtTest exits 0 for a data tag it does not have: require the row's PASS.
  string (REPLACE ":" "\\(" pattern "${function}")
  if (function MATCHES ":")
    string (APPEND pattern "\\)")
  else ()
    string (APPEND pattern "\\(\\)")
  endif ()
  set_tests_properties (test_streaming_decode_thread_${name} PROPERTIES
    LABELS "streaming;runtime"
    ENVIRONMENT "${decode_thread_environment}"
    FIXTURES_REQUIRED "${decode_thread_fixtures}"
    TIMEOUT ${decode_thread_timeout}
    PASS_REGULAR_EXPRESSION "PASS   : TestStreamingDecodeThread::${pattern}"
    # The PASS expression replaces the exit status, which a sanitizer report in the test sets.
    FAIL_REGULAR_EXPRESSION "FAIL!;ThreadSanitizer;AddressSanitizer")
  if (row_SERIAL)
    set_tests_properties (test_streaming_decode_thread_${name} PROPERTIES RUN_SERIAL TRUE)
  endif ()
  if (row_PROCESSORS)
    set_tests_properties (test_streaming_decode_thread_${name} PROPERTIES PROCESSORS ${row_PROCESSORS})
  endif ()
  # A row with a longer budget raises QtTest's limit with ctest's.
  if (row_TIMEOUT)
    set (environment ${decode_thread_environment})
    list (FILTER environment EXCLUDE REGEX "^QTEST_FUNCTION_TIMEOUT=")
    math (EXPR function_ms "${row_TIMEOUT} * 1000")
    list (APPEND environment "QTEST_FUNCTION_TIMEOUT=${function_ms}")
    set_tests_properties (test_streaming_decode_thread_${name} PROPERTIES
      TIMEOUT ${row_TIMEOUT} ENVIRONMENT "${environment}")
  endif ()
endfunction ()
# Source archives omit samples/: the rows need every recording they read.
set (decode_thread_samples TRUE)
foreach (sample FT8/210703_133430.wav FT4/000000_000002.wav JT9/130418_1742.wav JT65/JT65B/000000_0001.wav
    JT65/JT65B/000000_0002.wav JT65/JT65B/000000_0003.wav JT65/JT65B/000000_0004.wav JT65/JT65B/000000_0005.wav
    JT65/JT65B/000000_0006.wav FST4+FST4W/210115_0058.wav FST4+FST4W/201230_0300.wav
    Q65/300A_Optical_Scatter/201210_0505.wav MSK144/181211_120800.wav JTTY/260807_134110.wav)
  if (NOT EXISTS "${CMAKE_SOURCE_DIR}/samples/${sample}")
    set (decode_thread_samples FALSE)
  endif ()
endforeach ()
if (decode_thread_samples)
  # On the sanitizer legs: encodes beside every decode, a halt during one, and
  # MSK144 and JTTY beside one.
  foreach (row ft8 ft8-mtd ft4 fst4 q65 jt4 jt9 jt65 jt65jt9)
    wsjt_add_decode_thread_test (encodes_${row} "encodesDuringEachDecode:${row}" SERIAL PROCESSORS 2)
  endforeach ()
  foreach (row ft8 ft8-mtd)
    wsjt_add_decode_thread_test (halt_${row} "haltDuringADecode:${row}" PROCESSORS 2)
  endforeach ()
  wsjt_add_decode_thread_test (msk144_beside_jt9 "modeEnteredDuringADecode:msk144 beside jt9")
  wsjt_add_decode_thread_test (jtty_beside_jt9 "modeEnteredDuringADecode:jtty beside jt9")
  if (NOT WSJT_ENABLE_TSAN AND NOT WSJT_ENABLE_ASAN_UBSAN)
    # The per-render bound is a gate measured natively and on a CPU-capped runner before a merge.
    wsjt_add_decode_thread_test (renders_fst4 "rendersDuringTheSlowestDecodes:fst4-60-wide" SERIAL TIMING TIMEOUT 900)
    wsjt_add_decode_thread_test (renders_mtd "rendersDuringTheSlowestDecodes:multithreaded ft8" SERIAL TIMING
      TIMEOUT 900)
    foreach (row jt9 jt65 q65 fst4)
      wsjt_add_decode_thread_test (fox_${row} "foxRendersLeaveLiveDecodes:${row}")
    endforeach ()
    foreach (row ft8-mtd jt65jt9)
      wsjt_add_decode_thread_test (reference_${row} "decodesMatchTheReference:${row}" PROCESSORS 2)
    endforeach ()
    wsjt_add_decode_thread_test (mtd_labels multithreadedDecodesCarryTheirPassAndTime PROCESSORS 2)
    # Each mode's deepest decode against inline, up to about 80 s each on the Linux CI image: run before a merge.
    foreach (row ft8 ft8-mtd ft4 fst4-60 fst4w-1800 q65-300a jt4 jt9 jt65 jt65jt9)
      wsjt_add_decode_thread_test (deepest_${row} "deepestDecodeOnTheDecodeThread:${row}" PROCESSORS 2 TIMING)
    endforeach ()
    wsjt_add_decode_thread_test (marker markerDuringADecodeFollowsIt)
    wsjt_add_decode_thread_test (cancel_after_the_decode cancelAfterTheDecodeRunsTheWaitingFinal SERIAL PROCESSORS 2)
    wsjt_add_decode_thread_test (deferred_plain "deferredFinalRunsWithNoFurtherInput:plain" PROCESSORS 2)
    wsjt_add_decode_thread_test (deferred_take_held "deferredFinalRunsWithNoFurtherInput:take held" PROCESSORS 2)
    wsjt_add_decode_thread_test (reply_declined "repliesComeAtOnce:declined configure" SERIAL)
    wsjt_add_decode_thread_test (reply_msk144 "repliesComeAtOnce:msk144 refusal" SERIAL)
    wsjt_add_decode_thread_test (render_starts_the_final renderAfterTheDecodeStartsTheWaitingFinal SERIAL PROCESSORS 2)
    wsjt_add_decode_thread_test (waiting_final_reader_blocks "waitingFinalBehindLongRenders:reader blocks" PROCESSORS 2)
    wsjt_add_decode_thread_test (waiting_final_marker "waitingFinalBehindLongRenders:marker" PROCESSORS 2)
    wsjt_add_decode_thread_test (flush_releases_the_waiting_final flushRunsTheWaitingFinalFirst PROCESSORS 2)
    wsjt_add_decode_thread_test (replies_whole repliesStayWholeDuringDecodes PROCESSORS 2)
    wsjt_add_decode_thread_test (clock_ignores_busy_waits waitsDuringADecodeLeaveTheClock PROCESSORS 2)
    wsjt_add_decode_thread_test (retry_after_the_queue aSnapshotRetriesOnceTheQueueDrains)
    wsjt_add_decode_thread_test (inline_switch theInlineSwitchTakesEffect)
    wsjt_add_decode_thread_test (busy_false_before_a_later_line busyFalseComesBeforeALaterLine)
    wsjt_add_decode_thread_test (busy_false_before_a_refusal busyFalseComesBeforeARefusal)
    wsjt_add_decode_thread_test (plan_gate_opens_at_the_end planGateOpensAtTheEnd)
    # Resident memory from /proc or ps; a link to a file.
    if (UNIX)
      wsjt_add_decode_thread_test (control_frames_cap controlFramesQueueUpToTheCap)
      wsjt_add_decode_thread_test (jt9_wisdom_in_place jt9SavesItsWisdomInPlace)
    endif ()
    wsjt_add_decode_thread_test (fast_feeder fastFeederRunsEveryPass PROCESSORS 2)
    wsjt_add_decode_thread_test (queue_cap fastFeederQueuesAudioUpToTheCap)
    wsjt_add_decode_thread_test (msk144_entered "modeEnteredDuringADecode:msk144" SERIAL)
    wsjt_add_decode_thread_test (jtty_entered "modeEnteredDuringADecode:jtty" SERIAL)
    # Most of these rows' time is JTTY's FFTW_MEASURE planning in a fresh -a (about 33 s on the Linux CI image).
    wsjt_add_decode_thread_test (jtty_measures "jttyMeasuresBesideADecode:from a file" TIMING)
    wsjt_add_decode_thread_test (jtty_measures_live "jttyMeasuresBesideADecode:live ft8" TIMING)
    wsjt_add_decode_thread_test (busy_planning "busyEvents:planning" PROCESSORS 2 TIMING)
    wsjt_add_decode_thread_test (busy_planning_warm "busyEvents:planning warm" TIMING)
    wsjt_add_decode_thread_test (busy_without_the_key "busyEvents:without the key" TIMING)
    wsjt_add_decode_thread_test (ignored_planning "clientsIgnoringTheEventsStillWork:planning" TIMING)
    foreach (row passes again deferral discontinuity cancelled replaced memory progress)
      wsjt_add_decode_thread_test (busy_${row} "busyEvents:${row}" PROCESSORS 2)
    endforeach ()
    wsjt_add_decode_thread_test (busy_key_mid_session "busyEvents:key mid-session" PROCESSORS 2)
    wsjt_add_decode_thread_test (busy_after_a_drained_final "busyEvents:after a drained final" PROCESSORS 2)
    wsjt_add_decode_thread_test (busy_generation "busyEvents:generation")
    wsjt_add_decode_thread_test (deadline_held_decode "clientDeadlines:held decode" SERIAL)
    wsjt_add_decode_thread_test (deadline_held_between "clientDeadlines:held between operations" SERIAL)
    # On a CPU-capped runner its decodes run slower than real time.
    wsjt_add_decode_thread_test (deadline_fast_feeder "clientDeadlines:fast feeder" PROCESSORS 2 TIMEOUT 900)
    wsjt_add_decode_thread_test (deadline_long_reply "clientDeadlines:long reply" SERIAL PROCESSORS 2)
    wsjt_add_decode_thread_test (deadline_long_hold "clientDeadlines:long hold" SERIAL PROCESSORS 2)
    foreach (row busy pass_progress)
      wsjt_add_decode_thread_test (ignored_${row} "clientsIgnoringTheEventsStillWork:${row}")
    endforeach ()
    wsjt_add_decode_thread_test (clear_average_in_msk144 clearAverageSentInMsk144WaitsForJt65)
    foreach (row standard multithreaded defer replace)
      wsjt_add_decode_thread_test (memory_${row} "snapshotAllocationFails:${row}" PROCESSORS 2)
    endforeach ()
    wsjt_add_decode_thread_test (memory_held_again "snapshotAllocationFails:held again" PROCESSORS 2)
  endif ()
endif ()

# Stream rows with the decode handler run inline on the main thread, as when
# no decode thread exists: the drain and its deferred final, decode-again and
# the order of its lines, and MSK144's placement.
if (NOT WSJT_ENABLE_TSAN AND NOT WSJT_ENABLE_ASAN_UBSAN)
  foreach (row mtdBehindOnePeriodSkipsAndDefers mtdDeferredFinalOutlivesADiscontinuity decodeAgainRefusedWhileBusy
      decodeAgainRunsWhenFree)
    if (TEST test_streaming_backlog_${row})
      add_test (NAME test_streaming_decode_thread_inline_${row} COMMAND test_streaming_backlog ${row})
      set_tests_properties (test_streaming_decode_thread_inline_${row} PROPERTIES
        LABELS "streaming;runtime"
        ENVIRONMENT "${streaming_backlog_environment};WSJT_CODEC_TEST_INLINE=1"
        FIXTURES_REQUIRED ft8_engine_wav
        TIMEOUT ${streaming_backlog_timeout})
      if (row MATCHES "^mtd")
        set_tests_properties (test_streaming_decode_thread_inline_${row} PROPERTIES PROCESSORS 2)
      endif ()
    endif ()
  endforeach ()
  add_test (NAME test_streaming_decode_thread_inline_decode_again
    COMMAND test_streaming_consumers decodeAgainNeedsADecodedPeriod decodeAgainDecodesAtTheRxFrequency
      decodeAgainLeavesThePeriodInFlight discontinuityRunsAWaitingDecodeAgain wsjtCodecHaltsAfterThePeriodInFlight)
  set_tests_properties (test_streaming_decode_thread_inline_decode_again PROPERTIES
    LABELS "streaming;runtime"
    ENVIRONMENT "${streaming_consumers_environment};WSJT_CODEC_TEST_INLINE=1"
    FIXTURES_REQUIRED "${streaming_consumer_fixtures}"
    TIMEOUT 300)
  if (have_msk144_samples)
    add_test (NAME test_streaming_decode_thread_inline_msk144_blocks
      COMMAND test_streaming_msk144 samplesDecodeAtTheGuiBlocks)
    set_tests_properties (test_streaming_decode_thread_inline_msk144_blocks PROPERTIES
      LABELS "streaming;runtime"
      ENVIRONMENT "WSJT_CODEC_TEST_INLINE=1"
      TIMEOUT ${streaming_msk144_timeout})
  endif ()
  # Every stream row but these and the decode thread's again inline, a nested ctest of about 400 s: run before
  # a merge.
  if (WSJT_ENABLE_STREAM_TIMING_TESTS)
    add_test (NAME test_streaming_decode_thread_inline_suite
      COMMAND ${CMAKE_CTEST_COMMAND} -L streaming -E "^test_streaming_decode_thread_" -j 4 --output-on-failure
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties (test_streaming_decode_thread_inline_suite PROPERTIES
      LABELS "streaming;runtime"
      ENVIRONMENT "WSJT_CODEC_TEST_INLINE=1"
      RUN_SERIAL TRUE
      TIMEOUT 3600)
  endif ()
endif ()
