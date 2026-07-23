if (NOT DEFINED ECHOCALLSIM OR NOT DEFINED TESTECHOCALL OR NOT DEFINED TEST_DIR)
  message (FATAL_ERROR "EchoCall filename test is missing a required path")
endif ()

file (MAKE_DIRECTORY "${TEST_DIR}")
set (source_wav "${TEST_DIR}/000000_000000.wav")
file (REMOVE "${source_wav}")

execute_process (
  COMMAND "${TESTECHOCALL}"
  RESULT_VARIABLE no_argument_result
  OUTPUT_VARIABLE no_argument_output
  ERROR_VARIABLE no_argument_error
)
if (no_argument_result EQUAL 0)
  message (FATAL_ERROR "testEchoCall accepted a missing input filename")
endif ()
set (no_argument_output "${no_argument_output}${no_argument_error}")
if (NOT no_argument_output MATCHES "Usage: testEchoCall")
  message (FATAL_ERROR
    "testEchoCall missing-input output was unexpected:\n${no_argument_output}")
endif ()

execute_process (
  COMMAND "${ECHOCALLSIM}" K1JT 1500 0 0 10 1 99
  WORKING_DIRECTORY "${TEST_DIR}"
  RESULT_VARIABLE simulator_result
  OUTPUT_VARIABLE simulator_output
  ERROR_VARIABLE simulator_error
)
if (NOT simulator_result EQUAL 0)
  message (FATAL_ERROR
    "EchoCallSim failed (${simulator_result}):\n${simulator_output}${simulator_error}")
endif ()
if (NOT EXISTS "${source_wav}")
  message (FATAL_ERROR "EchoCallSim did not create ${source_wav}")
endif ()

function (expect_accepted filename expected_timestamp)
  configure_file ("${source_wav}" "${TEST_DIR}/${filename}" COPYONLY)
  execute_process (
    COMMAND "${TESTECHOCALL}" "${TEST_DIR}/${filename}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if (NOT result EQUAL 0)
    message (FATAL_ERROR
      "testEchoCall rejected ${filename} (${result}):\n${output}${error}")
  endif ()
  if (NOT output MATCHES "${expected_timestamp}.*K1JT")
    message (FATAL_ERROR
      "testEchoCall output was unexpected for ${filename}:\n${output}")
  endif ()
endfunction ()

function (expect_rejected filename)
  configure_file ("${source_wav}" "${TEST_DIR}/${filename}" COPYONLY)
  execute_process (
    COMMAND "${TESTECHOCALL}" "${TEST_DIR}/${filename}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if (result EQUAL 0)
    message (FATAL_ERROR "testEchoCall accepted invalid filename ${filename}")
  endif ()
  set (combined_output "${output}${error}")
  if (NOT combined_output MATCHES
      "filename must end with a valid HHMMSS\\.wav timestamp")
    message (FATAL_ERROR
      "testEchoCall rejection was unexpected for ${filename}:\n${combined_output}")
  endif ()
  if (combined_output MATCHES "Fortran runtime error")
    message (FATAL_ERROR
      "testEchoCall raised a runtime error for ${filename}:\n${combined_output}")
  endif ()
endfunction ()

function (expect_io_failure filename expected_message)
  execute_process (
    COMMAND "${TESTECHOCALL}" "${TEST_DIR}/${filename}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if (result EQUAL 0)
    message (FATAL_ERROR "testEchoCall accepted unreadable input ${filename}")
  endif ()
  set (combined_output "${output}${error}")
  if (NOT combined_output MATCHES "${expected_message}")
    message (FATAL_ERROR
      "testEchoCall I/O rejection was unexpected for ${filename}:\n${combined_output}")
  endif ()
  if (combined_output MATCHES "Fortran runtime error")
    message (FATAL_ERROR
      "testEchoCall raised a runtime error for ${filename}:\n${combined_output}")
  endif ()
endfunction ()

expect_accepted ("123456.wav" "123456")
expect_accepted ("235959.WAV" "235959")

set (long_path_component
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
file (MAKE_DIRECTORY "${TEST_DIR}/${long_path_component}")
expect_accepted ("${long_path_component}/123456.wav" "123456")

expect_rejected ("badname")
expect_rejected ("abc.wav")
expect_rejected ("abcdef.wav")
expect_rejected ("240000.wav")
expect_rejected ("126000.wav")
expect_rejected ("125960.wav")

set (missing_wav "${TEST_DIR}/010203.wav")
file (REMOVE "${missing_wav}")
expect_io_failure ("010203.wav" "testEchoCall: cannot open")
if (EXISTS "${missing_wav}")
  message (FATAL_ERROR "testEchoCall created missing input ${missing_wav}")
endif ()

file (WRITE "${TEST_DIR}/010204.wav" "truncated")
expect_io_failure ("010204.wav" "testEchoCall: cannot read")

set (invalid_wav_payload "not a RIFF/WAVE file")
foreach (iteration RANGE 1 12)
  set (invalid_wav_payload "${invalid_wav_payload}${invalid_wav_payload}")
endforeach ()
file (WRITE "${TEST_DIR}/010205.wav" "${invalid_wav_payload}")
expect_io_failure ("010205.wav" "not a RIFF/WAVE file")
