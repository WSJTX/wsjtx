add_library (test_decoder_engine_signal STATIC decoder_engine_signal.f90 jt9_engine_signal.f90)
set_target_properties (test_decoder_engine_signal PROPERTIES
  Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/signal-modules)
target_include_directories (test_decoder_engine_signal PRIVATE ${CMAKE_SOURCE_DIR}/lib)
wsjt_link_common_fortran (test_decoder_engine_signal)

add_executable (test_decoder_engine test_decoder_engine.c)
set_target_properties (test_decoder_engine PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories (test_decoder_engine PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decoder_engine PRIVATE test_decoder_engine_signal)
add_test (NAME test_decoder_engine COMMAND test_decoder_engine)
set_tests_properties (test_decoder_engine PROPERTIES TIMEOUT 90)
add_test (NAME prepare_ft8_engine_wav COMMAND test_decoder_engine
  --write-wav ${CMAKE_CURRENT_BINARY_DIR}/ft8-engine.wav)
set_tests_properties (prepare_ft8_engine_wav PROPERTIES FIXTURES_SETUP ft8_engine_wav TIMEOUT 15)

add_executable (test_decoder_engine_ft4 test_decoder_engine_ft4.c)
set_target_properties (test_decoder_engine_ft4 PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories (test_decoder_engine_ft4 PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decoder_engine_ft4 PRIVATE test_decoder_engine_signal)
add_test (NAME test_decoder_engine_ft4 COMMAND test_decoder_engine_ft4)
set_tests_properties (test_decoder_engine_ft4 PROPERTIES TIMEOUT 90)

find_package (Threads REQUIRED)
add_executable (test_decoder_engine_jt9 test_decoder_engine_jt9.c)
set_target_properties (test_decoder_engine_jt9 PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories (test_decoder_engine_jt9 PRIVATE ${CMAKE_SOURCE_DIR} ${FFTW3_INCLUDE_DIRS})
target_link_libraries (test_decoder_engine_jt9 PRIVATE test_decoder_engine_signal Threads::Threads)
add_test (NAME test_decoder_engine_jt9 COMMAND test_decoder_engine_jt9 --smoke)
set_tests_properties (test_decoder_engine_jt9 PROPERTIES TIMEOUT 90)
if (TARGET wsjt_fort)
  add_test (NAME test_decoder_engine_jt9_small_stack COMMAND test_decoder_engine_jt9 --small-stack)
  set_tests_properties (test_decoder_engine_jt9_small_stack PROPERTIES TIMEOUT 90)
else ()
  # The common target also uses wsjt_fort_omp; retain a one-thread smoke.
  set_tests_properties (test_decoder_engine_jt9 PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=1;OMP_THREAD_LIMIT=1;OMP_STACKSIZE=1M")
endif ()

add_library (test_decoder_engine_jt65_signal STATIC
  ../jt65/jt65_test_fixture.f90 jt65_engine_signal.f90)
set_target_properties (test_decoder_engine_jt65_signal PROPERTIES
  Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/jt65-signal-modules)
target_link_libraries (test_decoder_engine_jt65_signal PUBLIC jt65_test_vectors)
wsjt_link_common_fortran (test_decoder_engine_jt65_signal)

add_executable (test_decoder_engine_jt65 test_decoder_engine_jt65.c)
set_target_properties (test_decoder_engine_jt65 PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories (test_decoder_engine_jt65 PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decoder_engine_jt65 PRIVATE test_decoder_engine_jt65_signal Threads::Threads)
# Full behavioral coverage belongs to one constrained-stack test per archive.
add_test (NAME test_decoder_engine_jt65 COMMAND test_decoder_engine_jt65 --smoke)
set_tests_properties (test_decoder_engine_jt65 PROPERTIES TIMEOUT 360)
if (TARGET wsjt_fort)
  add_test (NAME test_decoder_engine_jt65_small_stack COMMAND test_decoder_engine_jt65 --small-stack)
  set_tests_properties (test_decoder_engine_jt65_small_stack PROPERTIES TIMEOUT 360)
else ()
  # The common target also uses wsjt_fort_omp; retain a one-thread smoke.
  set_tests_properties (test_decoder_engine_jt65 PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=1;OMP_THREAD_LIMIT=1;OMP_STACKSIZE=1M")
endif ()
add_test (NAME prepare_jt65_engine_wav COMMAND test_decoder_engine_jt65
  --write-wav ${CMAKE_CURRENT_BINARY_DIR}/jt65-engine.wav)
set_tests_properties (prepare_jt65_engine_wav PROPERTIES FIXTURES_SETUP jt65_engine_wav TIMEOUT 15)

add_executable (test_decoder_engine_jt65_host test_decoder_engine_jt65_host.f90)
target_link_libraries (test_decoder_engine_jt65_host PRIVATE test_decoder_engine_jt65_signal)
wsjt_link_common_fortran (test_decoder_engine_jt65_host)
file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/jt65-host)
add_test (NAME test_decoder_engine_jt65_host COMMAND test_decoder_engine_jt65_host)
set_tests_properties (test_decoder_engine_jt65_host PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/jt65-host TIMEOUT 60)

add_library (test_decoder_engine_q65_signal STATIC q65_engine_signal.f90)
wsjt_link_common_fortran (test_decoder_engine_q65_signal)
add_executable (test_decoder_engine_q65 test_decoder_engine_q65.c)
set_target_properties (test_decoder_engine_q65 PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories (test_decoder_engine_q65 PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decoder_engine_q65 PRIVATE test_decoder_engine_q65_signal Threads::Threads)
add_test (NAME test_decoder_engine_q65 COMMAND test_decoder_engine_q65 --smoke)
set_tests_properties (test_decoder_engine_q65 PROPERTIES TIMEOUT 180)
if (TARGET wsjt_fort)
  add_test (NAME test_decoder_engine_q65_small_stack COMMAND test_decoder_engine_q65 --small-stack)
  set_tests_properties (test_decoder_engine_q65_small_stack PROPERTIES TIMEOUT 180)
else ()
  set_tests_properties (test_decoder_engine_q65 PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=1;OMP_THREAD_LIMIT=1;OMP_STACKSIZE=1M")
endif ()
add_test (NAME prepare_q65_engine_wav COMMAND test_decoder_engine_q65
  --write-wav ${CMAKE_CURRENT_BINARY_DIR}/q65-engine.wav)
set_tests_properties (prepare_q65_engine_wav PROPERTIES FIXTURES_SETUP q65_engine_wav TIMEOUT 15)
add_test (NAME prepare_q65_engine_60s_wav COMMAND test_decoder_engine_q65
  --write-wav ${CMAKE_CURRENT_BINARY_DIR}/261003_001500.wav 60)
set_tests_properties (prepare_q65_engine_60s_wav PROPERTIES FIXTURES_SETUP q65_engine_60s_wav TIMEOUT 15)

add_executable (test_decoder_engine_q65_host test_decoder_engine_q65_host.f90)
set_target_properties (test_decoder_engine_q65_host PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries (test_decoder_engine_q65_host PRIVATE test_decoder_engine_q65_signal)
wsjt_link_common_fortran (test_decoder_engine_q65_host)
file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/q65-host)
add_test (NAME test_decoder_engine_q65_host COMMAND test_decoder_engine_q65_host)
set_tests_properties (test_decoder_engine_q65_host PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/q65-host TIMEOUT 360)
file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/q65-host-contest)
add_test (NAME test_decoder_engine_q65_host_contest COMMAND test_decoder_engine_q65_host --contest)
set_tests_properties (test_decoder_engine_q65_host_contest PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/q65-host-contest TIMEOUT 90)

if (TARGET wsjt_fort_omp)
  add_library (test_decoder_engine_signal_omp STATIC decoder_engine_signal.f90 jt9_engine_signal.f90)
  set_target_properties (test_decoder_engine_signal_omp PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/signal-omp-modules)
  target_include_directories (test_decoder_engine_signal_omp PRIVATE
    ${WSJT_FORTRAN_OPENMP_MODULE_DIR} ${CMAKE_SOURCE_DIR}/lib)
  target_link_libraries (test_decoder_engine_signal_omp PUBLIC
    wsjt_fort_omp wsjt_fortran_openmp)

  add_executable (test_decoder_engine_omp test_decoder_engine.c)
  set_target_properties (test_decoder_engine_omp PROPERTIES LINKER_LANGUAGE CXX)
  target_include_directories (test_decoder_engine_omp PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries (test_decoder_engine_omp PRIVATE test_decoder_engine_signal_omp)
  add_test (NAME test_decoder_engine_omp COMMAND test_decoder_engine_omp)
  set_tests_properties (test_decoder_engine_omp PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=2;OMP_THREAD_LIMIT=2"
    PROCESSORS 2
    TIMEOUT 90)

  add_executable (test_decoder_engine_ft4_omp test_decoder_engine_ft4.c)
  set_target_properties (test_decoder_engine_ft4_omp PROPERTIES LINKER_LANGUAGE CXX)
  target_include_directories (test_decoder_engine_ft4_omp PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries (test_decoder_engine_ft4_omp PRIVATE test_decoder_engine_signal_omp)
  add_test (NAME test_decoder_engine_ft4_omp COMMAND test_decoder_engine_ft4_omp)
  set_tests_properties (test_decoder_engine_ft4_omp PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=2;OMP_THREAD_LIMIT=2"
    PROCESSORS 2
    TIMEOUT 90)

  add_executable (test_decoder_engine_jt9_omp test_decoder_engine_jt9.c)
  set_target_properties (test_decoder_engine_jt9_omp PROPERTIES LINKER_LANGUAGE CXX)
  target_include_directories (test_decoder_engine_jt9_omp PRIVATE ${CMAKE_SOURCE_DIR} ${FFTW3_INCLUDE_DIRS})
  target_link_libraries (test_decoder_engine_jt9_omp PRIVATE test_decoder_engine_signal_omp Threads::Threads)
  add_test (NAME test_decoder_engine_jt9_omp COMMAND test_decoder_engine_jt9_omp --smoke)
  add_test (NAME test_decoder_engine_jt9_omp_small_stack COMMAND test_decoder_engine_jt9_omp --small-stack)
  set_tests_properties (test_decoder_engine_jt9_omp test_decoder_engine_jt9_omp_small_stack PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=2;OMP_THREAD_LIMIT=2;OMP_STACKSIZE=1M"
    PROCESSORS 2
    TIMEOUT 90)

  add_library (test_decoder_engine_jt65_signal_omp STATIC
    ../jt65/jt65_test_fixture.f90 jt65_engine_signal.f90)
  set_target_properties (test_decoder_engine_jt65_signal_omp PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/jt65-signal-omp-modules)
  target_link_libraries (test_decoder_engine_jt65_signal_omp PUBLIC
    jt65_test_vectors wsjt_fort_omp wsjt_fortran_openmp)

  add_executable (test_decoder_engine_jt65_omp test_decoder_engine_jt65.c)
  set_target_properties (test_decoder_engine_jt65_omp PROPERTIES LINKER_LANGUAGE CXX)
  target_include_directories (test_decoder_engine_jt65_omp PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries (test_decoder_engine_jt65_omp PRIVATE test_decoder_engine_jt65_signal_omp Threads::Threads)
  add_test (NAME test_decoder_engine_jt65_omp COMMAND test_decoder_engine_jt65_omp --smoke)
  add_test (NAME test_decoder_engine_jt65_omp_small_stack COMMAND test_decoder_engine_jt65_omp --small-stack)
  set_tests_properties (test_decoder_engine_jt65_omp test_decoder_engine_jt65_omp_small_stack PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=2;OMP_THREAD_LIMIT=2;OMP_STACKSIZE=1M"
    PROCESSORS 2
    TIMEOUT 360)

  add_library (test_decoder_engine_q65_signal_omp STATIC q65_engine_signal.f90)
  target_link_libraries (test_decoder_engine_q65_signal_omp PUBLIC wsjt_fort_omp wsjt_fortran_openmp)
  add_executable (test_decoder_engine_q65_omp test_decoder_engine_q65.c)
  set_target_properties (test_decoder_engine_q65_omp PROPERTIES LINKER_LANGUAGE CXX)
  target_include_directories (test_decoder_engine_q65_omp PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries (test_decoder_engine_q65_omp PRIVATE test_decoder_engine_q65_signal_omp Threads::Threads)
  add_test (NAME test_decoder_engine_q65_omp COMMAND test_decoder_engine_q65_omp --smoke)
  add_test (NAME test_decoder_engine_q65_omp_small_stack COMMAND test_decoder_engine_q65_omp --small-stack)
  set_tests_properties (test_decoder_engine_q65_omp test_decoder_engine_q65_omp_small_stack PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=2;OMP_THREAD_LIMIT=2;OMP_STACKSIZE=1M"
    PROCESSORS 2
    TIMEOUT 180)
endif ()

add_library (test_decoder_engine_fst4_signal STATIC fst4_engine_signal.f90)
set_target_properties (test_decoder_engine_fst4_signal PROPERTIES
  Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fst4-signal-modules)
wsjt_link_common_fortran (test_decoder_engine_fst4_signal)
add_executable (test_decoder_engine_fst4 test_decoder_engine_fst4.c)
set_target_properties (test_decoder_engine_fst4 PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories (test_decoder_engine_fst4 PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decoder_engine_fst4 PRIVATE test_decoder_engine_fst4_signal Threads::Threads)
add_test (NAME test_decoder_engine_fst4 COMMAND test_decoder_engine_fst4 --small-stack)
set_tests_properties (test_decoder_engine_fst4 PROPERTIES TIMEOUT 360
  ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=1;OMP_THREAD_LIMIT=1;OMP_STACKSIZE=1M")
option (WSJT_ENABLE_FST4_LONG_TESTS "Run FST4 engine long-period regressions." OFF)
if (WSJT_ENABLE_FST4_LONG_TESTS)
  add_test (NAME test_decoder_engine_fst4_long_periods COMMAND test_decoder_engine_fst4 --long-periods)
  set_tests_properties (test_decoder_engine_fst4_long_periods PROPERTIES
    LABELS "decoder;fst4;long-period" TIMEOUT 360
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=1;OMP_THREAD_LIMIT=1;OMP_STACKSIZE=1M")
endif ()
add_test (NAME prepare_fst4_engine_wav COMMAND test_decoder_engine_fst4
  --write-wav ${CMAKE_CURRENT_BINARY_DIR}/fst4-engine.wav 240 15)
set_tests_properties (prepare_fst4_engine_wav PROPERTIES FIXTURES_SETUP fst4_engine_wav TIMEOUT 30)
add_test (NAME prepare_fst4w_engine_wav COMMAND test_decoder_engine_fst4
  --write-wav ${CMAKE_CURRENT_BINARY_DIR}/fst4w-engine.wav 241 120)
set_tests_properties (prepare_fst4w_engine_wav PROPERTIES FIXTURES_SETUP fst4w_engine_wav TIMEOUT 30)
add_executable (test_decoder_engine_fst4_host test_decoder_engine_fst4_host.f90)
set_target_properties (test_decoder_engine_fst4_host PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries (test_decoder_engine_fst4_host PRIVATE test_decoder_engine_fst4_signal)
wsjt_link_common_fortran (test_decoder_engine_fst4_host)
file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fst4-host)
add_test (NAME test_decoder_engine_fst4_host COMMAND test_decoder_engine_fst4_host)
set_tests_properties (test_decoder_engine_fst4_host PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fst4-host TIMEOUT 360)
if (TARGET wsjt_fort_omp)
  add_library (test_decoder_engine_fst4_signal_omp STATIC fst4_engine_signal.f90)
  set_target_properties (test_decoder_engine_fst4_signal_omp PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fst4-signal-omp-modules)
  target_link_libraries (test_decoder_engine_fst4_signal_omp PUBLIC wsjt_fort_omp wsjt_fortran_openmp)
  add_executable (test_decoder_engine_fst4_omp test_decoder_engine_fst4.c)
  set_target_properties (test_decoder_engine_fst4_omp PROPERTIES LINKER_LANGUAGE CXX)
  target_include_directories (test_decoder_engine_fst4_omp PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries (test_decoder_engine_fst4_omp PRIVATE test_decoder_engine_fst4_signal_omp Threads::Threads)
  add_test (NAME test_decoder_engine_fst4_omp COMMAND test_decoder_engine_fst4_omp --small-stack)
  set_tests_properties (test_decoder_engine_fst4_omp PROPERTIES TIMEOUT 360 PROCESSORS 2
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=2;OMP_THREAD_LIMIT=2;OMP_STACKSIZE=1M")
endif ()

add_library (test_decoder_engine_jt4_signal STATIC jt4_engine_signal.f90)
wsjt_link_common_fortran (test_decoder_engine_jt4_signal)
add_executable (test_decoder_engine_jt4 test_decoder_engine_jt4.c)
set_target_properties (test_decoder_engine_jt4 PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories (test_decoder_engine_jt4 PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decoder_engine_jt4 PRIVATE test_decoder_engine_jt4_signal Threads::Threads)
add_test (NAME test_decoder_engine_jt4 COMMAND test_decoder_engine_jt4 --small-stack)
set_tests_properties (test_decoder_engine_jt4 PROPERTIES TIMEOUT 180
  ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=1;OMP_THREAD_LIMIT=1;OMP_STACKSIZE=1M")
add_executable (test_decoder_engine_jt4_state test_decoder_engine_jt4_state.f90)
set_target_properties (test_decoder_engine_jt4_state PROPERTIES LINKER_LANGUAGE CXX)
wsjt_link_common_fortran (test_decoder_engine_jt4_state)
add_test (NAME test_decoder_engine_jt4_state COMMAND test_decoder_engine_jt4_state)
set_tests_properties (test_decoder_engine_jt4_state PROPERTIES TIMEOUT 30)
add_test (NAME prepare_jt4_engine_wav COMMAND test_decoder_engine_jt4
  --write-wav ${CMAKE_CURRENT_BINARY_DIR}/jt4-engine.wav)
set_tests_properties (prepare_jt4_engine_wav PROPERTIES FIXTURES_SETUP jt4_engine_wav TIMEOUT 15)
if (TARGET wsjt_fort_omp AND TARGET wsjt_fort)
  add_library (test_decoder_engine_jt4_signal_omp STATIC jt4_engine_signal.f90)
  target_link_libraries (test_decoder_engine_jt4_signal_omp PUBLIC wsjt_fort_omp wsjt_fortran_openmp)
  add_executable (test_decoder_engine_jt4_omp test_decoder_engine_jt4.c)
  set_target_properties (test_decoder_engine_jt4_omp PROPERTIES LINKER_LANGUAGE CXX)
  target_include_directories (test_decoder_engine_jt4_omp PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries (test_decoder_engine_jt4_omp PRIVATE test_decoder_engine_jt4_signal_omp Threads::Threads)
  add_test (NAME test_decoder_engine_jt4_omp COMMAND test_decoder_engine_jt4_omp --smoke)
  set_tests_properties (test_decoder_engine_jt4_omp PROPERTIES TIMEOUT 90
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=1;OMP_THREAD_LIMIT=1;OMP_STACKSIZE=1M")
endif ()

add_test (NAME test_jt4_engine_cli COMMAND ${CMAKE_COMMAND}
  "-DJT9_EXECUTABLE=$<TARGET_FILE:jt9>"
  "-DJT4_WAV=${CMAKE_CURRENT_BINARY_DIR}/jt4-engine.wav"
  "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/jt4-cli"
  -P ${CMAKE_SOURCE_DIR}/tests/integration/jt9/verify_jt4_engine.cmake)
set_tests_properties (test_jt4_engine_cli PROPERTIES FIXTURES_REQUIRED jt4_engine_wav TIMEOUT 60)

add_executable (test_decoder_engine_jt4_host test_decoder_engine_jt4_host.f90)
set_target_properties (test_decoder_engine_jt4_host PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries (test_decoder_engine_jt4_host PRIVATE test_decoder_engine_jt4_signal)
wsjt_link_common_fortran (test_decoder_engine_jt4_host)
file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/jt4-host)
add_test (NAME test_decoder_engine_jt4_host COMMAND test_decoder_engine_jt4_host)
set_tests_properties (test_decoder_engine_jt4_host PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/jt4-host TIMEOUT 90)

add_test (NAME test_jt4_12000_read_extent COMMAND ${CMAKE_COMMAND}
  "-DJT9=$<TARGET_FILE:jt9>"
  "-DJT4_WAV=${CMAKE_CURRENT_BINARY_DIR}/jt4-engine.wav"
  "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/jt4-read-extent"
  -P ${CMAKE_SOURCE_DIR}/tests/integration/jt9/verify_jt4_12000_read_extent.cmake)
set_tests_properties (test_jt4_12000_read_extent PROPERTIES
  FIXTURES_REQUIRED jt4_engine_wav LABELS "decoder;jt4;integration" TIMEOUT 60)
