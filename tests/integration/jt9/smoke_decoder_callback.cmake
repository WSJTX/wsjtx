foreach (required_variable JT9 SAMPLE EXPECTED_MESSAGE WORK_DIR SOURCE_DIR)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

file (MAKE_DIRECTORY "${WORK_DIR}")
set (actual_file "${WORK_DIR}/actual.txt")

execute_process (
  COMMAND "${JT9}"
          -a "${WORK_DIR}"
          -t "${WORK_DIR}"
          -r "${SOURCE_DIR}"
          -8
          -d 1
          -x b
          -L 1000
          -H 1400
          "${SAMPLE}"
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE decoder_result
  OUTPUT_VARIABLE decoder_stdout
  ERROR_VARIABLE decoder_stderr)

file (WRITE "${actual_file}" "${decoder_stdout}")

if (NOT "${decoder_result}" STREQUAL "0")
  message (FATAL_ERROR
    "jt9 exited with ${decoder_result}\n"
    "stdout:\n${decoder_stdout}\n"
    "stderr:\n${decoder_stderr}")
endif ()

if (NOT "${decoder_stderr}" STREQUAL "")
  message (FATAL_ERROR "jt9 wrote to stderr:\n${decoder_stderr}")
endif ()

string (FIND "${decoder_stdout}" "${EXPECTED_MESSAGE}" message_position)
string (FIND "${decoder_stdout}" "<DecodeFinished>" completion_position)
if (message_position LESS 0 OR completion_position LESS 0)
  message (FATAL_ERROR
    "jt9 did not complete the expected FT8 callback path\n"
    "expected message: ${EXPECTED_MESSAGE}\n"
    "actual:\n${decoder_stdout}\n"
    "preserved output: ${actual_file}")
endif ()
