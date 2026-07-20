add_executable (test_tci_stream
  test_tci_stream.cpp
)
target_link_libraries (test_tci_stream Qt5::Test)
add_test (NAME test_tci_stream COMMAND test_tci_stream)
