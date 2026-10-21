execute_process (
  COMMAND "${TEST_EXECUTABLE}" --result-warning
  RESULT_VARIABLE status
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors)

if (NOT status STREQUAL "0")
  message (FATAL_ERROR "Result warning check failed (${status}): ${output}${errors}")
endif ()
if (NOT output STREQUAL "")
  message (FATAL_ERROR "Result warning check wrote unexpected stdout: ${output}")
endif ()
string (REPLACE "\r\n" "\n" errors "${errors}")
set (expected "Warning: FT8 MTD result capacity exceeded (200); additional results were dropped.\n")
if (NOT errors STREQUAL expected)
  message (FATAL_ERROR "Expected one result-capacity warning, received: ${errors}")
endif ()
