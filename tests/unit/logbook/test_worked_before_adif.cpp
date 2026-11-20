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
#include <atomic>
#include <functional>
#include <QSemaphore>
#include <QRunnable>

#include "Configuration.cpp"
#include "logbook/WorkedBefore.hpp"
#include "logbook/AdifQso.hpp"
#include "logbook/logbook.h"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () {return 0;}

namespace
{
  std::function<void (char const *, std::atomic_bool const&)> history_checkpoint;

  class ScopedHistoryCheckpoints final
  {
  public:
    ~ScopedHistoryCheckpoints () {history_checkpoint = {};}
  };
}

// Compile the loader here so checkpoint observers are absent from the production library.
#define WSJT_WORKED_BEFORE_LOAD_CHECKPOINT(point, stop) \
  do {if (history_checkpoint) history_checkpoint (point, stop);} while (false)
#if defined (__GNUC__) && !defined (__clang__)
// The included implementation keeps its future result type local to this translation unit.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#endif
#include "logbook/WorkedBefore.cpp"
#if defined (__GNUC__) && !defined (__clang__)
#pragma GCC diagnostic pop
#endif
#undef WSJT_WORKED_BEFORE_LOAD_CHECKPOINT

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
  void suspect_length_recovery_tradeoffs_data ();
  void suspect_length_recovery_tradeoffs ();
  void recovers_damaged_records_data ();
  void recovers_damaged_records ();
  void recovery_reports_partial_contacts_and_continues ();
  void unicode_recovery_prefers_bytes_across_read_chunks ();
  void unicode_embedded_eor_retains_following_fields ();
  void unused_field_endpoint_inside_used_value ();
  void damaged_appends_publish_recovery_status ();
  void reports_incomplete_sources_data ();
  void reports_incomplete_sources ();
  void primary_rescan_recovers_records_and_preserves_live_qsos ();
  void comment_cannot_supply_a_missing_band ();
  void call_worked_on_days_uses_every_qso_date ();
  void added_qso_carries_its_date ();
  void qmap_contest_views_from_a_history_log ();
  void country_views_survive_loading_and_live_additions ();
  void logbook_changes_country_view_without_rescanning ();
  void cancels_worker_without_partial_results_data ();
  void cancels_worker_without_partial_results ();
  void shutdown_during_scan_preserves_published_history ();
  void destruction_cancels_running_scan_data ();
  void destruction_cancels_running_scan ();
  void shutdown_discards_queued_completion ();
  void shutdown_during_completion_prevents_restart ();
  void destruction_cancels_queued_scan ();
  void instances_sharing_a_log_see_each_others_qsos ();
  void change_polling_can_be_disabled ();
  void foreign_and_own_appends_between_checks_are_both_seen ();
  void appended_records_are_read_without_a_full_reload ();
  void appended_records_carry_their_qso_dates ();
  void a_replaced_log_triggers_a_full_reload ();
  void erasing_the_log_removes_file_and_history ();
  void a_half_written_record_is_picked_up_once_completed ();
  void a_very_large_addition_is_loaded_by_a_full_reload ();
  void a_relative_log_path_is_made_absolute ();
  void own_appends_do_not_trigger_a_rescan ();
  void history_source_changes_are_picked_up ();
  void concurrent_appends_stay_whole_records ();
  void chosen_log_location_receives_new_qsos ();

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
  QTest::addColumn<bool> ("incomplete");
  QTest::addColumn<QByteArray> ("adif");
  auto add_row = [] (char const * name, bool incomplete = false) -> QTestData& {return QTest::newRow (name) << incomplete;};
  QTest::addColumn<QStringList> ("expected_calls");

  auto record = [] (QString const& call) {
    return QString {"<CALL:%1>%2<BAND:3>20M<MODE:3>FT8<GRIDSQUARE:4>FN42<EOR>"}
      .arg (call.size ()).arg (call);
  };
  auto call = [] (QString const& value) {return value.toUtf8 ();};

  add_row ("leading-whitespace")
    << QByteArray {" \r\n\t" + record ("K1ABC").toUtf8 ()} << QStringList {"K1ABC"};
  add_row ("headerless-single-line-no-trailing-newline")
    << call (record ("K1ABC")) << QStringList {"K1ABC"};
  add_row ("multiline-record")
    << QByteArray {"<CALL:5>K1ABC\n\n<BAND:3>20M\n\n<MODE:3>FT8\n<GRIDSQUARE:4>FN42\n<EOR>"}
    << QStringList {"K1ABC"};
  add_row ("multiple-records-on-one-line")
    << (record ("K1ABC") + record ("W1XYZ")).toUtf8 () << QStringList {"K1ABC", "W1XYZ"};
  add_row ("optional-header")
    << QByteArray {"ADIF Export\n<adif_ver:5>3.1.1\n<eoh>\n" + record ("K1ABC").toUtf8 ()}
    << QStringList {"K1ABC"};
  for (auto const& preamble : {"Exported by Foo <http://foo.example>", "Exported < by Foo", "Exported <http://foo.example> < by Foo"})
    add_row (qPrintable (QString {"header-free-text-%1"}.arg (preamble)))
      << (QByteArray {preamble} + "<EOH>" + record ("K1ABC").toUtf8 ())
      << QStringList {"K1ABC"};
  add_row ("leading-whitespace-optional-header")
    << QByteArray {" \n<adif_ver:5>3.1.1<eoh>\n" + record ("K1ABC").toUtf8 ()}
    << QStringList {"K1ABC"};
  add_row ("eoh-text-inside-length-delimited-header-field")
    << QByteArray {"ADIF Export\n<programid:5><EOH>\n<eoh>\n" + record ("K1ABC").toUtf8 ()}
    << QStringList {"K1ABC"};
  add_row ("final-record-with-trailing-newline")
    << (record ("K1ABC") + "\n\n" + record ("W1XYZ") + "\n").toUtf8 ()
    << QStringList {"K1ABC", "W1XYZ"};
  add_row ("final-record-without-trailing-newline")
    << (record ("K1ABC") + "\n\n" + record ("W1XYZ")).toUtf8 ()
    << QStringList {"K1ABC", "W1XYZ"};
  add_row ("eor-text-inside-length-delimited-field")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:8:M>a\r\n<EOR><BAND:3>20M<MODE:3>FT8<GRIDSQUARE:4>FN42<EOR>\n"}
    << QStringList {"K1ABC"};

  auto oversize_record = QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:9999>oops<EOR>\r\n"};
  add_row ("oversize-length-first-record", true)
    << (oversize_record + record ("W1XYZ").toUtf8 ()) << QStringList {"K1ABC", "W1XYZ"};
  add_row ("oversize-length-middle-record", true)
    << (record ("N2AAA").toUtf8 () + oversize_record + record ("W1XYZ").toUtf8 ())
    << QStringList {"N2AAA", "K1ABC", "W1XYZ"};

  auto slightly_long_record = QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:30>oops<EOR>\r\n"
                                          "<CALL:5>W1XYZ<BAND:3>20M<MODE:3>FT8<EOR>\r\n"};
  add_row ("slightly-long-length", true)
    << (slightly_long_record + record ("N2AAA").toUtf8 ()) << QStringList {"K1ABC", "W1XYZ", "N2AAA"};

  add_row ("suspect-endpoint-at-tag-closing-bracket", true)
    << (QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:8>oops<EOR>"} + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};

  for (auto const& field : {"CALL", "BAND", "MODE", "SUBMODE", "GRIDSQUARE"})
    {
      auto text = QString {"<%1:999>"}.arg (field).toUtf8 ();
      auto adif = QByteArray {"<COMMENT:"} + QByteArray::number (text.size ()) + ":S>" + text
        + record ("K1ABC").toUtf8 ();
      add_row (qPrintable (QString {"descriptor-inside-comment-%1"}.arg (field)))
        << adif << QStringList {"K1ABC"};
    }
  add_row ("fake-band-after-real-call")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:10><BAND:999><BAND:3>20M<MODE:3>FT8<EOR>"}
    << QStringList {"K1ABC"};

  add_row ("character-counted-comment-containing-eor")
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
          add_row (qPrintable (row))
            << (QString {"<CALL:5>K1ABC<COMMENT:%1:S>%2<BAND:3>20M<MODE:3>FT8<EOR>"}
                .arg (length).arg (value).toUtf8 () + record ("W1XYZ").toUtf8 ())
            << QStringList {"K1ABC", "W1XYZ"};
        }


  add_row ("character-counted-comment-with-malformed-looking-tag")
    << (QString {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:20>"}
        + QString (10, QChar {0x00e9}) + "<BAND:bad><EOR>").toUtf8 ()
    << QStringList {"K1ABC"};
  for (int padding : {0, 64 * 1024 - 40, 64 * 1024 - 20})
    add_row (qPrintable (QString {"character-counted-comment-suspect-byte-endpoint-%1"}.arg (padding)))
      << (QByteArray (padding, ' ') + (QString {"<CALL:5>K1ABC<COMMENT:20>"}
          + QString (4, QChar {0x00e9}) + "<EOR>abcdefghijk<BAND:3>20M<MODE:3>FT8<EOR>").toUtf8 ())
      << QStringList {"K1ABC"};
  add_row ("character-counted-header-with-embedded-eor")
    << (QString {"ADIF Export<PROGRAMID:10>"} + QString (5, QChar {0x00e9})
        + "<EOR><EOH>" + record ("K1ABC")).toUtf8 ()
    << QStringList {"K1ABC"};
  auto unicode_header = QString (4, QChar {0x00e9}) + "<EOH>";
  for (bool byte_counted : {false, true})
    for (bool preamble : {false, true})
      {
        auto row = QString {"unicode-header-%1-%2"}.arg (byte_counted ? "bytes" : "characters")
          .arg (preamble ? "preamble" : "field-first");
        add_row (qPrintable (row))
          << ((preamble ? QByteArray {"ADIF Export"} : QByteArray {})
              + QString {"<PROGRAMID:%1>%2<EOH>"}
                .arg (byte_counted ? unicode_header.toUtf8 ().size () : unicode_header.size ())
                .arg (unicode_header).toUtf8 () + record ("K1ABC").toUtf8 ())
          << QStringList {"K1ABC"};
      }

  add_row ("unicode-malformed-length-crosses-tags-in-both-interpretations", true)
    << ((QString {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:14>"}
         + QString (4, QChar {0x00e9}) + "oops<EOR>").toUtf8 () + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};
  add_row ("maximum-length-recovers-without-overflow", true)
    << (QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:18446744073709551615>oops<EOR>"}
        + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};
  add_row ("unicode-malformed-length-beyond-eof", true)
    << (QString {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:12>"}
        + QString (4, QChar {0x00e9}) + "<EOR>").toUtf8 ()
    << QStringList {"K1ABC"};

  auto unicode_comment = QString (5, QChar {0x00e9}) + "<BAND:999>";
  add_row ("character-counted-comment-containing-descriptor")
    << (QString {"<COMMENT:%1>%2"}.arg (unicode_comment.size ()).arg (unicode_comment).toUtf8 ()
        + record ("K1ABC").toUtf8 ()) << QStringList {"K1ABC"};
  auto name = QString {"Jos"} + QChar {0x00e9};
  auto name_record = QString {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<NAME:%1>%2<EOR>"};
  auto utf8_bom = QByteArray::fromHex ("efbbbf");
  add_row ("utf8-byte-counted-name")
    << (utf8_bom + name_record.arg (name.toUtf8 ().size ()).arg (name).toUtf8 () + record ("W1XYZ").toUtf8 ())
    << QStringList {"K1ABC", "W1XYZ"};
  add_row ("utf8-character-counted-name")
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
          add_row (qPrintable (row))
            << (record ("W1XYZ").toUtf8 () + final_record) << QStringList {"W1XYZ", "K1ABC"};
        }
  auto split_name = name_record.arg (10).arg (QString (5, QChar {0x00e9})).toUtf8 ();
  auto name_start = split_name.indexOf ("<NAME:10>") + QByteArray {"<NAME:10>"}.size ();
  add_row ("utf8-name-crosses-read-chunk")
    << (QByteArray (64 * 1024 - name_start - 2, ' ') + split_name)
    << QStringList {"K1ABC"};

  add_row ("embedded-eor-with-text-between-fields")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:5><EOR> padding<BAND:3>20M<MODE:3>FT8<GRIDSQUARE:4>FN42<EOR>"}
    << QStringList {"K1ABC"};
  add_row ("literal-angle-text-between-fields")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:9>literal x<literal>padding<BAND:3>20M<MODE:3>FT8<EOR>"}
    << QStringList {"K1ABC"};
  add_row ("embedded-eoh-with-text-between-fields")
    << (QByteArray {"ADIF Export<PROGRAMID:5><EOH>padding<EOH>"} + record ("K1ABC").toUtf8 ())
    << QStringList {"K1ABC"};
  add_row ("oversize-length-header-field", true)
    << (QByteArray {"ADIF Export<PROGRAMID:9999>oops<EOH>"} + record ("K1ABC").toUtf8 ())
    << QStringList {"K1ABC"};

  constexpr int chunk_size {64 * 1024};
  auto unicode_split_record = QString {"<CALL:5>K1ABC<COMMENT:12>"} + QString (4, QChar {0x00e9})
    + "<EOR>abc<BAND:3>20M<MODE:3>FT8<EOR>";
  auto embedded_eor = unicode_split_record.indexOf ("<EOR>");
  for (int offset : {-1, 0, 1, 4, 5, 6, 8, 9})
    {
      add_row (qPrintable (QString {"character-counted-eor-crosses-chunk-%1"}.arg (offset)))
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
  add_row ("suspect-endpoint-in-split-field-tag", true)
    << split_endpoint_tag (typed_tag_record, 30) << QStringList {"K1ABC", "W1XYZ"};
  auto short_record = QByteArray {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<COMMENT:5>oops<eOr>"};
  add_row ("suspect-endpoint-in-split-eor", true)
    << split_endpoint_tag (short_record + record ("W1XYZ").toUtf8 (), 5)
    << QStringList {"K1ABC", "W1XYZ"};

  auto long_value = QByteArray {"<EOR>"} + QByteArray (chunk_size + 17, 'x') + "<EOR>";
  add_row ("valid-field-longer-than-chunk")
    << (QByteArray {"<CALL:5>K1ABC<COMMENT:"} + QByteArray::number (long_value.size ()) + ">" + long_value
        + " padding<BAND:3>20M<MODE:3>FT8<EOR>" + record ("W1XYZ").toUtf8 ())
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
  QFETCH (bool, incomplete);
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
  QSignalSpy finished {&worked_before, &WorkedBefore::finished_loading};
  QVERIFY (finished.count () || finished.wait (5000));
  QCOMPARE (finished.count (), 1);
  record_count = finished.at (0).at (0).toInt ();
  error = finished.at (0).at (2).toString ();
  QCOMPARE (error.isEmpty (), !incomplete);
  QCOMPARE (record_count, expected_calls.size ());

  for (auto const& call : expected_calls)
    {
      QVERIFY2 (worked_before.call_worked (call, "FT8", "20M"), qPrintable (call));
    }
  QVERIFY (!worked_before.call_worked ("N0NOT", "FT8", "20M"));
}


void TestWorkedBeforeAdif::suspect_length_recovery_tradeoffs_data ()
{
  QTest::addColumn<QByteArray> ("adif");
  QTest::addColumn<int> ("expected_contacts");

  QTest::newRow ("embedded-eor-with-unseparated-padding")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:5><EOR>padding<BAND:3>20M<MODE:3>FT8<GRIDSQUARE:4>FN42<EOR>"}
    << 2;
  QTest::newRow ("declared-endpoint-inside-literal-text")
    << QByteArray {"<CALL:5>K1ABC<COMMENT:9><EOR>abc<literal>padding<BAND:3>20M<MODE:3>FT8<EOR>"}
    << 2;
  auto long_value = QByteArray {"<EOR>"} + QByteArray (64 * 1024 + 17, 'x') + "<EOR>";
  QTest::newRow ("long-field-with-unseparated-padding")
    << (QByteArray {"<CALL:5>K1ABC<COMMENT:"} + QByteArray::number (long_value.size ()) + ">" + long_value
        + "padding<BAND:3>20M<MODE:3>FT8<EOR><CALL:5>W1XYZ<BAND:3>20M<MODE:3>FT8<GRIDSQUARE:4>FN42<EOR>")
    << 3;
}

void TestWorkedBeforeAdif::suspect_length_recovery_tradeoffs ()
{
  QFETCH (QByteArray, adif);
  QFETCH (int, expected_contacts);
  adif += "<CALL:5>N2AAA<BAND:3>40M<MODE:3>FT4<GRIDSQUARE:4>EM00<EOR>";
  AD1CCty prefixes {configuration_.get ()};
  prefixes.reload (configuration_.get ());
  std::atomic_bool const stop {false};
  auto result = loader ("fixture.adi", &prefixes, 0, stop, &adif);

  // Suspect-length recovery can expose a payload's EOR even when its declared
  // length is correct. Keep this compatibility cost explicit alongside recovery.
  QCOMPARE (result.status, source_status::Warnings);
  QVERIFY (!result.error.isEmpty ());
  QVERIFY (!result.stopped);
  QCOMPARE (result.unfinished_tails, quint64 {0});
  QCOMPARE (result.imported_records, quint64 (expected_contacts));
  QCOMPARE (int (result.worked.size ()), expected_contacts);
  bool first_found {false};
  bool following_found {false};
  for (auto const& entry : result.worked)
    {
      if (entry.call_ == "K1ABC")
        {
          first_found = true;
          QVERIFY (entry.band_.isEmpty ());
          QVERIFY (entry.mode_.isEmpty ());
          QVERIFY (entry.grid_.isEmpty ());
        }
      else if (entry.call_ == "N2AAA")
        {
          following_found = true;
          QCOMPARE (entry.band_, QString {"40M"});
          QCOMPARE (entry.mode_, QString {"FT4"});
          QCOMPARE (entry.grid_, QString {"EM00"});
        }
      else
        {
          QCOMPARE (entry.call_, QString {"W1XYZ"});
          QCOMPARE (entry.band_, QString {"20M"});
          QCOMPARE (entry.mode_, QString {"FT8"});
          QCOMPARE (entry.grid_, QString {"FN42"});
        }
    }
  QVERIFY (first_found);
  QVERIFY (following_found);
}

void TestWorkedBeforeAdif::recovers_damaged_records_data ()
{
  QTest::addColumn<QByteArray> ("damage");
  QTest::addColumn<bool> ("supplemental");
  QTest::addColumn<QStringList> ("values");
  auto row = [] (char const * name, QByteArray const& damage, QStringList const& values) {
    for (bool supplemental : {false, true})
      QTest::newRow (qPrintable (QString {"%1-%2"}.arg (name).arg (supplemental ? "supplemental" : "primary")))
        << damage << supplemental << values;
  };
  QByteArray const base {"<CALL:5>W1XYZ<BAND:3>40M<MODE:3>FT4<GRIDSQUARE:4>EM00<QSO_DATE:8>20261114<EOR>"};
  QStringList const values {"W1XYZ", "40M", "FT4", "EM00", "20261114"};
  QStringList const fields {"CALL", "BAND", "MODE", "GRIDSQUARE", "QSO_DATE"};
  for (int i = 0; i < fields.size (); ++i)
    {
      auto descriptor = '<' + fields[i].toUtf8 () + ':' + QByteArray::number (values[i].size ());
      auto damaged = base;
      damaged.replace (descriptor + '>', '<' + fields[i].toUtf8 () + ":bad>");
      auto expected = values;
      expected[i].clear ();
      row (qPrintable ("bad-length-" + fields[i]), damaged, expected);
      damaged = base;
      damaged.replace (descriptor + '>', descriptor + ":BAD>");
      row (qPrintable ("bad-type-" + fields[i]), damaged, values);
    }
  row ("missing-closing-bracket", "<CALL:5>W1XYZ<GRIDSQUARE:4FN42<BAND:3>40M<MODE:3>FT4<EOR>", {"W1XYZ", "40M", "FT4", "", ""});
  row ("missing-eor", "<CALL:5>W1XYZ<MODE:3>FT4<CALL:5>K3BAD<BAND:3>40M<GRIDSQUARE:4>EM00<QSO_DATE:8>20261114<EOR>", {"W1XYZ", "", "FT4", "", ""});
  row ("unreadable-first-call", "<CALL:bad>K3BAD<CALL:5>W1XYZ<BAND:3>40M<MODE:3>FT4<EOR>", {"", "", "", "", ""});
  row ("sparse-after-damage", "<CALL:bad>K3BAD<EOR><CALL:5>W1XYZ<FREQ:6>14.074<EOR>", {"W1XYZ", "", "", "", ""});
  row ("later-readable-band", "<CALL:5>W1XYZ<BAND:bad>oops<BAND:3>40M<MODE:3>FT4<EOR>", {"W1XYZ", "40M", "FT4", "", ""});
  row ("conflicting-band", "<CALL:5>W1XYZ<BAND:3>40M<BAND:3>20M<MODE:3>FT4<EOR>", {"W1XYZ", "40M", "FT4", "", ""});
  row ("bad-submode", "<CALL:5>W1XYZ<MODE:4>MFSK<SUBMODE:bad>FT4<EOR>", {"W1XYZ", "", "", "", ""});
  row ("readable-submode", "<CALL:5>W1XYZ<MODE:bad>oops<SUBMODE:3>FT4<EOR>", {"W1XYZ", "", "FT4", "", ""});
  row ("unknown-fields", "<CALL:5>W1XYZ<APP_FOO:><note: text><http://foo.example><EOR>", {"W1XYZ", "", "", "", ""});
  row ("opaque-comment-during-recovery", "<CALL:5>W1XYZ<BAND:bad>oops<COMMENT:45><EOR><CALL:5>K3BAD<BAND:3>40M<MODE:3>FT4<EOR><EOR>", {"W1XYZ", "", "", "", ""});
  row ("too-long-band", "<CALL:5>W1XYZ<BAND:12>40M<MODE:3>FT4<EOR>", {"W1XYZ", "", "FT4", "", ""});
  row ("too-short-band", "<CALL:5>W1XYZ<BAND:2>40M<MODE:3>FT4<EOR>", {"W1XYZ", "", "FT4", "", ""});
  row ("too-short-call", "<CALL:3>W1XYZ<BAND:3>40M<MODE:3>FT4<EOR>", {"", "40M", "FT4", "", ""});
  row ("suspect-comment-endpoint", "<CALL:5>W1XYZ<COMMENT:6><EOR>abc<MODE:3>FT4<EOR>", {"W1XYZ", "", "", "", ""});
}

void TestWorkedBeforeAdif::recovers_damaged_records ()
{
  QFETCH (QByteArray, damage);
  QFETCH (bool, supplemental);
  QFETCH (QStringList, values);
  QByteArray before {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<EOR>\r\n"};
  QByteArray after {"<MODE:3>FT8<BAND:3>20M<CALL:5>N2AAA<EOR>"};
  auto const path = supplemental ? settings_dir_.filePath ("station.adi")
    : configuration_->writeable_data_dir ().filePath ("wsjtx_log.adi");
  for (bool first : {false, true})
    {
      auto contents = (first ? QByteArray {} : before) + damage + "\r\n" + after;
      QFile file {path};
      QVERIFY (file.open (QIODevice::WriteOnly | QIODevice::Truncate));
      QCOMPARE (file.write (contents), static_cast<qint64> (contents.size ()));
      file.close ();
      WorkedBefore worked {supplemental ? supplemental_configuration_.get () : configuration_.get ()};
      QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
      QVERIFY (finished.count () || finished.wait (5000));
      QCOMPARE (finished.count (), 1);
      bool const retained = !values[0].isEmpty ();
      QCOMPARE (finished.at (0).at (0).toInt (), (first ? 1 : 2) + int (retained));
      QCOMPARE (worked.call_worked ("K1ABC", "FT8", "20M"), !first);
      QVERIFY (worked.call_worked ("N2AAA", "FT8", "20M"));
      QCOMPARE (worked.call_worked ("W1XYZ", {}, {}), retained);
      QCOMPARE (worked.call_worked ("W1XYZ", {}, "40M"), retained && !values[1].isEmpty ());
      QCOMPARE (worked.call_worked ("W1XYZ", "FT4", {}), retained && !values[2].isEmpty ());
      QCOMPARE (worked.grid_worked ("EM00", {}, {}), retained && !values[3].isEmpty ());
      QCOMPARE (worked.country_worked ("Test Entity", "FT4", "40M", AD1CCty::EntityView::DXCC),
                retained && values[1] == "40M" && values[2] == "FT4");
      QCOMPARE (worked.call_worked_on_days ("W1XYZ", {}, {}, QSet<QDate> {QDate {2026, 11, 14}}), retained && !values[4].isEmpty ());
      QVERIFY (!worked.call_worked ("K3BAD", {}, {}));
      QVERIFY (worked.load_errors ().contains (QDir::toNativeSeparators (path)));
      QVERIFY (worked.load_errors ().contains ("Line"));
      QVERIFY (worked.load_status ().contains ("loaded with warnings"));
      QVERIFY (file.open (QIODevice::ReadOnly));
      QCOMPARE (file.readAll (), contents);
    }
}

void TestWorkedBeforeAdif::recovery_reports_partial_contacts_and_continues ()
{
  AD1CCty prefixes {configuration_.get ()};
  prefixes.reload (configuration_.get ());
  std::atomic_bool const stop {false};
  QByteArray good {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<EOR>\r\n"};
  QByteArray bad {"<CALL:5>W1XYZ<BAND:bad>40M<MODE:3>FT4<EOR>\r\n"};
  QByteArray contents;
  for (int i = 0; i != 2000; ++i) contents += good;
  for (int i = 0; i != 25; ++i) contents += bad;
  contents += good;
  auto result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.imported_records, quint64 {2026});
  QCOMPARE (result.partial_records, quint64 {25});
  QCOMPARE (result.rejected_records, quint64 {0});
  QCOMPARE (result.diagnostic_count, quint64 {25});
  QCOMPARE (result.diagnostics.size (), 20);
  QCOMPARE (result.diagnostics.first ().line, qint64 {2001});
  QVERIFY (result.error.contains ("5 additional diagnostics omitted"));
  QCOMPARE (result.status, source_status::Warnings);
  QVERIFY (!result.stopped);
  contents.clear ();
  for (int i = 0; i != 30000; ++i) contents += bad;
  contents += good;
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.imported_records, quint64 {30001});
  QCOMPARE (result.partial_records, quint64 {30000});
  QVERIFY (!result.stopped);
  contents = good + "<CALL:bad>W1XYZ<EOR>" + QByteArray (1024 * 1024 + 1, ' ') + "<CALL:5>N2AAA<EOR>";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.imported_records, quint64 {2});
  QCOMPARE (result.rejected_records, quint64 {1});
  contents = "<CALL:5>K1ABC<COMMENT:1048577>" + QByteArray (1024 * 1024 + 1, 'x') + "<EOR>";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.status, source_status::Complete);
  QCOMPARE (result.imported_records, quint64 {1});
  contents = good + "<CALL:5>W1XYZ<COMMENT:%1 off>oops<EOR>";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.imported_records, quint64 {2});
  QVERIFY (result.error.contains ("<COMMENT:%1 off>"));
  QVERIFY (!result.error.contains ("%5"));
  contents = good + "<CALL:5>W1XYZ";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.status, source_status::Incomplete);
  QCOMPARE (result.unfinished_tails, quint64 {1});
  QVERIFY (!result.stopped);
  QCOMPARE (result.imported_records, quint64 {1});
  contents = "<call:5>K1ABC<band:3>20m<comment:40>short note<EOR>\n<call:5>W1XYZ<band:3>40m<mode:3>FT4<EOR>";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.imported_records, quint64 {1});
  QCOMPARE (result.status, source_status::Complete);
  QCOMPARE (result.worked.begin ()->call_, QString {"K1ABC"});
  contents = (QString {"<CALL:5>K1ABC<COMMENT:20>"} + QString (10, QChar {0x00e9})
              + "<BAND:bad><GRIDSQUARE:bad>oops<EOR>").toUtf8 ();
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.imported_records, quint64 {1});
  QCOMPARE (result.diagnostic_count, quint64 {1});
  QCOMPARE (result.diagnostics.first ().field, QString {"GRIDSQUARE"});
  contents = "<CALL:5>K1ABC<COMMENT:18446744073709551615>oops<EOR><CALL:5>W1XYZ<EOR>";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.imported_records, quint64 {2});
  QCOMPARE (result.status, source_status::Warnings);
  contents = "<CALL:5>K1ABC<BAND:bad>oops<BAND:3>40M<EOR>";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.partial_records, quint64 {0});
  QCOMPARE (result.worked.begin ()->band_, QString {"40M"});
  contents = "<CALL:999999>K3BAD<EOR><CALL:5>W1XYZ<BAND:999999>40M<MODE:3>FT4<EOR>";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents, true);
  QCOMPARE (result.status, source_status::Warnings);
  QCOMPARE (result.imported_records, quint64 {1});
  QCOMPARE (result.rejected_records, quint64 {1});
  QCOMPARE (result.unfinished_tails, quint64 {0});
  QCOMPARE (result.worked.begin ()->band_, QString {});
  contents = "<CALL:0><EOR>";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.status, source_status::Warnings);
  QCOMPARE (result.rejected_records, quint64 {1});
  QCOMPARE (result.imported_records, quint64 {0});
  for (auto const& preamble : {"Exported < by Foo", "Exported by <http://foo.example>"})
    {
      contents = QByteArray {preamble} + "<PROGRAMID:bad>oops<EOH><CALL:5>K1ABC<EOR>";
      result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
      QCOMPARE (result.status, source_status::Warnings);
      QCOMPARE (result.imported_records, quint64 {1});
      QCOMPARE (result.diagnostic_count, quint64 {1});
    }
  contents = "<CALL:5>K1ABC<COMMENT:2>long prose<MODE:3:Z>FT8<BAND:3>20M<BAND:3>20M<EOR>";
  result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
  QCOMPARE (result.status, source_status::Complete);
  QCOMPARE (result.imported_records, quint64 {1});
}

