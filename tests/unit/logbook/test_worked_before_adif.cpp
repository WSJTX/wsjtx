#include "wsjtx_config.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QThreadPool>
#include <memory>

#include "Configuration.cpp"
#include "logbook/WorkedBefore.hpp"
#include "logbook/AdifQso.hpp"
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
  void jtty_round_trips_with_current_header ();
  void imports_records_across_layouts_and_length_errors_data ();
  void imports_records_across_layouts_and_length_errors ();
  void reports_incomplete_sources_data ();
  void reports_incomplete_sources ();
  void primary_rescan_recovers_records_and_preserves_live_qsos ();
  void comment_cannot_supply_a_missing_band ();

private:
  QString data_directory () const;
  bool wait_for_load (WorkedBefore& worked_before, int& record_count, QString& error);
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

void TestWorkedBeforeAdif::jtty_round_trips_with_current_header ()
{
  auto const path = configuration_->writeable_data_dir ().filePath ("wsjtx_log.adi");
  if (QFile::exists (path)) QVERIFY (QFile::remove (path));
  WorkedBefore worked_before {configuration_.get ()};
  int record_count {0};
  QString error;
  QVERIFY2 (wait_for_load (worked_before, record_count, error), qPrintable (error));
  QCOMPARE (record_count, 0);

  auto const time = QDateTime {QDate {2026, 10, 15}, QTime {12, 0}, Qt::UTC};
  auto const record = AdifQso::to_adif ("K1ABC", "FN42", "JTTY", "-10", "-12", time, time,
                                      "20m", {}, {}, "14.080000", "W1AW", "FN31", {}, {},
                                      {}, {}, {}, {}, {}, {}, AdifQso::Contest::None);
  QVERIFY (worked_before.add ("K1ABC", "FN42", "20m", "JTTY", record));
  QVERIFY (worked_before.call_worked ("K1ABC", "JTTY", "20M"));

  QFile log {path};
  QVERIFY (log.open (QIODevice::ReadOnly));
  auto const adif = log.readAll ();
  QVERIFY (adif.contains ("<adif_ver:5>3.1.8"));
  QVERIFY (adif.contains ("<mode:4>MFSK <submode:4>JTTY"));
  log.close ();

  worked_before.reload ();
  QVERIFY2 (wait_for_load (worked_before, record_count, error), qPrintable (error));
  QCOMPARE (record_count, 1);
  QVERIFY (worked_before.call_worked ("K1ABC", "JTTY", "20M"));
  QVERIFY (!worked_before.call_worked ("K1ABC", "MFSK", "20M"));
}

