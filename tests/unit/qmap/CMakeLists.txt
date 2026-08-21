add_executable (test_qmap_livecq_parser
  test_livecq_parser.cpp
  ${CMAKE_SOURCE_DIR}/qmap/livecq_parser.cpp
  ${CMAKE_SOURCE_DIR}/validators/LiveCQCallsign.cpp
)
target_link_libraries (test_qmap_livecq_parser Qt5::Test)
add_test (NAME test_qmap_livecq_parser COMMAND test_qmap_livecq_parser)
