if (NOT DEFINED JT9_EXECUTABLE OR NOT DEFINED JT4_WAV OR NOT DEFINED TEST_DIRECTORY)
  message (FATAL_ERROR "JT4 engine test requires JT9_EXECUTABLE, JT4_WAV and TEST_DIRECTORY")
endif ()
file (MAKE_DIRECTORY "${TEST_DIRECTORY}")
execute_process (COMMAND "${JT9_EXECUTABLE}" --jt4 -b A -f 1000 -d 1
  -a "${TEST_DIRECTORY}" -t "${TEST_DIRECTORY}" "${JT4_WAV}"
  WORKING_DIRECTORY "${TEST_DIRECTORY}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 60)
if (NOT status EQUAL 0)
  message (FATAL_ERROR "JT4 CLI failed (${status}): ${error}\n${output}")
endif ()
if (NOT output MATCHES "K1ABC W9XYZ FN42")
  message (FATAL_ERROR "JT4 CLI did not decode the generated recording:\n${output}\n${error}")
endif ()
string (REGEX MATCHALL "K1ABC W9XYZ FN42" messages "${output}")
list (LENGTH messages count)
if (NOT count EQUAL 1 OR NOT output MATCHES "<DecodeFinished>[^\n]*[ ]1[ ]+0")
  message (FATAL_ERROR "JT4 CLI produced unexpected messages/completion:\n${output}")
endif ()
