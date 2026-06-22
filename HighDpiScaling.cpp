#include "HighDpiScaling.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace
{
  char const * high_dpi_key = "HighDPI";
  char const * multi_settings_group = "MultiSettings";

  QStringList startupArguments (int argc, char * argv[])
  {
    QStringList result;
    for (int i = 0; i < argc; ++i)
      {
        result << QString::fromLocal8Bit (argv[i]);
      }
    return result;
  }

  bool readBool (QSettings * settings, QString const& key, bool default_value)
  {
    return settings->value (key, default_value).toBool ();
  }

  QCommandLineOption rigOption ()
  {
    return {{QStringLiteral ("r"), QStringLiteral ("rig-name")},
            QStringLiteral ("Where <rig-name> is for multi-instance support."),
            QStringLiteral ("rig-name")};
  }

  QCommandLineOption configOption ()
  {
    return {{QStringLiteral ("c"), QStringLiteral ("config")},
            QStringLiteral ("Where <configuration> is an existing one."),
            QStringLiteral ("configuration")};
  }

  QCommandLineOption testModeOption ()
  {
    return {QStringList {QStringLiteral ("test-mode")},
            QStringLiteral ("Writable files in test location. Use with caution, for testing only.")};
  }

  void configureStartupParser (QCommandLineParser * parser,
                               QCommandLineOption const& rig_option,
                               QCommandLineOption const& config_option,
                               QCommandLineOption const& test_mode_option)
  {
    parser->addHelpOption ();
    parser->addVersionOption ();

    parser->addOption (rig_option);
    parser->addOption (config_option);
    parser->addOption (test_mode_option);
    parser->addOption ({{QStringLiteral ("w"), QStringLiteral ("window-handle")},
                        QStringLiteral ("N1MM window handle (hexadecimal)."),
                        QStringLiteral ("N1MM Window handle")});
    parser->addOption ({{QStringLiteral ("l"), QStringLiteral ("language")},
                        QStringLiteral ("Where <language> is <lang-code>[-<country-code>]."),
                        QStringLiteral ("language")});
  }
}

namespace HighDpiScaling
{
  StartupOptions wsjtxStartupOptions (int argc, char * argv[])
  {
    StartupOptions options;
    auto const rig_option = rigOption ();
    auto const config_option = configOption ();
    auto const test_mode_option = testModeOption ();
    QCommandLineParser parser;
    configureStartupParser (&parser, rig_option, config_option, test_mode_option);
    if (!parser.parse (startupArguments (argc, argv)))
      {
        return options;
      }

    auto const rig_name = parser.value (rig_option);
    if (!rig_name.isEmpty ())
      {
        options.application_name += " - " + rig_name;
      }

    options.test_mode = parser.isSet (test_mode_option);
    if (options.test_mode)
      {
        options.application_name += " - test";
      }

    options.configuration_name = parser.value (config_option);
    return options;
  }

  QString wsjtxSettingsPath (QString const& application_name)
  {
    auto const config_directory = QStandardPaths::writableLocation (QStandardPaths::ConfigLocation);
    return QDir {config_directory}.absoluteFilePath (application_name + ".ini");
  }

  bool wsjtxEnabled (QString const& settings_path, QString const& configuration_name)
  {
    QSettings settings {settings_path, QSettings::IniFormat};
    if (!configuration_name.isEmpty ())
      {
        settings.beginGroup (multi_settings_group);
        if (settings.childGroups ().contains (configuration_name))
          {
            settings.beginGroup (configuration_name);
            auto const enabled = readBool (&settings, high_dpi_key, true);
            settings.endGroup ();
            settings.endGroup ();
            return enabled;
          }
        settings.endGroup ();
      }

    return readBool (&settings, high_dpi_key, true);
  }

  bool wsjtxEnabled (int argc, char * argv[])
  {
    auto const options = wsjtxStartupOptions (argc, argv);
    // These globals affect QStandardPaths before QApplication exists.
    if (options.test_mode)
      {
        QStandardPaths::setTestModeEnabled (true);
      }
    QCoreApplication::setApplicationName (options.application_name);
    return wsjtxEnabled (wsjtxSettingsPath (options.application_name),
                         options.configuration_name);
  }

  bool qmapEnabled (QString const& settings_path)
  {
    QSettings settings {settings_path, QSettings::IniFormat};
    settings.beginGroup ("Common");
    auto const enabled = readBool (&settings, high_dpi_key, true);
    settings.endGroup ();
    return enabled;
  }
}
