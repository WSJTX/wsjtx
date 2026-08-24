#ifndef MAP65_PSKREPORTER_SETTINGS_H
#define MAP65_PSKREPORTER_SETTINGS_H

class QSettings;

struct Map65PSKReporterSettings
{
  bool enabled;
  bool use_tcpip;
};

Map65PSKReporterSettings readMap65PSKReporterSettings(QSettings const& settings);

#endif
