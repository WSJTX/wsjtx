#include "wsjtx_config.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QThreadPool>
#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QTimer>
#include <memory>

#include "Configuration.cpp"
#include "logbook/WorkedBefore.hpp"
#include "logbook/AdifQso.hpp"
#include "logbook/logbook.h"
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
  std::unique_ptr<QSettings> supplemental_settings_;
  std::unique_ptr<Configuration> supplemental_configuration_;

private Q_SLOTS:
  void initTestCase ();
  void cleanupTestCase ();
  void init ();
  void jtty_round_trips_with_current_header ();
  void imports_records_across_layouts_and_length_errors_data ();
  void imports_records_across_layouts_and_length_errors ();
  void reports_incomplete_sources_data ();
  void reports_incomplete_sources ();
  void primary_rescan_recovers_records_and_preserves_live_qsos ();
  void comment_cannot_supply_a_missing_band ();
  void country_views_survive_loading_and_live_additions ();
  void logbook_changes_country_view_without_rescanning ();

private:
  QString data_directory () const;
  bool wait_for_load (WorkedBefore& worked_before, int& record_count, QString& error);
  bool write_data_file (QString const& name, QByteArray const& contents);
  bool write_country_views ();
  bool set_wae_enabled (Configuration const&, QWidget& parent, bool enabled);
};

QString TestWorkedBeforeAdif::data_directory () const
{
  return QStandardPaths::writableLocation (QStandardPaths::DataLocation);
}

bool TestWorkedBeforeAdif::write_data_file (QString const& name, QByteArray const& contents)
{
  QFile file {QDir {data_directory ()}.filePath (name)};
  return file.open (QIODevice::WriteOnly | QIODevice::Text) && file.write (contents) == contents.size ();
}

bool TestWorkedBeforeAdif::write_country_views ()
{
  return write_data_file ("cty.dat", QByteArray {
    "Scotland:14:27:EU:56:4:0:GM\nGM,2M,=G0FBJ;\n"
    "Shetland Islands:14:27:EU:60:1:0:*GM/s\n=G0FBJ,=2M0BDR;\n"
    "WAE Only:15:28:EU:42:-21:-1:*Z6\nZ6;\n"});
}

bool TestWorkedBeforeAdif::set_wae_enabled (Configuration const& configuration, QWidget& parent, bool enabled)
{
  QDialog * dialog {};
  for (auto * candidate : parent.findChildren<QDialog *> ())
    {
      if (candidate->findChild<QCheckBox *> ("include_WAE_check_box"))
        {
          dialog = candidate;
          break;
        }
    }
  if (!dialog) return false;
  dialog->findChild<QCheckBox *> ("include_WAE_check_box")->setChecked (enabled);
  auto ensure_option = [dialog] (char const * object_name, char const * option) {
    if (auto * combo = dialog->findChild<QComboBox *> (object_name);
        combo && combo->currentIndex () < 0) combo->addItem (option);
  };
  ensure_option ("sound_input_combo_box", "Test input");
  ensure_option ("sound_input_channel_combo_box", "Mono");
  ensure_option ("sound_output_combo_box", "Test output");
  QTimer dismiss_message;
  dismiss_message.setSingleShot (true);
  connect (&dismiss_message, &QTimer::timeout, [&parent] {
    for (auto * message : parent.findChildren<QMessageBox *> ()) message->accept ();
  });
  dismiss_message.start (1000);
  auto const invoked = QMetaObject::invokeMethod (dialog, "accept", Qt::DirectConnection);
  return invoked && configuration.include_WAE_entities () == enabled;
}

