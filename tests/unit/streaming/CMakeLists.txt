add_executable (test_streaming_consumers test_streaming_consumers.cpp)
target_link_libraries (test_streaming_consumers Qt5::Test)
target_compile_definitions (test_streaming_consumers PRIVATE
  JT9_EXECUTABLE="$<TARGET_FILE:jt9>"
  WSPRD_EXECUTABLE="$<TARGET_FILE:wsprd>"
)

add_test (NAME test_streaming_consumers COMMAND test_streaming_consumers)
set_tests_properties (test_streaming_consumers PROPERTIES
  LABELS "streaming;runtime"
  TIMEOUT 120
)
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_streaming_consumers PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
