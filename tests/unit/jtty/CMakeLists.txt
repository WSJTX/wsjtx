add_executable (test_jtty_pack test_jtty_pack.f90)
target_include_directories (test_jtty_pack PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_pack)

add_executable (test_jtty_source_codec test_jtty_source_codec.f90)
target_include_directories (test_jtty_source_codec PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_source_codec)
add_test (NAME test_jtty_source_codec COMMAND test_jtty_source_codec)
set_tests_properties (test_jtty_source_codec PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_source_codec: all checks passed"
)

add_executable (test_tbcc test_tbcc.f90)
target_include_directories (test_tbcc PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_tbcc)
add_test (NAME test_tbcc COMMAND test_tbcc)
set_tests_properties (test_tbcc PROPERTIES
  PASS_REGULAR_EXPRESSION "test_tbcc: all checks passed"
)

add_executable (test_jtty_block_pow test_jtty_block_pow.f90)
target_include_directories (test_jtty_block_pow PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_block_pow)
add_test (NAME test_jtty_block_pow COMMAND test_jtty_block_pow)
set_tests_properties (test_jtty_block_pow PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_block_pow: all checks passed"
)

add_executable (test_jtty_message_matching test_jtty_message_matching.f90)
target_include_directories (test_jtty_message_matching PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_message_matching)
add_test (NAME test_jtty_message_matching COMMAND test_jtty_message_matching)
set_tests_properties (test_jtty_message_matching PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_message_matching: all checks passed"
)

add_executable (test_jtty_receive_state test_jtty_receive_state.f90)
target_include_directories (test_jtty_receive_state PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_receive_state)
add_test (NAME test_jtty_receive_state COMMAND test_jtty_receive_state)
set_tests_properties (test_jtty_receive_state PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_receive_state: all checks passed"
)

add_executable (test_jtty_adjacent_decode test_jtty_adjacent_decode.f90)
target_include_directories (test_jtty_adjacent_decode PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_adjacent_decode)
add_test (NAME test_jtty_adjacent_decode COMMAND test_jtty_adjacent_decode)
set_tests_properties (test_jtty_adjacent_decode PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_adjacent_decode: all checks passed"
  TIMEOUT 60
)

add_executable (test_jtty_structured_decode
  test_jtty_structured_decode.f90
  jtty_cpp_n1mm_smoke.cpp)
target_include_directories (test_jtty_structured_decode PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_structured_decode)
target_link_libraries (test_jtty_structured_decode PRIVATE Qt5::Core)
add_test (NAME test_jtty_structured_decode COMMAND test_jtty_structured_decode)
set_tests_properties (test_jtty_structured_decode PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_structured_decode: all checks passed"
  TIMEOUT 60
)

add_executable (test_jtty_overlap_decode test_jtty_overlap_decode.f90)
target_include_directories (test_jtty_overlap_decode PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_overlap_decode)
add_test (NAME test_jtty_overlap_decode COMMAND test_jtty_overlap_decode)
set_tests_properties (test_jtty_overlap_decode PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_overlap_decode: all checks passed"
  TIMEOUT 60
)

add_test (NAME test_jtty_overlap_decode_480 COMMAND test_jtty_overlap_decode 480)
set_tests_properties (test_jtty_overlap_decode_480 PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_overlap_decode: all checks passed"
  TIMEOUT 60
)

add_executable (test_jtty_sticky_retry test_jtty_sticky_retry.f90)
target_include_directories (test_jtty_sticky_retry PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_sticky_retry)
add_test (NAME test_jtty_sticky_retry COMMAND test_jtty_sticky_retry)
set_tests_properties (test_jtty_sticky_retry PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_sticky_retry: all checks passed"
  TIMEOUT 60
)

add_executable (test_jtty_windowed_decode test_jtty_windowed_decode.f90)
target_include_directories (test_jtty_windowed_decode PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_windowed_decode)
add_test (NAME test_jtty_windowed_decode COMMAND test_jtty_windowed_decode)
set_tests_properties (test_jtty_windowed_decode PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_windowed_decode: all checks passed"
  TIMEOUT 60
)

add_executable (test_jtty_gap_merge test_jtty_gap_merge.f90)
target_include_directories (test_jtty_gap_merge PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_gap_merge)
add_test (NAME test_jtty_gap_merge COMMAND test_jtty_gap_merge)
set_tests_properties (test_jtty_gap_merge PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_gap_merge: all checks passed"
  TIMEOUT 60
)

