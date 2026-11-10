# SPDX-License-Identifier: GPL-3.0-or-later
foreach (required_variable JT9 WSJT_CODEC SOURCE_DIR WORK_DIR SAMPLE SECOND_SAMPLE)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

function (run_msk144 name program seed_decoded)
  set (directory "${WORK_DIR}/${name}")
  file (REMOVE_RECURSE "${directory}")
  file (MAKE_DIRECTORY "${directory}")
  set (decoded_file "${directory}/decoded.txt")
  if (seed_decoded)
    file (WRITE "${decoded_file}" "Existing decoder output must survive MSK144 WAV decoding.\n")
    file (SHA256 "${decoded_file}" decoded_hash)
  endif ()

  execute_process (
    COMMAND "${program}" -k -p 15 -f 1500 -F 50 -d 3 -w 0
            -a "${directory}" -t "${directory}" -r "${SOURCE_DIR}" ${ARGN}
    WORKING_DIRECTORY "${directory}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 90)
  file (WRITE "${directory}/actual.txt" "${stdout}")
  file (WRITE "${directory}/stderr.txt" "${stderr}")
  if (NOT "${result}" STREQUAL "0" OR NOT "${stderr}" STREQUAL "")
    message (FATAL_ERROR
      "${name} failed (exit ${result})\nstdout:\n${stdout}\nstderr:\n${stderr}")
  endif ()

  if (seed_decoded)
    if (NOT EXISTS "${decoded_file}")
      message (FATAL_ERROR "${name} removed the existing decoded.txt")
    endif ()
    file (SHA256 "${decoded_file}" actual_hash)
    if (NOT actual_hash STREQUAL decoded_hash)
      message (FATAL_ERROR "${name} changed the existing decoded.txt")
    endif ()
  elseif (EXISTS "${decoded_file}")
    message (FATAL_ERROR "${name} created decoded.txt")
  endif ()
  set (stdout "${stdout}" PARENT_SCOPE)
endfunction ()

function (check_completions name output expected_passes)
  string (REPLACE "\n" ";" lines "${output}")
  set (decodes 0)
  set (passes 0)
  set (decode_output "")
  foreach (line IN LISTS lines)
    if (line MATCHES "^<DecodeFinished>[ ]+0[ ]+([0-9]+)[ ]+0[ ]*$")
      set (reported_decodes "${CMAKE_MATCH_1}")
      if (NOT reported_decodes EQUAL decodes)
        message (FATAL_ERROR
          "${name} reported ${reported_decodes} decodes after emitting ${decodes}:\n${output}")
      endif ()
      if (passes EQUAL 0 AND NOT decodes EQUAL 2)
        message (FATAL_ERROR "${name} did not emit the sample's two decodes:\n${output}")
      endif ()
      math (EXPR passes "${passes} + 1")
      set (decodes 0)
    elseif (line MATCHES "^[0-9][0-9][0-9][0-9][0-9][0-9][ ]")
      math (EXPR decodes "${decodes} + 1")
      string (APPEND decode_output "${line}\n")
    elseif (NOT line STREQUAL "")
      message (FATAL_ERROR "${name} emitted an unexpected line: ${line}\n${output}")
    endif ()
  endforeach ()
  if (NOT passes EQUAL expected_passes OR NOT decodes EQUAL 0)
    message (FATAL_ERROR
      "${name} expected ${expected_passes} complete WAV passes, found ${passes} "
      "and ${decodes} unfinished decodes:\n${output}")
  endif ()
  set (decode_output "${decode_output}" PARENT_SCOPE)
endfunction ()

run_msk144 (normal "${JT9}" TRUE "${SAMPLE}")
check_completions (normal "${stdout}" 1)
set (normal_output "${stdout}")
set (normal_decodes "${decode_output}")

run_msk144 (quiet "${JT9}" FALSE -q "${SAMPLE}")
if (NOT stdout STREQUAL normal_decodes)
  message (FATAL_ERROR "Quiet mode changed MSK144 decodes or retained a completion:\n${stdout}")
endif ()

run_msk144 (codec "${WSJT_CODEC}" TRUE "${SAMPLE}")
if (NOT stdout STREQUAL normal_output)
  message (FATAL_ERROR
    "wsjt-codec MSK144 output differs from jt9\njt9:\n${normal_output}\nwsjt-codec:\n${stdout}")
endif ()

run_msk144 (multiple "${JT9}" FALSE "${SAMPLE}" "${SECOND_SAMPLE}")
check_completions (multiple "${stdout}" 2)
