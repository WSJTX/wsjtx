add_executable (test_message_filter
  test_message_filter.cpp
  ${CMAKE_SOURCE_DIR}/MessageFilter.cpp
)
target_link_libraries (test_message_filter Qt5::Test)
wsjt_add_qt_test (test_message_filter test_message_filter)

add_executable (test_msk144_filter_rules
  test_msk144_filter_rules.cpp
  ${CMAKE_SOURCE_DIR}/MessageFilter.cpp
  ${CMAKE_SOURCE_DIR}/MessageFilterRules.cpp
)
target_link_libraries (test_msk144_filter_rules Qt5::Test)
wsjt_add_qt_test (test_msk144_filter_rules test_msk144_filter_rules)

add_executable (test_country_filters test_country_filters.cpp)
target_link_libraries (test_country_filters wsjt_qt wsjt_cxx wsjt_fort_common
  Boost::log_setup Qt5::Test)
wsjt_add_qt_test (test_country_filters test_country_filters)
set_tests_properties (test_country_filters PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 60)
