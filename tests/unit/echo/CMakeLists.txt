add_executable (test_echo_morse test_echo_morse.f90)
wsjt_link_common_fortran (test_echo_morse)

add_test (NAME test_echo_morse COMMAND test_echo_morse)

add_executable (test_avecho_disk_metadata test_avecho_disk_metadata.f90)
wsjt_link_common_fortran (test_avecho_disk_metadata)

add_test (NAME test_avecho_disk_metadata COMMAND test_avecho_disk_metadata)
set_tests_properties (test_avecho_disk_metadata PROPERTIES
  PASS_REGULAR_EXPRESSION "Echo disk metadata test passed")

add_executable (test_avecho_silence test_avecho_silence.f90)
wsjt_link_common_fortran (test_avecho_silence)

add_test (NAME test_avecho_silence COMMAND test_avecho_silence)
