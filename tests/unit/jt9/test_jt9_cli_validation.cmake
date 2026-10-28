function (require_rejection option value)
  execute_process (
    COMMAND "${program}" "${option}" "${value}"
    RESULT_VARIABLE result
    ERROR_VARIABLE stderr)
  if (NOT "${result}" STREQUAL "2")
    message (FATAL_ERROR "${program_name} ${option}=${value} exited with ${result}, expected 2")
  endif ()
  if (NOT stderr MATCHES "${program_name}: invalid value")
    message (FATAL_ERROR "${option}=${value} did not report a validation error: ${stderr}")
  endif ()
endfunction ()

function (require_acceptance)
  execute_process (
    COMMAND "${program}" ${ARGN} --version
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if (NOT "${result}" STREQUAL "0" OR NOT stdout MATCHES "^${program_name} [0-9]" OR
      NOT stderr STREQUAL "")
    message (FATAL_ERROR "${program_name} ${ARGN} did not accept its arguments: ${stdout}${stderr}")
  endif ()
endfunction ()

function (require_text_acceptance option value)
  execute_process (
    COMMAND "${program}" "${option}" "${value}" --version
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if (NOT "${result}" STREQUAL "0" OR NOT stdout MATCHES "^${program_name} [0-9]" OR
      NOT stderr STREQUAL "")
    message (FATAL_ERROR "${program_name} ${option}=${value} did not accept its argument: ${stdout}${stderr}")
  endif ()
endfunction ()

function (require_mode_result expected_stderr)
  execute_process (
    COMMAND "${program}" ${ARGN} "${missing_wav}"
    RESULT_VARIABLE result
    ERROR_VARIABLE stderr
    TIMEOUT 10)
  if (NOT "${result}" STREQUAL "2" OR NOT stderr MATCHES "${expected_stderr}")
    message (FATAL_ERROR "${program_name} ${ARGN} produced an unexpected result (${result}): ${stderr}")
  endif ()
endfunction ()

set (missing_wav "${CMAKE_CURRENT_BINARY_DIR}/jt9-cli-validation-missing.wav")
if (EXISTS "${missing_wav}")
  message (FATAL_ERROR "CLI validation requires an absent WAV path: ${missing_wav}")
endif ()

foreach (program "${JT9}" "${WSJT_CODEC}")
  get_filename_component (program_name "${program}" NAME_WE)

  require_mode_result ("WAV decoding requires a mode")
  require_mode_result ("cannot read WAV file" --ft8)
  require_rejection (-Q 6)
  require_rejection (-Q invalid)
  require_rejection (-N 25)
  require_rejection (-p invalid)
  require_rejection (-m invalid)
  require_rejection (--tr-period invalid)

  foreach (option --patience -w)
    require_acceptance ("${option}" 0)
  endforeach ()

  foreach (option -W --fst4w -Y --fst4w-hash)
    require_acceptance ("${option}")
  endforeach ()
  execute_process (
    COMMAND "${program}" --help
    RESULT_VARIABLE result
    OUTPUT_VARIABLE help
    ERROR_VARIABLE stderr)
  if (NOT "${result}" STREQUAL "0" OR NOT stderr STREQUAL "" OR
      NOT help MATCHES "select a mode" OR
      NOT help MATCHES "-W[\r\n ]+--fst4w[\r\n]" OR
      NOT help MATCHES "-Y[\r\n ]+--fst4w-hash[\r\n]")
    message (FATAL_ERROR "${program_name} has incorrect FST4W option mappings: ${help}${stderr}")
  endif ()

  string (REPEAT K 501 oversized_call)
  foreach (option -c --my-call -x --his-call)
    require_acceptance ("${option}" PJ4/K1ABC)
    require_acceptance ("${option}" K1ABC/P)
    require_acceptance ("${option}" PJ4/K1ABC/P1)
    require_acceptance ("${option}" b)
    require_text_acceptance ("${option}" " W9XYZ ")
    require_text_acceptance ("${option}" "\tPJ4/K1ABC\t")
    require_text_acceptance ("${option}" " PJ4/K1ABC/P1 ")
    require_text_acceptance ("${option}" " b ")
    require_text_acceptance ("${option}" "")
    require_text_acceptance ("${option}" " \t ")
    require_rejection ("${option}" "K1 ABC")
    require_rejection ("${option}" "K1\tABC")
    require_rejection ("${option}" K1ABC/P123456)
    require_rejection ("${option}" "${oversized_call}")
  endforeach ()

  foreach (option -G --my-grid -g --his-grid)
    require_acceptance ("${option}" EN37aa)
    require_text_acceptance ("${option}" "\tEN37aa ")
    require_text_acceptance ("${option}" "")
    require_text_acceptance ("${option}" " \t ")
    require_rejection ("${option}" "EN 37")
    require_rejection ("${option}" "EN\t37")
    require_rejection ("${option}" EN37abc)
  endforeach ()

  foreach (option -b --sub-mode)
    foreach (submode A B C D E F G H)
      require_acceptance ("${option}" "${submode}")
    endforeach ()
    foreach (submode "" a h AA AB "A " 1 I Z @ /)
      require_rejection ("${option}" "${submode}")
    endforeach ()

    foreach (mode -6 --jt65)
      require_mode_result ("cannot read WAV file" "${mode}" "${option}" C)
      require_mode_result ("invalid value for -b: D.*A-C for JT65" "${mode}" "${option}" D)
      require_mode_result ("invalid value for -b: D.*A-C for JT65" "${option}" D "${mode}")
    endforeach ()
    require_mode_result ("cannot read WAV file" -4 "${option}" G)
    require_mode_result ("invalid value for -b: H.*A-G for JT4" -4 "${option}" H)
    require_mode_result ("invalid value for -b: H.*A-G for JT4" "${option}" H -4)
    require_mode_result ("cannot read WAV file" -9 "${option}" H)
    foreach (period 15 30 60 120 300)
      require_mode_result ("cannot read WAV file" --q65 -p "${period}" "${option}" F)
      require_mode_result ("invalid value for -b: G.*A-F for Q65" --q65 -p "${period}" "${option}" G)
      require_mode_result ("invalid value for -b: G.*A-F for Q65" "${option}" G -p "${period}" --q65)
    endforeach ()

    require_mode_result ("WAV decoding requires a mode" "${option}" H)
    require_mode_result ("cannot read WAV file" -6 -9 "${option}" H)
    require_mode_result ("invalid value for -b: D.*A-C for JT65" -9 -6 "${option}" D)
    require_mode_result ("invalid value for -b: D.*A-C for JT65" -8 -6 "${option}" D)
    require_mode_result ("cannot read WAV file" -6 -8 "${option}" H)
    require_mode_result ("cannot read WAV file" -8 "${option}" H)
    require_mode_result ("cannot read WAV file" -k "${option}" H)
  endforeach ()

  if (program_name STREQUAL "wsjt-codec")
    require_mode_result ("WAV decoding requires a mode" --stream)
    set (empty_stdin "${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary/jt9-cli-empty-stdin")
    file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary")
    file (WRITE "${empty_stdin}" "")
    execute_process (
      COMMAND "${program}" -b D --jt65
      INPUT_FILE "${empty_stdin}"
      RESULT_VARIABLE result
      ERROR_VARIABLE stderr
      TIMEOUT 10)
    if (NOT "${result}" STREQUAL "2" OR NOT stderr MATCHES "invalid value for -b: D.*A-C for JT65")
      message (FATAL_ERROR "wsjt-codec did not reject its invalid startup submode: ${stderr}")
    endif ()
  endif ()
endforeach ()
