add_executable (test_q65_workspace
  test_q65_workspace.f90
  ${CMAKE_SOURCE_DIR}/lib/qra/q65/q65_workspace.f90)

add_test (NAME test_q65_workspace COMMAND test_q65_workspace)
