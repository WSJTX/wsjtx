add_executable (test_bwf_file test_bwf_file.cpp)
target_link_libraries (test_bwf_file wsjt_qtmm Qt5::Test Qt5::Multimedia)
add_test (NAME test_bwf_file COMMAND test_bwf_file)
