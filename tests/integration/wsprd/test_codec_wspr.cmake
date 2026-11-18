# SPDX-License-Identifier: GPL-3.0-or-later
# wsjt-codec -2 against wsprd, the oracle, run as WSJT-X runs it
# (widgets/mainwindow.cpp): each comparison runs both in fresh data
# directories and requires the same exit status, no stderr, and byte-identical
# stdout, ALL_WSPR.TXT, wspr_spots.txt and hashtable.txt.
#
# -DCASE=sample|generated|hash|c2|cli -DWSJT_CODEC= -DJT9= -DWSPRD=
# -DAUDIO=<wspr_test_audio> -DSOURCE_DIR= -DWORK_DIR= [-DGOLDEN=ON: the sample case
# also compares wsjt-codec's stdout at wsprd's defaults with
# test/fixtures/wspr_150426_0918.expected.txt]
foreach (required CASE WSJT_CODEC JT9 WSPRD AUDIO SOURCE_DIR WORK_DIR)
  if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message (FATAL_ERROR "${required} is required")
  endif ()
endforeach ()

file (REMOVE_RECURSE "${WORK_DIR}")
file (MAKE_DIRECTORY "${WORK_DIR}")
set (failures "")
set (side_files ALL_WSPR.TXT wspr_spots.txt hashtable.txt)
set (dial 14.0956)

