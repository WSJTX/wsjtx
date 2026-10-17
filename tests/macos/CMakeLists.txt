qt5_add_resources (test_macos_l10n_RCC "${CMAKE_BINARY_DIR}/wsjtx.qrc")
add_executable (test_macos_l10n MACOSX_BUNDLE
  test_l10n_bundle.cpp
  ${CMAKE_SOURCE_DIR}/L10nLoader.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
  ${test_macos_l10n_RCC})
add_dependencies (test_macos_l10n translations)
target_include_directories (test_macos_l10n PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_macos_l10n Boost::log_setup Qt5::Widgets Qt5::Network)
set_target_properties (test_macos_l10n PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Darwin/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.wsjtx.tests.localization")

foreach (case automatic_chinese automatic_traditional english_override german_override)
  set (apple_locale zh_CN)
  set (apple_languages "(zh-Hans-CN,en-US)")
  set (language_override "")
  set (expected_language zh)
  if (case STREQUAL "automatic_traditional")
    set (apple_locale zh_TW)
    set (apple_languages "(zh-Hant-TW,en-US)")
    set (expected_language zh_TW)
  elseif (case STREQUAL "english_override")
    set (language_override en)
    set (expected_language en)
  elseif (case STREQUAL "german_override")
    set (language_override de)
    set (expected_language de)
  endif ()
  add_test (NAME test_macos_l10n_${case}
    COMMAND $<TARGET_FILE:test_macos_l10n>
      -AppleLanguages "${apple_languages}" -AppleLocale "${apple_locale}")
  set_tests_properties (test_macos_l10n_${case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WSJTX_TEST_LANGUAGE_OVERRIDE=${language_override};WSJTX_TEST_EXPECTED_LANGUAGE=${expected_language}"
    TIMEOUT 15)
endforeach ()
