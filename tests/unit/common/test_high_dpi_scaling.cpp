#include <QCoreApplication>
#include <QObject>
#include <QStandardPaths>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

#include "HighDpiScaling.hpp"

namespace
{
  QString settingsPath (QTemporaryDir const& dir)
  {
    return dir.filePath ("settings.ini");
  }

  void setRootHighDpi (QString const& path, bool enabled)
  {
    QSettings settings {path, QSettings::IniFormat};
    settings.setValue ("HighDPI", enabled);
    settings.sync ();
  }

  void setConfigHighDpi (QString const& path, QString const& config_name, bool enabled)
  {
    QSettings settings {path, QSettings::IniFormat};
    settings.beginGroup ("MultiSettings");
    settings.beginGroup (config_name);
    settings.setValue ("HighDPI", enabled);
    settings.sync ();
  }

  void setQmapHighDpi (QString const& path, bool enabled)
  {
    QSettings settings {path, QSettings::IniFormat};
    settings.beginGroup ("Common");
    settings.setValue ("HighDPI", enabled);
    settings.sync ();
  }
}

class TestHighDpiScaling final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void wsjtxDefaultsToEnabledWithoutSettings ();
  void wsjtxReadsRootSetting ();
  void wsjtxReadsNamedConfiguration ();
  void wsjtxFallsBackToRootForMissingConfiguration ();
  void wsjtxDefaultsNamedConfigurationWithoutKeyToEnabled ();
  void wsjtxParsesStartupOptions ();
  void wsjtxStartupReadSetsApplicationNameBeforeResolvingPath ();
  void qmapDefaultsToEnabledWithoutSettings ();
  void qmapReadsCommonSetting ();
};

void TestHighDpiScaling::wsjtxDefaultsToEnabledWithoutSettings ()
{
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  QCOMPARE (HighDpiScaling::wsjtxEnabled (settingsPath (dir), QString {}), true);
}

void TestHighDpiScaling::wsjtxReadsRootSetting ()
{
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  auto const path = settingsPath (dir);

  setRootHighDpi (path, false);
  QCOMPARE (HighDpiScaling::wsjtxEnabled (path, QString {}), false);

  setRootHighDpi (path, true);
  QCOMPARE (HighDpiScaling::wsjtxEnabled (path, QString {}), true);
}

void TestHighDpiScaling::wsjtxReadsNamedConfiguration ()
{
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  auto const path = settingsPath (dir);

  setRootHighDpi (path, true);
  setConfigHighDpi (path, "Portable", false);

  QCOMPARE (HighDpiScaling::wsjtxEnabled (path, "Portable"), false);
}

void TestHighDpiScaling::wsjtxFallsBackToRootForMissingConfiguration ()
{
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  auto const path = settingsPath (dir);

  setRootHighDpi (path, false);

  QCOMPARE (HighDpiScaling::wsjtxEnabled (path, "Missing"), false);
}

void TestHighDpiScaling::wsjtxDefaultsNamedConfigurationWithoutKeyToEnabled ()
{
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  auto const path = settingsPath (dir);

  QSettings settings {path, QSettings::IniFormat};
  settings.beginGroup ("MultiSettings");
  settings.beginGroup ("Empty");
  settings.setValue ("MultiSettingsPlaceHolder", QVariant {});
  settings.sync ();

  QCOMPARE (HighDpiScaling::wsjtxEnabled (path, "Empty"), true);
}

void TestHighDpiScaling::wsjtxParsesStartupOptions ()
{
  char arg0[] = "wsjtx";
  char arg1[] = "-rOld";
  char arg2[] = "--rig-name=Station";
  char arg3[] = "-cOld";
  char arg4[] = "-cPortable";
  char arg5[] = "--test-mode";
  char * argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};

  auto const options = HighDpiScaling::wsjtxStartupOptions (6, argv);

  QCOMPARE (options.application_name, QString {"WSJT-X - Station - test"});
  QCOMPARE (options.configuration_name, QString {"Portable"});
  QCOMPARE (options.test_mode, true);
}

void TestHighDpiScaling::wsjtxStartupReadSetsApplicationNameBeforeResolvingPath ()
{
  auto const previous_name = QCoreApplication::applicationName ();
  QStandardPaths::setTestModeEnabled (true);
  QCoreApplication::setApplicationName ("WrongName");

  char arg0[] = "wsjtx";
  char arg1[] = "-r";
  char arg2[] = "Station";
  char arg3[] = "--test-mode";
  char * argv[] = {arg0, arg1, arg2, arg3};

  auto const options = HighDpiScaling::wsjtxStartupOptions (4, argv);
  QCoreApplication::setApplicationName (options.application_name);
  auto const path = HighDpiScaling::wsjtxSettingsPath (options.application_name);
  setRootHighDpi (path, false);
  QCoreApplication::setApplicationName ("WrongName");

  QCOMPARE (HighDpiScaling::wsjtxEnabled (4, argv), false);
  QCOMPARE (QCoreApplication::applicationName (), options.application_name);

  QStandardPaths::setTestModeEnabled (false);
  QCoreApplication::setApplicationName (previous_name);
}

void TestHighDpiScaling::qmapDefaultsToEnabledWithoutSettings ()
{
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  QCOMPARE (HighDpiScaling::qmapEnabled (settingsPath (dir)), true);
}

void TestHighDpiScaling::qmapReadsCommonSetting ()
{
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  auto const path = settingsPath (dir);

  setQmapHighDpi (path, false);
  QCOMPARE (HighDpiScaling::qmapEnabled (path), false);

  setQmapHighDpi (path, true);
  QCOMPARE (HighDpiScaling::qmapEnabled (path), true);
}

QTEST_APPLESS_MAIN (TestHighDpiScaling)

#include "test_high_dpi_scaling.moc"
