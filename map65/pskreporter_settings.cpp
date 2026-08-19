#include "pskreporter_settings.h"

#include <QSettings>

Map65PSKReporterSettings readMap65PSKReporterSettings(QSettings const& settings)
{
  return {
    settings.value ("Common/spotPSK", true).toBool (),
    settings.value ("Common/PSKReporterTCPIP", false).toBool ()
  };
}
