add_executable (test_jtty_pack test_jtty_pack.f90)
target_include_directories (test_jtty_pack PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_pack)

configure_file(${CMAKE_SOURCE_DIR}/tests/fixtures/jtty/jtty_msgs.txt "${CMAKE_CURRENT_BINARY_DIR}/jtty_msgs.txt" COPYONLY)

add_test (NAME test_jtty_pack COMMAND test_jtty_pack)
set_tests_properties (test_jtty_pack PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  PASS_REGULAR_EXPRESSION "^ 3     3    67 14     NOW IS THE TIME FOR ALL GOOD MEN TO COME TO THE AID OF THEIR PARTY\.;^ 3     3    52 11     THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG, TWICE\.;\nTotal messages: 36   Number of errors:  0\n$"
)

add_executable (test_jtty_txstream
  test_jtty_txstream.cpp
  ${CMAKE_SOURCE_DIR}/Modulator/JttyPcmFifo.cpp
  ${CMAKE_SOURCE_DIR}/Modulator/JttyTxBuffer.cpp
  ${CMAKE_SOURCE_DIR}/Modulator/JttyTxStream.cpp
  ${CMAKE_SOURCE_DIR}/Audio/soundout.cpp)
target_link_libraries (test_jtty_txstream wsjt_qt Qt5::Test Qt5::Multimedia)
add_test (NAME test_jtty_txstream COMMAND test_jtty_txstream)

add_executable (test_jtty_messages test_jtty_messages.cpp)
target_link_libraries (test_jtty_messages Qt5::Test)
add_test (NAME test_jtty_messages COMMAND test_jtty_messages)
