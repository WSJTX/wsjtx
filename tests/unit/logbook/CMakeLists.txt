add_executable (test_logbook_adif
  test_logbook_adif.cpp
  ${CMAKE_SOURCE_DIR}/logbook/AdifQso.cpp
)
target_link_libraries (test_logbook_adif Qt5::Test)
add_test (NAME test_logbook_adif COMMAND test_logbook_adif)

add_executable (test_worked_before_load_state test_worked_before_load_state.cpp)
target_link_libraries (test_worked_before_load_state Qt5::Test)
add_test (NAME test_worked_before_load_state COMMAND test_worked_before_load_state)

add_executable (test_worked_before_adif test_worked_before_adif.cpp)
add_dependencies (test_worked_before_adif wsjt_qt_autogen)
target_include_directories (test_worked_before_adif PRIVATE
  ${CMAKE_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/wsjt_qt_autogen/include
  ${CMAKE_BINARY_DIR}/wsjt_qt_autogen/include_$<CONFIG>)
target_link_libraries (test_worked_before_adif wsjt_qt wsjt_cxx wsjt_fort_common
  Boost::log_setup Qt5::Test)
add_test (NAME test_worked_before_adif COMMAND test_worked_before_adif)
set_tests_properties (test_worked_before_adif PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 60)
