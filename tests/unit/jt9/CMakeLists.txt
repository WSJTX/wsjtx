add_executable (test_jt9_input_validation test_jt9_input_validation.f90)
wsjt_link_common_fortran (test_jt9_input_validation)
add_test (NAME test_jt9_input_validation COMMAND test_jt9_input_validation)

add_executable (test_jt9_params_init test_jt9_params_init.f90)
target_include_directories (test_jt9_params_init PRIVATE ${PROJECT_SOURCE_DIR}/lib)
wsjt_link_common_fortran (test_jt9_params_init)
add_test (NAME test_jt9_params_init COMMAND test_jt9_params_init)

add_executable (test_jt9_wav_request test_jt9_wav_request.f90)
target_include_directories (test_jt9_wav_request PRIVATE ${PROJECT_SOURCE_DIR}/lib)
wsjt_link_common_fortran (test_jt9_wav_request)
add_test (NAME test_jt9_wav_request COMMAND test_jt9_wav_request)

add_executable (test_readwav_validation test_readwav_validation.f90)
wsjt_link_common_fortran (test_readwav_validation)
add_test (NAME test_readwav_validation COMMAND test_readwav_validation)
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_readwav_validation PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()

add_executable (test_jt9_wav_policy test_jt9_wav_policy.f90)
wsjt_link_common_fortran (test_jt9_wav_policy)
add_test (NAME test_jt9_wav_policy COMMAND test_jt9_wav_policy)
