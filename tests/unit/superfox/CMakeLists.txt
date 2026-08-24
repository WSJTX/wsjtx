add_executable (test_superfox_pack test_superfox_pack.f90)
target_include_directories (test_superfox_pack PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_superfox_pack)

add_test (NAME test_superfox_pack COMMAND test_superfox_pack)

add_executable (test_superfox_tx_handoff test_superfox_tx_handoff.f90)
target_include_directories (test_superfox_tx_handoff PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_superfox_tx_handoff)

add_test (NAME test_superfox_tx_handoff COMMAND test_superfox_tx_handoff)

add_executable (test_superfox_unpack test_superfox_unpack.f90)
target_include_directories (test_superfox_unpack PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_superfox_unpack)

add_test (NAME test_superfox_unpack COMMAND test_superfox_unpack)
set_tests_properties (test_superfox_unpack PROPERTIES
  PASS_REGULAR_EXPRESSION "CQ K1JT FN20;.*2S/IZFUZBRXJF")

add_executable (test_superfox_unpack_unsupported
  test_superfox_unpack_unsupported.f90)
target_include_directories (test_superfox_unpack_unsupported
  PRIVATE ${CMAKE_BINARY_DIR})
wsjt_link_common_fortran (test_superfox_unpack_unsupported)

add_test (NAME test_superfox_unpack_unsupported
  COMMAND test_superfox_unpack_unsupported)
set_tests_properties (test_superfox_unpack_unsupported PROPERTIES
  PASS_REGULAR_EXPRESSION "unsupported i3=1 ignored"
  FAIL_REGULAR_EXPRESSION "~")
