add_executable (test_msk_spectrum_edges test_msk_spectrum_edges.f90)
wsjt_link_common_fortran (test_msk_spectrum_edges)
add_test (NAME test_msk_spectrum_edges COMMAND test_msk_spectrum_edges)

add_executable (test_hspec_committed_boundary test_hspec_committed_boundary.f90)
set_source_files_properties (test_hspec_committed_boundary.f90 PROPERTIES
  COMPILE_FLAGS "-Wno-unused-dummy-argument")
wsjt_link_common_fortran (test_hspec_committed_boundary)
add_test (NAME test_hspec_committed_boundary COMMAND test_hspec_committed_boundary)
set_tests_properties (test_hspec_committed_boundary PROPERTIES
  TIMEOUT 30 LABELS "receive-handoff")
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_hspec_committed_boundary PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:exitcode=66")
endif ()

add_executable (test_msk144_stream_window test_msk144_stream_window.f90)
set_source_files_properties (test_msk144_stream_window.f90 PROPERTIES
  COMPILE_FLAGS "-Wno-unused-dummy-argument")
wsjt_link_common_fortran (test_msk144_stream_window)
add_test (NAME test_msk144_stream_window COMMAND test_msk144_stream_window)
set_tests_properties (test_msk144_stream_window PROPERTIES TIMEOUT 30 LABELS "streaming")
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_msk144_stream_window PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()

add_executable (test_msk144_training_file test_msk144_training_file.f90)
wsjt_link_common_fortran (test_msk144_training_file)
file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/msk144_training")
add_test (NAME test_msk144_training_file
  COMMAND test_msk144_training_file "${CMAKE_CURRENT_BINARY_DIR}/msk144_training")
set_tests_properties (test_msk144_training_file PROPERTIES TIMEOUT 30 LABELS "streaming")
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_msk144_training_file PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=${wsjt_tsan_fortran_options}")
endif ()
