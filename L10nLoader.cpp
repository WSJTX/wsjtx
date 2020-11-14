#include "L10nLoader.hpp"

#include <vector>
#include <memory>

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QRegularExpression>
#include <QDebug>

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

  // we  don't load  translators  if the  language  override is  'en',
  // 'en_US', or 'en-US'. In these cases  we assume the user is trying
  // to disable translations loaded because of their locale. We cannot
  // load any locale based translations in this case.
  auto skip_locale = language_override.contains (QRegularExpression {"^(?:en|en[-_]US)$"});

  //
  // Enable base i18n
  //
  QString translations_dir {":/Translations"};
  if (!skip_locale)
    {
      LOG_TRACE ("Looking for locale based Qt translations in resources filesystem");
      if (m_->load_translator (locale, "qt", "_", translations_dir))
        {
          LOG_INFO ("Loaded Qt translations for current locale from resources");
        }

      // Default translations for releases  use translations stored in
      // the   resources   file    system   under   the   Translations
      // directory. These are built by the CMake build system from .ts
      // files in the translations source directory. New languages are
      // added by  enabling the  UPDATE_TRANSLATIONS CMake  option and
      // building with the  new language added to  the LANGUAGES CMake
      // list  variable.  UPDATE_TRANSLATIONS  will preserve  existing
      // translations  but   should  only  be  set   when  adding  new
      // languages.  The  resulting .ts  files should be  checked info
      // source control for translators to access and update.

      // try and load the base translation
      LOG_TRACE ("Looking for WSJT-X translations based on UI languages in the resources filesystem");
      for (QString locale_name : locale.uiLanguages ())
        {
          auto language = locale_name.left (2);
          if (locale.uiLanguages ().front ().left (2) == language)
            {
              LOG_TRACE (QString {"Trying %1"}.arg (language));
              if (m_->load_translator ("wsjtx_" + language, translations_dir))
                {
                  LOG_INFO (QString {"Loaded WSJT-X base translation file from %1 based on language %2"}
                     .arg (translations_dir)
                     .arg (language));
                  break;
                }
            }
        }
      // now try and load the most specific translations (may be a
      // duplicate but we shouldn't care)
      LOG_TRACE ("Looking for WSJT-X translations based on locale in the resources filesystem");
      if (m_->load_translator (locale, "wsjtx", "_", translations_dir))
        {
          LOG_INFO ("Loaded WSJT-X translations for current locale from resources");
        }
    }
  
  // Load  any matching  translation  from  the current  directory
  // using the command line  option language override. This allows
  // translators to  easily test  their translations  by releasing
  // (lrelease)  a .qm  file  into the  current  directory with  a
  // suitable name  (e.g.  wsjtx_en_GB.qm), then running  wsjtx to
  // view the  results.
  if (language_override.size ())
    {
      auto language = language_override;
      language.replace ('-', '_');
      // try and load the base translation
      auto base_language = language.left (2);
      LOG_TRACE ("Looking for WSJT-X translations based on command line region override in the resources filesystem");
      if (m_->load_translator ("wsjtx_" + base_language, translations_dir))
        {
          LOG_INFO (QString {"Loaded base translation file from %1 based on language %2"}
             .arg (translations_dir)
             .arg (base_language));
        }
      // now load the requested translations (may be a duplicate
      // but we shouldn't care)
      LOG_TRACE ("Looking for WSJT-X translations based on command line override country in the resources filesystem");
      if (m_->load_translator ("wsjtx_" + language, translations_dir))
        {
          LOG_INFO (QString {"Loaded translation file from %1 based on language %2"}
              .arg (translations_dir)
             .arg (language));
        }
    }

  // Load any  matching translation  from the current  directory using
  // the current locale. This allows  translators to easily test their
  // translations by releasing (lrelease) a  .qm file into the current
  // directory  with  a  suitable name  (e.g.   wsjtx_en_GB.qm),  then
  // running wsjtx to view the results. The system locale setting will
  // be used to select the translation file which can be overridden by
  // the LANG environment variable on non-Windows system.

  // try and load the base translation
  LOG_TRACE ("Looking for WSJT-X translations based on command line override country in the current directory");
  for (QString locale_name : locale.uiLanguages ())
    {
      auto language = locale_name.left (2);
      if (locale.uiLanguages ().front ().left (2) == language)
        {
          LOG_TRACE (QString {"Trying %1"}.arg (language));
          if (m_->load_translator ("wsjtx_" + language))
            {
              LOG_INFO (QString {"Loaded base translation file from $cwd based on language %1"}.arg (language));
              break;
            }
        }
    }

  if (!skip_locale)
    {
      // now try  and load  the most specific  translations (may  be a
      // duplicate but we shouldn't care)
      LOG_TRACE ("Looking for WSJT-X translations based on locale in the resources filesystem");
      if (m_->load_translator (locale, "wsjtx", "_"))
        {
          LOG_INFO ("loaded translations for current locale from a file");
        }
    }

  // Load any  matching translation  from the current  directory using
  // the   command  line   option  language   override.  This   allows
  // translators  to  easily test  their  translations  on Windows  by
  // releasing (lrelease) a .qm file into the current directory with a
  // suitable name (e.g.  wsjtx_en_GB.qm),  then running wsjtx to view
  // the results.
  if (language_override.size ())
    {
      auto language = language_override;
      language.replace ('-', '_');
      // try and load the base translation
      auto base_language = language.left (2);
      LOG_TRACE ("Looking for WSJT-X translations based on command line override country in the current directory");
      if (m_->load_translator ("wsjtx_" + base_language))
        {
          LOG_INFO (QString {"Loaded base translation file from $cwd based on language %1"}.arg (base_language));
        }
      // now load the requested translations (may be a duplicate
      // but we shouldn't care)
      LOG_TRACE ("Looking for WSJT-X translations based on command line region in the current directory");
      if (m_->load_translator ("wsjtx_" + language))
        {
          LOG_INFO (QString {"loaded translation file from $cwd based on language %1"}.arg (language));
        }
    }
}

L10nLoader::~L10nLoader ()
{
}
