add_executable (test_msk_spectrum_edges test_msk_spectrum_edges.f90)
wsjt_link_common_fortran (test_msk_spectrum_edges)
add_test (NAME test_msk_spectrum_edges COMMAND test_msk_spectrum_edges)

add_executable (test_hspec_committed_boundary test_hspec_committed_boundary.f90)
wsjt_link_common_fortran (test_hspec_committed_boundary)
add_test (NAME test_hspec_committed_boundary COMMAND test_hspec_committed_boundary)
set_tests_properties (test_hspec_committed_boundary PROPERTIES
  TIMEOUT 30 LABELS "receive-handoff")
if (WSJT_ENABLE_TSAN)
  set_tests_properties (test_hspec_committed_boundary PROPERTIES
    ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:exitcode=66")
endif ()
