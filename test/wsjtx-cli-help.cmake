# Checks that wsjtx prints its command line help, version and errors to the
# terminal, with no timing log line after them.
# Usage: cmake -DWSJTX=<path to wsjtx> -P wsjtx-cli-help.cmake

function (run_wsjtx expected_exit output_var)
  execute_process (
    COMMAND "${WSJTX}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 20)
  if (expected_exit STREQUAL "0")
    if (NOT "${result}" STREQUAL "0")
      message (FATAL_ERROR "wsjtx ${ARGN} did not exit cleanly (${result})\nstdout: ${stdout}\nstderr: ${stderr}")
    endif ()
  elseif ("${result}" STREQUAL "0" OR NOT "${result}" MATCHES "^[0-9]+$")
    message (FATAL_ERROR "wsjtx ${ARGN} should have failed, got (${result})\nstdout: ${stdout}\nstderr: ${stderr}")
  endif ()
  if (stderr MATCHES "PERF ")
    message (FATAL_ERROR "wsjtx ${ARGN} logged a timing line: ${stderr}")
  endif ()
  set (${output_var} "${stdout}" PARENT_SCOPE)
  set (${output_var}_stderr "${stderr}" PARENT_SCOPE)
endfunction ()

run_wsjtx (0 help_text --help)
if (NOT help_text MATCHES "Usage:" OR NOT help_text MATCHES "--rig-name")
  message (FATAL_ERROR "--help did not print the option list to stdout: ${help_text}")
endif ()
if (help_text MATCHES "--platform")
  message (FATAL_ERROR "--help printed Qt options, which belong to --help-all")
endif ()

run_wsjtx (0 version_text --version)
if (NOT version_text MATCHES "^WSJT-X [0-9]+\\.[0-9]+")
  message (FATAL_ERROR "--version did not print the name and version to stdout: ${version_text}")
endif ()

run_wsjtx (0 help_all_text --help-all)
if (NOT help_all_text MATCHES "--rig-name" OR NOT help_all_text MATCHES "--platform")
  message (FATAL_ERROR "--help-all did not print the application and Qt options: ${help_all_text}")
endif ()

run_wsjtx (1 ignored --no-such-option)
if (NOT ignored_stderr MATCHES "no-such-option")
  message (FATAL_ERROR "an unknown option was not reported on stderr: ${ignored_stderr}")
endif ()
