add_executable (test_logging_lifecycle
  test_logging_lifecycle.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
  ${CMAKE_SOURCE_DIR}/WSJTXLogging.cpp
  )
target_include_directories (test_logging_lifecycle PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_logging_lifecycle PRIVATE
  Boost::log_setup Boost::log Qt5::Test Qt5::Widgets Qt5::Network Threads::Threads)
add_test (NAME test_logging_lifecycle COMMAND test_logging_lifecycle)
set_tests_properties (test_logging_lifecycle PROPERTIES TIMEOUT 60)

add_executable (test_logging_platform
  test_logging_platform.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
  ${CMAKE_SOURCE_DIR}/WSJTXLogging.cpp
  )
target_include_directories (test_logging_platform PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_logging_platform PRIVATE
  Boost::log_setup Boost::log Qt5::Test Qt5::Widgets Qt5::Network Threads::Threads)
add_test (NAME test_logging_platform COMMAND test_logging_platform)
set_tests_properties (test_logging_platform PROPERTIES TIMEOUT 60)
