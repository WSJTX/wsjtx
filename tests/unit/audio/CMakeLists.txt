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
