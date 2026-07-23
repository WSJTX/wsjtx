add_executable (test_echo_morse test_echo_morse.f90)
wsjt_link_common_fortran (test_echo_morse)

add_test (NAME test_echo_morse COMMAND test_echo_morse)
