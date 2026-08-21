foreach (required_variable WSJTX WORK_DIR EXPECTED)
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

set (capture "${WORK_DIR}/jtty-tx-loopback.wav")
set (capture_environment
  "XDG_CONFIG_HOME=${WORK_DIR}/capture/config"
  "XDG_DATA_HOME=${WORK_DIR}/capture/data"
  "XDG_CACHE_HOME=${WORK_DIR}/capture/cache")
set (replay_environment
  "XDG_CONFIG_HOME=${WORK_DIR}/replay/config"
  "XDG_DATA_HOME=${WORK_DIR}/replay/data"
  "XDG_CACHE_HOME=${WORK_DIR}/replay/cache")
if (NOT APPLE)
  list (APPEND capture_environment "TMPDIR=${WORK_DIR}/capture/tmp")
  list (APPEND replay_environment "TMPDIR=${WORK_DIR}/replay/tmp")
endif ()

execute_process (
  COMMAND "${CMAKE_COMMAND}" -E env
    ${capture_environment}
    "${WSJTX}"
    --jtty-tx-loopback-test "${capture}"
    --rig-name CTEST-JTTY-TX-CAPTURE
  WORKING_DIRECTORY "${WORK_DIR}/capture"
  RESULT_VARIABLE capture_result
  OUTPUT_VARIABLE capture_stdout
  ERROR_VARIABLE capture_stderr)
if (NOT capture_result STREQUAL "0")
  message (FATAL_ERROR
    "WSJT-X JTTY transmit capture failed with exit code ${capture_result}\n"
    "stdout:\n${capture_stdout}\n"
    "stderr:\n${capture_stderr}")
endif ()

if (NOT EXISTS "${capture}")
  message (FATAL_ERROR "WSJT-X did not create ${capture}")
endif ()
file (SIZE "${capture}" capture_size)
if (capture_size LESS 45)
  message (FATAL_ERROR "WSJT-X created a malformed ${capture_size}-byte WAV")
endif ()
file (READ "${capture}" capture_header OFFSET 0 LIMIT 28 HEX)
if (NOT capture_header MATCHES
    "^52494646........57415645666d7420100000000100010080bb0000$")
  message (FATAL_ERROR
    "WSJT-X capture is not a 48 kHz mono PCM RIFF/WAVE file: ${capture_header}")
endif ()

execute_process (
  COMMAND "${CMAKE_COMMAND}" -E env
    ${replay_environment}
    "${WSJTX}"
    --jtty-live-audio-test "${capture}"
    --jtty-live-audio-expected "${EXPECTED}"
    --rig-name CTEST-JTTY-TX-REPLAY
  WORKING_DIRECTORY "${WORK_DIR}/replay"
  RESULT_VARIABLE replay_result
  OUTPUT_VARIABLE replay_stdout
  ERROR_VARIABLE replay_stderr)
if (NOT replay_result STREQUAL "0")
  message (FATAL_ERROR
    "WSJT-X JTTY transmit replay failed with exit code ${replay_result}\n"
    "stdout:\n${replay_stdout}\n"
    "stderr:\n${replay_stderr}")
endif ()

message (STATUS "WSJT-X JTTY transmit loopback smoke test passed")
