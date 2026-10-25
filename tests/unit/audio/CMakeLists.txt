add_executable (test_bwf_file test_bwf_file.cpp)
target_link_libraries (test_bwf_file wsjt_qtmm wsjt_qt Qt5::Test Qt5::Multimedia)
add_test (NAME test_bwf_file COMMAND test_bwf_file)
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_bwf_file PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif ()

if (WSJT_BUILD_UTILS)
  add_executable (test_echosim_wav_trailer test_echosim_wav_trailer.cpp)
  target_link_libraries (test_echosim_wav_trailer wsjt_qtmm Qt5::Test Qt5::Multimedia)
  target_compile_definitions (test_echosim_wav_trailer PRIVATE
    ECHOSIM_EXECUTABLE="$<TARGET_FILE:echosim>")
  add_dependencies (test_echosim_wav_trailer echosim)
  add_test (NAME test_echosim_wav_trailer COMMAND test_echosim_wav_trailer)
endif ()

add_executable (test_wav_input_loading
  test_wav_input_loading.cpp
  ${CMAKE_SOURCE_DIR}/Audio/WavInputLoader.cpp)
target_link_libraries (test_wav_input_loading
  wsjt_qtmm wsjt_qt wsjt_fort_common Qt5::Test Qt5::Multimedia)
add_test (NAME test_wav_input_loading COMMAND test_wav_input_loading)

add_executable (test_wav_load_coordinator
  test_wav_load_coordinator.cpp
  ${CMAKE_SOURCE_DIR}/Audio/WavLoadCoordinator.cpp)
target_link_libraries (test_wav_load_coordinator wsjt_qt Qt5::Test)
add_test (NAME test_wav_load_coordinator COMMAND test_wav_load_coordinator)
set_tests_properties (test_wav_load_coordinator PROPERTIES TIMEOUT 15)

add_executable (test_tx_playback_diagnostics
  test_tx_playback_diagnostics.cpp
  ${CMAKE_SOURCE_DIR}/Audio/TxPlaybackDiagnostics.cpp)
target_link_libraries (test_tx_playback_diagnostics Qt5::Core Qt5::Test)
add_test (NAME test_tx_playback_diagnostics COMMAND test_tx_playback_diagnostics)

add_executable (test_tx_request test_tx_request.cpp)
target_link_libraries (test_tx_request Qt5::Test)
add_test (NAME test_tx_request COMMAND test_tx_request)

add_executable (test_tx_audio_queue
  test_tx_audio_queue.cpp
  ${CMAKE_SOURCE_DIR}/Audio/TxAudioQueue.cpp
  ${CMAKE_SOURCE_DIR}/Modulator/JttyPcmFifo.cpp)
target_link_libraries (test_tx_audio_queue Qt5::Core Qt5::Test)
add_test (NAME test_tx_audio_queue COMMAND test_tx_audio_queue)

add_executable (test_audio_input_stream_descriptor
  test_audio_input_stream_descriptor.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioDevice.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioInputSource.hpp
  ${CMAKE_SOURCE_DIR}/Audio/FixtureAudioInput.cpp
  ${CMAKE_SOURCE_DIR}/DecDataMutex.cpp
  ${CMAKE_SOURCE_DIR}/Detector/Detector.cpp)
target_link_libraries (test_audio_input_stream_descriptor
  wsjt_qtmm Qt5::Core Qt5::Test Qt5::Multimedia)
add_test (NAME test_audio_input_stream_descriptor
  COMMAND test_audio_input_stream_descriptor)

add_executable (test_sound_input
  test_sound_input.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioDevice.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioInputSource.hpp
  ${CMAKE_SOURCE_DIR}/Audio/soundin.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp)
target_link_libraries (test_sound_input
  wsjt_qtmm wsjt_qt Boost::log_setup Qt5::Test Qt5::Multimedia)
add_test (NAME test_sound_input COMMAND test_sound_input)

add_executable (test_fixture_audio_output
  test_fixture_audio_output.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioDevice.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioOutputSink.hpp
  ${CMAKE_SOURCE_DIR}/Audio/FixtureSoundOutput.cpp)
target_link_libraries (test_fixture_audio_output
  wsjt_qtmm Qt5::Core Qt5::Test Qt5::Multimedia)
add_test (NAME test_fixture_audio_output COMMAND test_fixture_audio_output)

add_executable (test_sound_output
  test_sound_output.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioOutputSink.hpp
  ${CMAKE_SOURCE_DIR}/Audio/soundout.cpp)
target_link_libraries (test_sound_output wsjt_qt Qt5::Test Qt5::Multimedia)
add_test (NAME test_sound_output COMMAND test_sound_output)

add_executable (test_modulator
  test_modulator.cpp
  ${CMAKE_SOURCE_DIR}/Modulator/Modulator.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioDevice.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioOutputSink.hpp)
target_link_libraries (test_modulator
  wsjt_fort_common Qt5::Core Qt5::Test Qt5::Multimedia)
add_test (NAME test_modulator COMMAND test_modulator)

add_executable (test_receive_audio_handoff
  test_receive_audio_handoff.cpp
  receive_reference_probe.f90
  ${CMAKE_SOURCE_DIR}/Audio/AudioDevice.cpp
  ${CMAKE_SOURCE_DIR}/DecDataMutex.cpp
  ${CMAKE_SOURCE_DIR}/Detector/Detector.cpp)
target_link_libraries (test_receive_audio_handoff
  wsjt_fort_common Qt5::Core Qt5::Test Qt5::Multimedia)
