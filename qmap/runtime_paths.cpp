#include "runtime_paths.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

QString qmapDataDir()
{
  QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (dataDir.isEmpty()) {
    dataDir = QDir::home().absoluteFilePath(".qmap");
  }

  if (!QDir{}.mkpath(dataDir)) {
    qWarning() << "Unable to create QMAP data directory:" << dataDir;
  }

  QDir dir {dataDir};
  if (!dir.mkpath("save")) {
    qWarning() << "Unable to create QMAP save directory:" << dir.absoluteFilePath("save");
  }
  return dataDir;
}

QString qmapSettingsFile(QString const& appDir, QString const& dataDir)
{
  QString settingsFile = QDir {dataDir}.absoluteFilePath("qmap.ini");
  QString legacySettingsFile = QDir {appDir}.absoluteFilePath("qmap.ini");
  if (!QFile::exists(settingsFile) && QFile::exists(legacySettingsFile)) {
    if (QFile::copy(legacySettingsFile, settingsFile)) {
      QFile::setPermissions(settingsFile, QFile::ReadOwner | QFile::WriteOwner
                            | QFile::ReadGroup | QFile::ReadOther);
    } else {
      qWarning() << "Unable to migrate QMAP settings from" << legacySettingsFile
                 << "to" << settingsFile;
    }
  }
  return settingsFile;
}
