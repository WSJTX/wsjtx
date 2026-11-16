foreach (required_variable ENGINE WSPRD WORK_DIR)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

foreach (variant IN ITEMS 2 15)
  if (variant EQUAL 2)
    set (frequency 1520)
    set (low 1390)
    set (high 1610)
    set (variant_args)
  else ()
    set (frequency 1615)
    set (low 1598.75)
    set (high 1626.25)
    set (variant_args -m)
  endif ()
  set (sample "${WORK_DIR}/${variant}/000000_0000.wav")
  file (MAKE_DIRECTORY "${WORK_DIR}/${variant}")
  execute_process (
    COMMAND "${ENGINE}" --write-wav "${sample}" "K1ABC FN42 33" "${variant}" "${frequency}" 1.2
    RESULT_VARIABLE result ERROR_VARIABLE errors)
  if (NOT "${result}" STREQUAL "0")
    message (FATAL_ERROR "WSPR fixture generation failed: ${result}\n${errors}")
  endif ()
  foreach (effort IN ITEMS 0 1 2 3)
    if (effort EQUAL 0)
      set (effort_args)
    elseif (effort EQUAL 1)
      set (effort_args -qB)
    elseif (effort EQUAL 2)
      set (effort_args -C 500 -o 4)
    else ()
      set (effort_args -C 500 -o 4 -d)
    endif ()
    foreach (calibration IN ITEMS 0 3.25 -2.5)
      set (case_dir "${WORK_DIR}/${variant}/${effort}/${calibration}")
      file (MAKE_DIRECTORY "${case_dir}")
      execute_process (
        COMMAND "${WSPRD}" -a "${case_dir}" -H -f 14.0956 -e "${calibration}"
          ${variant_args} ${effort_args} "${sample}"
        WORKING_DIRECTORY "${case_dir}"
        RESULT_VARIABLE cli_result OUTPUT_VARIABLE cli_output ERROR_VARIABLE cli_error)
      execute_process (
        COMMAND "${ENGINE}" --decode-wav "${sample}" 0000 "${variant}" "${effort}"
          "${calibration}" "${low}" "${high}" 14.0956
        WORKING_DIRECTORY "${case_dir}"
        RESULT_VARIABLE engine_result OUTPUT_VARIABLE engine_output ERROR_VARIABLE engine_error)
      file (WRITE "${case_dir}/cli.txt" "${cli_output}")
      file (WRITE "${case_dir}/engine.txt" "${engine_output}")
      if (NOT "${cli_result}" STREQUAL "0" OR NOT "${engine_result}" STREQUAL "0")
        message (FATAL_ERROR
          "WSPR ${variant}/${effort}/${calibration} failed: CLI=${cli_result}, engine=${engine_result}\n"
          "CLI stderr:\n${cli_error}\nEngine stderr:\n${engine_error}")
      endif ()
      if (NOT "${cli_output}" STREQUAL "${engine_output}")
        message (FATAL_ERROR
          "WSPR ${variant}/${effort}/${calibration} CLI/memory outputs differ\n"
          "CLI:\n${cli_output}\nMemory:\n${engine_output}")
      endif ()
      if (NOT cli_output MATCHES "K1ABC FN42 33")
        message (FATAL_ERROR "WSPR ${variant}/${effort}/${calibration} did not decode its signal")
      endif ()
    endforeach ()
  endforeach ()
endforeach ()
