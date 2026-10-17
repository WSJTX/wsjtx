// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QLocale>
#include <QTextStream>
#include <QTranslator>

#include "L10nLoader.hpp"
#include "Logger.hpp"

int main (int argc, char * argv[])
{
  QApplication application {argc, argv};
  Logger::disable ();
  QLocale const locale;
  auto const language_override = qEnvironmentVariable ("WSJTX_TEST_LANGUAGE_OVERRIDE");
  auto const expected_language = qEnvironmentVariable ("WSJTX_TEST_EXPECTED_LANGUAGE");
  if (expected_language.isEmpty ()) return 2;

  L10nLoader loader {&application, locale, language_override};
  QString expected {"User Defined"};
  if (expected_language != "en")
    {
      QTranslator catalog;
      if (!catalog.load ("wsjtx_" + expected_language, ":/Translations")) return 2;
      expected = catalog.translate ("WideGraph", "User Defined");
      if (expected.isEmpty () || expected == "User Defined") return 2;
    }
  auto const actual = QCoreApplication::translate ("WideGraph", "User Defined");
  QTextStream output {stdout};
  output << "Qt: " << qVersion () << " locale: " << locale.name ()
         << " UI languages: " << locale.uiLanguages ().join (", ")
         << " override: " << language_override << '\n'
         << "Expected: " << expected << " actual: " << actual << '\n';
  return actual == expected ? 0 : 1;
}
