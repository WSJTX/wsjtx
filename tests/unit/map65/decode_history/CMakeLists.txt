# Compile the production control flow against deterministic DSP boundaries.
# Keep replacement module files out of the production module directory.
add_executable (test_map65_decode_history
  decode_history_stubs.f90
  test_map65_decode_history.f90
  ${CMAKE_SOURCE_DIR}/map65/libm65/map65a.f90
  ${CMAKE_SOURCE_DIR}/map65/libm65/getdphi.f90)
target_include_directories (test_map65_decode_history BEFORE PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${PROJECT_BINARY_DIR}/map65/libm65)
target_link_libraries (test_map65_decode_history PRIVATE m65impl)
target_compile_options (test_map65_decode_history PRIVATE
  $<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-fcheck=bounds>)
set_source_files_properties (decode_history_stubs.f90 PROPERTIES
  COMPILE_OPTIONS "$<$<COMPILE_LANG_AND_ID:Fortran,GNU>:-Wno-unused-dummy-argument>")
add_test (NAME test_map65_decode_history COMMAND test_map65_decode_history)
set_tests_properties (test_map65_decode_history PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} TIMEOUT 30)
