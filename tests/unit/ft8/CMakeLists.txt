add_executable (test_ft8_mtd_decode_scheduler
  test_ft8_mtd_decode_scheduler.cpp
  ${CMAKE_SOURCE_DIR}/Ft8MtdDecodeScheduler.cpp
)
target_include_directories (test_ft8_mtd_decode_scheduler PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_ft8_mtd_decode_scheduler Qt5::Test)
add_test (NAME test_ft8_mtd_decode_scheduler COMMAND test_ft8_mtd_decode_scheduler)

add_executable (test_ft8_mtd_decode_coordinator
  test_ft8_mtd_decode_coordinator.cpp
  ${CMAKE_SOURCE_DIR}/Ft8MtdDecodeCoordinator.cpp
  ${CMAKE_SOURCE_DIR}/Ft8MtdDecodeScheduler.cpp
  ${CMAKE_SOURCE_DIR}/DecoderIpc.cpp
)
target_include_directories (test_ft8_mtd_decode_coordinator PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/lib
)
target_link_libraries (test_ft8_mtd_decode_coordinator wsjt_cxx Qt5::Test)
add_test (NAME test_ft8_mtd_decode_coordinator COMMAND test_ft8_mtd_decode_coordinator)

add_executable (test_ft8_decode_ranges
  test_ft8_decode_ranges.f90)
target_include_directories (test_ft8_decode_ranges PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_ft8_decode_ranges)

add_test (NAME test_ft8_decode_ranges COMMAND test_ft8_decode_ranges)

add_executable (test_ft8_mtd_residual
  test_ft8_mtd_residual.f90)
target_include_directories (test_ft8_mtd_residual PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_ft8_mtd_residual)

add_test (NAME test_ft8_mtd_residual COMMAND test_ft8_mtd_residual)

add_executable (test_ft8_symbol_spectrum
  test_ft8_symbol_spectrum.f90)
wsjt_link_common_fortran (test_ft8_symbol_spectrum)
add_test (NAME test_ft8_symbol_spectrum COMMAND test_ft8_symbol_spectrum)

add_executable (test_ft8_fft32
  test_ft8_fft32.f90)
wsjt_link_common_fortran (test_ft8_fft32)
add_test (NAME test_ft8_fft32 COMMAND test_ft8_fft32)

if (TARGET wsjt_fort_omp)
  add_executable (test_ft8_sync_fft_concurrent
    test_ft8_sync_fft_concurrent.f90)
  target_include_directories (test_ft8_sync_fft_concurrent PRIVATE
    ${WSJT_FORTRAN_OPENMP_MODULE_DIR})
  target_link_libraries (test_ft8_sync_fft_concurrent PRIVATE
    wsjt_fort_omp wsjt_fortran_openmp)

  add_test (NAME test_ft8_sync_fft_concurrent
    COMMAND test_ft8_sync_fft_concurrent)
  set_tests_properties (test_ft8_sync_fft_concurrent PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=4;OMP_THREAD_LIMIT=4"
    PROCESSORS 4
    TIMEOUT 30)

  add_executable (test_ft8_mtd_transaction_concurrent
    test_ft8_mtd_transaction_concurrent.f90)
  target_include_directories (test_ft8_mtd_transaction_concurrent PRIVATE
    ${WSJT_FORTRAN_OPENMP_MODULE_DIR})
  target_link_libraries (test_ft8_mtd_transaction_concurrent PRIVATE
    wsjt_fort_omp wsjt_fortran_openmp)

  add_test (NAME test_ft8_mtd_transaction_concurrent
    COMMAND test_ft8_mtd_transaction_concurrent)
  set_tests_properties (test_ft8_mtd_transaction_concurrent PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=4;OMP_THREAD_LIMIT=4"
    PROCESSORS 4
    TIMEOUT 30)

  add_executable (test_ft8_mtd_decode_state_concurrent
    test_ft8_mtd_decode_state_concurrent.f90)
  target_include_directories (test_ft8_mtd_decode_state_concurrent PRIVATE
    ${WSJT_FORTRAN_OPENMP_MODULE_DIR})
  target_link_libraries (test_ft8_mtd_decode_state_concurrent PRIVATE
    wsjt_fort_omp wsjt_fortran_openmp)

  add_test (NAME test_ft8_mtd_decode_state_concurrent
    COMMAND test_ft8_mtd_decode_state_concurrent)
  set_tests_properties (test_ft8_mtd_decode_state_concurrent PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=2;OMP_THREAD_LIMIT=2"
    PROCESSORS 2
    TIMEOUT 30)
  add_test (NAME test_ft8_mtd_result_warning
    COMMAND ${CMAKE_COMMAND}
      "-DTEST_EXECUTABLE=$<TARGET_FILE:test_ft8_mtd_decode_state_concurrent>"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/check_ft8_result_warning.cmake)
  set_tests_properties (test_ft8_mtd_result_warning PROPERTIES
    ENVIRONMENT "OMP_DYNAMIC=FALSE;OMP_NUM_THREADS=2;OMP_THREAD_LIMIT=2"
    PROCESSORS 1
    TIMEOUT 30)
  if (WSJT_ENABLE_TSAN)
    set_property (TEST test_ft8_mtd_decode_state_concurrent test_ft8_mtd_result_warning APPEND PROPERTY
      ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
  endif ()
endif ()

add_executable (test_ft8_a7_suffix_calls test_ft8_a7_suffix_calls.f90)
target_include_directories (test_ft8_a7_suffix_calls PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_ft8_a7_suffix_calls)
add_test (NAME test_ft8_a7_suffix_calls COMMAND test_ft8_a7_suffix_calls)
