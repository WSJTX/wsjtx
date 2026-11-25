execute_process (COMMAND "${WSPRSIM}" -d "<K1ABC> FN20 37"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if (NOT result STREQUAL "0" OR NOT output MATCHES "Data is :9B D1 C9 13 2F 26 80 00 00 00 00")
  message (FATAL_ERROR "wsprsim encoded the four-character locator incorrectly: ${result} ${output} ${error}")
endif ()

execute_process (COMMAND "${WSPRSIM}" -cd "K1ABC FN20"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if (NOT result STREQUAL "1" OR NOT error MATCHES "Invalid WSPR message" OR NOT output STREQUAL "")
  message (FATAL_ERROR "wsprsim failed to reject missing power: ${result} ${output} ${error}")
endif ()
