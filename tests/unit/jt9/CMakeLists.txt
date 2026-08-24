add_executable (test_jt9_input_validation test_jt9_input_validation.f90)
wsjt_link_common_fortran (test_jt9_input_validation)
add_test (NAME test_jt9_input_validation COMMAND test_jt9_input_validation)

add_executable (test_readwav_validation test_readwav_validation.f90)
wsjt_link_common_fortran (test_readwav_validation)
add_test (NAME test_readwav_validation COMMAND test_readwav_validation)
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_readwav_validation PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
