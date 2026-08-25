foreach (required_variable JT9 SAMPLE EXPECTED WORK_DIR SOURCE_DIR MTD_WORKERS
    DECODE_CYCLES FFT_THREADS)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

file (MAKE_DIRECTORY "${WORK_DIR}")

set (input_samples "${SAMPLE}")
set (expected_completion_count 1)
if (CONSECUTIVE_PERIODS)
  set (input_samples)
  foreach (utc IN ITEMS 133430 133445 133500 133515)
    set (period_sample "${WORK_DIR}/210703_${utc}.wav")
    configure_file ("${SAMPLE}" "${period_sample}" COPYONLY)
    list (APPEND input_samples "${period_sample}")
  endforeach ()
  list (LENGTH input_samples expected_completion_count)
endif ()

set (qso_arguments)
set (qso_variables MY_CALL DX_CALL QSO_FREQUENCY QSO_PROGRESS)
set (configured_qso_variable_count 0)
foreach (qso_variable IN LISTS qso_variables)
  if (DEFINED ${qso_variable} AND NOT "${${qso_variable}}" STREQUAL "")
    math (EXPR configured_qso_variable_count
      "${configured_qso_variable_count} + 1")
  endif ()
endforeach ()
if (configured_qso_variable_count GREATER 0 AND
    NOT configured_qso_variable_count EQUAL 4)
  message (FATAL_ERROR
    "MY_CALL, DX_CALL, QSO_FREQUENCY, and QSO_PROGRESS must be set together")
elseif (configured_qso_variable_count EQUAL 4)
  list (APPEND qso_arguments
    -c "${MY_CALL}" -x "${DX_CALL}" -f "${QSO_FREQUENCY}" -Q "${QSO_PROGRESS}")
endif ()

set (actual_file "${WORK_DIR}/actual.txt")
set (stderr_file "${WORK_DIR}/stderr.txt")
file (WRITE "${stderr_file}" "")

execute_process (
  COMMAND "${CMAKE_COMMAND}" -E env OMP_STACKSIZE=16M
          "${JT9}"
          -8 -M -N ${MTD_WORKERS} -d 3 -C ${DECODE_CYCLES} -E 3 -D 3
          -p 15 -m ${FFT_THREADS} -L 200 -H 3000
          -a "${WORK_DIR}"
          -t "${WORK_DIR}"
          -r "${SOURCE_DIR}"
          ${qso_arguments}
          ${input_samples}
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE decoder_result
  OUTPUT_VARIABLE decoder_stdout
  ERROR_FILE "${stderr_file}")

file (WRITE "${actual_file}" "${decoder_stdout}")
file (READ "${stderr_file}" decoder_stderr)

if (NOT "${decoder_result}" STREQUAL "0")
  message (FATAL_ERROR
    "jt9 MTD exited with ${decoder_result}\n"
    "stdout:\n${decoder_stdout}\n"
    "stderr:\n${decoder_stderr}")
endif ()

if (NOT "${decoder_stderr}" STREQUAL "")
  message (FATAL_ERROR "jt9 MTD wrote to stderr:\n${decoder_stderr}")
endif ()

string (REGEX MATCHALL "<DecodeFinished>" completion_markers "${decoder_stdout}")
list (LENGTH completion_markers completion_count)
if (NOT completion_count EQUAL expected_completion_count)
  message (FATAL_ERROR
    "jt9 MTD completed ${completion_count} decode periods; "
    "expected ${expected_completion_count}")
endif ()

file (READ "${EXPECTED}" expected_output)

# OpenMP worker completion order and numeric decode metadata are not stable invariants.
function (extract_ft8_messages decoder_output output_variable)
  set (required_period "${ARGV2}")
  string (REPLACE "\r\n" "\n" normalized_output "${decoder_output}")
  string (REPLACE "\n" ";" output_lines "${normalized_output}")
  set (messages)
  foreach (line IN LISTS output_lines)
    if (NOT "${required_period}" STREQUAL "")
      string (STRIP "${line}" stripped_line)
      string (FIND "${stripped_line}" "${required_period} " period_position)
      if (NOT period_position EQUAL 0)
        continue ()
      endif ()
    endif ()
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

if (CONSECUTIVE_PERIODS)
  foreach (utc IN ITEMS 133430 133445 133500 133515)
    extract_ft8_messages ("${decoder_stdout}" period_messages "${utc}")
    if (NOT "${period_messages}" STREQUAL "${expected_messages}")
      message (FATAL_ERROR
        "jt9 MTD decoded message set differs in period ${utc}")
    endif ()
  endforeach ()
endif ()

if (NOT "${actual_messages}" STREQUAL "${expected_messages}")
  string (REPLACE ";" "\n  " expected_display "${expected_messages}")
  string (REPLACE ";" "\n  " actual_display "${actual_messages}")
  message (FATAL_ERROR
    "jt9 MTD decoded message set differs from ${EXPECTED}\n"
    "expected:\n  ${expected_display}\n"
    "actual:\n  ${actual_display}\n"
    "preserved output: ${actual_file}")
endif ()
