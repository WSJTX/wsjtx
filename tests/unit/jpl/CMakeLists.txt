add_executable (test_jpl_setup test_jpl_setup.f90)
wsjt_link_common_fortran (test_jpl_setup)

add_test (NAME test_jpl_setup COMMAND test_jpl_setup)

add_executable (test_emedop ../../../lib/emedop.f90)
wsjt_link_common_fortran (test_emedop)

add_test (NAME test_emedop_invalid_start
  COMMAND test_emedop emedop_invalid_start.txt)
set_tests_properties (test_emedop_invalid_start PROPERTIES
  PASS_REGULAR_EXPRESSION "Invalid date or time in input file"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_test (NAME test_emedop_cross_date
  COMMAND test_emedop emedop_cross_date.txt)
set_tests_properties (test_emedop_cross_date PROPERTIES
  PASS_REGULAR_EXPRESSION "Start and stop dates must be the same"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable (test_jpl_resilience test_jpl_resilience.f90)
wsjt_link_common_fortran (test_jpl_resilience)

set (jpl_ephemeris_file "${CMAKE_SOURCE_DIR}/contrib/Ephemeris/JPLEPH")
set (jpl_resilience_scenarios
  valid_reference
  unit_collision
  missing_fallback
  analytic_failure
  truncated_header
  truncated_coefficients
  absurd_coefficients
  invalid_header
  out_of_range
  nonfinite_epoch
  invalid_input
  optional_sections
  source_transition)

foreach (scenario IN LISTS jpl_resilience_scenarios)
  set (test_name "test_jpl_resilience_${scenario}")
  add_test (NAME ${test_name}
    COMMAND test_jpl_resilience ${scenario} ${jpl_ephemeris_file})
  set_tests_properties (${test_name} PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS ${scenario}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach ()
