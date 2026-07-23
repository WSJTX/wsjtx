execute_process(
  COMMAND "${Q65CODE}" "THIS FREE TEXT IS TOO LONG"
  RESULT_VARIABLE fallback_result
  OUTPUT_VARIABLE fallback_output
  ERROR_VARIABLE fallback_error
)
if (NOT fallback_result EQUAL 0)
  message(FATAL_ERROR "Legacy fallback failed (${fallback_result}):\n${fallback_output}${fallback_error}")
endif ()
if (NOT fallback_output MATCHES "Message sent:[ ]+THIS FREE TEX")
  message(FATAL_ERROR "Fallback output was unexpected:\n${fallback_output}")
endif ()

execute_process(
  COMMAND "${Q65CODE}" "HELLO@WORLD"
  RESULT_VARIABLE rejected_result
  OUTPUT_VARIABLE rejected_output
  ERROR_VARIABLE rejected_error
)
if (rejected_result EQUAL 0)
  message(FATAL_ERROR "Unrepresentable message returned success:\n${rejected_output}")
endif ()
if (NOT rejected_output MATCHES "Cannot encode message:[ ]+HELLO@WORLD")
  message(FATAL_ERROR "Rejected message output was unexpected:\n${rejected_output}${rejected_error}")
endif ()
