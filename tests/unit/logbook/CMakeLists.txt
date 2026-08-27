add_executable (test_logbook_adif
  test_logbook_adif.cpp
  ${CMAKE_SOURCE_DIR}/logbook/AdifQso.cpp
)
target_link_libraries (test_logbook_adif Qt5::Test)
add_test (NAME test_logbook_adif COMMAND test_logbook_adif)

add_executable (test_worked_before_load_state test_worked_before_load_state.cpp)
target_link_libraries (test_worked_before_load_state Qt5::Test)
add_test (NAME test_worked_before_load_state COMMAND test_worked_before_load_state)
