add_executable (test_tci_stream
  test_tci_stream.cpp
)
target_link_libraries (test_tci_stream Qt5::Test)
add_test (NAME test_tci_stream COMMAND test_tci_stream)

add_executable (test_tci_protocol
  test_tci_protocol.cpp
)
target_link_libraries (test_tci_protocol Qt5::Test)
add_test (NAME test_tci_protocol COMMAND test_tci_protocol)

add_library (tci_simulator STATIC
  TciSimScript.cpp
  TciSimScript.hpp
  TciSimServer.cpp
  TciSimServer.hpp
)
target_include_directories (tci_simulator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries (tci_simulator PUBLIC Qt5::Core Qt5::Network Qt5::WebSockets)

add_executable (test_tci_sim_smoke test_tci_sim_smoke.cpp)
target_link_libraries (test_tci_sim_smoke PRIVATE tci_simulator Qt5::Test)
add_test (NAME test_tci_sim_smoke COMMAND test_tci_sim_smoke)
set_tests_properties (test_tci_sim_smoke PROPERTIES TIMEOUT 30)

add_executable (test_tci_transceiver_characterization
  test_tci_transceiver_characterization.cpp
)
target_link_libraries (test_tci_transceiver_characterization PRIVATE
  tci_simulator wsjt_qt wsjt_fort_common Qt5::Test)
add_test (NAME test_tci_transceiver_characterization
  COMMAND test_tci_transceiver_characterization)
set_tests_properties (test_tci_transceiver_characterization PROPERTIES TIMEOUT 120)
