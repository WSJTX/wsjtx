foreach (required_variable JT9 JT4SIM WORK_DIR)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

file (MAKE_DIRECTORY "${WORK_DIR}")
execute_process (
  COMMAND "${JT4SIM}" "G3WDG OK1KIR RRR" F 1 0 0 1 99
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE simulator_result
  OUTPUT_QUIET
  ERROR_VARIABLE simulator_stderr)
if (NOT "${simulator_result}" STREQUAL "0")
  message (FATAL_ERROR
    "jt4sim exited with ${simulator_result}\n${simulator_stderr}")
endif ()

set (full_wav "${WORK_DIR}/000000_0001.wav")
set (truncated_wav "${WORK_DIR}/000000_0001_truncated.wav")
# Keep enough samples to distinguish a complete JT4 read from premature termination.
set (truncate_script [=[
import struct
import sys

sample_count = 580000
file_size = 44 + 2 * sample_count
with open(sys.argv[1], "rb") as source_file:
    wav_data = bytearray(source_file.read(file_size))
if len(wav_data) != file_size:
    raise RuntimeError("generated JT4 WAV is too short")
struct.pack_into("<I", wav_data, 4, file_size - 8)
struct.pack_into("<I", wav_data, 40, 2 * sample_count)
with open(sys.argv[2], "wb") as truncated_file:
    truncated_file.write(wav_data)
]=])
execute_process (
  COMMAND python3 -c "${truncate_script}" "${full_wav}" "${truncated_wav}"
  RESULT_VARIABLE truncate_result
  OUTPUT_QUIET
  ERROR_VARIABLE truncate_stderr)
if (NOT "${truncate_result}" STREQUAL "0")
  message (FATAL_ERROR "Failed to create truncated JT4 WAV\n${truncate_stderr}")
endif ()

execute_process (
  COMMAND "${JT9}"
          -a "${WORK_DIR}"
          -t "${WORK_DIR}"
          --jt4 -b F -f 1000 -d 3
          "${truncated_wav}"
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE decoder_result
  OUTPUT_VARIABLE decoder_stdout
  ERROR_VARIABLE decoder_stderr)
if (NOT "${decoder_result}" STREQUAL "0")
  message (FATAL_ERROR
    "jt9 exited with ${decoder_result}\n"
    "stdout:\n${decoder_stdout}\n"
    "stderr:\n${decoder_stderr}")
endif ()
if (NOT "${decoder_stdout}" MATCHES "EOF on input file")
  message (FATAL_ERROR
    "jt9 did not read beyond the obsolete 570240-sample cutoff\n"
    "stdout:\n${decoder_stdout}\n"
    "stderr:\n${decoder_stderr}")
endif ()
