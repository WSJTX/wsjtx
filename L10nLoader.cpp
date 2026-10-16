#include "L10nLoader.hpp"

#include <vector>
#include <memory>

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QRegularExpression>
#include <QDir>

#include "qt_helpers.hpp"
#include "Logger.hpp"

#include "pimpl_impl.hpp"

class L10nLoader::impl final
{
public:
  explicit impl(QApplication * app)
    : app_ {app}
  {
  }

  bool load_translator (QString const& filename
                        , QString const& directory = QString {}
                        , QString const& search_delimiters = QString {}
                        , QString const& suffix = QString {})
  {
    std::unique_ptr<QTranslator> translator {new QTranslator};
    if (translator->load (filename, directory, search_delimiters, suffix))
      {
        install (std::move (translator));
        return true;
      }
    return false;
  }

  bool load_translator (QLocale const& locale, QString const& filename
                        , QString const& prefix = QString {}
                        , QString const& directory = QString {}
                        , QString const& suffix = QString {})
  {
    std::unique_ptr<QTranslator> translator {new QTranslator};
    if (translator->load (locale, filename, prefix, directory, suffix))
      {
        install (std::move (translator));
        return true;
      }
    return false;
  }

  void install (std::unique_ptr<QTranslator> translator)
  {
    app_->installTranslator (translator.get ());
    translators_.push_back (std::move (translator));
  }
  
  QApplication * app_;
  std::vector<std::unique_ptr<QTranslator>> translators_;
};

L10nLoader::L10nLoader (QApplication * app, QLocale const& locale, QString const& language_override)
  : m_ {app}
{
  LOG_INFO (QString {"locale: language: %1 script: %2 country: %3 ui-languages: %4"}
     .arg (QLocale::languageToString (locale.language ()))
     .arg (QLocale::scriptToString (locale.script ()))
     .arg (QLocale::countryToString (locale.country ()))
     .arg (locale.uiLanguages ().join (", ")));

  // Explicit source English must also bypass local catalogs used by translators.
  if (language_override.contains (QRegularExpression {"^(?:en|en[-_]US)$"}))
    {
      return;
    }

  // System locales retain the OS preference list; explicit choices use one language.
  QLocale const selected_locale {language_override.isEmpty () ? locale : QLocale {language_override}};
  auto const ui_languages = selected_locale.uiLanguages ();
  auto const primary_locale = ui_languages.isEmpty () ? selected_locale : QLocale {ui_languages.front ()};
  auto const base_language = primary_locale.name ().section ('_', 0, 0);
  QString const translations_dir {":/Translations"};

  LOG_INFO (QString {"Selected interface language: %1"}.arg (selected_locale.name ()));
  if (m_->load_translator (selected_locale, "qt", "_", translations_dir))
    {
      LOG_INFO ("Loaded Qt translations for selected language from resources");
    }

  // Install base catalogs first so regional catalogs can override individual strings.
  m_->load_translator ("wsjtx_" + base_language, translations_dir);
  m_->load_translator (selected_locale, "wsjtx", "_", translations_dir);

  // Local catalogs let translators preview only the selected interface language.
  m_->load_translator ("wsjtx_" + base_language);
  m_->load_translator (selected_locale, "wsjtx", "_");
}

L10nLoader::~L10nLoader ()
{
}

QStringList L10nLoader::available_languages ()
{
  QStringList languages {"en"};
  for (auto const& filename : QDir {":/Translations"}.entryList ({"wsjtx_*.qm"}, QDir::Files))
    {
      languages.append (filename.mid (6, filename.size () - 9));
    }
  languages.removeDuplicates ();
  languages.sort ();
  return languages;
}
