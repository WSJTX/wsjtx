foreach (required_variable ENGINE AUDIO WORK_DIR)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

file (MAKE_DIRECTORY "${WORK_DIR}")
set (sample "${WORK_DIR}/261116_0016.wav")
set (message "PJ4/W9XYZ 23")
execute_process (
  COMMAND "${AUDIO}" wav "${sample}" 4 "${message}" 1500 -29 1
  RESULT_VARIABLE result ERROR_VARIABLE errors)
if (NOT "${result}" STREQUAL "0")
  message (FATAL_ERROR "WSPR fixture generation failed: ${result}\n${errors}")
endif ()

# DEFAULT establishes that this reception is decodable. FAST skips the time
# offset search needed to recover the same station.
foreach (effort IN ITEMS 0 1)
  execute_process (
    COMMAND "${ENGINE}" --decode-wav "${sample}" 0016 2 "${effort}" 0 1390 1610 14.0956
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if (NOT "${result}" STREQUAL "0" OR NOT output MATCHES "<DecodeFinished>\r?\n$")
    message (FATAL_ERROR "WSPR engine effort ${effort} failed: ${result}\n${output}${errors}")
  endif ()
  if (effort EQUAL 0 AND NOT output MATCHES "${message}[ ]*\r?\n")
    message (FATAL_ERROR "WSPR DEFAULT did not decode the weak signal:\n${output}")
  elseif (effort EQUAL 1 AND output MATCHES "${message}[ ]*\r?\n")
    message (FATAL_ERROR "WSPR FAST decoded the weak signal:\n${output}")
  endif ()
endforeach ()
