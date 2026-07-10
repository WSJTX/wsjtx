execute_process(
  COMMAND "${ENCODE77}" "CQ K1ABC FN42"
  RESULT_VARIABLE valid_result
  OUTPUT_VARIABLE valid_output
  ERROR_VARIABLE valid_error
)
if (NOT valid_result EQUAL 0)
  message(FATAL_ERROR "Valid message failed (${valid_result}):\n${valid_output}${valid_error}")
endif ()
if (NOT valid_output MATCHES "1[ ]+OK[ ]+CQ K1ABC FN42[ ]+CQ K1ABC FN42")
  message(FATAL_ERROR "Valid message output was unexpected:\n${valid_output}")
endif ()

execute_process(
  COMMAND "${ENCODE77}" "THIS FREE TEXT IS TOO LONG"
  RESULT_VARIABLE rejected_result
  OUTPUT_VARIABLE rejected_output
  ERROR_VARIABLE rejected_error
)
if (rejected_result EQUAL 0)
  message(FATAL_ERROR "Rejected message returned success:\n${rejected_output}")
endif ()
if (NOT rejected_output MATCHES "REJECTED[ ]+THIS FREE TEXT IS TOO LONG[ ]+free text too long")
  message(FATAL_ERROR "Rejected message output was unexpected:\n${rejected_output}${rejected_error}")
endif ()

execute_process(
  COMMAND "${ENCODE77}" -f "${INPUT_FILE}"
  WORKING_DIRECTORY "${INPUT_DIR}"
  RESULT_VARIABLE batch_result
  OUTPUT_VARIABLE batch_output
  ERROR_VARIABLE batch_error
)
if (batch_result EQUAL 0)
  message(FATAL_ERROR "Batch with a rejection returned success:\n${batch_output}")
endif ()
if (NOT batch_output MATCHES "Summary: 2 encoded, 1 rejected, 0 unpack failures, 0 differences")
  message(FATAL_ERROR "Batch summary was unexpected:\n${batch_output}${batch_error}")
endif ()
