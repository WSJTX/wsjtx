foreach (required_variable SJTTY WSJTX WORK_DIR EXPECTED)
  if (NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message (FATAL_ERROR "${required_variable} is required")
  endif ()
endforeach ()

file (REMOVE_RECURSE "${WORK_DIR}")
file (MAKE_DIRECTORY
  "${WORK_DIR}/config"
  "${WORK_DIR}/data"
  "${WORK_DIR}/cache"
  "${WORK_DIR}/tmp")

execute_process (
  COMMAND "${SJTTY}"
    "CQ TEST DE KA1ABC LIVE AUDIO SMOKE TEST"
    1500 0.25 AW 0 384 1 99
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE generator_result
  OUTPUT_VARIABLE generator_stdout
  ERROR_VARIABLE generator_stderr)
if (NOT generator_result STREQUAL "0")
  message (FATAL_ERROR
    "sjtty failed with exit code ${generator_result}\n"
    "stdout:\n${generator_stdout}\n"
    "stderr:\n${generator_stderr}")
endif ()

set (fixture "${WORK_DIR}/000000_000001.wav")
if (NOT EXISTS "${fixture}")
  message (FATAL_ERROR "sjtty did not create ${fixture}")
endif ()
file (SIZE "${fixture}" fixture_size)
if (fixture_size LESS 45)
  message (FATAL_ERROR "sjtty created a malformed ${fixture_size}-byte WAV")
endif ()

set (wsjtx_environment
  "XDG_CONFIG_HOME=${WORK_DIR}/config"
  "XDG_DATA_HOME=${WORK_DIR}/data"
  "XDG_CACHE_HOME=${WORK_DIR}/cache")
if (NOT APPLE)
  list (APPEND wsjtx_environment "TMPDIR=${WORK_DIR}/tmp")
endif ()

execute_process (
  COMMAND "${CMAKE_COMMAND}" -E env
    ${wsjtx_environment}
    "${WSJTX}"
    --jtty-live-audio-test "${fixture}"
    --jtty-live-audio-expected "${EXPECTED}"
    --rig-name CTEST-LIVE-AUDIO-JTTY
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE wsjtx_result
  OUTPUT_VARIABLE wsjtx_stdout
  ERROR_VARIABLE wsjtx_stderr)
if (NOT wsjtx_result STREQUAL "0")
  message (FATAL_ERROR
    "WSJT-X JTTY live-audio smoke test failed with exit code ${wsjtx_result}\n"
    "stdout:\n${wsjtx_stdout}\n"
    "stderr:\n${wsjtx_stderr}")
endif ()

message (STATUS "WSJT-X JTTY live-audio smoke test passed")