configure_file(${CMAKE_SOURCE_DIR}/tests/fixtures/jtty/jtty_msgs.txt "${CMAKE_CURRENT_BINARY_DIR}/jtty_msgs.txt" COPYONLY)

add_test (NAME test_jtty_pack COMMAND test_jtty_pack)
set_tests_properties (test_jtty_pack PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  PASS_REGULAR_EXPRESSION "^ 3     3    67 14     NOW IS THE TIME FOR ALL GOOD MEN TO COME TO THE AID OF THEIR PARTY\.;^ 3     3    52 11     THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG, TWICE\.;\nTotal messages: 36   Number of errors:  0\n$"
)

add_executable (test_jtty_frequency_window test_jtty_frequency_window.f90)
wsjt_link_common_fortran (test_jtty_frequency_window)
add_test (NAME test_jtty_frequency_window COMMAND test_jtty_frequency_window)

add_executable (test_jtty_surface_ranges test_jtty_surface_ranges.f90)
wsjt_link_common_fortran (test_jtty_surface_ranges)
add_test (NAME test_jtty_surface_ranges COMMAND test_jtty_surface_ranges)
set_tests_properties (test_jtty_surface_ranges PROPERTIES TIMEOUT 60)

add_executable (test_jtty_get_updates test_jtty_get_updates.f90)
target_include_directories (test_jtty_get_updates PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_jtty_get_updates)
add_test (NAME test_jtty_get_updates COMMAND test_jtty_get_updates)
set_tests_properties (test_jtty_get_updates PROPERTIES
  PASS_REGULAR_EXPRESSION "test_jtty_get_updates: all checks passed"
)

add_executable (test_jtty_txstream
  test_jtty_txstream.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
  ${CMAKE_SOURCE_DIR}/Modulator/JttyPcmFifo.cpp
  ${CMAKE_SOURCE_DIR}/Audio/TxAudioQueue.cpp
  ${CMAKE_SOURCE_DIR}/Modulator/JttyTxStream.cpp
  ${CMAKE_SOURCE_DIR}/Audio/soundout.cpp)
target_link_libraries (test_jtty_txstream wsjt_qt Boost::log_setup Qt5::Test Qt5::Multimedia)
add_test (NAME test_jtty_txstream COMMAND test_jtty_txstream)
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_jtty_txstream PROPERTIES
    ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;TSAN_OPTIONS=${wsjt_tsan_gui_options}")
endif ()

add_executable (test_jtty_messages test_jtty_messages.cpp)
target_link_libraries (test_jtty_messages Qt5::Test)
add_test (NAME test_jtty_messages COMMAND test_jtty_messages)

add_executable (test_jtty_n1mm test_jtty_n1mm.cpp)
target_link_libraries (test_jtty_n1mm Qt5::Test)
add_test (NAME test_jtty_n1mm COMMAND test_jtty_n1mm)

add_executable (test_mmttyif
  test_mmttyif.cpp
  ${CMAKE_SOURCE_DIR}/widgets/MMTTYIF.cpp
)
target_link_libraries (test_mmttyif Qt5::Test Qt5::Network)
add_test (NAME test_mmttyif COMMAND test_mmttyif)
# Connection setup may retry for several seconds on slow Windows runners.
set_tests_properties (test_mmttyif PROPERTIES TIMEOUT 30)

add_executable (test_jtty_tbcc_list_decoder test_jtty_tbcc_list_decoder.f90)
wsjt_link_common_fortran (test_jtty_tbcc_list_decoder)
add_test (NAME test_jtty_tbcc_list_decoder COMMAND test_jtty_tbcc_list_decoder)

add_executable (test_jtty_tbcc_decoder test_jtty_tbcc_decoder.f90)
wsjt_link_common_fortran (test_jtty_tbcc_decoder)
add_test (NAME test_jtty_tbcc_decoder COMMAND test_jtty_tbcc_decoder)

add_executable (test_jtty_tbcc_code_profile test_jtty_tbcc_code_profile.f90)
wsjt_link_common_fortran (test_jtty_tbcc_code_profile)
add_test (NAME test_jtty_tbcc_code_profile COMMAND test_jtty_tbcc_code_profile)

add_executable (test_jtty_payload_correlators test_jtty_payload_correlators.f90)
wsjt_link_common_fortran (test_jtty_payload_correlators)
add_test (NAME test_jtty_payload_correlators COMMAND test_jtty_payload_correlators)
