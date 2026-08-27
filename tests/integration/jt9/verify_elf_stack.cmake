foreach (required_variable READELF EXECUTABLE)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

execute_process (
  COMMAND "${READELF}" -W -l "${EXECUTABLE}"
  RESULT_VARIABLE readelf_result
  OUTPUT_VARIABLE readelf_stdout
  ERROR_VARIABLE readelf_stderr)

if (NOT "${readelf_result}" STREQUAL "0")
  message (FATAL_ERROR
    "readelf exited with ${readelf_result}\n"
    "stdout:\n${readelf_stdout}\n"
    "stderr:\n${readelf_stderr}")
endif ()

string (REGEX MATCH "GNU_STACK[^\r\n]*" stack_header "${readelf_stdout}")
if (NOT stack_header)
  message (FATAL_ERROR "${EXECUTABLE} has no GNU_STACK program header")
endif ()

if (stack_header MATCHES "[ \t]RWE([ \t]|$)" OR
    NOT stack_header MATCHES "[ \t]RW([ \t]|$)")
  message (FATAL_ERROR
    "${EXECUTABLE} does not have a non-executable read/write stack:\n"
    "${stack_header}")
endif ()
