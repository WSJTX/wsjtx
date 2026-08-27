add_executable (test_map65_decode_display_filter
  test_map65_decode_display_filter.cpp
  ${CMAKE_SOURCE_DIR}/map65/map65_decode_display_filter.cpp
)
target_include_directories (test_map65_decode_display_filter PRIVATE ${CMAKE_SOURCE_DIR}/map65)
target_link_libraries (test_map65_decode_display_filter Qt5::Test)
add_test (NAME test_map65_decode_display_filter COMMAND test_map65_decode_display_filter)

add_executable (test_map65_half_symbol_scheduler
  test_map65_half_symbol_scheduler.cpp
)
target_include_directories (test_map65_half_symbol_scheduler PRIVATE ${CMAKE_SOURCE_DIR}/map65)
target_link_libraries (test_map65_half_symbol_scheduler Qt5::Test)
add_test (NAME test_map65_half_symbol_scheduler COMMAND test_map65_half_symbol_scheduler)
