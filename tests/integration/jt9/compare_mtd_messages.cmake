foreach (required_variable JT9 SAMPLE EXPECTED WORK_DIR SOURCE_DIR)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

file (MAKE_DIRECTORY "${WORK_DIR}")
set (actual_file "${WORK_DIR}/actual.txt")
set (stderr_file "${WORK_DIR}/stderr.txt")

execute_process (
  COMMAND "${CMAKE_COMMAND}" -E env OMP_STACKSIZE=16M
          "${JT9}"
          -8 -M -N 4 -d 3 -C 3 -E 3 -D 3
          -p 15 -m 3 -L 200 -H 3000
          -a "${WORK_DIR}"
          -t "${WORK_DIR}"
          -r "${SOURCE_DIR}"
          "${SAMPLE}"
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE decoder_result
  OUTPUT_VARIABLE decoder_stdout
  ERROR_VARIABLE decoder_stderr)

file (WRITE "${actual_file}" "${decoder_stdout}")
file (WRITE "${stderr_file}" "${decoder_stderr}")

if (NOT "${decoder_result}" STREQUAL "0")
  message (FATAL_ERROR
    "jt9 MTD exited with ${decoder_result}\n"
    "stdout:\n${decoder_stdout}\n"
    "stderr:\n${decoder_stderr}")
endif ()

if (NOT "${decoder_stderr}" STREQUAL "")
  message (FATAL_ERROR "jt9 MTD wrote to stderr:\n${decoder_stderr}")
endif ()

file (READ "${EXPECTED}" expected_output)

# OpenMP worker completion order and numeric decode metadata are not stable invariants.
function (extract_ft8_messages decoder_output output_variable)
  string (REPLACE "\r\n" "\n" normalized_output "${decoder_output}")
  string (REPLACE "\n" ";" output_lines "${normalized_output}")
  set (messages)
  foreach (line IN LISTS output_lines)
    string (FIND "${line}" "~" marker_position)
    if (marker_position GREATER_EQUAL 0)
      math (EXPR message_position "${marker_position} + 1")
      string (SUBSTRING "${line}" ${message_position} -1 message)
      string (STRIP "${message}" message)
      if (NOT "${message}" STREQUAL "")
        list (APPEND messages "${message}")
      endif ()
    endif ()
  endforeach ()
  list (REMOVE_DUPLICATES messages)
  list (SORT messages)
  set (${output_variable} "${messages}" PARENT_SCOPE)
endfunction ()

extract_ft8_messages ("${expected_output}" expected_messages)
extract_ft8_messages ("${decoder_stdout}" actual_messages)

if (NOT "${actual_messages}" STREQUAL "${expected_messages}")
  string (REPLACE ";" "\n  " expected_display "${expected_messages}")
  string (REPLACE ";" "\n  " actual_display "${actual_messages}")
  message (FATAL_ERROR
    "jt9 MTD decoded message set differs from ${EXPECTED}\n"
    "expected:\n  ${expected_display}\n"
    "actual:\n  ${actual_display}\n"
    "preserved output: ${actual_file}")
endif ()