void TestWorkedBeforeAdif::unused_field_endpoint_inside_used_value ()
{
  AD1CCty prefixes {configuration_.get ()};
  prefixes.reload (configuration_.get ());
  std::atomic_bool const stop {false};
  for (int padding : {0, 64 * 1024 - 39, 64 * 1024 - 41})
    {
      QByteArray contents = QByteArray (padding, ' ')
        + "<CALL:5>W1XYZ<COMMENT:15>short<BAND:3>40M<MODE:3>FT4<EOR>";
      auto result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
      QCOMPARE (result.status, source_status::Warnings);
      QCOMPARE (result.imported_records, quint64 {1});
      QCOMPARE (result.diagnostic_count, quint64 {1});
      QCOMPARE (result.diagnostics.first ().field, QString {"COMMENT"});
      QCOMPARE (result.worked.begin ()->band_, QString {"40M"});
      QCOMPARE (result.worked.begin ()->mode_, QString {"FT4"});
      contents = QByteArray (padding, ' ') + (QString {"<CALL:5>W1XYZ<COMMENT:23>"}
        + QString (4, QChar {0x00e9}) + "short<BAND:3>40M<MODE:3>FT4<EOR>").toUtf8 ();
      result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
      QCOMPARE (result.status, source_status::Warnings);
      QCOMPARE (result.worked.begin ()->band_, QString {"40M"});
      for (bool characters : {false, true})
        {
          // The byte endpoint cuts the quoted BAND value; character counting
          // consumes the entire legitimate comment and must take precedence.
          auto payload = QString (4, QChar {0x00e9}) + "<BAND:3>40M xy";
          auto text = QString {"<CALL:5>W1XYZ<COMMENT:%1>%2<MODE:3>FT4<EOR>"}
            .arg (characters ? payload.size () : payload.toUtf8 ().size ()).arg (payload);
          contents = QByteArray (padding, ' ') + text.toUtf8 ();
          result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
          QCOMPARE (result.status, source_status::Complete);
          QCOMPARE (result.imported_records, quint64 {1});
          QVERIFY (result.worked.begin ()->band_.isEmpty ());
        }
    }
  for (auto const& comment : {"<COMMENT:16>short<BAND:3>40M", "<COMMENT:15>short<NOTE:3>40M",
                             "<COMMENT:16>short<BAND:3>40M prose"})
    {
      QByteArray contents = QByteArray {"<CALL:5>W1XYZ"} + comment + "<MODE:3>FT4<EOR>";
      auto result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
      QCOMPARE (result.status, source_status::Complete);
      QCOMPARE (result.imported_records, quint64 {1});
      QVERIFY (result.worked.begin ()->band_.isEmpty ());
    }
}

