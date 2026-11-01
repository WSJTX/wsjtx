add_executable (test_hrd_message
  test_hrd_message.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/HRDMessage.cpp
)
target_link_libraries (test_hrd_message Qt5::Test)
add_test (NAME test_hrd_message COMMAND test_hrd_message)

add_executable (test_hrd_transceiver test_hrd_transceiver.cpp)
target_link_libraries (test_hrd_transceiver wsjt_qt Qt5::Test)
add_test (NAME test_hrd_transceiver COMMAND test_hrd_transceiver)
set_tests_properties (test_hrd_transceiver PROPERTIES TIMEOUT 90)
