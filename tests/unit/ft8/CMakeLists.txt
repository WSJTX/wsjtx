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

if (TARGET wsjt_fort_omp)
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
endif ()
