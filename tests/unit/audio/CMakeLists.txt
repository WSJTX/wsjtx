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
