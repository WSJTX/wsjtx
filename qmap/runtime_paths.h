#ifndef QMAP_RUNTIME_PATHS_H
#define QMAP_RUNTIME_PATHS_H

#include <QString>

QString qmapDataDir();
QString qmapSettingsFile(QString const& appDir, QString const& dataDir);

#endif
