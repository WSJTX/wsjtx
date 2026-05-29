add_executable (test_jtty_pack test_jtty_pack.f90)
target_include_directories (test_jtty_pack PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_jtty_pack wsjt_fort wsjt_cxx)

configure_file(${CMAKE_SOURCE_DIR}/tests/fixtures/jtty/jtty_msgs.txt "${CMAKE_CURRENT_BINARY_DIR}/jtty_msgs.txt" COPYONLY)

add_test (NAME test_jtty_pack COMMAND test_jtty_pack)
set_tests_properties (test_jtty_pack PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  PASS_REGULAR_EXPRESSION "^ 3     3    67 14     NOW IS THE TIME FOR ALL GOOD MEN TO COME TO THE AID OF THEIR PARTY\.;^ 3     3    52 11     THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG, TWICE\.;\nTotal messages: 36   Number of errors:  1\n$"
)
