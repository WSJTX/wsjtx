#ifndef MAP65_RUNTIME_PATHS_H
#define MAP65_RUNTIME_PATHS_H

#include <QString>

class QSettings;

QString writableMap65DataDir();
QString map65SettingsFile(QString const& appDir, QString const& dataDir);
QString map65RuntimeFile(QString const& dataDir, QString const& fileName);
QString map65RuntimeSourceFile(QString const& appDir, QString const& installedDataDir,
                               QString const& writableDataDir, QString const& fileName);
QString ensureMap65RuntimeFile(QString const& appDir, QString const& installedDataDir,
                               QString const& writableDataDir, QString const& fileName);

// Reads the "FSam96000" key (expected to already be scoped, e.g. via
// settings.beginGroup("Common") / SettingsGroup) as text and interprets it
// under both conventions that have shared a single map65.ini across
// versions: legacy MAP65 stored this as a bool (true==96000, false==95238);
// current MAP65 stores it as an integer (0==95238, 1==96000, 2==192000).
// Returns defaultValue if the stored text doesn't match either convention.
int readFSam96000(QSettings const& settings, int defaultValue = 1);

#endif
