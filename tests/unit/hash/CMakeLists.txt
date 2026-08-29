add_executable (test_lookup3
  test_lookup3.c
  ${CMAKE_SOURCE_DIR}/lib/lookup3.c
  ${CMAKE_SOURCE_DIR}/lib/wsprd/nhash.c
  ${CMAKE_SOURCE_DIR}/lib/wsprcode/nhash.c
  ${CMAKE_SOURCE_DIR}/lib/superfox/qpc/nhash2.c)

add_test (NAME test_lookup3 COMMAND test_lookup3)
