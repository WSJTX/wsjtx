add_executable (test_qmap_livecq_parser
  test_livecq_parser.cpp
  ${CMAKE_SOURCE_DIR}/qmap/livecq_parser.cpp
  ${CMAKE_SOURCE_DIR}/validators/LiveCQCallsign.cpp
)
target_link_libraries (test_qmap_livecq_parser Qt5::Test)
add_test (NAME test_qmap_livecq_parser COMMAND test_qmap_livecq_parser)

add_executable (test_qmap_plotter_initialization
  test_plotter_initialization.cpp
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
set_tests_properties (test_qmap_plotter_initialization PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable (test_qmap_ipc test_qmap_ipc.cpp)
target_link_libraries (test_qmap_ipc Qt5::Test)
add_test (NAME test_qmap_ipc COMMAND test_qmap_ipc)
