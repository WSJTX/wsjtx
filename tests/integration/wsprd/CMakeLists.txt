# SPDX-License-Identifier: GPL-3.0-or-later
# wsjt-codec -2 against wsprd on the in-tree sample, generated periods, a .c2
# file written as WSJT-X writes it, and the command line.
add_executable (wspr_test_audio wspr_test_audio.f90)
wsjt_link_common_fortran (wspr_test_audio)

# The sample case's byte-exact check against a fixture runs only where the
# golden rows do (top-level CMakeLists.txt, test/fixtures/README.md).
set (codec_wspr_golden OFF)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set (codec_wspr_golden ON)
endif ()

set (codec_wspr_cases generated hash c2 cli)
# Source archives omit samples/.
if (EXISTS "${CMAKE_SOURCE_DIR}/samples/WSPR/150426_0918.wav")
  list (APPEND codec_wspr_cases sample)
endif ()
foreach (case IN LISTS codec_wspr_cases)
  add_test (NAME test_wsjt_codec_wspr_${case}
    COMMAND ${CMAKE_COMMAND}
      -DCASE=${case}
      -DWSJT_CODEC=$<TARGET_FILE:wsjt-codec>
      -DJT9=$<TARGET_FILE:jt9>
      -DWSPRD=$<TARGET_FILE:wsprd>
      -DAUDIO=$<TARGET_FILE:wspr_test_audio>
      -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
      -DGOLDEN=${codec_wspr_golden}
      -DWORK_DIR=${CMAKE_BINARY_DIR}/Testing/CodecWspr/${case}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test_codec_wspr.cmake)
  set_tests_properties (test_wsjt_codec_wspr_${case} PROPERTIES
    LABELS "decoder;wspr;integration" TIMEOUT 300)
  if (WSJT_ENABLE_TSAN)
    set_tests_properties (test_wsjt_codec_wspr_${case} PROPERTIES
      ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
  endif ()
endforeach ()
