add_executable (test_qt_helpers
  test_qt_helpers.cpp
  ${CMAKE_SOURCE_DIR}/qt_helpers.cpp
)
target_link_libraries (test_qt_helpers Qt5::Test Qt5::Network Qt5::Widgets)
add_test (NAME test_qt_helpers COMMAND test_qt_helpers)

add_executable (test_http_user_agent
  test_http_user_agent.cpp
  )
target_link_libraries (test_http_user_agent wsjt_qt Qt5::Test)
add_test (NAME test_http_user_agent COMMAND test_http_user_agent)

add_executable (test_fox_guard_bands
  test_fox_guard_bands.cpp
  ${CMAKE_SOURCE_DIR}/FoxGuardBands.cpp
)
target_link_libraries (test_fox_guard_bands Qt5::Test)
add_test (NAME test_fox_guard_bands COMMAND test_fox_guard_bands)

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
