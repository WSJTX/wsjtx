add_executable (test_crc test_crc.f90)
wsjt_link_common_fortran (test_crc)
add_test (NAME test_crc COMMAND test_crc)
