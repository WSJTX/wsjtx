add_executable (test_band_hopping
  test_band_hopping.cpp
  ${CMAKE_SOURCE_DIR}/widgets/BandHopping.cpp
)
target_link_libraries (test_band_hopping Qt5::Test)
add_test (NAME test_band_hopping COMMAND test_band_hopping)