void TestWorkedBeforeAdif::unicode_embedded_eor_retains_following_fields ()
{
  AD1CCty prefixes {configuration_.get ()};
  prefixes.reload (configuration_.get ());
  std::atomic_bool const stop {false};
  for (int length : {8, 9})
    {
      auto text = QString {"<CALL:4>W3SZ<NOTES:%1>"}.arg (length)
        + QChar {0x00e9} + "x<EOR>y<MODE:3>FT8<EOR>"
        + "<CALL:5>N2AAA<BAND:3>40M<MODE:3>FT4<EOR>";
      for (int padding : {0, 64 * 1024 - text.indexOf ("<EOR>") - 3})
        {
          auto contents = (QString (padding, ' ') + text).toUtf8 ();
          auto result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
          QCOMPARE (result.status, source_status::Complete);
          QCOMPARE (result.imported_records, quint64 {2});
          QCOMPARE (result.worked.size (), std::size_t {2});
          for (auto const& entry : result.worked)
            {
              if (entry.call_ == "W3SZ")
                QCOMPARE (entry.mode_, QString {"FT8"});
              else
                {
                  QCOMPARE (entry.call_, QString {"N2AAA"});
                  QCOMPARE (entry.band_, QString {"40M"});
                  QCOMPARE (entry.mode_, QString {"FT4"});
                }
            }
        }
    }
}

