add_executable (test_packjt_legacy72 test_packjt_legacy72.f90)
target_include_directories (test_packjt_legacy72 PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_packjt_legacy72)

add_test (NAME test_packjt_legacy72 COMMAND test_packjt_legacy72)
