add_executable (test_wsprd_utils
  test_wsprd_utils.c
  ${CMAKE_SOURCE_DIR}/lib/lookup3.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/wsprd_utils.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/nhash.c)

add_test (NAME test_wsprd_utils COMMAND test_wsprd_utils)

# wsprd_decode.h's calls over the decoder, in one process. Three rows decode
# the in-tree sample, which source archives omit.
add_executable (test_wsprd_decode test_wsprd_decode.c
  $<TARGET_OBJECTS:wsprd_decode>
  $<TARGET_OBJECTS:wsjt_wspr>
  ${CMAKE_SOURCE_DIR}/lib/lookup3.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/nhash.c
  ${CMAKE_SOURCE_DIR}/lib/indexx.f90
  ${CMAKE_SOURCE_DIR}/lib/wsprd/osdwspr.f90
  ${CMAKE_SOURCE_DIR}/lib/wsprd/wspr_parallel.f90)
target_include_directories (test_wsprd_decode PRIVATE ${CMAKE_SOURCE_DIR} ${FFTW3_INCLUDE_DIRS})
target_link_libraries (test_wsprd_decode ${FFTW3_LIBRARIES} ${LIBM_LIBRARIES})
# With OpenMP where the build has it, so that the threads rows compare
# thread counts (wsjt-codec's build of the decoder).
if (TARGET wsjt_fortran_openmp)
  target_link_libraries (test_wsprd_decode wsjt_fortran_openmp)
endif ()
set (wsprd_decode_sample "${CMAKE_SOURCE_DIR}/samples/WSPR/150426_0918.wav")
set (wsprd_decode_scratch "${CMAKE_CURRENT_BINARY_DIR}/test_wsprd_decode_hash")
file (MAKE_DIRECTORY "${wsprd_decode_scratch}")
add_test (NAME test_wsprd_decode_repeat
  COMMAND test_wsprd_decode repeat "${wsprd_decode_sample}")
add_test (NAME test_wsprd_decode_hash_roundtrip
  COMMAND test_wsprd_decode hash_roundtrip "${wsprd_decode_sample}" "${wsprd_decode_scratch}")
add_test (NAME test_wsprd_decode_run_file
  COMMAND test_wsprd_decode run_file "${wsprd_decode_sample}" "${wsprd_decode_scratch}")
add_test (NAME test_wsprd_decode_short_name
  COMMAND test_wsprd_decode short_name "${wsprd_decode_sample}" "${wsprd_decode_scratch}")
add_test (NAME test_wsprd_decode_threads
  COMMAND test_wsprd_decode threads "${wsprd_decode_sample}")
set (wsprd_decode_tests
  test_wsprd_decode_repeat test_wsprd_decode_hash_roundtrip test_wsprd_decode_run_file
  test_wsprd_decode_short_name test_wsprd_decode_threads)
if (NOT EXISTS "${wsprd_decode_sample}")
  set_tests_properties (${wsprd_decode_tests} PROPERTIES DISABLED TRUE)
endif ()
add_test (NAME test_wsprd_decode_unresolved_type3 COMMAND test_wsprd_decode unresolved_type3)
add_test (NAME test_wsprd_decode_type2_grid
  COMMAND test_wsprd_decode type2_grid "${wsprd_decode_scratch}")
add_test (NAME test_wsprd_decode_unencodable_text COMMAND test_wsprd_decode unencodable_text)
add_test (NAME test_wsprd_decode_calibration_passes COMMAND test_wsprd_decode calibration_passes)
add_test (NAME test_wsprd_decode_osd_distance COMMAND test_wsprd_decode osd_distance)
add_test (NAME test_wsprd_decode_decoder_api COMMAND test_wsprd_decode decoder_api)
list (APPEND wsprd_decode_tests test_wsprd_decode_unresolved_type3 test_wsprd_decode_type2_grid
  test_wsprd_decode_unencodable_text test_wsprd_decode_calibration_passes
  test_wsprd_decode_osd_distance test_wsprd_decode_decoder_api)
set_tests_properties (${wsprd_decode_tests} PROPERTIES LABELS "decoder;wsprd" TIMEOUT 120)
# The thread rows decode 62 periods and run OSD on 8 threads: 30 s and 1 s on
# an arm64 Mac, 108 s and 7 s on a Windows labs runner. The OSD
# row runs with dynamic teams on and one thread by default, which the shim
# must override to run the eight it asks for.
add_test (NAME test_wsprd_decode_osd_threads COMMAND test_wsprd_decode osd_threads)
set (wsprd_decode_thread_timeout 600)
if (WSJT_ENABLE_ASAN_UBSAN)
  set (wsprd_decode_thread_timeout 1800)
endif ()
if (WSJT_ENABLE_TSAN)
  # The threads row's brief form (depth 2) runs there; the whole row took
  # over an hour under TSan on an arm64 host.
  set (wsprd_decode_thread_timeout 3600)
