add_executable (test_qmap_livecq_parser
  test_livecq_parser.cpp
  ${CMAKE_SOURCE_DIR}/qmap/livecq_parser.cpp
  ${CMAKE_SOURCE_DIR}/validators/LiveCQCallsign.cpp
)
target_link_libraries (test_qmap_livecq_parser Qt5::Test)
add_test (NAME test_qmap_livecq_parser COMMAND test_qmap_livecq_parser)

add_executable (test_qmap_plotter_initialization
  test_plotter_initialization.cpp
  ${CMAKE_SOURCE_DIR}/qmap/decode_click_coalescer.cpp
  ${CMAKE_SOURCE_DIR}/qmap/plotter.cpp
)
target_link_libraries (test_qmap_plotter_initialization
  wsjt_qt
  qmap_impl
  ${FFTW3_LIBRARIES}
  Qt5::Test
  Qt5::Widgets
)
add_test (NAME test_qmap_plotter_initialization COMMAND test_qmap_plotter_initialization)
if (WIN32)
  set_tests_properties (test_qmap_plotter_initialization PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=windows"
  )
else ()
  set_tests_properties (test_qmap_plotter_initialization PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  )
endif ()

add_executable (test_qmap_sample_rates
  test_qmap_sample_rates.cpp
  ${CMAKE_SOURCE_DIR}/qmap/getfile.cpp
)
target_include_directories (test_qmap_sample_rates PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_qmap_sample_rates wsjt_qt qmap_impl Qt5::Test)
add_test (NAME test_qmap_sample_rates COMMAND test_qmap_sample_rates)

add_executable (test_qmap_qm_rate test_qmap_qm_rate.f90)
target_include_directories (test_qmap_qm_rate PRIVATE ${CMAKE_BINARY_DIR}/qmap/libqmap)
target_link_libraries (test_qmap_qm_rate PRIVATE qmap_impl)
add_test (NAME test_qmap_qm_rate COMMAND test_qmap_qm_rate)

add_executable (test_qmap_ipc test_qmap_ipc.cpp)
target_link_libraries (test_qmap_ipc Qt5::Test)
add_test (NAME test_qmap_ipc COMMAND test_qmap_ipc)

add_executable (test_qmap_decode_record test_qmap_decode_record.cpp)
target_link_libraries (test_qmap_decode_record wsjt_qt wsjt_fort_common Qt5::Test)
add_test (NAME test_qmap_decode_record COMMAND test_qmap_decode_record)

add_executable (test_qmap_decode_click_coalescer
  test_decode_click_coalescer.cpp
  ${CMAKE_SOURCE_DIR}/qmap/decode_click_coalescer.cpp
)
target_link_libraries (test_qmap_decode_click_coalescer Qt5::Test Qt5::Widgets)
add_test (NAME test_qmap_decode_click_coalescer COMMAND test_qmap_decode_click_coalescer)

add_executable (test_qmap_candidates test_qmap_candidates.f90)
target_include_directories (test_qmap_candidates PRIVATE ${CMAKE_BINARY_DIR}/qmap/libqmap)
target_link_libraries (test_qmap_candidates PRIVATE qmap_impl)
add_test (NAME test_qmap_candidates COMMAND test_qmap_candidates)

add_executable (test_qmap_symspec_contract test_qmap_symspec_contract.f90)
target_include_directories (test_qmap_symspec_contract PRIVATE ${CMAKE_BINARY_DIR}/qmap/libqmap)
target_link_libraries (test_qmap_symspec_contract PRIVATE qmap_impl)
add_test (NAME test_qmap_symspec_contract COMMAND test_qmap_symspec_contract)

add_executable (test_qmap_q65_handoff test_qmap_q65_handoff.f90)
target_include_directories (test_qmap_q65_handoff PRIVATE ${CMAKE_BINARY_DIR}/qmap/libqmap)
target_link_libraries (test_qmap_q65_handoff PRIVATE qmap_impl)
add_test (NAME test_qmap_q65_handoff COMMAND test_qmap_q65_handoff)

add_executable (test_qmap_q65_connection
  ${CMAKE_SOURCE_DIR}/tests/unit/q65/q65_test_fixture.f90
  test_qmap_q65_connection.f90)
target_include_directories (test_qmap_q65_connection PRIVATE ${CMAKE_BINARY_DIR}/qmap/libqmap)
target_link_libraries (test_qmap_q65_connection PRIVATE qmap_impl)
add_test (NAME test_qmap_q65_connection COMMAND test_qmap_q65_connection)
add_test (NAME test_qmap_q65_connection_30_second_half COMMAND test_qmap_q65_connection 30)
