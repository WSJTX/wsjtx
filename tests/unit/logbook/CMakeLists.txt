add_executable (test_logbook_adif
  test_logbook_adif.cpp
  ${CMAKE_SOURCE_DIR}/logbook/AdifQso.cpp
)
target_link_libraries (test_logbook_adif Qt5::Test)
add_test (NAME test_logbook_adif COMMAND test_logbook_adif)
