add_executable (test_message_filter
  test_message_filter.cpp
  ${CMAKE_SOURCE_DIR}/MessageFilter.cpp
)
target_link_libraries (test_message_filter Qt5::Test)
add_test (NAME test_message_filter COMMAND test_message_filter)

add_executable (test_msk144_message_filter
  test_msk144_message_filter.cpp
  ${CMAKE_SOURCE_DIR}/MessageFilter.cpp
  ${CMAKE_SOURCE_DIR}/MessageFilterRules.cpp
)
target_link_libraries (test_msk144_message_filter Qt5::Test)
add_test (NAME test_msk144_message_filter COMMAND test_msk144_message_filter)
