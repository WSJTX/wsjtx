#ifndef HIGHDPI_SCALING_HPP__
#define HIGHDPI_SCALING_HPP__

#include <QString>

namespace HighDpiScaling
{
  struct StartupOptions
  {
    QString application_name {"WSJT-X"};
    QString configuration_name;
    bool test_mode {false};
  };

  StartupOptions wsjtxStartupOptions (int argc, char * argv[]);
  QString wsjtxSettingsPath (QString const& application_name);
  bool wsjtxEnabled (QString const& settings_path, QString const& configuration_name);
  bool wsjtxEnabled (int argc, char * argv[]);
  bool qmapEnabled (QString const& settings_path);
}

#endif
