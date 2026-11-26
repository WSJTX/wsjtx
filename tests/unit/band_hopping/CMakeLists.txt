add_executable (test_band_hopping
  test_band_hopping.cpp
  ${CMAKE_SOURCE_DIR}/widgets/BandHopping.cpp
)
target_link_libraries (test_band_hopping Qt5::Test)
wsjt_add_qt_test (test_band_hopping test_band_hopping)