void TestWorkedBeforeAdif::initTestCase ()
{
  register_types ();
  QStandardPaths::setTestModeEnabled (true);
  QCoreApplication::setOrganizationName ("WSJT-X");
  QCoreApplication::setApplicationName ("test_worked_before_adif");
  QVERIFY (settings_dir_.isValid ());

  QVERIFY (QDir {}.mkpath (data_directory ()));
  QVERIFY (write_data_file ("cty.dat", QByteArray {"Test Entity:5:8:NA:37:-96:-5:K\nK,W;\n"}));
  settings_.reset (new QSettings {settings_dir_.filePath ("settings.ini"), QSettings::IniFormat});
  settings_->clear ();
  configuration_.reset (new Configuration {&network_, QDir {settings_dir_.path ()}, settings_.get (), nullptr});
  supplemental_settings_.reset (new QSettings {settings_dir_.filePath ("supplemental-settings.ini"), QSettings::IniFormat});
  supplemental_settings_->clear ();
  supplemental_settings_->setValue ("Configuration/SupplementalHistoryFiles", QStringList {settings_dir_.filePath ("station.adi")});
  supplemental_configuration_.reset (new Configuration {&network_, QDir {settings_dir_.path ()}, supplemental_settings_.get (), nullptr});
}

void TestWorkedBeforeAdif::cleanupTestCase ()
{
  supplemental_configuration_.reset ();
  configuration_.reset ();
  supplemental_settings_.reset ();
  settings_.reset ();
  QVERIFY (QDir {data_directory ()}.removeRecursively ());
}

