foreach (required_variable ENGINE WSPRD WORK_DIR VARIANT)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

set (message "K1ABC FN42 33")
file (MAKE_DIRECTORY "${WORK_DIR}")

function (write_signal sample frequency drift)
  execute_process (
    COMMAND "${ENGINE}" --write-wav "${sample}" "${message}" "${VARIANT}" "${frequency}" 1.2 "${drift}"
    RESULT_VARIABLE result ERROR_VARIABLE errors)
  if (NOT "${result}" STREQUAL "0")
    message (FATAL_ERROR "WSPR fixture generation failed: ${result}\n${errors}")
  endif ()
endfunction ()

function (check_pair sample case effort calibration frequency_pattern drift_pattern)
  set (case_dir "${WORK_DIR}/${case}")
  file (MAKE_DIRECTORY "${case_dir}")
  file (REMOVE "${case_dir}/wspr_wisdom.dat" "${case_dir}/wspr_timer.out" "${case_dir}/ALL_WSPR.TXT")
  execute_process (
    COMMAND "${WSPRD}" -a "${case_dir}" -H -f 14.0956 -e "${calibration}"
      ${variant_args} ${ARGN} "${sample}"
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE cli_result OUTPUT_VARIABLE cli_output ERROR_VARIABLE cli_error)
  execute_process (
    COMMAND "${ENGINE}" --decode-wav "${sample}" 0000 "${VARIANT}" "${effort}"
      "${calibration}" "${low}" "${high}" 14.0956
    WORKING_DIRECTORY "${case_dir}"
    RESULT_VARIABLE engine_result OUTPUT_VARIABLE engine_output ERROR_VARIABLE engine_error)
  file (WRITE "${case_dir}/cli.txt" "${cli_output}")
  file (WRITE "${case_dir}/engine.txt" "${engine_output}")
  if (NOT "${cli_result}" STREQUAL "0" OR NOT "${engine_result}" STREQUAL "0")
    message (FATAL_ERROR
      "WSPR-${VARIANT} ${case} failed: CLI=${cli_result}, engine=${engine_result}\n"
      "CLI stderr:\n${cli_error}\nEngine stderr:\n${engine_error}")
  endif ()
  if (NOT "${cli_output}" STREQUAL "${engine_output}")
    message (FATAL_ERROR
      "WSPR-${VARIANT} ${case} CLI/memory outputs differ\nCLI:\n${cli_output}\nMemory:\n${engine_output}")
  endif ()
  if (NOT cli_output MATCHES "<DecodeFinished>\r?\n$")
    message (FATAL_ERROR "WSPR-${VARIANT} ${case} did not report completion:\n${cli_output}")
  endif ()
  if (frequency_pattern STREQUAL "")
    if (NOT cli_output MATCHES "^<DecodeFinished>\r?\n$")
      message (FATAL_ERROR "WSPR-${VARIANT} ${case} admitted an out-of-band signal:\n${cli_output}")
    endif ()
  elseif (NOT cli_output MATCHES "[ ]${frequency_pattern}[ ]+${drift_pattern}[ ]+${message}[ ]*\r?\n")
    message (FATAL_ERROR "WSPR-${VARIANT} ${case} did not report the expected frequency and drift:\n${cli_output}")
  endif ()
endfunction ()

set (sample "${WORK_DIR}/000000_0000.wav")
if (VARIANT EQUAL 2)
  set (low 1390)
  set (high 1610)
  write_signal ("${sample}" 1520 0)
  # DEEP can report a second candidate for this signal. Retain the full stdout comparison.
  check_pair ("${sample}" deep-positive 3 3.25 "14[.]09711[678]" 0 -C 500 -o 4 -d)
  check_pair ("${sample}" normal-negative 2 -2.5 "14[.]09712[23]" 0 -C 500 -o 4)
elseif (VARIANT EQUAL 15)
  set (low 1598.75)
  set (high 1626.25)
  set (variant_args -m)
  write_signal ("${sample}" 1615 0)
  check_pair ("${sample}" fast-negative 1 -2.5 "14[.]09721[78]" 0 -qB)

  # The legacy per-pass calibration admits this signal on FAST's second pass.
  # The displayed drift is in WSPR-15 decoder units (eight times physical Hz).
  set (sample "${WORK_DIR}/edge/000000_0000.wav")
  file (MAKE_DIRECTORY "${WORK_DIR}/edge")
  write_signal ("${sample}" 1626.8 0.25)
  check_pair ("${sample}" fast-edge-positive 1 3.25 "14[.]09722[345]" "[123]" -qB)
  check_pair ("${sample}" fast-edge-zero 1 0 "" "" -qB)
else ()
  message (FATAL_ERROR "Unsupported WSPR variant: ${VARIANT}")
endif ()
