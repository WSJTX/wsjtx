#include "runtime_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QStringList>
#include <QSettings>
#include <QVariant>

namespace
{
  QStringList runtimeSourceCandidates(QString const& appDir, QString const& fileName)
  {
    QDir app {appDir};
    QDir cwd {QDir::currentPath()};

    return {
      app.absoluteFilePath(fileName),
      app.absoluteFilePath("resources/" + fileName),
      app.absoluteFilePath("../" + fileName),
      app.absoluteFilePath("../map65/resources/" + fileName),
      cwd.absoluteFilePath(fileName),
      cwd.absoluteFilePath("map65/resources/" + fileName)
    };
  }
}

QString writableMap65DataDir()
{
  QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (dataDir.isEmpty()) {
    dataDir = QDir::home().absoluteFilePath(".map65");
  }

  if (!QDir{}.mkpath(dataDir)) {
    qWarning() << "Unable to create MAP65 data directory:" << dataDir;
  }

  QDir dir {dataDir};
  if (!dir.mkpath("save")) {
    qWarning() << "Unable to create MAP65 save directory:" << dir.absoluteFilePath("save");
  }
  return dataDir;
}

QString map65SettingsFile(QString const& appDir, QString const& dataDir)
{
  QString settingsFile = QDir {dataDir}.absoluteFilePath("map65.ini");
  QString legacySettingsFile = QDir {appDir}.absoluteFilePath("map65.ini");
  if (!QFile::exists(settingsFile) && QFile::exists(legacySettingsFile)) {
    if (QFile::copy(legacySettingsFile, settingsFile)) {
      QFile::setPermissions(settingsFile, QFile::ReadOwner | QFile::WriteOwner
                            | QFile::ReadGroup | QFile::ReadOther);
    } else {
      qWarning() << "Unable to migrate MAP65 settings from" << legacySettingsFile
                 << "to" << settingsFile;
    }
  }
  return settingsFile;
}

int readFSam96000(QSettings const& settings, int defaultValue)
{
  QString text = settings.value("FSam96000", QString::number(defaultValue)).toString();
  if (text.compare("true", Qt::CaseInsensitive) == 0) return 1;    // legacy bool: true  == 96000
  if (text.compare("false", Qt::CaseInsensitive) == 0) return 0;   // legacy bool: false == 95238
  bool ok = false;
  int value = text.toInt(&ok);
  if (ok && value >= 0 && value <= 2) return value;                // current int: 0/1/2
  return defaultValue;
}

QString map65RuntimeFile(QString const& dataDir, QString const& fileName)
{
  return QDir {dataDir}.absoluteFilePath(fileName);
}

QString ensureMap65RuntimeFile(QString const& appDir, QString const& dataDir,
                               QString const& fileName, bool replaceEmpty)
{
  QString writablePath = map65RuntimeFile(dataDir, fileName);
  QFileInfo writableInfo {writablePath};
  if (writableInfo.exists() && (!replaceEmpty || writableInfo.size() > 0)) {
    return writablePath;
  }

  for (auto const& sourcePath : runtimeSourceCandidates(appDir, fileName)) {
    QFileInfo sourceInfo {sourcePath};
    if (!sourceInfo.exists() || !sourceInfo.isFile() || sourceInfo.size() == 0
        || sourceInfo.absoluteFilePath() == writableInfo.absoluteFilePath()) {
      continue;
    }

    if (writableInfo.exists() && writableInfo.size() == 0) {
      QFile::remove(writablePath);
    }

    if (QFile::copy(sourceInfo.absoluteFilePath(), writablePath)) {
      QFile::setPermissions(writablePath, QFile::ReadOwner | QFile::WriteOwner
                            | QFile::ReadGroup | QFile::ReadOther);
      return writablePath;
    }

    qWarning() << "Unable to migrate MAP65 runtime file from" << sourcePath
               << "to" << writablePath;
  }

  return writablePath;
}
