add_executable (test_jpl_setup test_jpl_setup.f90)
wsjt_link_common_fortran (test_jpl_setup)

add_test (NAME test_jpl_setup COMMAND test_jpl_setup)
