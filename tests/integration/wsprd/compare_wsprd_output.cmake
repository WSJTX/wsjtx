# Runs wsprd on SAMPLE and compares its stdout with EXPECTED.
#
# -DWSPRD= -DSAMPLE= -DEXPECTED= -DWORK_DIR=, and optionally:
#   -DOPTIONS=<wsprd options before -a, ;-separated>  -DDIAL=<MHz, given as -f after -a>
#   -DSIDE_FILES=ON: wsprd runs in a fresh data directory, WORK_DIR/data, and its
#     ALL_WSPR.TXT, wspr_spots.txt and hashtable.txt must match <name>.ALL_WSPR.expected.txt,
#     <name>.wspr_spots.expected.txt and <name>.hashtable.expected.txt beside EXPECTED,
#     <name>.expected.txt.
#   -DAUDIO=<wspr_test_audio> -DAUDIO_ARGS=<seed;message;Hz;snr;start...> -DAUDIO_SHA256=<hex>:
#     wspr_test_audio writes SAMPLE first, and the test fails before decoding unless its
#     SHA-256 is the one the fixtures were recorded from.
foreach (required_variable WSPRD SAMPLE EXPECTED WORK_DIR)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
  # By hand, relative paths are from the current directory; wsprd runs in the data directory.
  get_filename_component (${required_variable} "${${required_variable}}" ABSOLUTE)
endforeach ()
if (DEFINED AUDIO AND NOT "${AUDIO}" STREQUAL "")
  get_filename_component (AUDIO "${AUDIO}" ABSOLUTE)
endif ()

if (SIDE_FILES)
  set (data_dir "${WORK_DIR}/data")
  file (REMOVE_RECURSE "${data_dir}")
  file (MAKE_DIRECTORY "${WORK_DIR}" "${data_dir}")
else ()
  # Reused between runs: start each with an empty hash table.
  set (data_dir "${WORK_DIR}")
  file (MAKE_DIRECTORY "${WORK_DIR}")
  file (WRITE "${data_dir}/hashtable.txt" "")
endif ()
set (actual_file "${WORK_DIR}/actual.txt")
set (stderr_file "${WORK_DIR}/stderr.txt")

if (DEFINED AUDIO AND NOT "${AUDIO}" STREQUAL "")
  execute_process (
    COMMAND "${AUDIO}" wav "${SAMPLE}" ${AUDIO_ARGS}
    RESULT_VARIABLE audio_result
    ERROR_VARIABLE audio_stderr)
  if (audio_result)
    message (FATAL_ERROR "wspr_test_audio failed for ${SAMPLE}: ${audio_stderr}")
  endif ()
  file (SHA256 "${SAMPLE}" audio_sha256)
  if (NOT audio_sha256 STREQUAL "${AUDIO_SHA256}")
    message (FATAL_ERROR
      "generated audio differs from the audio the fixtures were recorded from\n"
      "${SAMPLE}: SHA-256 ${audio_sha256}, recorded ${AUDIO_SHA256}")
  endif ()
endif ()

set (dial_args "")
if (DEFINED DIAL AND NOT "${DIAL}" STREQUAL "")
  set (dial_args -f "${DIAL}")
endif ()

execute_process (
  COMMAND "${WSPRD}"
          ${OPTIONS}
          -a "${data_dir}"
          ${dial_args}
          "${SAMPLE}"
  WORKING_DIRECTORY "${data_dir}"
  RESULT_VARIABLE decoder_result
  OUTPUT_VARIABLE decoder_stdout
  ERROR_VARIABLE decoder_stderr)

file (WRITE "${actual_file}" "${decoder_stdout}")
file (WRITE "${stderr_file}" "${decoder_stderr}")

if (NOT "${decoder_result}" STREQUAL "0")
  message (FATAL_ERROR
    "wsprd exited with ${decoder_result}\n"
    "stdout:\n${decoder_stdout}\n"
    "stderr:\n${decoder_stderr}")
endif ()

if (NOT "${decoder_stderr}" STREQUAL "")
  message (FATAL_ERROR "wsprd wrote to stderr:\n${decoder_stderr}")
endif ()

set (failures "")
file (READ "${EXPECTED}" expected_output)
if (NOT "${decoder_stdout}" STREQUAL "${expected_output}")
  string (APPEND failures
    "wsprd output differs from ${EXPECTED}\n"
    "expected:\n${expected_output}\n"
    "actual:\n${decoder_stdout}\n"
    "preserved output: ${actual_file}\n")
endif ()

if (SIDE_FILES)
  string (REGEX REPLACE "\\.expected\\.txt$" "" fixture_name "${EXPECTED}")
  foreach (side_file ALL_WSPR.TXT wspr_spots.txt hashtable.txt)
    get_filename_component (side_name "${side_file}" NAME_WE)
    set (side_expected "${fixture_name}.${side_name}.expected.txt")
    if (NOT EXISTS "${data_dir}/${side_file}")
      string (APPEND failures "wsprd wrote no ${side_file}\n")
      continue ()
    endif ()
    execute_process (COMMAND "${CMAKE_COMMAND}" -E compare_files
      "${side_expected}" "${data_dir}/${side_file}" RESULT_VARIABLE differ)
    if (differ)
      file (READ "${side_expected}" expected_side)
      file (READ "${data_dir}/${side_file}" actual_side)
      string (APPEND failures
        "${side_file} differs from ${side_expected}\n"
        "expected:\n${expected_side}\n"
        "actual:\n${actual_side}\n"
        "preserved output: ${data_dir}/${side_file}\n")
    endif ()
  endforeach ()
endif ()

if (NOT failures STREQUAL "")
  message (FATAL_ERROR "${failures}")
endif ()