endif ()
set_tests_properties (test_wsprd_decode_osd_threads PROPERTIES LABELS "decoder;wsprd"
  ENVIRONMENT "OMP_DYNAMIC=true;OMP_NUM_THREADS=1")
foreach (row test_wsprd_decode_threads test_wsprd_decode_osd_threads)
  if (TEST ${row})
    set_tests_properties (${row} PROPERTIES TIMEOUT ${wsprd_decode_thread_timeout})
  endif ()
endforeach ()
# Under TSan the whole threads row takes over 40 minutes (arm64, GCC 12):
# a brief one there.
if (WSJT_ENABLE_TSAN AND TEST test_wsprd_decode_threads)
  set_tests_properties (test_wsprd_decode_threads PROPERTIES ENVIRONMENT "WSPRD_DECODE_THREADS_BRIEF=1")
endif ()

# The decoder without the memory a decode needs: it includes wspr_decoder.c,
# with its allocations counted and refused, in place of wsjt_wspr's object,
# and builds the rest of wsjt_wspr's sources (wspr_decoder_CSRCS in
# CMake/Sources.cmake) as they are.
if (NOT "lib/wsprd/wspr_decoder.c" IN_LIST wspr_decoder_CSRCS)
  message (FATAL_ERROR "test_wsprd_memory: wspr_decoder_CSRCS (CMake/Sources.cmake) does not list lib/wsprd/wspr_decoder.c")
endif ()
set (wsprd_memory_decoder_sources ${wspr_decoder_CSRCS})
list (REMOVE_ITEM wsprd_memory_decoder_sources lib/wsprd/wspr_decoder.c)
list (TRANSFORM wsprd_memory_decoder_sources PREPEND "${CMAKE_SOURCE_DIR}/")
add_executable (test_wsprd_memory test_wsprd_memory.c
  $<TARGET_OBJECTS:wsprd_decode>
  ${wsprd_memory_decoder_sources}
  ${CMAKE_SOURCE_DIR}/lib/lookup3.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/nhash.c
  ${CMAKE_SOURCE_DIR}/lib/indexx.f90
  ${CMAKE_SOURCE_DIR}/lib/wsprd/osdwspr.f90
  ${CMAKE_SOURCE_DIR}/lib/wsprd/wspr_parallel.f90)
target_include_directories (test_wsprd_memory PRIVATE ${CMAKE_SOURCE_DIR} ${FFTW3_INCLUDE_DIRS})
target_link_libraries (test_wsprd_memory ${FFTW3_LIBRARIES} ${LIBM_LIBRARIES})
if (TARGET wsjt_fortran_openmp)
  target_link_libraries (test_wsprd_memory wsjt_fortran_openmp)
endif ()
set (wsprd_memory_scratch "${CMAKE_CURRENT_BINARY_DIR}/test_wsprd_memory_scratch")
file (MAKE_DIRECTORY "${wsprd_memory_scratch}")
add_test (NAME test_wsprd_memory_osd
  COMMAND test_wsprd_memory osd "${wsprd_decode_sample}" "${wsprd_memory_scratch}")
add_test (NAME test_wsprd_memory_each_allocation
  COMMAND test_wsprd_memory each_allocation "${wsprd_decode_sample}")
add_test (NAME test_wsprd_memory_depth3
  COMMAND test_wsprd_memory depth3 "${wsprd_decode_sample}")
add_test (NAME test_wsprd_memory_wsprd_file
  COMMAND test_wsprd_memory wsprd_file "${wsprd_decode_sample}" "${wsprd_memory_scratch}")
set (wsprd_memory_tests
  test_wsprd_memory_osd test_wsprd_memory_each_allocation test_wsprd_memory_depth3
  test_wsprd_memory_wsprd_file)
# Two-thread decodes of the sample: a few seconds a row on an arm64 Mac.
set_tests_properties (${wsprd_memory_tests} PROPERTIES LABELS "decoder;wsprd"
  TIMEOUT ${wsprd_decode_thread_timeout})
if (NOT EXISTS "${wsprd_decode_sample}")
  set_tests_properties (${wsprd_memory_tests} PROPERTIES DISABLED TRUE)
endif ()

foreach (component noncoherent demodulation)
  add_executable (test_wspr_${component}
    test_wspr_${component}.c
    ${CMAKE_SOURCE_DIR}/lib/wsprd/wspr_noncoherent.c)
  target_include_directories (test_wspr_${component} PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries (test_wspr_${component} PRIVATE ${LIBM_LIBRARIES})
  set_target_properties (test_wspr_${component} PROPERTIES AUTOMOC OFF)
  add_test (NAME test_wspr_${component} COMMAND test_wspr_${component})
  set_tests_properties (test_wspr_${component} PROPERTIES LABELS "decoder;wspr" TIMEOUT 60)
endforeach ()
