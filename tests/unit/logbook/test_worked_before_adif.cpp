#include "wsjtx_config.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <memory>

#include "Configuration.cpp"
#include "logbook/WorkedBefore.hpp"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () {return 0;}

class TestWorkedBeforeAdif final : public QObject
{
  Q_OBJECT

  QTemporaryDir settings_dir_;
  QNetworkAccessManager network_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Configuration> configuration_;

private Q_SLOTS:
  void initTestCase ();
  void cleanupTestCase ();
  void init ();
  void cleanup ();
  void imports_records_independently_of_line_layout_data ();
  void imports_records_independently_of_line_layout ();

private:
  QString data_directory () const;
  bool wait_for_load (WorkedBefore& worked_before, QString& error);
};

QString TestWorkedBeforeAdif::data_directory () const
{
  return QStandardPaths::writableLocation (QStandardPaths::DataLocation);
}

void TestWorkedBeforeAdif::initTestCase ()
{
  register_types ();
  QStandardPaths::setTestModeEnabled (true);
  QCoreApplication::setOrganizationName ("WSJT-X");
  QCoreApplication::setApplicationName ("test_worked_before_adif");
  QVERIFY (settings_dir_.isValid ());

  QVERIFY (QDir {}.mkpath (data_directory ()));
  QFile cty_file {QDir {data_directory ()}.filePath ("cty.dat")};
  QVERIFY (cty_file.open (QIODevice::WriteOnly | QIODevice::Text));
  QByteArray cty_data {"Test Entity:5:8:NA:37:-96:-5:K\nK,W;\n"};
  QCOMPARE (cty_file.write (cty_data), static_cast<qint64> (cty_data.size ()));
}

void TestWorkedBeforeAdif::cleanupTestCase ()
{
  configuration_.reset ();
  QDir {data_directory ()}.removeRecursively ();
}

void TestWorkedBeforeAdif::init ()
{
  settings_.reset (new QSettings {settings_dir_.filePath ("settings.ini"), QSettings::IniFormat});
  settings_->clear ();
  configuration_.reset (new Configuration {&network_, QDir {settings_dir_.path ()}, settings_.get (), nullptr});
}

void TestWorkedBeforeAdif::cleanup ()
{
  configuration_.reset ();
  settings_.reset ();
}

void TestWorkedBeforeAdif::imports_records_independently_of_line_layout_data ()
{
  QTest::addColumn<QByteArray> ("adif");
  QTest::addColumn<QStringList> ("expected_calls");

  auto record = [] (QString const& call) {
    return QString {"<CALL:%1>%2<BAND:3>20M<MODE:3>FT8<GRIDSQUARE:4>FN42<EOR>"}
      .arg (call.size ()).arg (call);
  };
  auto call = [] (QString const& value) {return value.toUtf8 ();};

  QTest::newRow ("leading-whitespace")
    << QByteArray {" \r\n\t" + record ("K1ABC").toUtf8 ()} << QStringList {"K1ABC"};
  QTest::newRow ("headerless-single-line-no-trailing-newline")
    << call (record ("K1ABC")) << QStringList {"K1ABC"};
  QTest::newRow ("multiline-record")
    << QByteArray {"<CALL:5>K1ABC\n\n<BAND:3>20M\n\n<MODE:3>FT8\n<GRIDSQUARE:4>FN42\n<EOR>"}
    << QStringList {"K1ABC"};
  QTest::newRow ("multiple-records-on-one-line")
    << (record ("K1ABC") + record ("W1XYZ")).toUtf8 () << QStringList {"K1ABC", "W1XYZ"};
  QTest::newRow ("optional-header")
    << QByteArray {"ADIF Export\n<adif_ver:5>3.1.1\n<eoh>\n" + record ("K1ABC").toUtf8 ()}
    << QStringList {"K1ABC"};
  QTest::newRow ("leading-whitespace-optional-header")
    << QByteArray {" \n<adif_ver:5>3.1.1<eoh>\n" + record ("K1ABC").toUtf8 ()}
    << QStringList {"K1ABC"};
  QTest::newRow ("eoh-text-inside-length-delimited-header-field")
    << QByteArray {"ADIF Export\n<programid:5><EOH>\n<eoh>\n" + record ("K1ABC").toUtf8 ()}
    << QStringList {"K1ABC"};
  QTest::newRow ("final-record-with-trailing-newline")
    << (record ("K1ABC") + "\n\n" + record ("W1XYZ") + "\n").toUtf8 ()
    << QStringList {"K1ABC", "W1XYZ"};
  QTest::newRow ("final-record-without-trailing-newline")
    << (record ("K1ABC") + "\n\n" + record ("W1XYZ")).toUtf8 ()
    << QStringList {"K1ABC", "W1XYZ"};
  QTest::newRow ("eor-text-inside-length-delimited-field")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:8:M>a\r\n<EOR><BAND:3>20M<MODE:3>FT8<GRIDSQUARE:4>FN42<EOR>\n"}
    << QStringList {"K1ABC"};
}

bool TestWorkedBeforeAdif::wait_for_load (WorkedBefore& worked_before, QString& error)
{
  QSignalSpy loaded {&worked_before, &WorkedBefore::finished_loading};
  if (!loaded.wait (5000))
    {
      error = QStringLiteral ("Timed out waiting for ADIF load");
      return false;
    }
  error = loaded.takeFirst ().at (2).toString ();
  return error.isEmpty ();
}

void TestWorkedBeforeAdif::imports_records_independently_of_line_layout ()
{
  QFETCH (QByteArray, adif);
  QFETCH (QStringList, expected_calls);

  QVERIFY (QDir {}.mkpath (configuration_->writeable_data_dir ().absolutePath ()));
  QFile initial_log {configuration_->writeable_data_dir ().filePath ("wsjtx_log.adi")};
  QVERIFY (initial_log.open (QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE (initial_log.write (adif), static_cast<qint64> (adif.size ()));
  initial_log.close ();

  WorkedBefore worked_before {configuration_.get ()};
  QString error;
  QVERIFY2 (wait_for_load (worked_before, error), qPrintable (error));

  for (auto const& call : expected_calls)
    {
      QVERIFY2 (worked_before.call_worked (call, "FT8", "20M"), qPrintable (call));
    }
  QVERIFY (!worked_before.call_worked ("N0NOT", "FT8", "20M"));
}

QTEST_MAIN (TestWorkedBeforeAdif)

#include "test_worked_before_adif.moc"
