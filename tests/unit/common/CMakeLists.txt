add_executable (test_qt_helpers
  test_qt_helpers.cpp
  ${CMAKE_SOURCE_DIR}/qt_helpers.cpp
)
target_link_libraries (test_qt_helpers Qt5::Test Qt5::Network Qt5::Widgets)
add_test (NAME test_qt_helpers COMMAND test_qt_helpers)

add_executable (test_fox_guard_bands
  test_fox_guard_bands.cpp
  ${CMAKE_SOURCE_DIR}/FoxGuardBands.cpp
)
target_link_libraries (test_fox_guard_bands Qt5::Test)
add_test (NAME test_fox_guard_bands COMMAND test_fox_guard_bands)
