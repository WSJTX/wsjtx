foreach (message IN ITEMS "K1ABC FN20 37" "PJ4/K1ABC 37" "<K1ABC> FN20AB 37" "<K1ABC> FN20 37")
  execute_process (COMMAND "${WSPRCODE}" "${message}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if (NOT result EQUAL 0 OR NOT output MATCHES "Source-encoded message")
    message (FATAL_ERROR "wsprcode rejected ${message}: ${result} ${error}")
  endif ()
endforeach ()
foreach (message IN ITEMS "PJ4/ABC 37" "<K1ABC> ZZ 37" "K1ABC FN20 X")
  execute_process (COMMAND "${WSPRCODE}" "${message}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if (result EQUAL 0 OR NOT output MATCHES "Invalid WSPR message" OR output MATCHES "Source-encoded message")
    message (FATAL_ERROR "wsprcode failed to reject ${message}: ${result} ${output} ${error}")
  endif ()
endforeach ()
