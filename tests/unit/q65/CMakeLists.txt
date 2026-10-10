add_executable (test_q65_workspace test_q65_workspace.f90)
wsjt_link_common_fortran (test_q65_workspace)

add_test (NAME test_q65_workspace COMMAND test_q65_workspace)

add_executable (test_q65_source_encoding
  test_q65_source_encoding.f90)
wsjt_link_common_fortran (test_q65_source_encoding)

add_test (NAME test_q65_source_encoding COMMAND test_q65_source_encoding)

add_executable (test_q65_decode_pipeline
  q65_test_fixture.f90
  test_q65_decode_pipeline.f90)
set_source_files_properties (test_q65_decode_pipeline.f90 PROPERTIES
  COMPILE_FLAGS "-Wno-unused-dummy-argument")
wsjt_link_common_fortran (test_q65_decode_pipeline)

add_test (NAME test_q65_decode_pipeline COMMAND test_q65_decode_pipeline)
set_tests_properties (test_q65_decode_pipeline PROPERTIES
  COST 10
  # Allow for hosted ARM emulation without leaving the test unbounded.
  # The full-period spectra and drift cases added about a third to the runtime.
  TIMEOUT 360)
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_q65_decode_pipeline PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()

add_executable (test_q65_llh_contracts
  test_q65_llh_contracts.c)
target_include_directories (test_q65_llh_contracts PRIVATE
  ${CMAKE_SOURCE_DIR}/lib/qra/q65)
target_link_libraries (test_q65_llh_contracts PRIVATE wsjt_cxx)
add_test (NAME test_q65_llh_contracts COMMAND test_q65_llh_contracts)

add_executable (test_q65_callers test_q65_callers.f90)
wsjt_link_common_fortran (test_q65_callers)
add_test (NAME test_q65_callers COMMAND test_q65_callers)

add_executable (test_q65_caller_display test_q65_caller_display.f90)
wsjt_link_common_fortran (test_q65_caller_display)
add_test (NAME test_q65_caller_display COMMAND test_q65_caller_display)

add_executable (test_q65_set_list_suffix test_q65_set_list_suffix.f90)
wsjt_link_common_fortran (test_q65_set_list_suffix)
add_test (NAME test_q65_set_list_suffix COMMAND test_q65_set_list_suffix)
