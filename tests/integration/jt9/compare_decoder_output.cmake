foreach (required_variable JT9 MODE SAMPLE EXPECTED WORK_DIR SOURCE_DIR)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

file (MAKE_DIRECTORY "${WORK_DIR}")
set (actual_file "${WORK_DIR}/actual.txt")
set (stderr_file "${WORK_DIR}/stderr.txt")

execute_process (
  COMMAND "${JT9}"
          -a "${WORK_DIR}"
          -t "${WORK_DIR}"
          -r "${SOURCE_DIR}"
          "${MODE}"
          "${SAMPLE}"
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE decoder_result
  OUTPUT_VARIABLE decoder_stdout
  ERROR_VARIABLE decoder_stderr)

file (WRITE "${actual_file}" "${decoder_stdout}")
file (WRITE "${stderr_file}" "${decoder_stderr}")

if (NOT "${decoder_result}" STREQUAL "0")
  message (FATAL_ERROR
    "jt9 exited with ${decoder_result}\n"
    "stdout:\n${decoder_stdout}\n"
    "stderr:\n${decoder_stderr}")
endif ()

if (NOT "${decoder_stderr}" STREQUAL "")
  message (FATAL_ERROR "jt9 wrote to stderr:\n${decoder_stderr}")
endif ()

file (READ "${EXPECTED}" expected_output)
if (NOT "${decoder_stdout}" STREQUAL "${expected_output}")
  message (FATAL_ERROR
    "jt9 output differs from ${EXPECTED}\n"
    "expected:\n${expected_output}\n"
    "actual:\n${decoder_stdout}\n"
    "preserved output: ${actual_file}")
endif ()