void TestWorkedBeforeAdif::imports_records_across_layouts_and_length_errors_data ()
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

  auto oversize_record = QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:9999>oops<EOR>\r\n"};
  QTest::newRow ("oversize-length-first-record")
    << (oversize_record + record ("W1XYZ").toUtf8 ()) << QStringList {"K1ABC", "W1XYZ"};
  QTest::newRow ("oversize-length-middle-record")
    << (record ("N2AAA").toUtf8 () + oversize_record + record ("W1XYZ").toUtf8 ())
    << QStringList {"N2AAA", "K1ABC", "W1XYZ"};

  auto slightly_long_record = QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:30>oops<EOR>\r\n"
                                          "<CALL:5>W1XYZ<BAND:3>20M<MODE:3>FT8<EOR>\r\n"};
  QTest::newRow ("slightly-long-length")
    << (slightly_long_record + record ("N2AAA").toUtf8 ()) << QStringList {"K1ABC", "W1XYZ", "N2AAA"};

  QTest::newRow ("suspect-endpoint-at-tag-closing-bracket")
    << (QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:8>oops<EOR>"} + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};

  for (auto const& field : {"CALL", "BAND", "MODE", "SUBMODE", "GRIDSQUARE"})
    {
      auto text = QString {"<%1:999>"}.arg (field).toUtf8 ();
      auto adif = QByteArray {"<COMMENT:"} + QByteArray::number (text.size ()) + ":S>" + text
        + record ("K1ABC").toUtf8 ();
      QTest::newRow (qPrintable (QString {"descriptor-inside-comment-%1"}.arg (field)))
        << adif << QStringList {"K1ABC"};
    }
  QTest::newRow ("fake-band-after-real-call")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:10><BAND:999><BAND:3>20M<MODE:3>FT8<EOR>"}
    << QStringList {"K1ABC"};

  auto unicode_comment = QString (5, QChar {0x00e9}) + "<BAND:999>";
  QTest::newRow ("character-counted-comment-containing-descriptor")
    << (QString {"<COMMENT:%1>%2"}.arg (unicode_comment.size ()).arg (unicode_comment).toUtf8 ()
        + record ("K1ABC").toUtf8 ()) << QStringList {"K1ABC"};
  auto name = QString {"Jos"} + QChar {0x00e9};
  auto name_record = QString {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<NAME:%1>%2<EOR>"};
  auto utf8_bom = QByteArray::fromHex ("efbbbf");
  QTest::newRow ("utf8-byte-counted-name")
    << (utf8_bom + name_record.arg (name.toUtf8 ().size ()).arg (name).toUtf8 () + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};
  QTest::newRow ("utf8-character-counted-name")
    << (utf8_bom + name_record.arg (name.size ()).arg (name).toUtf8 () + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};

  for (int count : {4, 5, 20})
    for (bool byte_counted : {false, true})
      for (bool newline : {false, true})
        {
          auto value = QString (count, QChar {0x00e9});
          auto length = byte_counted ? value.toUtf8 ().size () : value.size ();
          auto final_record = name_record.arg (length).arg (value).toUtf8 ();
          if (newline) final_record += "\n";
          auto row = QString {"final-utf8-name-%1-%2-%3"}.arg (count)
            .arg (byte_counted ? "bytes" : "characters").arg (newline ? "newline" : "eof");
          QTest::newRow (qPrintable (row))
            << (record ("W1XYZ").toUtf8 () + final_record) << QStringList {"W1XYZ", "K1ABC"};
        }
  auto split_name = name_record.arg (10).arg (QString (5, QChar {0x00e9})).toUtf8 ();
  auto name_start = split_name.indexOf ("<NAME:10>") + QByteArray {"<NAME:10>"}.size ();
  QTest::newRow ("utf8-name-crosses-read-chunk")
    << (QByteArray (64 * 1024 - name_start - 2, ' ') + split_name)
    << QStringList {"K1ABC"};

  QTest::newRow ("embedded-eor-with-text-between-fields")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:5><EOR>padding<BAND:3>20M<MODE:3>FT8<GRIDSQUARE:4>FN42<EOR>"}
    << QStringList {"K1ABC"};
  QTest::newRow ("valid-endpoint-inside-non-tag-text")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:9><EOR>abc<literal>padding<BAND:3>20M<MODE:3>FT8<EOR>"}
    << QStringList {"K1ABC"};
  QTest::newRow ("embedded-eoh-with-text-between-fields")
    << (QByteArray {"ADIF Export<PROGRAMID:5><EOH>padding<EOH>"} + record ("K1ABC").toUtf8 ())
    << QStringList {"K1ABC"};
  QTest::newRow ("oversize-length-header-field")
    << (QByteArray {"ADIF Export<PROGRAMID:9999>oops<EOH>"} + record ("K1ABC").toUtf8 ())
    << QStringList {"K1ABC"};

  constexpr int chunk_size {64 * 1024};
  auto split_endpoint_tag = [] (QByteArray const& adif, int field_length) {
    auto data_start = adif.indexOf ('>', adif.indexOf ("<COMMENT:")) + 1;
    return QByteArray (chunk_size - data_start - field_length - 1, ' ') + adif;
  };
  auto typed_tag_record = slightly_long_record;
  typed_tag_record.replace ("<BAND:3>", "<BAND:3:S>");
  QTest::newRow ("suspect-endpoint-in-split-field-tag")
    << split_endpoint_tag (typed_tag_record, 30) << QStringList {"K1ABC", "W1XYZ"};
  auto short_record = QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:5>oops<eOr>"};
  QTest::newRow ("suspect-endpoint-in-split-eor")
    << split_endpoint_tag (short_record + record ("W1XYZ").toUtf8 (), 5)
    << QStringList {"K1ABC", "W1XYZ"};

  auto long_value = QByteArray {"<EOR>"} + QByteArray (chunk_size + 17, 'x') + "<EOR>";
  QTest::newRow ("valid-field-longer-than-chunk")
    << (QByteArray {"<CALL:5>K1ABC<COMMENT:"} + QByteArray::number (long_value.size ()) + ">" + long_value
        + "padding<BAND:3>20M<MODE:3>FT8<EOR>" + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};
}

bool TestWorkedBeforeAdif::wait_for_load (WorkedBefore& worked_before, int& record_count, QString& error)
{
  QSignalSpy loaded {&worked_before, &WorkedBefore::finished_loading};
  if (!loaded.wait (5000))
    {
      error = QStringLiteral ("Timed out waiting for ADIF load");
      return false;
    }
  auto result = loaded.takeFirst ();
  record_count = result.at (0).toInt ();
  error = result.at (2).toString ();
  return error.isEmpty ();
}

void TestWorkedBeforeAdif::imports_records_across_layouts_and_length_errors ()
{
  QFETCH (QByteArray, adif);
  QFETCH (QStringList, expected_calls);

  QVERIFY (QDir {}.mkpath (configuration_->writeable_data_dir ().absolutePath ()));
  QFile initial_log {configuration_->writeable_data_dir ().filePath ("wsjtx_log.adi")};
  QVERIFY (initial_log.open (QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE (initial_log.write (adif), static_cast<qint64> (adif.size ()));
  initial_log.close ();

  WorkedBefore worked_before {configuration_.get ()};
  int record_count {0};
  QString error;
  QVERIFY2 (wait_for_load (worked_before, record_count, error), qPrintable (error));
  QCOMPARE (record_count, expected_calls.size ());

  for (auto const& call : expected_calls)
    {
      QVERIFY2 (worked_before.call_worked (call, "FT8", "20M"), qPrintable (call));
    }
  QVERIFY (!worked_before.call_worked ("N0NOT", "FT8", "20M"));
}

void TestWorkedBeforeAdif::reports_incomplete_sources_data ()
{
  QTest::addColumn<QByteArray> ("contents");
  QTest::addColumn<bool> ("primary");
  QTest::addColumn<bool> ("accepted");
  QTest::addColumn<int> ("entries");
  auto row = [] (char const * name, QByteArray contents, bool accepted, int entries) {
    for (bool primary : {false, true})
      QTest::newRow (qPrintable (QString {"%1-%2"}.arg (name).arg (primary ? "primary" : "supplemental")))
        << contents << primary << accepted << entries;
  };
  row ("empty", {}, true, 0);
  row ("whitespace", " \t\r\n", true, 0);
  row ("whitespace-across-chunks", QByteArray (128 * 1024, ' '), true, 0);
  row ("header-only", "ADIF Export\n<adif_ver:5>3.1.1\n<eoh>\n", true, 0);
  row ("field-starting-header-only", "<adif_ver:5>3.1.1<eoh>", true, 0);
  row ("header-marker-in-comment", "<call:5>K1ABC<band:3>20m<mode:3>FT8<comment:5><EOH><eor>", true, 1);
  row ("typed-header-marker-in-comment", "<call:5>K1ABC<band:3>20m<mode:3>FT8<comment:5:S><eOh><eor>", true, 1);
  row ("header-comment-before-header-end", "ADIF Export\n<comment:5><EOH><eoh><call:5>K1ABC<band:3>20m<mode:3>FT8<eor>", true, 1);
  row ("ignored-footer", "<call:5>K1ABC<band:3>20m<mode:3>FT8<eor>\nExport complete\n", true, 1);
  row ("invalid-header", "not an ADIF export", false, 0);
  row ("invalid-field-length", "<call:bad>K1ABC<eor>", false, 0);
  row ("invalid-field-type", "<call:5:BAD>K1ABC<eor>", false, 0);
  row ("empty-field-type", "<call:5:>K1ABC<eor>", false, 0);
  row ("extra-field-colon", "<call:5:S:S>K1ABC<eor>", false, 0);
  row ("truncated-field", "<call:100>K1ABC<eor>", false, 0);
  row ("unterminated-record", "<call:5>K1ABC<band:3>20m<mode:3>FT8", false, 0);
  QByteArray complete {"<call:5>K1ABC<band:3>20m<mode:3>FT8<eor>"};
  row ("unterminated-record-after-valid-record", complete + "<call:5>W1XYZ<band:3>20m<mode:3>FT8", false, 1);
  row ("bad-field-after-valid-record", complete + "<call:bad>W1XYZ<eor>", false, 1);
  row ("bad-band-after-valid-record", complete + "<call:5>W1XYZ<band:bad>20m<eor>", false, 1);
  row ("bad-field-before-another-valid-record", complete + "<call:bad>W1XYZ<eor><call:5>N2AAA<band:3>20m<mode:3>FT8<eor>", false, 1);
}

void TestWorkedBeforeAdif::reports_incomplete_sources ()
{
  QFETCH (QByteArray, contents);
  QFETCH (bool, primary);
  QFETCH (bool, accepted);
  QFETCH (int, entries);
  auto const primary_path = configuration_->writeable_data_dir ().filePath ("wsjtx_log.adi");
  if (QFile::exists (primary_path)) QVERIFY (QFile::remove (primary_path));
  auto const source = primary ? primary_path : settings_dir_.filePath ("station.adi");
  if (!primary)
    {
      configuration_.reset ();
      settings_->setValue ("Configuration/SupplementalHistoryFiles", QStringList {source});
      configuration_.reset (new Configuration {&network_, QDir {settings_dir_.path ()}, settings_.get (), nullptr});
    }
  QFile file {source};
  QVERIFY (file.open (QIODevice::WriteOnly));
  QCOMPARE (file.write (contents), static_cast<qint64> (contents.size ()));
  file.close ();
  WorkedBefore worked {configuration_.get ()};
  QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
  QTRY_COMPARE (finished.count (), 1);
  int expected = primary || accepted ? entries : 0;
  QCOMPARE (finished.at (0).at (0).toInt (), expected);
  QCOMPARE (worked.load_errors ().isEmpty (), accepted);
  QCOMPARE (worked.call_worked ("K1ABC", "FT8", "20M"), expected > 0);
  if (!accepted)
    {
      QVERIFY (worked.load_errors ().contains (QDir::toNativeSeparators (source)));
      QVERIFY (worked.load_status ().contains (primary && entries ? "1 partially recovered" : "1 failed"));
    }
}

void TestWorkedBeforeAdif::primary_rescan_recovers_records_and_preserves_live_qsos ()
{
  auto const primary = configuration_->writeable_data_dir ().filePath ("wsjtx_log.adi");
  auto write = [&] (QByteArray const& contents) {
    QFile file {primary};
    return file.open (QIODevice::WriteOnly) && file.write (contents) == contents.size ();
  };
  QVERIFY (write ("<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<EOR>"));
  WorkedBefore worked {configuration_.get ()};
  QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
  QTRY_COMPARE (finished.count (), 1);
  QVERIFY (worked.call_worked ("K1ABC", {}, {}));
  QVERIFY (write ("<CALL:5>W1XYZ<BAND:3>20M<MODE:3>FT8<EOR><CALL:5>N2AAA"));
  worked.reload ();
  QVERIFY (QThreadPool::globalInstance ()->waitForDone (5000));
  QVERIFY (worked.add ("K3NEW", "FN20", "20M", "FT8", "<CALL:5>K3NEW<BAND:3>20M<MODE:3>FT8"));
  QTRY_COMPARE (finished.count (), 2);
  QCOMPARE (finished.at (1).at (0).toInt (), 2);
  QVERIFY (!worked.call_worked ("K1ABC", {}, {}));
  QVERIFY (worked.call_worked ("W1XYZ", "FT8", "20M"));
  QVERIFY (worked.call_worked ("K3NEW", "FT8", "20M"));
  QVERIFY (!worked.call_worked ("N2AAA", {}, {}));
  QVERIFY (worked.load_errors ().contains (QDir::toNativeSeparators (primary)));
  QVERIFY (worked.load_status ().contains ("1 partially recovered"));
}

void TestWorkedBeforeAdif::comment_cannot_supply_a_missing_band ()
{
  QFile file {configuration_->writeable_data_dir ().filePath ("wsjtx_log.adi")};
  QVERIFY (file.open (QIODevice::WriteOnly));
  QByteArray const contents {"<CALL:5>K1ABC<COMMENT:10><BAND:999><MODE:3>FT8<EOR>"};
  QCOMPARE (file.write (contents), static_cast<qint64> (contents.size ()));
  file.close ();
  WorkedBefore worked {configuration_.get ()};
  int count {0};
  QString error;
  QVERIFY2 (wait_for_load (worked, count, error), qPrintable (error));
  QCOMPARE (count, 1);
  QVERIFY (worked.call_worked ("K1ABC", "FT8", {}));
  QVERIFY (!worked.call_worked ("K1ABC", "FT8", "20M"));
}

QTEST_MAIN (TestWorkedBeforeAdif)

#include "test_worked_before_adif.moc"
