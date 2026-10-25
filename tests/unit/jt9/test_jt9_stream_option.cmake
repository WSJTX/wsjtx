# SPDX-License-Identifier: GPL-3.0-or-later
# jt9 refuses --stream and -0 before writing anything and names wsjt-codec.
# wsjt-codec runs its stream when given no file and no -s, the same with
# --stream or -0; it refuses -s and names itself in its diagnostics. jt9's help
# offers the shared-memory worker and not the stream; wsjt-codec's offers the
# stream and not the worker. Neither help shows --stream or -0.
file (REMOVE_RECURSE "${WORK_DIR}")
file (MAKE_DIRECTORY "${WORK_DIR}")
file (WRITE "${WORK_DIR}/empty" "")

# Runs program with args in a fresh, empty directory that is also its default
# data and temporary directory, with empty stdin.
function (run_fresh name program)
  set (directory "${WORK_DIR}/${name}")
  file (MAKE_DIRECTORY "${directory}")
  execute_process (
    COMMAND "${program}" ${ARGN}
    WORKING_DIRECTORY "${directory}"
    INPUT_FILE "${WORK_DIR}/empty"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  file (GLOB written "${directory}/*")
  set (result "${result}" PARENT_SCOPE)
  set (stdout "${stdout}" PARENT_SCOPE)
  set (stderr "${stderr}" PARENT_SCOPE)
  set (written "${written}" PARENT_SCOPE)
endfunction ()

foreach (option --stream -0)
  run_fresh ("jt9${option}" "${JT9}" ${option})
  if (NOT result EQUAL 2 OR NOT stdout STREQUAL "" OR NOT stderr MATCHES "Use `wsjt-codec`\\.")
    message (FATAL_ERROR "jt9 ${option} did not refuse and name wsjt-codec (exit ${result}): ${stdout}${stderr}")
  endif ()
  if (written)
    message (FATAL_ERROR "jt9 ${option} wrote before refusing: ${written}")
  endif ()
endforeach ()

run_fresh (wsjt-codec "${WSJT_CODEC}")
if (NOT result EQUAL 1 OR NOT stdout MATCHES "^{[^\n]*\"short header")
  message (FATAL_ERROR "wsjt-codec did not run the stream (exit ${result}): ${stdout}${stderr}")
endif ()
set (stream_result "${result}")
set (stream_stdout "${stdout}")
set (stream_stderr "${stderr}")
foreach (option --stream -0)
  run_fresh ("wsjt-codec${option}" "${WSJT_CODEC}" ${option})
  if (NOT result EQUAL stream_result OR NOT stdout STREQUAL stream_stdout OR NOT stderr STREQUAL stream_stderr)
    message (FATAL_ERROR "wsjt-codec ${option} did not run the stream as wsjt-codec does (exit ${result}): ${stdout}${stderr}")
  endif ()
endforeach ()

run_fresh (wsjt-codec-s "${WSJT_CODEC}" -s nokey)
if (NOT result EQUAL 2 OR NOT stderr MATCHES "Use `wsjt-codec` \\(stream on stdin\\) or `wsjt-codec <file.wav>`")
  message (FATAL_ERROR "wsjt-codec -s did not refuse and name its stream (exit ${result}): ${stdout}${stderr}")
endif ()

run_fresh (wsjt-codec-Q "${WSJT_CODEC}" -Q 6)
if (NOT result EQUAL 2 OR NOT stderr MATCHES "wsjt-codec: invalid value for -Q")
  message (FATAL_ERROR "wsjt-codec did not name itself in a diagnostic (exit ${result}): ${stdout}${stderr}")
endif ()

# Each help describes only the program that prints it.
function (check_help program)
  cmake_parse_arguments (PARSE_ARGV 1 help "" "" "OFFERS;OMITS")
  execute_process (
    COMMAND "${program}" --help
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if (NOT result EQUAL 0)
    message (FATAL_ERROR "${program} --help exited ${result}: ${stdout}${stderr}")
  endif ()
  foreach (pattern IN LISTS help_OFFERS)
    if (NOT stdout MATCHES "${pattern}")
      message (FATAL_ERROR "${program} --help does not show '${pattern}':\n${stdout}")
    endif ()
  endforeach ()
  foreach (pattern IN LISTS help_OMITS)
    if (stdout MATCHES "${pattern}")
      message (FATAL_ERROR "${program} --help must not show '${pattern}':\n${stdout}")
    endif ()
  endforeach ()
endfunction ()

check_help ("${JT9}"
  OFFERS "Usage: jt9 \\[OPTIONS\\]" "jt9 -s <key>" "\n -s NAME" "\n --shmem NAME" "\n --ipc-lock PATH"
         "\n -r PATH" "\n --share-path PATH"
  OMITS "--stream" "wsjt-codec")
check_help ("${WSJT_CODEC}"
  OFFERS "Usage: wsjt-codec \\[OPTIONS\\]" "cat <pcm-stream> \\| wsjt-codec \\[OPTIONS\\]"
         "\n -r PATH" "\n --share-path PATH"
  OMITS "--stream" "\n -0" "Usage: jt9 " "-s <key>" "\n -s NAME" "--shmem" "--ipc-lock")
