add_executable (test_map65_decode_display_filter
  test_map65_decode_display_filter.cpp
  ${CMAKE_SOURCE_DIR}/map65/map65_decode_display_filter.cpp
)
target_include_directories (test_map65_decode_display_filter PRIVATE ${CMAKE_SOURCE_DIR}/map65)
target_link_libraries (test_map65_decode_display_filter Qt5::Test)
wsjt_add_qt_test (test_map65_decode_display_filter test_map65_decode_display_filter)

add_executable (test_map65_half_symbol_scheduler
  test_map65_half_symbol_scheduler.cpp
)
target_include_directories (test_map65_half_symbol_scheduler PRIVATE ${CMAKE_SOURCE_DIR}/map65)
target_link_libraries (test_map65_half_symbol_scheduler Qt5::Test)
wsjt_add_qt_test (test_map65_half_symbol_scheduler test_map65_half_symbol_scheduler)

add_executable (test_map65_runtime_paths
  test_map65_runtime_paths.cpp
  ${CMAKE_SOURCE_DIR}/map65/runtime_paths.cpp
)
target_include_directories (test_map65_runtime_paths PRIVATE ${CMAKE_SOURCE_DIR}/map65)
target_link_libraries (test_map65_runtime_paths Qt5::Test)
wsjt_add_qt_test (test_map65_runtime_paths test_map65_runtime_paths)

if (UNIX)
  add_executable (test_map65_ptt_unix test_map65_ptt_unix.c)
  set_target_properties (test_map65_ptt_unix PROPERTIES AUTOMOC OFF)
  add_test (NAME test_map65_ptt_unix COMMAND test_map65_ptt_unix)
endif ()

add_executable (test_map65_ptt_windows test_map65_ptt_windows.c)
target_include_directories (test_map65_ptt_windows PRIVATE windows_fake)
set_target_properties (test_map65_ptt_windows PROPERTIES AUTOMOC OFF)
add_test (NAME test_map65_ptt_windows COMMAND test_map65_ptt_windows)
