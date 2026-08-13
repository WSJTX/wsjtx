foreach (required_variable WSJTX WORK_DIR EXPECTED DATA_DIR)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

file (REMOVE_RECURSE "${WORK_DIR}")
file (MAKE_DIRECTORY
  "${WORK_DIR}/capture/config"
  "${WORK_DIR}/capture/data"
  "${WORK_DIR}/capture/cache"
  "${WORK_DIR}/capture/tmp"
  "${WORK_DIR}/replay/config"
  "${WORK_DIR}/replay/data"
  "${WORK_DIR}/replay/cache"
  "${WORK_DIR}/replay/tmp")

set (capture "${WORK_DIR}/ft8-tx-loopback.wav")
execute_process (
  COMMAND "${CMAKE_COMMAND}" -E env
    "XDG_CONFIG_HOME=${WORK_DIR}/capture/config"
    "XDG_DATA_HOME=${WORK_DIR}/capture/data"
    "XDG_CACHE_HOME=${WORK_DIR}/capture/cache"
    "TMPDIR=${WORK_DIR}/capture/tmp"
    "${WSJTX}"
    --ft8-tx-loopback-test "${capture}"
    --rig-name CTEST-FT8-TX-CAPTURE
  WORKING_DIRECTORY "${WORK_DIR}/capture"
  RESULT_VARIABLE capture_result
  OUTPUT_VARIABLE capture_stdout
  ERROR_VARIABLE capture_stderr)
if (NOT capture_result STREQUAL "0")
  message (FATAL_ERROR
    "WSJT-X FT8 transmit capture failed with exit code ${capture_result}\n"
    "stdout:\n${capture_stdout}\n"
    "stderr:\n${capture_stderr}")
endif ()

if (NOT EXISTS "${capture}")
  message (FATAL_ERROR "WSJT-X did not create ${capture}")
endif ()
file (SIZE "${capture}" capture_size)
if (NOT capture_size EQUAL 1440044)
  message (FATAL_ERROR
    "WSJT-X FT8 capture must contain a 44-byte header and 720000 frames; size=${capture_size}")
endif ()
file (READ "${capture}" capture_header OFFSET 0 LIMIT 44 HEX)
if (NOT capture_header MATCHES
    "^5249464624f9150057415645666d7420100000000100010080bb000000770100020010006461746100f91500$")
  message (FATAL_ERROR
    "WSJT-X capture is not an exact 48 kHz mono signed 16-bit PCM RIFF/WAVE period: ${capture_header}")
endif ()

execute_process (
  COMMAND "${CMAKE_COMMAND}" -E env
    "XDG_CONFIG_HOME=${WORK_DIR}/replay/config"
    "XDG_DATA_HOME=${WORK_DIR}/replay/data"
    "XDG_CACHE_HOME=${WORK_DIR}/replay/cache"
    "TMPDIR=${WORK_DIR}/replay/tmp"
    "${WSJTX}"
    --live-audio-test "${capture}"
    --live-audio-expected "${EXPECTED}"
    --live-audio-data-dir "${DATA_DIR}"
    --rig-name CTEST-FT8-TX-REPLAY
  WORKING_DIRECTORY "${WORK_DIR}/replay"
  RESULT_VARIABLE replay_result
  OUTPUT_VARIABLE replay_stdout
  ERROR_VARIABLE replay_stderr)
if (NOT replay_result STREQUAL "0")
  message (FATAL_ERROR
    "WSJT-X FT8 transmit replay failed with exit code ${replay_result}\n"
    "stdout:\n${replay_stdout}\n"
    "stderr:\n${replay_stderr}")
endif ()

message (STATUS "WSJT-X FT8 transmit loopback smoke test passed")
