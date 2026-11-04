execute_process (
  COMMAND "${TEST_EXECUTABLE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 30)
string (REPLACE "\r\n" "\n" output "${output}")
set (expected "{\"t\":\"reply-start\"}\n{\"t\":\"reply-end\"}\n{\"v\":1,\"t\":\"pass_progress\",\"mode\":\"FT8\",\"period_end\":\"222222\",\"nzhsym\":49}\n")
if (NOT result STREQUAL "0" OR NOT output STREQUAL expected)
  message (FATAL_ERROR "Progress contention test failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
endif ()