void TestWorkedBeforeAdif::init ()
{
  for (auto const& path : {QDir {data_directory ()}.filePath ("wsjtx_log.adi"), settings_dir_.filePath ("station.adi")})
    if (QFile::exists (path)) QVERIFY (QFile::remove (path));
  QVERIFY (write_data_file ("cty.dat", QByteArray {"Test Entity:5:8:NA:37:-96:-5:K\nK,W;\n"}));
  QVERIFY (configuration_->supplemental_history_files ().isEmpty ());
  QCOMPARE (supplemental_configuration_->supplemental_history_files (), QStringList {settings_dir_.filePath ("station.adi")});
  QVERIFY (!configuration_->include_WAE_entities ());
  QVERIFY (!supplemental_configuration_->include_WAE_entities ());
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

  QTest::newRow ("character-counted-comment-containing-eor")
    << (QString {"<CALL:5>K1ABC<COMMENT:9>"} + QString (4, QChar {0x00e9})
        + "<EOR><BAND:3>20M<MODE:3>FT8<EOR>").toUtf8 ()
    << QStringList {"K1ABC"};

  for (auto const& tag : {"<eOr>", "<eOh>", "<BAND:999:S>", "<BAND:3>40M"})
    for (int count : {1, 2, 3, 4})
      for (bool byte_counted : {false, true})
        {
          auto value = QString (count, QChar {0x00e9}) + tag;
          auto length = byte_counted ? value.toUtf8 ().size () : value.size ();
          auto row = QString {"unicode-comment-%1-%2-%3"}.arg (tag).arg (count)
            .arg (byte_counted ? "bytes" : "characters");
          QTest::newRow (qPrintable (row))
            << (QString {"<CALL:5>K1ABC<COMMENT:%1:S>%2<BAND:3>20M<MODE:3>FT8<EOR>"}
                .arg (length).arg (value).toUtf8 () + record ("W1XYZ").toUtf8 ())
            << QStringList {"K1ABC", "W1XYZ"};
        }

  auto unicode_header = QString (4, QChar {0x00e9}) + "<EOH>";
  for (bool byte_counted : {false, true})
    for (bool preamble : {false, true})
      {
        auto row = QString {"unicode-header-%1-%2"}.arg (byte_counted ? "bytes" : "characters")
          .arg (preamble ? "preamble" : "field-first");
        QTest::newRow (qPrintable (row))
          << ((preamble ? QByteArray {"ADIF Export"} : QByteArray {})
              + QString {"<PROGRAMID:%1>%2<EOH>"}
                .arg (byte_counted ? unicode_header.toUtf8 ().size () : unicode_header.size ())
                .arg (unicode_header).toUtf8 () + record ("K1ABC").toUtf8 ())
          << QStringList {"K1ABC"};
      }

  QTest::newRow ("unicode-malformed-length-crosses-tags-in-both-interpretations")
    << ((QString {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:14>"}
         + QString (4, QChar {0x00e9}) + "oops<EOR>").toUtf8 () + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};
  QTest::newRow ("maximum-length-recovers-without-overflow")
    << (QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:18446744073709551615>oops<EOR>"}
        + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};
  QTest::newRow ("unicode-malformed-length-beyond-eof")
    << (QString {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:12>"}
        + QString (4, QChar {0x00e9}) + "<EOR>").toUtf8 ()
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
  auto unicode_split_record = QString {"<CALL:5>K1ABC<COMMENT:12>"} + QString (4, QChar {0x00e9})
    + "<EOR>abc<BAND:3>20M<MODE:3>FT8<EOR>";
  auto embedded_eor = unicode_split_record.indexOf ("<EOR>");
  for (int offset : {-1, 0, 1, 4, 5, 6, 8, 9})
    {
      QTest::newRow (qPrintable (QString {"character-counted-eor-crosses-chunk-%1"}.arg (offset)))
        << (QByteArray (chunk_size - embedded_eor - offset, ' ') + unicode_split_record.toUtf8 ()
            + record ("W1XYZ").toUtf8 ())
        << QStringList {"K1ABC", "W1XYZ"};
    }
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
  QFile file {source};
  QVERIFY (file.open (QIODevice::WriteOnly));
  QCOMPARE (file.write (contents), static_cast<qint64> (contents.size ()));
  file.close ();
  WorkedBefore worked {primary ? configuration_.get () : supplemental_configuration_.get ()};
  QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
  QVERIFY (finished.count () || finished.wait (5000));
  QCOMPARE (finished.count (), 1);
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
  QVERIFY (finished.count () >= 1 || finished.wait (5000));
  QCOMPARE (finished.count (), 1);
  QVERIFY (worked.call_worked ("K1ABC", {}, {}));
  QVERIFY (write ("<CALL:5>W1XYZ<BAND:3>20M<MODE:3>FT8<EOR><CALL:5>N2AAA"));
  worked.reload ();
  QVERIFY (QThreadPool::globalInstance ()->waitForDone (5000));
  QVERIFY (worked.add ("K3NEW", "FN20", "20M", "FT8", "<CALL:5>K3NEW<BAND:3>20M<MODE:3>FT8"));
  QVERIFY (finished.count () >= 2 || finished.wait (5000));
  QCOMPARE (finished.count (), 2);
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

void TestWorkedBeforeAdif::country_views_survive_loading_and_live_additions ()
{
  using View = AD1CCty::EntityView;
  QVERIFY (write_country_views ());
  QVERIFY (write_data_file ("wsjtx_log.adi", QByteArray {
    "<CALL:6>2M0BDR<BAND:3>20M<MODE:3>FT8<EOR>\n"
    "<CALL:5>Z61AA<BAND:3>40M<MODE:3>FT4<EOR>\n"}));
  WorkedBefore worked {configuration_.get ()};
  QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
  QVERIFY (finished.count () >= 1 || finished.wait (5000));
  QCOMPARE (finished.count (), 1);
  QCOMPARE (finished.at (0).at (0).toInt (), 2);
  auto const shetland = worked.countries ()->lookup ("2M0BDR");
  auto const wae_only = worked.countries ()->lookup ("Z61AA");
  QVERIFY (wae_only.dxcc.name.isEmpty ());
  for (auto view : {View::DXCC, View::WAE, View::DXCC})
    {
      auto const country = shetland.identity (view).name;
      QCOMPARE (country, view == View::DXCC ? QString {"Scotland"} : QString {"Shetland Islands"});
      QVERIFY (worked.country_worked (country, "ft8", "20m", view));
      QVERIFY (worked.country_worked (country, "ft8", {}, view));
      QVERIFY (worked.country_worked (country, {}, "20m", view));
      QVERIFY (worked.country_worked (country, {}, {}, view));
      QVERIFY (!worked.country_worked (country, "FT4", "20M", view));
      QVERIFY (!worked.country_worked (country, "FT8", "40M", view));
      QVERIFY (worked.country_worked (wae_only.identity (view).name, "FT4", "40M", view));
    }
  QCOMPARE (finished.count (), 1);
  QVERIFY (!worked.country_worked ("Scotland", {}, {}, View::WAE));
  QVERIFY (!worked.country_worked ("Shetland Islands", {}, {}, View::DXCC));
  QVERIFY (worked.continent_worked (AD1CCty::Continent::EU, "FT8", "20M"));
  QVERIFY (worked.CQ_zone_worked (14, "FT8", "20M"));
  QVERIFY (worked.ITU_zone_worked (27, "FT8", "20M"));

  worked.reload ();
  QVERIFY (QThreadPool::globalInstance ()->waitForDone (5000));
  // The loader has read the file, but its result has not reached the GUI thread.
  QVERIFY (worked.add ("G0FBJ", "IO99", "15M", "FT4", "<CALL:5>G0FBJ<BAND:3>15M<MODE:3>FT4"));
  QVERIFY (finished.count () >= 2 || finished.wait (5000));
  QCOMPARE (finished.count (), 2);
  QCOMPARE (finished.at (1).at (0).toInt (), 3);
  QVERIFY (worked.country_worked ("Scotland", "FT4", "15M", View::DXCC));
  QVERIFY (worked.country_worked ("Shetland Islands", "FT4", "15M", View::WAE));
  worked.reload ();
  QVERIFY (finished.count () >= 3 || finished.wait (5000));
  QCOMPARE (finished.count (), 3);
  QCOMPARE (finished.at (2).at (0).toInt (), 3);
  QVERIFY (worked.country_worked ("Scotland", "FT4", "15M", View::DXCC));
  QVERIFY (worked.country_worked ("Shetland Islands", "FT4", "15M", View::WAE));
}

void TestWorkedBeforeAdif::logbook_changes_country_view_without_rescanning ()
{
  QVERIFY (write_country_views ());
  QVERIFY (write_data_file ("wsjtx_log.adi", QByteArray {
    "<CALL:6>2M0BDR<BAND:3>20M<MODE:3>FT8<EOR>\n"
    "<CALL:5>Z61AA<BAND:3>20M<MODE:3>FT8<EOR>\n"}));
  QSettings settings {settings_dir_.filePath ("wae-settings.ini"), QSettings::IniFormat};
  settings.clear ();
  QWidget parent;
  Configuration configuration {&network_, QDir {settings_dir_.path ()}, &settings, nullptr, &parent};
  LogBook logbook {&configuration};
  QSignalSpy finished {&logbook, &LogBook::finished_loading};
  QSignalSpy started {&logbook, &LogBook::loading_started};
  QVERIFY (finished.count () >= 1 || finished.wait (5000));
  QCOMPARE (finished.count (), 1);
  auto country_worked = [&] (QString const& call) {
    bool call_b4, country_b4, grid_b4, continent_b4, cq_b4, itu_b4;
    logbook.match (call, "FT8", {}, logbook.countries ()->lookup (call),
                   call_b4, country_b4, grid_b4, continent_b4, cq_b4, itu_b4, "20M");
    return country_b4;
  };
  QVERIFY (!configuration.include_WAE_entities ());
  QVERIFY (country_worked ("GM4ABC"));
  QVERIFY (country_worked ("G0FBJ"));
  QVERIFY (country_worked ("Z62BB"));
  QVERIFY (set_wae_enabled (configuration, parent, true));
  QVERIFY (!country_worked ("GM4ABC"));
  QVERIFY (country_worked ("G0FBJ"));
  QVERIFY (country_worked ("Z62BB"));
  QVERIFY (set_wae_enabled (configuration, parent, false));
  QVERIFY (country_worked ("GM4ABC"));
  QVERIFY (country_worked ("Z62BB"));
  QCOMPARE (started.count (), 0);
  QCOMPARE (finished.count (), 1);
}

QTEST_MAIN (TestWorkedBeforeAdif)

#include "test_worked_before_adif.moc"
