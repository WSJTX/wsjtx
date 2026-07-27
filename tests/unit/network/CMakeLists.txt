add_executable (test_pskreporter_ipfix
  test_pskreporter_ipfix.cpp
  ${CMAKE_SOURCE_DIR}/Network/PSKReporterIPFIX.cpp
  )
target_link_libraries (test_pskreporter_ipfix Qt5::Test)
add_test (NAME test_pskreporter_ipfix COMMAND test_pskreporter_ipfix)

add_executable (test_wsprnet_upload
  test_wsprnet_upload.cpp
  ${CMAKE_SOURCE_DIR}/Network/wsprnet.cpp
  )
target_link_libraries (test_wsprnet_upload Qt5::Test Qt5::Network)
add_test (NAME test_wsprnet_upload COMMAND test_wsprnet_upload)

add_executable (test_message_client_replay
  test_message_client_replay.cpp
  )
target_link_libraries (test_message_client_replay wsjt_qt Qt5::Test Qt5::Network)
add_test (NAME test_message_client_replay COMMAND test_message_client_replay)

add_executable (test_message_server
  test_message_server.cpp
  )
target_link_libraries (test_message_server wsjtx_udp-static Qt5::Test Qt5::Network)
add_test (NAME test_message_server COMMAND test_message_server)
