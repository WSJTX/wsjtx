add_executable (test_lotw_users
  test_lotw_users.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
)
target_link_libraries (test_lotw_users wsjt_qt Boost::log_setup Qt5::Test Qt5::Network)
add_test (NAME test_lotw_users COMMAND test_lotw_users)

add_executable (test_pskreporter_ipfix
  test_pskreporter_ipfix.cpp
  ${CMAKE_SOURCE_DIR}/Network/PSKReporterIPFIX.cpp
  )
target_link_libraries (test_pskreporter_ipfix Qt5::Test)
add_test (NAME test_pskreporter_ipfix COMMAND test_pskreporter_ipfix)

add_executable (test_pskreporter_transport
  test_pskreporter_transport.cpp
  ${CMAKE_SOURCE_DIR}/Network/PSKReporter.cpp
  ${CMAKE_SOURCE_DIR}/Network/PSKReporterIPFIX.cpp
  )
target_link_libraries (test_pskreporter_transport wsjt_cxx Qt5::Test Qt5::Network)
add_test (NAME test_pskreporter_transport COMMAND test_pskreporter_transport)

add_executable (test_map65_pskreporter_settings
  test_map65_pskreporter_settings.cpp
  ${CMAKE_SOURCE_DIR}/map65/pskreporter_settings.cpp
  )
target_link_libraries (test_map65_pskreporter_settings Qt5::Test)
add_test (NAME test_map65_pskreporter_settings COMMAND test_map65_pskreporter_settings)

add_executable (test_map65_pskreporter_decode
  test_map65_pskreporter_decode.cpp
  ${CMAKE_SOURCE_DIR}/map65/pskreporter_decode.cpp
  ${CMAKE_SOURCE_DIR}/Network/DecodedTime.cpp
  ${CMAKE_SOURCE_DIR}/validators/LiveCQCallsign.cpp
)
target_link_libraries (test_map65_pskreporter_decode Qt5::Test)
add_test (NAME test_map65_pskreporter_decode COMMAND test_map65_pskreporter_decode)

add_executable (test_map65_livecq_decode
  test_map65_livecq_decode.cpp
  ${CMAKE_SOURCE_DIR}/map65/livecq_decode.cpp
  ${CMAKE_SOURCE_DIR}/Network/DecodedTime.cpp
  ${CMAKE_SOURCE_DIR}/validators/LiveCQCallsign.cpp
)
target_link_libraries (test_map65_livecq_decode Qt5::Test)
add_test (NAME test_map65_livecq_decode COMMAND test_map65_livecq_decode)

add_executable (test_wsprnet_upload
  test_wsprnet_upload.cpp
  ${CMAKE_SOURCE_DIR}/Network/wsprnet.cpp
  )
target_link_libraries (test_wsprnet_upload Qt5::Test Qt5::Network)
add_test (NAME test_wsprnet_upload COMMAND test_wsprnet_upload)

add_executable (test_livecq_upload
  test_livecq_upload.cpp
  ${CMAKE_SOURCE_DIR}/Network/LiveCQUpload.cpp
  )
target_link_libraries (test_livecq_upload Qt5::Test Qt5::Network)
add_test (NAME test_livecq_upload COMMAND test_livecq_upload)

add_executable (test_eqsl_upload test_eqsl_upload.cpp)
target_link_libraries (test_eqsl_upload wsjt_qt Qt5::Test Qt5::Network)
add_test (NAME test_eqsl_upload COMMAND test_eqsl_upload)

add_executable (test_message_client_adif test_message_client_adif.cpp ${CMAKE_SOURCE_DIR}/Logger.cpp)
target_link_libraries (test_message_client_adif wsjt_qt Boost::log_setup Qt5::Test Qt5::Network)
add_test (NAME test_message_client_adif COMMAND test_message_client_adif)

add_executable (test_message_client_replay
  test_message_client_replay.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
  )
target_link_libraries (test_message_client_replay wsjt_qt Boost::log_setup Qt5::Test Qt5::Network)
add_test (NAME test_message_client_replay COMMAND test_message_client_replay)

add_executable (test_message_client_inhibit_status
  test_message_client_inhibit_status.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
  )
target_link_libraries (test_message_client_inhibit_status wsjt_qt Boost::log_setup Qt5::Test Qt5::Network)
add_test (NAME test_message_client_inhibit_status COMMAND test_message_client_inhibit_status)

add_executable (test_message_server
  test_message_server.cpp
  )
target_link_libraries (test_message_server wsjtx_udp-static Qt5::Test Qt5::Network)
add_test (NAME test_message_server COMMAND test_message_server)

add_executable (test_cloudlog_connection_check
  test_cloudlog_connection_check.cpp
  test_cloudlog_revision_stub.cpp
  ${CMAKE_SOURCE_DIR}/Network/Cloudlog.cpp
  )
add_dependencies (test_cloudlog_connection_check revisiontag)
target_include_directories (test_cloudlog_connection_check PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_cloudlog_connection_check Boost::log Qt5::Test Qt5::Network Qt5::Widgets)
add_test (NAME test_cloudlog_connection_check COMMAND test_cloudlog_connection_check)

add_executable (test_sample_downloader
  test_sample_downloader.cpp
  ${CMAKE_SOURCE_DIR}/SampleDownloader/RemoteFile.cpp
  )
target_include_directories (test_sample_downloader PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_sample_downloader Qt5::Test Qt5::Network)
add_test (NAME test_sample_downloader COMMAND test_sample_downloader)

if (WSJT_ENABLE_TSAN)
  set (wsjt_tsan_network_test_environment
    "QT_QPA_PLATFORM=offscreen"
    "TSAN_OPTIONS=${wsjt_tsan_network_options}")
  set_tests_properties (
    test_pskreporter_transport
    test_message_client_replay
    test_message_server
    test_cloudlog_connection_check
    PROPERTIES ENVIRONMENT "${wsjt_tsan_network_test_environment}")
endif ()
