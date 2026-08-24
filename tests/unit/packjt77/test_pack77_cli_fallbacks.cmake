function(expect_fallback label executable expected_output)
  execute_process(
    COMMAND "${executable}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message(FATAL_ERROR "${label} fallback failed (${result}):\n${output}${error}")
  endif ()
  if (NOT output MATCHES "${expected_output}")
    message(FATAL_ERROR "${label} fallback output was unexpected:\n${output}")
  endif ()
endfunction()

function(expect_rejected label executable)
  execute_process(
    COMMAND "${executable}" "HELLO@WORLD" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if (result EQUAL 0)
    message(FATAL_ERROR "${label} accepted an unrepresentable message:\n${output}${error}")
  endif ()
  if (NOT output MATCHES "Cannot encode message:[ ]+HELLO@WORLD")
    message(FATAL_ERROR "${label} rejection output was unexpected:\n${output}${error}")
  endif ()
endfunction()

expect_fallback("ft4code" "${FT4CODE}"
  "THIS FREE TEX.*0\\.0[ ]+Free text" "THIS FREE TEXT IS TOO LONG")
expect_rejected("ft4code" "${FT4CODE}")

expect_fallback("ft4sim" "${FT4SIM}"
  "THIS FREE TEX.*i3.n3:[ ]+0\\.0" "THIS FREE TEXT IS TOO LONG"
  1500 0.0 0.0 0.0 0 99)
expect_rejected("ft4sim" "${FT4SIM}" 1500 0.0 0.0 0.0 0 99)

execute_process(
  COMMAND "${FST4SIM}" "THIS FREE TEXT IS TOO LONG" 60 1500 0.0 0.0 0.0 0 99 F
  RESULT_VARIABLE fst4_result
  OUTPUT_VARIABLE fst4_output
  ERROR_VARIABLE fst4_error
)
if (NOT fst4_result EQUAL 0)
  message(FATAL_ERROR "fst4sim fallback failed (${fst4_result}):\n${fst4_output}${fst4_error}")
endif ()
if (NOT fst4_output MATCHES "THIS FREE TEX")
  message(FATAL_ERROR "fst4sim did not report the canonical fallback message:\n${fst4_output}")
endif ()
if (NOT fst4_output MATCHES "iwspr:[ ]+0")
  message(FATAL_ERROR "fst4sim did not select FST4 for the fallback message:\n${fst4_output}")
endif ()
expect_rejected("fst4sim" "${FST4SIM}" 60 1500 0.0 0.0 0.0 0 99 F)

expect_fallback("ldpcsim240_101" "${LDPCSIM240_101}"
  "Message sent:[ ]+THIS FREE TEX" 1 -1 0 1.0 101
  "THIS FREE TEXT IS TOO LONG")
execute_process(
  COMMAND "${LDPCSIM240_101}" 1 -1 0 1.0 101 "HELLO@WORLD"
  RESULT_VARIABLE ldpc_rejected_result
  OUTPUT_VARIABLE ldpc_rejected_output
  ERROR_VARIABLE ldpc_rejected_error
)
if (ldpc_rejected_result EQUAL 0)
  message(FATAL_ERROR "ldpcsim240_101 accepted an unrepresentable message:\n${ldpc_rejected_output}${ldpc_rejected_error}")
endif ()
if (NOT ldpc_rejected_output MATCHES "Cannot encode message:[ ]+HELLO@WORLD")
  message(FATAL_ERROR "ldpcsim240_101 rejection output was unexpected:\n${ldpc_rejected_output}${ldpc_rejected_error}")
endif ()

expect_fallback("msk144code" "${MSK144CODE}"
  "THIS FREE TEX.*0\\.0[ ]+Free text" "THIS FREE TEXT IS TOO LONG")
expect_rejected("msk144code" "${MSK144CODE}")
