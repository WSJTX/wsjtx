# SPDX-License-Identifier: GPL-3.0-or-later
# Every example client in doc/codec/examples implements one command line, which check_client.py checks, comparing its decodes with the Python example's. The Python example needs only what the check needs and registers below; each other language registers in its own <language>.cmake here, calling wsjt_add_codec_example_test (<language> <command that starts its client>...) once its toolchain is found. The command runs from this build directory.
find_package (Python3 COMPONENTS Interpreter QUIET)
set (codec_example_sample "${CMAKE_SOURCE_DIR}/samples/FT8/210703_133430.wav")
if (NOT Python3_Interpreter_FOUND OR Python3_VERSION VERSION_LESS 3.8 OR NOT EXISTS "${codec_example_sample}")
  message (STATUS "Skipping the codec example tests: they need Python 3.8 or later and samples/FT8/210703_133430.wav")
  return ()
endif ()

function (wsjt_add_codec_example_test language)
  set (live)
  if (WSJT_ENABLE_STREAM_TIMING_TESTS)
    set (live --live)
  endif ()
  add_test (NAME test_codec_example_${language}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_client.py
      --codec $<TARGET_FILE:wsjt-codec>
      --sample ${codec_example_sample}
      --expected ${CMAKE_SOURCE_DIR}/test/fixtures/ft8_210703_133430_mtd.expected.txt
      --work-dir ${CMAKE_CURRENT_BINARY_DIR}/${language}
      ${live}
      -- ${ARGN}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties (test_codec_example_${language} PROPERTIES
    LABELS "codec-examples;integration" TIMEOUT 300)
  if (WSJT_ENABLE_STREAM_TIMING_TESTS)
    set_tests_properties (test_codec_example_${language} PROPERTIES RUN_SERIAL TRUE)
  endif ()
  if (WSJT_ENABLE_TSAN)
    set_tests_properties (test_codec_example_${language} PROPERTIES
      ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
  endif ()
endfunction ()

wsjt_add_codec_example_test (python ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/doc/codec/examples/python/codec_client.py)

option (WSJT_ENABLE_CODEC_EXAMPLE_LANGUAGE_TESTS "Also check the opt-in example clients when their toolchains are found" OFF)
file (GLOB codec_example_languages CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cmake")
foreach (codec_example_language IN LISTS codec_example_languages)
  include (${codec_example_language})
endforeach ()