# wsprd's options for each of the GUI's depths; 4 stands for the depths the
# GUI gives no options.
function (gui_depth_args depth out)
  if (depth STREQUAL "1")
    set (${out} -qB PARENT_SCOPE)
  elseif (depth STREQUAL "2")
    set (${out} -C 500 -o 4 PARENT_SCOPE)
  elseif (depth STREQUAL "3")
    set (${out} -C 500 -o 4 -d PARENT_SCOPE)
  else ()
    set (${out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (fail text)
  set (failures "${failures}${text}\n" PARENT_SCOPE)
endfunction ()

# Runs a command in <dir>, its stdout and stderr to <dir>.<step>.out and .err.
function (run_step dir step expected_status)
  execute_process (
    COMMAND ${ARGN}
    WORKING_DIRECTORY "${dir}"
    OUTPUT_FILE "${dir}.${step}.out"
    ERROR_FILE "${dir}.${step}.err"
    RESULT_VARIABLE status
    TIMEOUT 120)
  file (READ "${dir}.${step}.err" stderr)
  if (NOT "${status}" STREQUAL "${expected_status}")
    message (FATAL_ERROR "exit ${status}, expected ${expected_status}: ${ARGN}\n${stderr}")
  endif ()
  if (expected_status STREQUAL "0" AND NOT stderr STREQUAL "")
    message (FATAL_ERROR "stderr from ${ARGN}:\n${stderr}")
  endif ()
endfunction ()

function (fresh name)
  file (REMOVE_RECURSE "${WORK_DIR}/${name}")
  file (MAKE_DIRECTORY "${WORK_DIR}/${name}/codec" "${WORK_DIR}/${name}/wsprd")
endfunction ()

function (compare_step name step)
  set (base "${WORK_DIR}/${name}")
  # Byte for byte, as hex: file (READ) drops the CR before each LF, and
  # run_pair's file (APPEND) writes LF as CR LF on Windows. run_pair passes
  # wsprd's stdout as hex.
  file (READ "${base}/codec.${step}.out" codec_hex HEX)
  if (ARGC GREATER 2)
    set (wsprd_hex "${ARGV2}")
  else ()
    file (READ "${base}/wsprd.${step}.out" wsprd_hex HEX)
  endif ()
  if (NOT codec_hex STREQUAL wsprd_hex)
    file (READ "${base}/codec.${step}.out" codec_out)
    file (READ "${base}/wsprd.${step}.out" wsprd_out)
    fail ("${name} step ${step}: stdout differs (as bytes; this text drops the CR before each LF)\nwsjt-codec:\n${codec_out}wsprd:\n${wsprd_out}")
  endif ()
  foreach (side_file IN LISTS side_files)
    if (NOT EXISTS "${base}/codec/${side_file}" OR NOT EXISTS "${base}/wsprd/${side_file}")
      fail ("${name} step ${step}: ${side_file} missing")
      continue ()
    endif ()
    execute_process (COMMAND "${CMAKE_COMMAND}" -E compare_files
      "${base}/codec/${side_file}" "${base}/wsprd/${side_file}" RESULT_VARIABLE differ)
    if (differ)
      file (READ "${base}/codec/${side_file}" codec_out)
      file (READ "${base}/wsprd/${side_file}" wsprd_out)
      fail ("${name} step ${step}: ${side_file} differs\nwsjt-codec:\n${codec_out}wsprd:\n${wsprd_out}")
    endif ()
  endforeach ()
  set (failures "${failures}" PARENT_SCOPE)
endfunction ()

# One step on a pair's data directories: wsjt-codec once on every file,
# wsprd once per file, their stdout joined.
function (run_pair name step depth dial_mhz)
  set (base "${WORK_DIR}/${name}")
  gui_depth_args ("${depth}" wsprd_args)
  set (codec_args -2 -d ${depth})
  set (wsprd_dial "")
  if (NOT dial_mhz STREQUAL "")
    list (APPEND codec_args -O ${dial_mhz})
    set (wsprd_dial -f ${dial_mhz})
  endif ()
  run_step ("${base}/codec" "${step}" 0 "${WSJT_CODEC}" ${codec_args} -a "${base}/codec" ${ARGN})
  file (WRITE "${base}/wsprd.${step}.out" "")
  set (wsprd_hex "")
  set (index 0)
  foreach (input IN LISTS ARGN)
    math (EXPR index "${index} + 1")
    run_step ("${base}/wsprd" "${step}.${index}" 0
      "${WSPRD}" ${wsprd_args} -a "${base}/wsprd" ${wsprd_dial} "${input}")
    file (READ "${base}/wsprd.${step}.${index}.out" part)
    file (APPEND "${base}/wsprd.${step}.out" "${part}")
    file (READ "${base}/wsprd.${step}.${index}.out" part HEX)
    string (APPEND wsprd_hex "${part}")
  endforeach ()
  compare_step ("${name}" "${step}" "${wsprd_hex}")
  set (failures "${failures}" PARENT_SCOPE)
endfunction ()

function (pair name depth dial_mhz)
  fresh ("${name}")
  run_pair ("${name}" 1 "${depth}" "${dial_mhz}" ${ARGN})
  set (failures "${failures}" PARENT_SCOPE)
endfunction ()

# Fails unless wsjt-codec's stdout for the step holds the text.
function (expect name step text)
  file (READ "${WORK_DIR}/${name}/codec.${step}.out" out)
  string (FIND "${out}" "${text}" at)
  if (at LESS 0)
    fail ("${name} step ${step}: no \"${text}\" in\n${out}")
    set (failures "${failures}" PARENT_SCOPE)
  endif ()
endfunction ()

function (generate file seed)
  execute_process (COMMAND "${AUDIO}" wav "${WORK_DIR}/${file}" ${seed} ${ARGN}
    RESULT_VARIABLE status ERROR_VARIABLE stderr)
  if (status)
    message (FATAL_ERROR "wspr_test_audio failed for ${file}: ${stderr}")
  endif ()
endfunction ()

# Generated periods: one message each, then several over one another.
set (t1 "${WORK_DIR}/261116_0002.wav")
set (t2 "${WORK_DIR}/261116_0004.wav")
set (t3 "${WORK_DIR}/261116_0006.wav")
set (mix "${WORK_DIR}/261116_0008.wav")
macro (generate_t1)
  generate (261116_0002.wav 101 "K1ABC FN42 37" 1500 -15 1)
endmacro ()
macro (generate_t3)
  generate (261116_0006.wav 106 "<K1ABC> FN42AX 37" 1500 -15 1)
endmacro ()
macro (generate_mix)
  generate (261116_0008.wav 108
    "K1ABC FN42 37" 1420 -28 1     "PJ4/W9XYZ 23" 1450 -18 1.5
    "<K1ABC> FN42AX 37" 1480 -20 1 "W5XYZ EM10 33" 1510 -29 1.2
    "G4ABC/P 10" 1540 -24 2        "JA1ABC PM95 40" 1570 -30 1)
endmacro ()

if (CASE STREQUAL "sample")
  set (sample "${SOURCE_DIR}/samples/WSPR/150426_0918.wav")
  foreach (depth 1 2 3)
    pair (sample_d${depth} ${depth} ${dial} "${sample}")
    expect (sample_d${depth} 1 "ND6P DM04 30")
    # WSJT-X decodes a file it opens without a dial (-f).
    pair (sample_d${depth}_nodial ${depth} "" "${sample}")
  endforeach ()
  # Depths 0 and 4-7 give wsprd's defaults, which its golden test runs.
  pair (sample_defaults 4 "" "${sample}")
  # Byte-exact decoder output is pinned to the golden rows' platform.
  if (GOLDEN)
    execute_process (COMMAND "${CMAKE_COMMAND}" -E compare_files
      "${WORK_DIR}/sample_defaults/codec.1.out"
      "${SOURCE_DIR}/test/fixtures/wspr_150426_0918.expected.txt" RESULT_VARIABLE differ)
    if (differ)
      fail ("sample_defaults: stdout differs from test/fixtures/wspr_150426_0918.expected.txt")
    endif ()
  endif ()

elseif (CASE STREQUAL "generated")
  generate_t1 ()
  generate (261116_0004.wav 102 "PJ4/W9XYZ 23" 1500 -15 1)
  generate_mix ()
  # Weak periods that wsprd decodes only without -q (0012) or only with more
  # than -C 500's cycles (0014), on macOS arm64 at least.
  generate (261116_0012.wav 12 "K1ABC FN42 37" 1500 -29 1)
  generate (261116_0014.wav 16 "K1ABC FN42 37" 1500 -30 1)
  foreach (depth 1 2 3)
    pair (t1_d${depth} ${depth} ${dial} "${t1}")
    expect (t1_d${depth} 1 "K1ABC FN42 37")
    pair (t2_d${depth} ${depth} ${dial} "${t2}")
    expect (t2_d${depth} 1 "PJ4/W9XYZ 23")
    pair (mix_d${depth} ${depth} ${dial} "${mix}")
    expect (mix_d${depth} 1 "PJ4/W9XYZ 23")
    pair (weak_q_d${depth} ${depth} ${dial} "${WORK_DIR}/261116_0012.wav")
    pair (weak_c_d${depth} ${depth} ${dial} "${WORK_DIR}/261116_0014.wav")
  endforeach ()

elseif (CASE STREQUAL "hash")
  generate_t1 ()
  generate_t3 ()
  # Too weak for Fano; OSD takes it only for a call and grid in the table.
  set (weak "${WORK_DIR}/261116_0010.wav")
  generate (261116_0010.wav 13 "K1ABC FN42 37" 1500 -31 1)
  foreach (depth 1 2 3)
    # Runs on one data directory: the type 1 call is in hashtable.txt when
    # the type 3 message and the weak one come.
    fresh (seeded_d${depth})
    run_pair (seeded_d${depth} 1 ${depth} ${dial} "${t1}")
    run_pair (seeded_d${depth} 2 ${depth} ${dial} "${t3}")
    expect (seeded_d${depth} 2 "<K1ABC> FN42AX 37")
    run_pair (seeded_d${depth} 3 ${depth} ${dial} "${weak}")
    if (NOT depth EQUAL 1)
      # -o 4: an OSD decode, decodetype 1 in ALL_WSPR.TXT, is in the comparison.
      expect (seeded_d${depth} 3 "K1ABC FN42 37")
      file (STRINGS "${WORK_DIR}/seeded_d${depth}/codec/ALL_WSPR.TXT" all_wspr REGEX "^261116 0010 ")
      if (NOT all_wspr MATCHES " 1 +[0-9]+ +[0-9]+ +-?[0-9]+$")
        fail ("seeded_d${depth}: the weak period's decode is not OSD's: ${all_wspr}")
      endif ()
    endif ()
    # The same in one wsjt-codec run with two files.
    pair (two_files_d${depth} ${depth} ${dial} "${t1}" "${t3}")
    expect (two_files_d${depth} 1 "<K1ABC> FN42AX 37")
    # No table: the call stays hashed.
    pair (unseeded_d${depth} ${depth} ${dial} "${t3}")
    expect (unseeded_d${depth} 1 "<...> FN42AX 37")
  endforeach ()

elseif (CASE STREQUAL "c2")
  generate_mix ()
  set (c2 "${WORK_DIR}/261116_0008.c2")
  execute_process (COMMAND "${AUDIO}" c2 "${mix}" "${c2}" ${dial}
    RESULT_VARIABLE status ERROR_VARIABLE stderr)
  if (status)
    message (FATAL_ERROR "wspr_test_audio c2 failed: ${stderr}")
  endif ()
  foreach (depth 1 2 3)
    pair (c2_d${depth} ${depth} "" "${c2}")
    expect (c2_d${depth} 1 " 14.097")
    expect (c2_d${depth} 1 "PJ4/W9XYZ 23")
    # A .c2 file carries its own dial frequency, which -O and -f do not change.
    pair (c2_d${depth}_dial ${depth} 7.0386 "${c2}")
    expect (c2_d${depth}_dial 1 " 14.097")
  endforeach ()

elseif (CASE STREQUAL "cli")
  generate_t1 ()
  # jt9 refuses -2 before writing anything, as it refuses the stream.
  file (MAKE_DIRECTORY "${WORK_DIR}/jt9")
  run_step ("${WORK_DIR}/jt9" 1 2 "${JT9}" -2 -a "${WORK_DIR}/jt9" "${t1}")
  file (READ "${WORK_DIR}/jt9.1.out" out)
  file (READ "${WORK_DIR}/jt9.1.err" err)
  file (GLOB written "${WORK_DIR}/jt9/*")
  if (NOT out STREQUAL "" OR NOT err MATCHES "jt9: WSPR is not available in jt9\\. Use `wsjt-codec`\\." OR written)
    fail ("jt9 -2 did not refuse cleanly (written: ${written}):\n${out}${err}")
  endif ()
  execute_process (COMMAND "${JT9}" --help OUTPUT_VARIABLE help RESULT_VARIABLE status)
  if (status OR help MATCHES "--wspr|--dial")
    fail ("jt9 --help offers WSPR options:\n${help}")
  endif ()

  execute_process (COMMAND "${WSJT_CODEC}" --help OUTPUT_VARIABLE help RESULT_VARIABLE status)
  if (status OR NOT help MATCHES "-2[\r\n ]+--wspr[\r\n]" OR NOT help MATCHES "-O MHZ[\r\n ]+--dial MHZ[\r\n]")
    fail ("wsjt-codec --help lacks -2/--wspr or -O/--dial:\n${help}")
  endif ()

  # -2 with no file starts a session in WSPR (test_streaming_wspr_commandLineStartsWspr);
  # its period is 120 s there too.
  file (WRITE "${WORK_DIR}/empty" "")
  file (MAKE_DIRECTORY "${WORK_DIR}/nofile")
  execute_process (COMMAND "${WSJT_CODEC}" -2 -p 60 -a "${WORK_DIR}/nofile"
    WORKING_DIRECTORY "${WORK_DIR}/nofile" INPUT_FILE "${WORK_DIR}/empty"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 10)
  file (GLOB written "${WORK_DIR}/nofile/*")
  if (NOT status STREQUAL "2" OR NOT out STREQUAL "" OR NOT err MATCHES "wsjt-codec: invalid value for -p: " OR written)
    fail ("wsjt-codec -2 -p 60 with no file was not refused: exit ${status} (written: ${written}):\n${out}${err}")
  endif ()

  # The period is 120 s; -p may say so and nothing else.
  foreach (arguments "-p;60" "--tr-period;300" "-O;abc" "--dial;14.0956x")
    execute_process (COMMAND "${WSJT_CODEC}" -2 ${arguments} "${t1}"
      RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 10)
    list (GET arguments 0 option)
    if (option MATCHES "p")
      set (letter p)
    else ()
      set (letter O)
    endif ()
    if (NOT status STREQUAL "2" OR NOT out STREQUAL "" OR NOT err MATCHES "wsjt-codec: invalid value for -${letter}: ")
      fail ("wsjt-codec -2 ${arguments} was not refused: exit ${status}\n${out}${err}")
    endif ()
  endforeach ()

  # An -a that is missing or is a file gets wsprd's message and status 1, before
  # anything decodes or is written there.
  file (WRITE "${WORK_DIR}/not-a-directory" "")
  foreach (data "${WORK_DIR}/no-such-directory" "${WORK_DIR}/not-a-directory")
    execute_process (COMMAND "${WSJT_CODEC}" -2 -a "${data}" "${t1}"
      RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
    if (NOT status STREQUAL "1" OR NOT out STREQUAL "" OR NOT err MATCHES "Error: inaccessible data directory: '")
      fail ("wsjt-codec -2 -a ${data}: exit ${status}\n${out}${err}")
    endif ()
  endforeach ()
  if (EXISTS "${WORK_DIR}/no-such-directory")
    fail ("wsjt-codec -2 created its missing -a")
  endif ()
  fresh (period)
  run_step ("${WORK_DIR}/period/codec" 1 0 "${WSJT_CODEC}" -2 -p 120 -a "${WORK_DIR}/period/codec" "${t1}")
  run_step ("${WORK_DIR}/period/wsprd" 1 0 "${WSPRD}" -qB -a "${WORK_DIR}/period/wsprd" "${t1}")
  compare_step (period 1)

  # -q drops <DecodeFinished>, as it does in every mode, and nothing else.
  fresh (quiet)
  run_step ("${WORK_DIR}/quiet/codec" 1 0 "${WSJT_CODEC}" -2 -q --dial ${dial} -a "${WORK_DIR}/quiet/codec" "${t1}")
  run_step ("${WORK_DIR}/quiet/wsprd" 1 0 "${WSPRD}" -qB -a "${WORK_DIR}/quiet/wsprd" -f ${dial} "${t1}")
  file (READ "${WORK_DIR}/quiet/codec.1.out" out)
  file (READ "${WORK_DIR}/quiet/wsprd.1.out" expected)
  string (REPLACE "<DecodeFinished>\n" "" expected "${expected}")
  if (NOT out STREQUAL expected OR NOT out MATCHES "K1ABC FN42 37")
    fail ("wsjt-codec -2 -q:\n${out}expected:\n${expected}")
  endif ()

  # wsprd's suffix rule and messages, and its exit status.
  file (WRITE "${WORK_DIR}/261116_0010.txt" "")
  foreach (input "${WORK_DIR}/261116_0010.txt" "${WORK_DIR}/missing_261116_0012.wav")
    get_filename_component (input_name "${input}" NAME)
    fresh ("${input_name}")
    run_step ("${WORK_DIR}/${input_name}/codec" 1 1 "${WSJT_CODEC}" -2 -a "${WORK_DIR}/${input_name}/codec" "${input}")
    run_step ("${WORK_DIR}/${input_name}/wsprd" 1 1 "${WSPRD}" -qB -a "${WORK_DIR}/${input_name}/wsprd" "${input}")
    execute_process (COMMAND "${CMAKE_COMMAND}" -E compare_files
      "${WORK_DIR}/${input_name}/codec.1.out" "${WORK_DIR}/${input_name}/wsprd.1.out" RESULT_VARIABLE differ)
    file (READ "${WORK_DIR}/${input_name}/codec.1.err" codec_err)
    file (READ "${WORK_DIR}/${input_name}/wsprd.1.err" wsprd_err)
    string (FIND "${codec_err}" "${wsprd_err}" at)
    if (differ OR NOT at EQUAL 0)
      fail ("wsjt-codec -2 ${input_name} did not fail as wsprd does:\n${codec_err}\nwsprd:\n${wsprd_err}")
    endif ()
  endforeach ()

else ()
  message (FATAL_ERROR "unknown CASE ${CASE}")
endif ()

if (NOT failures STREQUAL "")
  message (FATAL_ERROR "${failures}")
endif ()
message (STATUS "${CASE}: wsjt-codec -2 matches wsprd")