void TestWorkedBeforeAdif::unicode_recovery_prefers_bytes_across_read_chunks ()
{
  AD1CCty prefixes {configuration_.get ()};
  prefixes.reload (configuration_.get ());
  std::atomic_bool const stop {false};
  QString const candidate = QString {"<CALL:5>W1XYZ<BAND:3>20M<MODE:3>FT8<COMMENT:10>"}
    + QString (5, QChar {0x00e9}) + "<EOR>";
  QString const damage {"<CALL:bad>K3BAD<EOR>"};
  for (int offset : {-1, 0, 1})
    {
      // Both interpretations are valid, but the character-counted value
      // includes the byte-counted record's closing delimiter.
      auto contents = (QString (64 * 1024 - damage.size () - candidate.size () + offset, ' ')
                       + damage + candidate + "<EOR>").toUtf8 ();
      auto result = loader ("fixture.adi", &prefixes, 0, stop, &contents);
      QVERIFY (!result.stopped);
      QCOMPARE (result.imported_records, quint64 {1});
      QCOMPARE (result.rejected_records, quint64 {1});
    }
}

void TestWorkedBeforeAdif::damaged_appends_publish_recovery_status ()
{
  QVERIFY (write_data_file ("wsjtx_log.adi", "<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<EOR>\n"));
  WorkedBefore worked {configuration_.get ()};
  QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
  QVERIFY (finished.count () || finished.wait (5000));
  worked.set_source_poll_interval (25);
  QFile file {worked.path ()};
  QVERIFY (file.open (QIODevice::WriteOnly | QIODevice::Append));
  QByteArray appended {"<CALL:bad>W1XYZ<EOR>\n<CALL:5>N2AAA<BAND:3>20M<MODE:3>FT8<EOR>\n"};
  QCOMPARE (file.write (appended), static_cast<qint64> (appended.size ()));
  file.close ();
  QTRY_COMPARE_WITH_TIMEOUT (finished.count (), 2, 5000);
  QVERIFY (worked.call_worked ("K1ABC", "FT8", "20M"));
  QVERIFY (worked.call_worked ("N2AAA", "FT8", "20M"));
  QVERIFY (!worked.call_worked ("W1XYZ", {}, {}));
  QVERIFY (worked.load_errors ().contains ("Line 2"));
  QVERIFY (worked.load_errors ().contains ("Contact skipped"));
  QVERIFY (worked.load_status ().contains ("loaded with warnings"));
  worked.set_source_poll_interval (0);
  QVERIFY (QThreadPool::globalInstance ()->waitForDone (5000));
  QHash<QString, file_signature> known;
  known.insert (worked.path (), signature_of (worked.path (), true));
  QCOMPARE (check_sources ({worked.path ()}, known).what, poll_result::action::None);
  QSignalSpy started {&worked, &WorkedBefore::loading_started};
  QVERIFY (file.open (QIODevice::WriteOnly | QIODevice::Append));
  file.write ("<CALL:5>K3NEW<BAND:3>40M<MODE:3>FT4<EOR>\n");
  file.close ();
  QCOMPARE (check_sources ({worked.path ()}, known).what, poll_result::action::Append);
  worked.set_source_poll_interval (25);
  QTRY_VERIFY_WITH_TIMEOUT (worked.call_worked ("K3NEW", "FT4", "40M"), 5000);
  QCOMPARE (started.count (), 0);
  QCOMPARE (finished.count (), 2);
  QVERIFY (worked.load_errors ().contains ("Line 2"));
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
  row ("invalid-field-type", "<call:5:BAD>K1ABC<eor>", false, 1);
  row ("empty-field-type", "<call:5:>K1ABC<eor>", false, 1);
  row ("extra-field-colon", "<call:5:S:S>K1ABC<eor>", false, 1);
  row ("truncated-field", "<call:100>K1ABC<eor>", false, 0);
  row ("unterminated-record", "<call:5>K1ABC<band:3>20m<mode:3>FT8", false, 0);
  QByteArray complete {"<call:5>K1ABC<band:3>20m<mode:3>FT8<eor>"};
  row ("unterminated-record-after-valid-record", complete + "<call:5>W1XYZ<band:3>20m<mode:3>FT8", false, 1);
  row ("bad-field-after-valid-record", complete + "<call:bad>W1XYZ<eor>", false, 1);
  row ("bad-band-after-valid-record", complete + "<call:5>W1XYZ<band:bad>20m<eor>", false, 2);
  row ("bad-field-before-another-valid-record", complete + "<call:bad>W1XYZ<eor><call:5>N2AAA<band:3>20m<mode:3>FT8<eor>", false, 2);
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
  int expected = entries;
  QCOMPARE (finished.at (0).at (0).toInt (), expected);
  QCOMPARE (worked.load_errors ().isEmpty (), accepted);
  QCOMPARE (worked.call_worked ("K1ABC", {}, {}), expected > 0);
  if (!accepted)
    {
      QVERIFY (worked.load_errors ().contains (QDir::toNativeSeparators (source)));
      QVERIFY (worked.load_status ().contains (contents.contains ("not an ADIF") || !contents.endsWith ("<eor>") ? "Incomplete history" : "loaded with warnings"));
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
  QVERIFY (worked.load_status ().contains ("1 incomplete"));
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

void TestWorkedBeforeAdif::call_worked_on_days_uses_every_qso_date ()
{
  // A contest over two weekends: 2026-10-31/11-01 and 2026-11-28/29.
  // Primary log: K1ABC long before, W2UYH in weekend 1, K1ABC on 13cm in
  // weekend 1, W4NOD without a date, K3XYZ old here and again BETWEEN the
  // weekends in the supplemental file (not a contest QSO), W5TWO in
  // weekend 2 and also before the contest (two dates on one entry).
  QVERIFY (write_data_file ("wsjtx_log.adi", QByteArray {
    "<CALL:5>K1ABC<BAND:4>23cm<MODE:4>MFSK<SUBMODE:3>Q65<QSO_DATE:8>20251109<EOR>\n"
    "<CALL:5>W2UYH<BAND:4>23cm<MODE:4>MFSK<SUBMODE:3>Q65<QSO_DATE:8>20261031<EOR>\n"
    "<CALL:5>K1ABC<BAND:4>13cm<MODE:3>FT8<QSO_DATE:8>20261101<EOR>\n"
    "<CALL:5>W4NOD<BAND:4>23cm<MODE:4>MFSK<SUBMODE:3>Q65<EOR>\n"
    "<CALL:5>K3XYZ<BAND:4>23cm<MODE:4>MFSK<SUBMODE:3>Q65<QSO_DATE:8>20250101<EOR>\n"
    "<CALL:5>W5TWO<BAND:4>23cm<MODE:4>MFSK<SUBMODE:3>Q65<QSO_DATE:8>20261129<EOR>\n"
    "<CALL:5>W5TWO<BAND:4>23cm<MODE:4>MFSK<SUBMODE:3>Q65<QSO_DATE:8>20260601<EOR>\n"}));
  {
    QFile station {settings_dir_.filePath ("station.adi")};
    QVERIFY (station.open (QIODevice::WriteOnly));
    QByteArray const between {"<CALL:5>K3XYZ<BAND:4>23cm<MODE:4>MFSK<SUBMODE:3>Q65<QSO_DATE:8>20261105<EOR>\n"};
    QCOMPARE (station.write (between), static_cast<qint64> (between.size ()));
  }
  WorkedBefore worked {supplemental_configuration_.get ()};
  int count {0};
  QString error;
  QVERIFY2 (wait_for_load (worked, count, error), qPrintable (error));

  QSet<QDate> const any;
  QSet<QDate> const contest {QDate {2026, 10, 31}, QDate {2026, 11, 1},
                             QDate {2026, 11, 28}, QDate {2026, 11, 29}};
  // No days: every QSO counts, as call_worked.
  for (auto const& call : {"K1ABC", "W2UYH", "W4NOD", "K3XYZ", "W5TWO"})
    QVERIFY2 (worked.call_worked_on_days (call, {}, "23cm", any), call);
  QVERIFY (!worked.call_worked_on_days ("N0NOT", {}, "23cm", any));
  // Contest days: only QSOs on those days, on that band.
  QVERIFY (!worked.call_worked_on_days ("K1ABC", {}, "23cm", contest));
  QVERIFY (worked.call_worked_on_days ("K1ABC", {}, "13cm", contest));
  QVERIFY (worked.call_worked_on_days ("K1ABC", {}, {}, contest));
  QVERIFY (worked.call_worked_on_days ("W2UYH", {}, "23CM", contest));
  QVERIFY (worked.call_worked_on_days ("W2UYH", "Q65", "23cm", contest));
  QVERIFY (!worked.call_worked_on_days ("W2UYH", "FT8", "23cm", contest));
  QVERIFY (!worked.call_worked_on_days ("W2UYH", {}, "23cm", QSet<QDate> {QDate {2026, 11, 1}}));
  // A QSO without QSO_DATE does not count once days are chosen.
  QVERIFY (!worked.call_worked_on_days ("W4NOD", {}, "23cm", contest));
  // Worked between the weekends only: still wanted in the contest.
  QVERIFY (!worked.call_worked_on_days ("K3XYZ", {}, "23cm", contest));
  QVERIFY (worked.call_worked_on_days ("K3XYZ", {}, "23cm", QSet<QDate> {QDate {2026, 11, 5}}));
  QVERIFY (worked.call_worked_on_days ("K3XYZ", {}, "23cm", QSet<QDate> {QDate {2025, 1, 1}}));
  // One entry with two dates: each date counts.
  QVERIFY (worked.call_worked_on_days ("W5TWO", {}, "23cm", contest));
  QVERIFY (worked.call_worked_on_days ("W5TWO", {}, "23cm", QSet<QDate> {QDate {2026, 6, 1}}));
  QVERIFY (!worked.call_worked_on_days ("W5TWO", {}, "23cm", QSet<QDate> {QDate {2026, 10, 31}}));

  // Different calls worked: each call once, whatever its bands and modes.
  QCOMPARE (worked.calls_worked_on_days ("23cm", any), 5);     // K1ABC K3XYZ W2UYH W4NOD W5TWO
  QCOMPARE (worked.calls_worked_on_days ("23CM", contest), 2); // W2UYH, W5TWO
  QCOMPARE (worked.calls_worked_on_days ({}, contest), 3);     // + K1ABC on 13cm
  QCOMPARE (worked.calls_worked_on_days ({}, any), 5);         // K1ABC counted once
  QCOMPARE (worked.calls_worked_on_days ("13cm", any), 1);
  QCOMPARE (worked.calls_worked_on_days ("13cm", QSet<QDate> {QDate {2026, 11, 28}}), 0);
  QCOMPARE (worked.calls_worked_on_days ("70cm", any), 0);

  // LogBook's band query (any mode) gives the same answers.
  LogBook logbook {supplemental_configuration_.get ()};
  QSignalSpy finished {&logbook, &LogBook::finished_loading};
  QVERIFY (finished.count () >= 1 || finished.wait (5000));
  QVERIFY (!logbook.call_worked_on_band ("K1ABC", "23cm", contest));
  QVERIFY (logbook.call_worked_on_band ("K1ABC", "23cm", any));
  QVERIFY (logbook.call_worked_on_band ("W2UYH", "23cm", contest));
  QVERIFY (!logbook.call_worked_on_band ("K3XYZ", "23cm", contest));
  QVERIFY (!logbook.call_worked_on_band ("W2UYH", "13cm", any));
  QCOMPARE (logbook.calls_worked_on_band ("23cm", contest), 2);
}

void TestWorkedBeforeAdif::qmap_contest_views_from_a_history_log ()
{
  // The 23cm contest log from another PC, added as a history source; the
  // primary log is empty. Five QMAP decodes: VK7MO, G3LTF and K1JT never
  // worked, KA1GT worked a year before the contest, K2UYH in it.
  QVERIFY (write_data_file ("wsjtx_log.adi", {}));
  {
    QFile contest_log {settings_dir_.filePath ("station.adi")};
    QVERIFY (contest_log.open (QIODevice::WriteOnly));
    QByteArray const records {
      "<CALL:5>KA1GT <GRIDSQUARE:4>FN54 <MODE:4>MFSK <SUBMODE:3>Q65 <QSO_DATE:8>20251109 "
      "<TIME_ON:6>034415 <BAND:4>23cm <FREQ:11>1296.061495 <EOR>\n"
      "<CALL:5>K2UYH <GRIDSQUARE:4>FN20 <MODE:4>MFSK <SUBMODE:3>Q65 <QSO_DATE:8>20261031 "
      "<TIME_ON:6>072030 <BAND:4>23cm <FREQ:11>1296.060995 <EOR>\n"};
    QCOMPARE (contest_log.write (records), static_cast<qint64> (records.size ()));
  }
  LogBook logbook {supplemental_configuration_.get ()};
  QSignalSpy finished {&logbook, &LogBook::finished_loading};
  QVERIFY (finished.count () >= 1 || finished.wait (5000));
  QVERIFY2 (finished.at (0).at (2).toString ().isEmpty (), qPrintable (finished.at (0).at (2).toString ()));

  QStringList const decoded {"VK7MO", "KA1GT", "G3LTF", "K1JT", "K2UYH"};
  auto wanted = [&] (QSet<QDate> const& days) {
    QStringList result;
    for (auto const& call : decoded)
      if (!logbook.call_worked_on_band (call, "23cm", days)) result << call;
    return result;
  };
  // No days: # (and Wanted only) on the three never worked.
  QCOMPARE (wanted ({}), (QStringList {"VK7MO", "G3LTF", "K1JT"}));
  QCOMPARE (logbook.calls_worked_on_band ("23cm", {}), 2);
  // Contest days 2026-10-31/11-01: KA1GT is wanted again, K2UYH stays worked.
  QSet<QDate> const contest {QDate {2026, 10, 31}, QDate {2026, 11, 1}};
  QCOMPARE (wanted (contest), (QStringList {"VK7MO", "KA1GT", "G3LTF", "K1JT"}));
  QCOMPARE (logbook.calls_worked_on_band ("23cm", contest), 1);
  // Not worked on another band.
  for (auto const& call : decoded)
    QVERIFY2 (!logbook.call_worked_on_band (call, "13cm", {}), qPrintable (call));
}

void TestWorkedBeforeAdif::added_qso_carries_its_date ()
{
  QVERIFY (write_data_file ("wsjtx_log.adi", QByteArray {
    "<CALL:5>K1ABC<BAND:4>23cm<MODE:4>MFSK<SUBMODE:3>Q65<GRIDSQUARE:4>FN42<QSO_DATE:8>20251109<EOR>\n"}));
  WorkedBefore worked {configuration_.get ()};
  int count {0};
  QString error;
  QVERIFY2 (wait_for_load (worked, count, error), qPrintable (error));
  QSet<QDate> const contest {QDate {2026, 11, 7}, QDate {2026, 11, 8}};
  auto on = [] (QDate const& day) {return QSet<QDate> {day};};
  QVERIFY (!worked.call_worked_on_days ("K1ABC", {}, "23cm", contest));

  auto const time = QDateTime {QDate {2026, 11, 7}, QTime {6, 12}, Qt::UTC};
  auto record = [&] (QString const& call) {
    return AdifQso::to_adif (call, "FN42", "Q65", "-20", "-21", time, time, "23cm", {}, {},
                             "1296.100000", "W1AW", "FN31", {}, {}, {}, {}, {}, {}, {}, {},
                             AdifQso::Contest::None);
  };
  // Worked again in the contest: the existing entry now carries both dates.
  QVERIFY (worked.add ("K1ABC", "FN42", "23cm", "Q65", record ("K1ABC")));
  QVERIFY (worked.call_worked_on_days ("K1ABC", {}, "23cm", contest));
  QVERIFY (worked.call_worked_on_days ("K1ABC", {}, "23cm", on (QDate {2025, 11, 9})));
  QVERIFY (worked.add ("W5NEW", "EM12", "23cm", "Q65", record ("W5NEW")));
  QVERIFY (worked.call_worked_on_days ("W5NEW", {}, "23cm", on (QDate {2026, 11, 7})));
  QVERIFY (!worked.call_worked_on_days ("W5NEW", {}, "23cm", on (QDate {2026, 11, 8})));
  // A record without QSO_DATE is taken as logged today (UTC).
  QVERIFY (worked.add ("W6NOW", "DM12", "23cm", "Q65", "<CALL:5>W6NOW<BAND:4>23cm"));
  QVERIFY (worked.call_worked_on_days ("W6NOW", {}, "23cm", on (QDateTime::currentDateTimeUtc ().date ())));

  // The dates survive a rescan of the written log.
  worked.reload ();
  QVERIFY2 (wait_for_load (worked, count, error), qPrintable (error));
  QVERIFY (worked.call_worked_on_days ("K1ABC", {}, "23cm", contest));
  QVERIFY (worked.call_worked_on_days ("K1ABC", {}, "23cm", on (QDate {2025, 11, 9})));
  QVERIFY (worked.call_worked_on_days ("W5NEW", {}, "23cm", on (QDate {2026, 11, 7})));
  QVERIFY (!worked.call_worked_on_days ("W5NEW", {}, "23cm", on (QDate {2026, 11, 8})));
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

void TestWorkedBeforeAdif::cancels_worker_without_partial_results_data ()
{
  QTest::addColumn<QByteArray> ("primary");
  QTest::addColumn<QByteArray> ("supplemental");
  QTest::addColumn<QByteArray> ("checkpoint");
  QTest::addColumn<int> ("occurrence");
  QTest::addColumn<bool> ("recursive");
  QByteArray const record {"<CALL:5>K1ABC<BAND:3>20M<MODE:3>FT8<EOR>"};
  auto row = [&] (char const * name, char const * checkpoint, int occurrence,
                  QByteArray primary, QByteArray supplemental = {}, bool recursive = false) {
    QTest::newRow (name) << primary << supplemental << QByteArray {checkpoint} << occurrence << recursive;
  };
  row ("before-discovery", "load_start", 1, record);
  row ("nested-directory", "directory", 2, record, {}, true);
  row ("directory-entry", "directory_entry", 1, record, {}, true);
  row ("chunk-read", "read_complete", 2, QByteArray (64 * 1024, ' ') + record);
  row ("long-header", "read", 2, QByteArray {"ADIF Export"} + QByteArray (64 * 1024, 'x'));
  row ("whitespace-loop", "whitespace", 2, QByteArray (8192, ' ') + record);
  row ("length-loop", "field_length", 2,
       QByteArray {"<COMMENT:"} + QByteArray (8192, '0') + "1>x<EOH>" + record);
  row ("field-data-loop", "field_data", 2,
       QByteArray {"<COMMENT:8192>"} + QByteArray (8192, 'x') + "<EOH>" + record);
  row ("unicode-fallback", "field_characters", 1,
       record.left (record.size () - 5) + "<COMMENT:12>" + QString (4, QChar {0x00e9}).toUtf8 () + "<EOR>");
  row ("between-records", "record", 2, record + record);
  row ("recovery", "recovery", 1, record + "<CALL:bad>W1XYZ<EOR>" + record);
  QByteArray supplemental;
  for (int i = 0; i != 257; ++i)
    supplemental += QString {"<CALL:6>K%1<BAND:3>20M<MODE:3>FT8<EOR>"}.arg (10000 + i).toUtf8 ();
  row ("merge-loop", "merge", 2, record, supplemental);
  row ("after-source-error", "file", 3, record, record);
  row ("before-publication", "load_complete", 1, record, record);
}

void TestWorkedBeforeAdif::cancels_worker_without_partial_results ()
{
  QFETCH (QByteArray, primary);
  QFETCH (QByteArray, supplemental);
  QFETCH (QByteArray, checkpoint);
  QFETCH (int, occurrence);
  QFETCH (bool, recursive);
  QVERIFY (write_data_file ("wsjtx_log.adi", primary));
  auto const station = settings_dir_.filePath ("station.adi");
  {
    QFile file {station};
    QVERIFY (file.open (QIODevice::WriteOnly));
    QCOMPARE (file.write (supplemental), static_cast<qint64> (supplemental.size ()));
  }
  HistoryFolders folders;
  QTemporaryDir tree;
  if (recursive)
    {
      QVERIFY (tree.isValid ());
      QVERIFY (QDir {}.mkpath (tree.filePath ("nested")));
      folders.append ({tree.path (), true});
    }
  AD1CCty prefixes {configuration_.get ()};
  prefixes.reload (configuration_.get ());
  std::atomic_bool stop {false};
  int matching_checks {0};
  int checks_after_stop {0};
  ScopedHistoryCheckpoints checkpoints;
  history_checkpoint = [&] (char const * point, std::atomic_bool const&) {
    if (stop.load (std::memory_order_relaxed)) ++checks_after_stop;
    if (checkpoint == point && ++matching_checks == occurrence)
      stop.store (true, std::memory_order_relaxed);
  };
  auto const result = load_sources (configuration_->writeable_data_dir ().filePath ("wsjtx_log.adi"),
                                    {settings_dir_.filePath ("missing.adi"), station}, folders, &prefixes, 0, stop);
  QCOMPARE (matching_checks, occurrence);
  QCOMPARE (checks_after_stop, 0);
  QVERIFY (result.cancelled);
  QVERIFY (result.worked.empty ());
  QVERIFY (result.errors.isEmpty ());
  QVERIFY (result.folder_summaries.isEmpty ());
  QCOMPARE (result.files_read, 0);
  QCOMPARE (result.files_incomplete, 0);
  QCOMPARE (result.files_failed, 0);
  QCOMPARE (result.files_expected, 0);
  QCOMPARE (result.directory_failures, 0);
  for (auto const& file_contents : {qMakePair (configuration_->writeable_data_dir ().filePath ("wsjtx_log.adi"), primary),
                                   qMakePair (station, supplemental)})
    {
      QFile file {file_contents.first};
      QVERIFY (file.open (QIODevice::ReadOnly));
      QCOMPARE (file.readAll (), file_contents.second);
    }
}

void TestWorkedBeforeAdif::shutdown_during_scan_preserves_published_history ()
{
  QVERIFY (write_data_file ("wsjtx_log.adi", "<CALL:5>K1ABC<EOR>"));
  QSemaphore entered, release;
  std::atomic_bool blocked {false}, timed_out {false};
  ScopedHistoryCheckpoints checkpoints;
  WorkedBefore worked {configuration_.get ()};
  int count {0};
  QString error;
  QVERIFY2 (wait_for_load (worked, count, error), qPrintable (error));
  QCOMPARE (count, 1);
  QVERIFY (write_data_file ("wsjtx_log.adi", "<CALL:5>W1XYZ<EOR>"));
  history_checkpoint = [&] (char const * point, std::atomic_bool const&) {
    if (!qstrcmp (point, "stop_requested")) release.release ();
    if (!qstrcmp (point, "read") && !blocked.exchange (true))
      {
        entered.release ();
        if (!release.tryAcquire (1, 5000)) timed_out.store (true);
      }
  };
  QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
  QSignalSpy started {&worked, &WorkedBefore::loading_started};
  worked.reload ();
  QVERIFY (entered.tryAcquire (1, 5000));
  auto const status = worked.load_status ();
  worked.reload ();
  QVERIFY (QMetaObject::invokeMethod (qApp, "aboutToQuit", Qt::DirectConnection));
  worked.reload ();
  QVERIFY (QThreadPool::globalInstance ()->waitForDone (5000));
  QCoreApplication::processEvents ();
  QVERIFY (!timed_out.load ());
  QCOMPARE (finished.count (), 0);
  QCOMPARE (started.count (), 2);
  QCOMPARE (worked.load_status (), status);
  QVERIFY (worked.load_errors ().isEmpty ());
  QVERIFY (worked.call_worked ("K1ABC", {}, {}));
  QVERIFY (!worked.call_worked ("W1XYZ", {}, {}));
  QVERIFY (worked.add ("K3NEW", "FN20", "20M", "FT8", "<CALL:5>K3NEW"));
  QVERIFY (worked.call_worked ("K3NEW", "FT8", "20M"));
}

void TestWorkedBeforeAdif::destruction_cancels_running_scan_data ()
{
  QTest::addColumn<bool> ("pending_rescan");
  QTest::newRow ("active") << false;
  QTest::newRow ("pending-rescan") << true;
}

void TestWorkedBeforeAdif::destruction_cancels_running_scan ()
{
  QFETCH (bool, pending_rescan);
  struct ConcurrentScans
  {
    QThreadPool * pool {QThreadPool::globalInstance ()};
    int previous_max {pool->maxThreadCount ()};
    ConcurrentScans () { pool->setMaxThreadCount (qMax (2, previous_max)); }
    ~ConcurrentScans () { pool->setMaxThreadCount (previous_max); }
  } concurrent_scans;
  QVERIFY (write_data_file ("wsjtx_log.adi", "<CALL:5>K1ABC<EOR>"));
  QSemaphore entered, release;
  std::atomic_bool timed_out {false};
  std::atomic<int> starts {0}, files {0};
  std::atomic<std::atomic_bool const *> selected {nullptr};
  ScopedHistoryCheckpoints checkpoints;
  history_checkpoint = [&] (char const * point, std::atomic_bool const& stop) {
    if (!qstrcmp (point, "load_start")) ++starts;
    if (!qstrcmp (point, "file")) ++files;
    if (!qstrcmp (point, "stop_requested") && selected.load () == &stop) release.release ();
    if (!qstrcmp (point, "read"))
      {
        std::atomic_bool const * expected = nullptr;
        if (selected.compare_exchange_strong (expected, &stop))
          {
            entered.release ();
            if (!release.tryAcquire (1, 30000)) timed_out.store (true);
          }
      }
  };
  auto worked = std::make_unique<WorkedBefore> (configuration_.get ());
  QSignalSpy finished {worked.get (), &WorkedBefore::finished_loading};
  QVERIFY (entered.tryAcquire (1, 5000));
  if (pending_rescan) worked->reload ();
  WorkedBefore other {configuration_.get ()};
  int count {0};
  QString error;
  QVERIFY2 (wait_for_load (other, count, error), qPrintable (error));
  worked.reset ();
  QVERIFY (!timed_out.load ());
  QCOMPARE (finished.count (), 0);
  QCOMPARE (starts.load (), 2);
  QCOMPARE (files.load (), 2);
  other.reload ();
  QVERIFY2 (wait_for_load (other, count, error), qPrintable (error));
  QCOMPARE (count, 1);
  WorkedBefore fresh {configuration_.get ()};
  QVERIFY2 (wait_for_load (fresh, count, error), qPrintable (error));
  QCOMPARE (count, 1);
}

void TestWorkedBeforeAdif::shutdown_discards_queued_completion ()
{
  QVERIFY (write_data_file ("wsjtx_log.adi", "<CALL:5>K1ABC<EOR>"));
  WorkedBefore worked {configuration_.get ()};
  QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
  QSignalSpy started {&worked, &WorkedBefore::loading_started};
  QVERIFY (QThreadPool::globalInstance ()->waitForDone (5000));
  QCOMPARE (finished.count (), 0);
  worked.reload ();
  QVERIFY (QMetaObject::invokeMethod (qApp, "aboutToQuit", Qt::DirectConnection));
  QVERIFY (QMetaObject::invokeMethod (qApp, "aboutToQuit", Qt::DirectConnection));
  auto const status = worked.load_status ();
  QCoreApplication::processEvents ();
  worked.reload ();
  QCOMPARE (finished.count (), 0);
  QCOMPARE (started.count (), 1);
  QCOMPARE (worked.load_status (), status);
  QVERIFY (!worked.call_worked ("K1ABC", {}, {}));
  WorkedBefore fresh {configuration_.get ()};
  int count {0};
  QString error;
  QVERIFY2 (wait_for_load (fresh, count, error), qPrintable (error));
  QCOMPARE (count, 1);
}

void TestWorkedBeforeAdif::shutdown_during_completion_prevents_restart ()
{
  QVERIFY (write_data_file ("wsjtx_log.adi", "<CALL:5>K1ABC<EOR>"));
  WorkedBefore worked {configuration_.get ()};
  bool quit_notified {false};
  connect (&worked, &WorkedBefore::finished_loading, &worked, [&] {
    worked.reload ();
    quit_notified = QMetaObject::invokeMethod (qApp, "aboutToQuit", Qt::DirectConnection);
  });
  QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
  QSignalSpy started {&worked, &WorkedBefore::loading_started};
  QVERIFY (finished.wait (5000));
  QVERIFY (quit_notified);
  QVERIFY (QThreadPool::globalInstance ()->waitForDone (5000));
  QCoreApplication::processEvents ();
  QCOMPARE (finished.count (), 1);
  QCOMPARE (started.count (), 1);
  QVERIFY (worked.call_worked ("K1ABC", {}, {}));
}

void TestWorkedBeforeAdif::destruction_cancels_queued_scan ()
{
  class BlockedPool final
  {
    class Blocker final : public QRunnable
    {
    public:
      Blocker (QSemaphore& entered, QSemaphore& release, std::atomic_bool& timed_out)
        : entered_ {entered}, release_ {release}, timed_out_ {timed_out} {}
      void run () override
      {
        entered_.release ();
        if (!release_.tryAcquire (1, 5000)) timed_out_.store (true);
      }
    private:
      QSemaphore& entered_;
      QSemaphore& release_;
      std::atomic_bool& timed_out_;
    };
  public:
    BlockedPool () : previous_max_ {QThreadPool::globalInstance ()->maxThreadCount ()}
    {
      QThreadPool::globalInstance ()->setMaxThreadCount (1);
      QThreadPool::globalInstance ()->start (new Blocker {entered, release, timed_out});
    }
    ~BlockedPool ()
    {
      release.release ();
      QThreadPool::globalInstance ()->waitForDone ();
      QThreadPool::globalInstance ()->setMaxThreadCount (previous_max_);
    }
    QSemaphore entered, release;
    std::atomic_bool timed_out {false};
  private:
    int previous_max_;
  } pool;
  QVERIFY (pool.entered.tryAcquire (1, 5000));
  std::atomic<int> starts {0}, files {0};
  ScopedHistoryCheckpoints checkpoints;
  history_checkpoint = [&] (char const * point, std::atomic_bool const&) {
    if (!qstrcmp (point, "stop_requested")) pool.release.release ();
    if (!qstrcmp (point, "load_start")) ++starts;
    if (!qstrcmp (point, "file")) ++files;
  };
  {
    WorkedBefore worked {configuration_.get ()};
    QCOMPARE (starts.load (), 0);
    worked.reload ();
  }
  QCOMPARE (starts.load (), 1);
  QCOMPARE (files.load (), 0);
  QVERIFY (!pool.timed_out.load ());
}

namespace
{
  QByteArray shared_log_record (QByteArray const& call)
  {
    return "<call:" + QByteArray::number (call.size ()) + ">" + call
      + " <band:3>20m <mode:3>FT8 <gridsquare:4>FN42";
  }

  bool add_qso (WorkedBefore& worked_before, QByteArray const& call)
  {
    return worked_before.add (call, "FN42", "20m", "FT8", shared_log_record (call));
  }
}

void TestWorkedBeforeAdif::instances_sharing_a_log_see_each_others_qsos ()
{
  // Two instances whose primary log is the same file.
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  WorkedBefore b {supplemental_configuration_.get ()};
  QSignalSpy b_loaded {&b, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty () && !b_loaded.isEmpty (), 5000);
  QCOMPARE (a.path (), b.path ());
  a.set_source_poll_interval (50);
  b.set_source_poll_interval (50);

  QVERIFY (add_qso (a, "K1ABC"));
  QVERIFY (a.call_worked ("K1ABC", "FT8", "20M"));
  QTRY_VERIFY_WITH_TIMEOUT (b.call_worked ("K1ABC", "FT8", "20M"), 5000);

  QVERIFY (add_qso (b, "W1XYZ"));
  QTRY_VERIFY_WITH_TIMEOUT (a.call_worked ("W1XYZ", "FT8", "20M"), 5000);
  QVERIFY (a.call_worked ("K1ABC", "FT8", "20M"));
  QVERIFY (b.call_worked ("W1XYZ", "FT8", "20M"));
}

void TestWorkedBeforeAdif::change_polling_can_be_disabled ()
{
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  WorkedBefore b {supplemental_configuration_.get ()};
  QSignalSpy b_loaded {&b, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty () && !b_loaded.isEmpty (), 5000);
  a.set_source_poll_interval (0);
  b.set_source_poll_interval (50);
  QVERIFY (add_qso (a, "K1ABC"));
  QTRY_VERIFY_WITH_TIMEOUT (b.call_worked ("K1ABC", "FT8", "20M"), 5000);
  b.set_source_poll_interval (0);
  QThreadPool::globalInstance ()->waitForDone ();
  QCoreApplication::processEvents ();
  QVERIFY (add_qso (a, "W1XYZ"));
  QTest::qWait (600); // many 50 ms checks would have fitted in here
  QVERIFY (!b.call_worked ("W1XYZ", "FT8", "20M"));
  b.reload ();
  QTRY_VERIFY_WITH_TIMEOUT (b.call_worked ("W1XYZ", "FT8", "20M"), 5000);
}

void TestWorkedBeforeAdif::own_appends_do_not_trigger_a_rescan ()
{
  WorkedBefore a {configuration_.get ()};
  QSignalSpy loaded {&a, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!loaded.isEmpty (), 5000);
  ScopedHistoryCheckpoints checkpoints;
  std::atomic_int history_reads {0};
  history_checkpoint = [&history_reads] (char const * point, std::atomic_bool const&) {
    if (QByteArray {point} == "file") ++history_reads;
  };
  a.set_source_poll_interval (50);
  QSignalSpy started {&a, &WorkedBefore::loading_started};
  for (auto const* call : {"K1ABC", "W1XYZ", "N2AAA"})
    {
      QVERIFY (add_qso (a, call));
      QTest::qWait (300);
    }
  QCOMPARE (started.count (), 0);
  QCOMPARE (history_reads.load (), 0); // not even the appended tail was parsed
  QVERIFY (a.call_worked ("N2AAA", "FT8", "20M"));
}

void TestWorkedBeforeAdif::history_source_changes_are_picked_up ()
{
  // station.adi is a read-only history source of the supplemental configuration.
  WorkedBefore b {supplemental_configuration_.get ()};
  QSignalSpy loaded {&b, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!loaded.isEmpty (), 5000);
  b.set_source_poll_interval (50);
  QFile station {settings_dir_.filePath ("station.adi")};
  QVERIFY (station.open (QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text));
  station.write (shared_log_record ("K1ABC") + " <eor>\n");
  station.close ();
  QTRY_VERIFY_WITH_TIMEOUT (b.call_worked ("K1ABC", "FT8", "20M"), 5000);
  QVERIFY (QFile::remove (settings_dir_.filePath ("station.adi")));
  QVERIFY (station.open (QIODevice::WriteOnly | QIODevice::Text));
  station.write (shared_log_record ("VK2ABCDE") + " <eor>\n");
  station.close ();
  QTRY_VERIFY_WITH_TIMEOUT (b.call_worked ("VK2ABCDE", "FT8", "20M"), 5000);
  QVERIFY (!b.call_worked ("K1ABC", "FT8", "20M"));
}

void TestWorkedBeforeAdif::foreign_and_own_appends_between_checks_are_both_seen ()
{
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  WorkedBefore b {supplemental_configuration_.get ()};
  QSignalSpy b_loaded {&b, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty () && !b_loaded.isEmpty (), 5000);
  a.set_source_poll_interval (0);
  b.set_source_poll_interval (0);
  // b appends, then a appends before a has looked: a must not treat the file as its own.
  QVERIFY (add_qso (b, "W1XYZ"));
  QVERIFY (add_qso (a, "K1ABC"));
  QVERIFY (!a.call_worked ("W1XYZ", "FT8", "20M"));
  a.set_source_poll_interval (50);
  QTRY_VERIFY_WITH_TIMEOUT (a.call_worked ("W1XYZ", "FT8", "20M"), 5000);
  QVERIFY (a.call_worked ("K1ABC", "FT8", "20M"));
}

void TestWorkedBeforeAdif::appended_records_are_read_without_a_full_reload ()
{
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  WorkedBefore b {supplemental_configuration_.get ()};
  QSignalSpy b_loaded {&b, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty () && !b_loaded.isEmpty (), 5000);
  QVERIFY (add_qso (a, "N2AAA")); // gives the log a non-empty baseline
  b.reload ();
  QTRY_COMPARE_WITH_TIMEOUT (b_loaded.count (), 2, 5000);
  QSignalSpy b_started {&b, &WorkedBefore::loading_started};
  b.set_source_poll_interval (50);
  QVERIFY (add_qso (a, "K1ABC"));
  QVERIFY (add_qso (a, "W1XYZ"));
  QTRY_VERIFY_WITH_TIMEOUT (b.call_worked ("K1ABC", "FT8", "20M") && b.call_worked ("W1XYZ", "FT8", "20M"), 5000);
  QVERIFY (b.call_worked ("N2AAA", "FT8", "20M"));
  QCOMPARE (b_started.count (), 0);
}

void TestWorkedBeforeAdif::appended_records_carry_their_qso_dates ()
{
  // QSOs another instance appends to a shared log are read by the tail check
  // with their QSO_DATE, and a station worked again gains the new date.
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  WorkedBefore b {supplemental_configuration_.get ()};
  QSignalSpy b_loaded {&b, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty () && !b_loaded.isEmpty (), 5000);
  auto dated = [&] (QByteArray const& call, QByteArray const& date) {
    return a.add (call, "FN42", "20m", "FT8", shared_log_record (call) + " <qso_date:8>" + date);
  };
  QVERIFY (dated ("N2AAA", "20251109"));
  b.reload ();
  QTRY_COMPARE_WITH_TIMEOUT (b_loaded.count (), 2, 5000);
  QSet<QDate> const contest {QDate {2026, 11, 28}, QDate {2026, 11, 29}};
  QVERIFY (!b.call_worked_on_days ("N2AAA", {}, "20M", contest));
  QSignalSpy b_started {&b, &WorkedBefore::loading_started};
  b.set_source_poll_interval (50);
  QVERIFY (dated ("K1ABC", "20261128"));
  QVERIFY (dated ("N2AAA", "20261129"));
  QTRY_VERIFY_WITH_TIMEOUT (b.call_worked_on_days ("K1ABC", {}, "20M", contest)
                            && b.call_worked_on_days ("N2AAA", {}, "20M", contest), 5000);
  QVERIFY (b.call_worked_on_days ("N2AAA", {}, "20M", QSet<QDate> {QDate {2025, 11, 9}}));
  QVERIFY (!b.call_worked_on_days ("K1ABC", {}, "20M", QSet<QDate> {QDate {2026, 11, 29}}));
  QCOMPARE (b.calls_worked_on_days ("20M", contest), 2);
  QCOMPARE (b_started.count (), 0);
}

void TestWorkedBeforeAdif::a_replaced_log_triggers_a_full_reload ()
{
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  WorkedBefore b {supplemental_configuration_.get ()};
  QSignalSpy b_loaded {&b, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty () && !b_loaded.isEmpty (), 5000);
  QVERIFY (add_qso (a, "K1ABC"));
  b.reload ();
  QTRY_COMPARE_WITH_TIMEOUT (b_loaded.count (), 2, 5000);
  QVERIFY (b.call_worked ("K1ABC", "FT8", "20M"));
  QSignalSpy b_started {&b, &WorkedBefore::loading_started};
  auto const old_size = QFileInfo {a.path ()}.size ();
  b.set_source_poll_interval (50);
  // Another program replaces the log with different, larger content.
  QFile log {a.path ()};
  QVERIFY (log.open (QIODevice::WriteOnly | QIODevice::Truncate));
  log.write ("ADIF Export\n<eoh>\n" + shared_log_record ("W1XYZ") + " <comment:600>" + QByteArray (600, 'x') + " <eor>\n");
  log.close ();
  QVERIFY (QFileInfo {a.path ()}.size () > old_size);
  QTRY_VERIFY_WITH_TIMEOUT (b.call_worked ("W1XYZ", "FT8", "20M"), 5000);
  QTRY_VERIFY_WITH_TIMEOUT (!b.call_worked ("K1ABC", "FT8", "20M"), 5000);
  QVERIFY (b_started.count () >= 1);
}

void TestWorkedBeforeAdif::erasing_the_log_removes_file_and_history ()
{
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty (), 5000);
  a.set_source_poll_interval (0);
  QVERIFY (add_qso (a, "K1ABC"));
  QVERIFY (QFile::exists (a.path ()));
  QVERIFY (a.erase_log ());
  QVERIFY (!QFile::exists (a.path ()));
  QTRY_VERIFY_WITH_TIMEOUT (!a.call_worked ("K1ABC", "FT8", "20M"), 5000);
  QVERIFY (a.erase_log ()); // nothing to erase is not a failure
}

void TestWorkedBeforeAdif::a_half_written_record_is_picked_up_once_completed ()
{
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty (), 5000);
  QVERIFY (add_qso (a, "N2AAA"));
  a.reload ();
  QTRY_COMPARE_WITH_TIMEOUT (a_loaded.count (), 2, 5000);
  QSignalSpy started {&a, &WorkedBefore::loading_started};
  a.set_source_poll_interval (50);
  auto append_raw = [&a] (QByteArray const& bytes) {
    QFile log {a.path ()};
    QVERIFY (log.open (QIODevice::WriteOnly | QIODevice::Append));
    QCOMPARE (log.write (bytes), static_cast<qint64> (bytes.size ()));
  };
  QHash<QString, file_signature> known;
  known.insert (a.path (), signature_of (a.path (), true));
  append_raw ("<call:5>K1ABC <band:3>20m ");
  QCOMPARE (check_sources ({a.path ()}, known).what, poll_result::action::None);
  QVERIFY (!a.call_worked ("K1ABC", "FT8", "20M"));
  append_raw ("<mode:3>FT8 <gridsquare:4>FN42 <eor>\r\n");
  QTRY_VERIFY_WITH_TIMEOUT (a.call_worked ("K1ABC", "FT8", "20M"), 5000);
  ScopedHistoryCheckpoints checkpoints;
  std::atomic_int completed {0};
  history_checkpoint = [&] (char const * point, std::atomic_bool const&) {
    if (QByteArray {point} == "file_complete") ++completed;
  };
  // An apparent terminator is still payload in an unfinished COMMENT.
  append_raw ("<CALL:5>W1XYZ<COMMENT:20><EOR>");
  QTRY_VERIFY_WITH_TIMEOUT (completed.load () > 0, 5000);
  QVERIFY (!a.call_worked ("W1XYZ", {}, {}));
  QCOMPARE (started.count (), 0);
  QVERIFY (a.load_errors ().isEmpty ());
  append_raw (QByteArray (15, 'x') + "<BAND:3>40M<MODE:3>FT4<EOR>");
  QTRY_VERIFY_WITH_TIMEOUT (a.call_worked ("W1XYZ", "FT4", "40M"), 5000);
  QCOMPARE (started.count (), 0);
  QVERIFY (a.load_errors ().isEmpty ());
}

void TestWorkedBeforeAdif::a_very_large_addition_is_loaded_by_a_full_reload ()
{
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty (), 5000);
  QVERIFY (add_qso (a, "N2AAA"));
  a.reload ();
  QTRY_COMPARE_WITH_TIMEOUT (a_loaded.count (), 2, 5000);
  QSignalSpy started {&a, &WorkedBefore::loading_started};
  a.set_source_poll_interval (50);
  QByteArray bulk;
  while (bulk.size () < 300 * 1024) bulk += shared_log_record ("K1ABC") + " <eor>\n";
  bulk += "<CALL:5>W1XYZ";
  QFile log {a.path ()};
  QVERIFY (log.open (QIODevice::WriteOnly | QIODevice::Append));
  QCOMPARE (log.write (bulk), static_cast<qint64> (bulk.size ()));
  log.close ();
  QTRY_VERIFY_WITH_TIMEOUT (a.call_worked ("K1ABC", "FT8", "20M"), 10000);
  QVERIFY (started.count () >= 1);
  QVERIFY (!a.call_worked ("W1XYZ", {}, {}));
  QVERIFY (a.load_status ().contains ("Incomplete history"));
  QVERIFY (log.open (QIODevice::WriteOnly | QIODevice::Append));
  log.write ("<BAND:3>40M<MODE:3>FT4<EOR>");
  log.close ();
  QTRY_VERIFY_WITH_TIMEOUT (a.call_worked ("W1XYZ", "FT4", "40M"), 5000);
  QVERIFY (a.load_errors ().isEmpty ());
}

