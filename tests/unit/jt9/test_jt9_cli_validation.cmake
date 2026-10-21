function (require_rejection option value)
  execute_process (
    COMMAND "${JT9}" "${option}" "${value}"
    RESULT_VARIABLE result
    ERROR_VARIABLE stderr)
  if (result EQUAL 0)
    message (FATAL_ERROR "${option}=${value} unexpectedly succeeded")
  endif ()
  if (NOT stderr MATCHES "jt9: invalid value")
    message (FATAL_ERROR "${option}=${value} did not report a validation error: ${stderr}")
  endif ()
endfunction ()

require_rejection (-Q 6)
require_rejection (-Q invalid)
require_rejection (-N 25)
require_rejection (-p invalid)
require_rejection (-m invalid)

foreach (option --patience -w)
  execute_process (
    COMMAND "${JT9}" "${option}" 0 --version
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if (NOT result EQUAL 0 OR NOT stdout MATCHES "^jt9 [0-9]" OR NOT stderr STREQUAL "")
    message (FATAL_ERROR "${option} did not consume exactly one argument: ${stdout}${stderr}")
  endif ()
endforeach ()

require_rejection (--tr-period invalid)
