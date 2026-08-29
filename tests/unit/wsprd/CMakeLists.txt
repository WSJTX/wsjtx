add_executable (test_wsprd_utils
  test_wsprd_utils.c
  ${CMAKE_SOURCE_DIR}/lib/lookup3.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/wsprd_utils.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/nhash.c)

add_test (NAME test_wsprd_utils COMMAND test_wsprd_utils)
