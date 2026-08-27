#include <QtTest>

#include <QSettings>
#include <QTemporaryDir>

#include "map65/pskreporter_settings.h"

class TestMap65PSKReporterSettings final : public QObject
{
  Q_OBJECT

private slots:
  void defaultsToEnabledUdp()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QSettings settings {directory.filePath("map65.ini"), QSettings::IniFormat};
    auto const values = readMap65PSKReporterSettings(settings);

    QVERIFY(values.enabled);
    QVERIFY(!values.use_tcpip);
  }

  void preservesExplicitEnableAndTransportValues()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto const filename = directory.filePath("map65.ini");
    {
      QSettings settings {filename, QSettings::IniFormat};
      settings.beginGroup("Common");
      settings.setValue("spotPSK", false);
      settings.setValue("PSKReporterTCPIP", true);
      settings.endGroup();
      settings.sync();
    }

    QSettings settings {filename, QSettings::IniFormat};
    auto const values = readMap65PSKReporterSettings(settings);

    QVERIFY(!values.enabled);
    QVERIFY(values.use_tcpip);
  }
};

QTEST_MAIN(TestMap65PSKReporterSettings)

#include "test_map65_pskreporter_settings.moc"
