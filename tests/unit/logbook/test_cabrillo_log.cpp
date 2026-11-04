#include "wsjtx_config.h"

#include <QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>

#include "Configuration.cpp"
#include "logbook/AD1CCty.hpp"
#include "models/CabrilloLog.hpp"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () {return 0;}

class TestCabrilloLog final : public QObject
{
  Q_OBJECT

  QTemporaryDir settings_dir_;
  QNetworkAccessManager network_;
  QSqlDatabase database_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Configuration> configuration_;
  std::unique_ptr<AD1CCty> countries_;
  std::unique_ptr<CabrilloLog> log_;

private Q_SLOTS:
  void initTestCase ();
  void cleanupTestCase ();
  void init ();
  void cleanup ();
  void counts_dxcc_entities_per_band_data ();
  void counts_dxcc_entities_per_band ();
};

void TestCabrilloLog::initTestCase ()
{
  register_types ();
  QStandardPaths::setTestModeEnabled (true);
  QCoreApplication::setOrganizationName ("WSJT-X");
  QCoreApplication::setApplicationName ("test_cabrillo_log");
  QVERIFY (settings_dir_.isValid ());
  QDir const data_dir {QStandardPaths::writableLocation (QStandardPaths::DataLocation)};
  QVERIFY (data_dir.mkpath ("."));
  QFile cty_file {data_dir.filePath ("cty.dat")};
  QVERIFY (cty_file.open (QIODevice::WriteOnly | QIODevice::Text));
  QByteArray const contents {
    "Scotland:14:27:EU:56:4:0:GM\nGM,=G0FBJ;\n"
    "Shetland Islands:14:27:EU:60:1:0:*GM/s\n=G0FBJ;\n"
    "WAE-only Entity:15:28:EU:42:-21:-1:*Z6\nZ6;\n"};
  QCOMPARE (cty_file.write (contents), static_cast<qint64> (contents.size ()));
}

void TestCabrilloLog::cleanupTestCase ()
{
  QDir {QStandardPaths::writableLocation (QStandardPaths::DataLocation)}.removeRecursively ();
}

void TestCabrilloLog::init ()
{
  database_ = QSqlDatabase::addDatabase ("QSQLITE");
  database_.setDatabaseName (":memory:");
  QVERIFY2 (database_.open (), qPrintable (database_.lastError ().text ()));
  settings_.reset (new QSettings {settings_dir_.filePath ("settings.ini"), QSettings::IniFormat});
  settings_->clear ();
}

void TestCabrilloLog::cleanup ()
{
  log_.reset ();
  countries_.reset ();
  configuration_.reset ();
  settings_.reset ();
  auto const connection = database_.connectionName ();
  database_.close ();
  database_ = QSqlDatabase {};
  QSqlDatabase::removeDatabase (connection);
}

void TestCabrilloLog::counts_dxcc_entities_per_band_data ()
{
  QTest::addColumn<bool> ("include_wae");
  QTest::newRow ("wae-off") << false;
  QTest::newRow ("wae-on") << true;
}

void TestCabrilloLog::counts_dxcc_entities_per_band ()
{
  QFETCH (bool, include_wae);
  settings_->setValue ("Configuration/IncludeWAEEntities", include_wae);
  configuration_.reset (new Configuration {&network_, QDir {settings_dir_.path ()}, settings_.get (), nullptr});
  QCOMPARE (configuration_->include_WAE_entities (), include_wae);
  countries_.reset (new AD1CCty {configuration_.get ()});
  log_.reset (new CabrilloLog {configuration_.get ()});

  auto const when = QDateTime {QDate {2026, 10, 30}, QTime {12, 0}, Qt::UTC};
  QVERIFY (log_->add_QSO (14074000, "FT8", when, "GM0AAA", "001", "001"));
  QVERIFY (log_->add_QSO (14076000, "FT8", when, "G0FBJ", "002", "002"));
  QVERIFY (log_->add_QSO (7074000, "FT8", when, "G0FBJ", "003", "003"));
  QVERIFY (log_->add_QSO (14074000, "FT8", when, "Z6ABC", "004", "004"));
  QVERIFY (log_->add_QSO (14074000, "FT8", when, "ZZ9ABC", "005", "005"));
  QCOMPARE (log_->n_qso (), 5);

  auto const fallback = countries_->lookup ("Z6ABC");
  QCOMPARE (fallback.identity (AD1CCty::EntityView::DXCC).name, QString {"WAE-only Entity"});
  QVERIFY (fallback.dxcc.primary_prefix.isEmpty ());
  QVERIFY (countries_->lookup ("ZZ9ABC").dxcc.primary_prefix.isEmpty ());

  auto const entities = log_->unique_DXCC_entities (countries_.get ());
  QCOMPARE (entities.size (), 2);
  QVERIFY (entities.contains (CabrilloLog::worked_item {"20m", "GM"}));
  QVERIFY (entities.contains (CabrilloLog::worked_item {"40m", "GM"}));
}

QTEST_MAIN (TestCabrilloLog)
#include "test_cabrillo_log.moc"
