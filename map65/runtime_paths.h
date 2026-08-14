#ifndef MAP65_RUNTIME_PATHS_H
#define MAP65_RUNTIME_PATHS_H

#include <QString>

QString writableMap65DataDir();
QString map65SettingsFile(QString const& appDir, QString const& dataDir);
QString map65RuntimeFile(QString const& dataDir, QString const& fileName);
QString ensureMap65RuntimeFile(QString const& appDir, QString const& dataDir,
                               QString const& fileName, bool replaceEmpty = false);

#endif
