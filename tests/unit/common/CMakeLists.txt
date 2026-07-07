add_executable (test_qt_helpers
  test_qt_helpers.cpp
  ${CMAKE_SOURCE_DIR}/qt_helpers.cpp
)
target_link_libraries (test_qt_helpers Qt5::Test Qt5::Network Qt5::Widgets)
add_test (NAME test_qt_helpers COMMAND test_qt_helpers)

add_executable (test_wf_palette
  test_wf_palette.cpp
)
target_link_libraries (test_wf_palette wsjt_qt Qt5::Test)
add_test (NAME test_wf_palette COMMAND test_wf_palette)

add_executable (test_http_user_agent
  test_http_user_agent.cpp
  )
target_link_libraries (test_http_user_agent wsjt_qt Qt5::Test)
add_test (NAME test_http_user_agent COMMAND test_http_user_agent)

add_executable (test_high_dpi_scaling
  test_high_dpi_scaling.cpp
  ${CMAKE_SOURCE_DIR}/HighDpiScaling.cpp
)
target_link_libraries (test_high_dpi_scaling Qt5::Test)
add_test (NAME test_high_dpi_scaling COMMAND test_high_dpi_scaling)

add_executable (test_fox_guard_bands
  test_fox_guard_bands.cpp
  ${CMAKE_SOURCE_DIR}/FoxGuardBands.cpp
)
target_link_libraries (test_fox_guard_bands Qt5::Test)
add_test (NAME test_fox_guard_bands COMMAND test_fox_guard_bands)

add_executable (test_tx_start_policy
  test_tx_start_policy.cpp
)
target_link_libraries (test_tx_start_policy Qt5::Test)
add_test (NAME test_tx_start_policy COMMAND test_tx_start_policy)

add_executable (test_superfox_tx_planner
  test_superfox_tx_planner.cpp
  ${CMAKE_SOURCE_DIR}/SuperFoxTxPlanner.cpp
)
target_link_libraries (test_superfox_tx_planner Qt5::Test)
add_test (NAME test_superfox_tx_planner COMMAND test_superfox_tx_planner)

add_executable (test_auto_respond_selection_latch
  test_auto_respond_selection_latch.cpp
  ${CMAKE_SOURCE_DIR}/AutoRespondSelectionLatch.cpp
)
target_link_libraries (test_auto_respond_selection_latch Qt5::Test)
add_test (NAME test_auto_respond_selection_latch COMMAND test_auto_respond_selection_latch)

add_executable (test_emulate_split_transceiver
  test_emulate_split_transceiver.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/EmulateSplitTransceiver.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/Transceiver.cpp
  ${CMAKE_SOURCE_DIR}/qt_helpers.cpp
)
target_link_libraries (test_emulate_split_transceiver Boost::log Qt5::Test Qt5::Network Qt5::Widgets)
add_test (NAME test_emulate_split_transceiver COMMAND test_emulate_split_transceiver)

add_executable (test_active_station_list
  test_active_station_list.cpp
  ${CMAKE_SOURCE_DIR}/ActiveStationList.cpp
)
target_link_libraries (test_active_station_list Qt5::Test)
add_test (NAME test_active_station_list COMMAND test_active_station_list)

add_executable (test_frequency_editors
  test_frequency_editors.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
)
target_link_libraries (test_frequency_editors wsjt_qt Boost::log_setup Qt5::Test Qt5::Widgets)
add_test (NAME test_frequency_editors COMMAND test_frequency_editors)

add_executable (test_decoded_message_reaction
  test_decoded_message_reaction.cpp
)
target_link_libraries (test_decoded_message_reaction wsjt_qt wsjt_fort_common Qt5::Test)
add_test (NAME test_decoded_message_reaction COMMAND test_decoded_message_reaction)

add_executable (test_auto_sequence_reaction
  test_auto_sequence_reaction.cpp
)
target_link_libraries (test_auto_sequence_reaction wsjt_qt wsjt_fort_common Qt5::Test)
add_test (NAME test_auto_sequence_reaction COMMAND test_auto_sequence_reaction)