void TestWorkedBeforeAdif::a_relative_log_path_is_made_absolute ()
{
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  QSettings settings {dir.filePath ("relative.ini"), QSettings::IniFormat};
  settings.setValue ("Configuration/AdifLogFile", "some/where/../our_relative_log.adi");
  Configuration configuration {&network_, QDir {dir.path ()}, &settings, nullptr};
  // relative to the data directory, so instances started from different folders agree
  auto const expected = QDir::cleanPath (configuration.writeable_data_dir ().absoluteFilePath ("some/where/../our_relative_log.adi"));
  QCOMPARE (configuration.adif_log_file_path (), expected);
  QVERIFY (QFileInfo {configuration.adif_log_file_path ()}.isAbsolute ());
}

void TestWorkedBeforeAdif::concurrent_appends_stay_whole_records ()
{
  WorkedBefore a {configuration_.get ()};
  QSignalSpy a_loaded {&a, &WorkedBefore::finished_loading};
  WorkedBefore b {supplemental_configuration_.get ()};
  QSignalSpy b_loaded {&b, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!a_loaded.isEmpty () && !b_loaded.isEmpty (), 5000);
  a.set_source_poll_interval (0);
  b.set_source_poll_interval (0);
  constexpr int per_writer {60};
  auto writer = [] (WorkedBefore * worked_before, char tag) {
    int failures {0};
    for (int i = 0; i < per_writer; ++i)
      if (!add_qso (*worked_before, "K" + QByteArray {1, tag} + QByteArray::number (i))) ++failures;
    return failures;
  };
  auto fa = QtConcurrent::run (writer, &a, 'A');
  auto fb = QtConcurrent::run (writer, &b, 'B');
  fa.waitForFinished ();
  fb.waitForFinished ();
  QCOMPARE (fa.result (), 0);
  QCOMPARE (fb.result (), 0);

  QFile log {a.path ()};
  QVERIFY (log.open (QIODevice::ReadOnly));
  auto const raw = log.readAll ();
  auto const text = QString::fromUtf8 (raw);
#ifdef Q_OS_WIN
  QCOMPARE (raw.count ('\r'), raw.count ('\n')); // every line ends CR LF
#else
  QCOMPARE (raw.count ('\r'), 0);
#endif
  QCOMPARE (text.count (QStringLiteral ("<eoh>")), 1);
  static QRegularExpression const record_line {
    QStringLiteral ("^<call:\\d+>K[AB]\\d+ <band:3>20m <mode:3>FT8 <gridsquare:4>FN42 <eor>$")};
  int records {0};
  for (auto const& line : text.split (QRegularExpression {"[\r\n]+"}, Qt::SkipEmptyParts))
    {
      auto const trimmed = line.trimmed ();
      if (trimmed.startsWith ("<call:"))
        {
          QVERIFY2 (record_line.match (trimmed).hasMatch (), qPrintable (trimmed));
          ++records;
        }
    }
  QCOMPARE (records, 2 * per_writer);
}