target_include_directories (test_receive_audio_handoff PRIVATE ${CMAKE_BINARY_DIR})
set (receive_handoff_scenarios
    block_size_changes_preserve_captured_partial_audio
    continuous_end_overflow_keeps_original_session
    continuous_detached_batch_excludes_later_publications
    continuous_receive_preserves_partial_block_across_clock_wrap
    continuous_stop_flushes_only_captured_downsampled_audio
    continuous_mailbox_overrun_is_explicit_and_rearms_notification
    downsamplersRetainIndependentHistory
    producersRemainIsolatedAcrossSourceSwitch
    queued_prefix_without_reuse
    handoff_rejects_gaps_and_disk_stale_work
    queued_period_must_retain_identity
    reference_input_packetization
    echo_metadata_vs_append
    echo_metadata_vs_reset
    stale_echo_metadata_must_not_modify_next_period)
add_test (NAME test_receive_audio_handoff
  COMMAND test_receive_audio_handoff ${receive_handoff_scenarios})

# refspectrum has saved accumulation state, so keep DSP cases in fresh
# processes.
set (receive_handoff_isolated_scenarios
    reference_measurement_vs_append
    reference_measurement_vs_reset
    reference_application_preserves_uncommitted_suffix
    reference_application_packetization
    reference_measurement_packetization)
set (receive_handoff_tests test_receive_audio_handoff)
foreach (scenario IN LISTS receive_handoff_isolated_scenarios)
  add_test (NAME test_receive_${scenario}
    COMMAND test_receive_audio_handoff ${scenario})
  list (APPEND receive_handoff_tests test_receive_${scenario})
endforeach ()
set_tests_properties (${receive_handoff_tests} PROPERTIES
  TIMEOUT 30 LABELS "receive-handoff")
if (WSJT_ENABLE_TSAN)
  # Do not inherit GUI event-dispatch suppressions: they can hide the very
  # application callback accesses that these tests are intended to expose.
  set_tests_properties (${receive_handoff_tests} PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:exitcode=66")
  # Reference file I/O triggers a libgfortran-internal lock-order warning.
  set_tests_properties (
    test_receive_reference_application_preserves_uncommitted_suffix
    test_receive_reference_application_packetization
    test_receive_reference_measurement_packetization PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:exitcode=66:detect_deadlocks=0")
endif ()

add_executable (test_fast_decode_ownership
  test_fast_decode_ownership.cpp
  ${CMAKE_SOURCE_DIR}/FastDecode.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioDevice.cpp
  ${CMAKE_SOURCE_DIR}/DecDataMutex.cpp
  ${CMAKE_SOURCE_DIR}/Detector/Detector.cpp)
target_link_libraries (test_fast_decode_ownership
  wsjt_fort_common Qt5::Core Qt5::Test Qt5::Multimedia)
target_include_directories (test_fast_decode_ownership PRIVATE ${CMAKE_BINARY_DIR})
add_test (NAME test_fast_decode_ownership COMMAND test_fast_decode_ownership
  captures_submission_inputs capture_does_not_race_live_append
  overlapping_jobs_retain_submission_identity)
set_tests_properties (test_fast_decode_ownership PROPERTIES
  TIMEOUT 30 LABELS "receive-handoff;fast-decode-ownership")
if (WSJT_ENABLE_TSAN)
  add_test (NAME test_fast_decode_overlap_does_not_race_decoder
    COMMAND test_fast_decode_ownership
      overlapping_submission_does_not_race_decoder)
  set_tests_properties (test_fast_decode_ownership
    test_fast_decode_overlap_does_not_race_decoder PROPERTIES
    TIMEOUT 30 LABELS "receive-handoff;fast-decode-ownership"
    ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:exitcode=66")
endif ()

# The codec's slot-mode pack and render against the GUI's generators and its
# Modulator. One process per mode, so each starts from a fresh GUI's and a
# fresh codec's transmit hash tables.
add_executable (test_wsjt_codec_slot_encode
  test_wsjt_codec_slot_encode.cpp
  ${CMAKE_SOURCE_DIR}/Modulator/Modulator.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioDevice.cpp
  ${CMAKE_SOURCE_DIR}/Audio/AudioOutputSink.hpp)
target_include_directories (test_wsjt_codec_slot_encode PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_wsjt_codec_slot_encode
  wsjt_fort_common Qt5::Core Qt5::Test Qt5::Multimedia)
target_compile_definitions (test_wsjt_codec_slot_encode PRIVATE
  WSJT_CODEC_EXECUTABLE="$<TARGET_FILE:wsjt-codec>")
add_dependencies (test_wsjt_codec_slot_encode wsjt-codec)
# The GUI's stack: the test runs the GUI's Fox generation, foxfilt's 2.4 MB array, in process.
if (WIN32)
  set_property (TARGET test_wsjt_codec_slot_encode APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--stack,16777216")
endif ()
# Without a console (a Windows runner) QtTest writes to the debugger, not to ctest.
set (slot_encode_environment "QT_FORCE_STDERR_LOGGING=1")
if (WSJT_ENABLE_TSAN)
  list (APPEND slot_encode_environment "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
foreach (function ft8 ft4 fst4 fst4w q65 msk144 jt4 jt9 jt65 wspr tune echo singleTones fastJt9 cwId
    cwIdStation foxSlots foxTransmitState foxSequences superFox transmitState jttyUnchanged
    readyListsEncodes decodesItsOwnRender renderAfterDecode refusals accepted)
  add_test (NAME test_wsjt_codec_slot_encode_${function}
    COMMAND test_wsjt_codec_slot_encode ${function})
  set_tests_properties (test_wsjt_codec_slot_encode_${function} PROPERTIES
    LABELS "streaming;encode;runtime" TIMEOUT 300 ENVIRONMENT "${slot_encode_environment}")
endforeach ()
