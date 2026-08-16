find_package (Qt5Test 5 REQUIRED)

#
# Compiler options
#
set (CMAKE_CXX_STANDARD 11)
add_compile_options ("$<$<COMPILE_LANGUAGE:Fortran>:-Wall;-Wno-conversion;-fno-second-underscore;-fno-f2c>")
add_compile_options ("$<$<AND:$<COMPILE_LANGUAGE:Fortran>,$<CONFIG:Debug>>:-fbounds-check>")
add_compile_options ("$<$<AND:$<COMPILE_LANGUAGE:Fortran>,$<NOT:$<CONFIG:Debug>>>:-funroll-all-loops>")
add_compile_options ("$<$<AND:$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>>>:-Wall;-Wextra>")
add_compile_options ("$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU>>:-Wno-pragmas>")
add_compile_options ("$<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:GNU>>,$<NOT:$<CONFIG:Debug>>>:-fdata-sections;-ffunction-sections>")
if (${OPENMP_FOUND} OR APPLE)
  add_compile_options ("$<$<AND:$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>,$<C_COMPILER_ID:GNU>>:${OpenMP_C_FLAGS}>")
endif ()

# Tell CMake to run moc when necessary
set (CMAKE_AUTOMOC ON)
include_directories (${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR})

add_executable (test_qt_helpers test_qt_helpers.cpp)
target_link_libraries (test_qt_helpers wsjt_qt Qt5::Test)
add_test (test_qt_helpers test_qt_helpers)
add_subdirectory (unit/hrd)
add_subdirectory (unit/packjt77)

add_executable (test_http_user_agent test_http_user_agent.cpp)
target_link_libraries (test_http_user_agent wsjt_qt Qt5::Test)
add_test (test_http_user_agent test_http_user_agent)

add_executable (test_emulate_split_transceiver test_emulate_split_transceiver.cpp)
target_link_libraries (test_emulate_split_transceiver Boost::log wsjt_qt Qt5::Test Qt5::Network Qt5::Widgets)
add_test (test_emulate_split_transceiver test_emulate_split_transceiver)

add_executable (test_tx_inhibit_logic test_tx_inhibit_logic.cpp)
target_link_libraries (test_tx_inhibit_logic wsjt_qt Qt5::Test Qt5::Network)
add_test (test_tx_inhibit_logic test_tx_inhibit_logic)

add_executable (test_keying_monitor
  ${CMAKE_SOURCE_DIR}/tools/inhibit-agent/test_keying_monitor.cpp)
target_include_directories (test_keying_monitor PRIVATE
  ${CMAKE_SOURCE_DIR}/tools/inhibit-agent)
target_link_libraries (test_keying_monitor Qt5::Core)
add_test (test_keying_monitor test_keying_monitor)

# Logger.cpp is compiled into the wsjtx app, not wsjt_qt, but archive
# members this test drags in (Configuration -> FileDownload on MinGW)
# reference the sys/data global loggers it defines.
add_executable (test_tx_inhibit_gate test_tx_inhibit_gate.cpp ${CMAKE_SOURCE_DIR}/Logger.cpp)
target_link_libraries (test_tx_inhibit_gate wsjt_qt Boost::log_setup Qt5::Test Qt5::Network)
add_test (test_tx_inhibit_gate test_tx_inhibit_gate)

add_executable (test_bwf_file test_bwf_file.cpp)
target_link_libraries (test_bwf_file wsjt_qt wsjt_qtmm Qt5::Test Qt5::Multimedia)
add_test (test_bwf_file test_bwf_file)