void TestWorkedBeforeAdif::chosen_log_location_receives_new_qsos ()
{
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  QSettings settings {dir.filePath ("custom.ini"), QSettings::IniFormat};
  auto const custom = QDir::cleanPath (dir.filePath ("shared/our_log.adi"));
  settings.setValue ("Configuration/AdifLogFile", custom);
  Configuration configuration {&network_, QDir {dir.path ()}, &settings, nullptr};
  QCOMPARE (configuration.adif_log_file_path (), custom);
  QCOMPARE (configuration_->adif_log_file_path (),
            QDir {data_directory ()}.filePath ("wsjtx_log.adi"));

  WorkedBefore worked_before {&configuration};
  QSignalSpy loaded {&worked_before, &WorkedBefore::finished_loading};
  QTRY_VERIFY_WITH_TIMEOUT (!loaded.isEmpty (), 5000);
  QCOMPARE (worked_before.path (), custom);
  QVERIFY (add_qso (worked_before, "K1ABC"));
  QVERIFY (QFile::exists (custom));
  QVERIFY (!QFile::exists (QDir {data_directory ()}.filePath ("wsjtx_log.adi")));
  worked_before.reload ();
  QTRY_COMPARE_WITH_TIMEOUT (loaded.count (), 2, 5000);
  QVERIFY (worked_before.call_worked ("K1ABC", "FT8", "20M"));
}

QTEST_MAIN (TestWorkedBeforeAdif)

#include "test_worked_before_adif.moc"
