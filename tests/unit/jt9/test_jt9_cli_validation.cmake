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

foreach (program "${JT9}" "${WSJT_CODEC}")
  get_filename_component (program_name "${program}" NAME_WE)

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
  endforeach ()
endforeach ()
