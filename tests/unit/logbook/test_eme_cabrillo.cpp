#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QFileInfo>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>

#include "logbook/EmeCabrillo.hpp"
#include "widgets/EmeCabrilloDialog.hpp"

namespace
{
  QByteArray field (char const * name, QString const& value)
  {
    auto const bytes = value.toUtf8 ();
    return QByteArray {"<"} + name + ':' + QByteArray::number (bytes.size ()) + '>' + bytes + ' ';
  }

  QByteArray record (QString const& call, QString const& date, QString const& time, QString const& band,
                     QString const& grid = {}, QString const& mode = "MFSK", QByteArray const& extra = {})
  {
    return field ("CALL", call) + field ("QSO_DATE", date) + field ("TIME_ON", time)
      + field ("BAND", band) + (grid.isEmpty () ? QByteArray {} : field ("GRIDSQUARE", grid))
      + field ("MODE", mode) + extra + "<EOR>\n";
  }

  // The same cases as EMELite's self-test.
  QByteArray contest_log ()
  {
    return QByteArray {"WSJT-X ADIF Export<eoh>\n"}
      + record ("K1JT", "20261031", "010000", "23cm", "FN20")
      + record ("K1JT", "20261101", "020000", "23cm", "FN20")           // dupe
      + record ("K1JT", "20261031", "030000", "2m", "FN20", "CW")       // another band
      + record ("W5LUA", "20261031", "040000", "23cm", "EM13")
      + record ("W5LUA", "20261101", "050000", "23cm", "EM12")          // new grid: counts
      + record ("OK1KIR", "20261031", "060000", "23cm")                 // grid known
      + record ("OK1KIR", "20261105", "070000", "23cm", "JO70")         // between weekends
      + record ("G4CCH", "20261031", "080000", "23cm")                  // no grid
      + record ("ON8BB", "20261031", "090000", "13cm", "JO20", "FT8", field ("SAT_NAME", "QO-100"))
      + record ("PA3DZL", "20261031", "100000", "6m", "JO22", "SSB");
  }

  QSet<QDate> contest_days ()
  {
    return {QDate {2026, 10, 31}, QDate {2026, 11, 1}, QDate {2026, 11, 28}, QDate {2026, 11, 29}};
  }

  QStringList calls (QVector<EmeCabrillo::Qso> const& qsos)
  {
    QStringList result;
    for (auto const& qso : qsos) result << qso.call;
    return result;
  }
}

class TestEmeCabrillo : public QObject
{
  Q_OBJECT

private slots:
  void reads_adif_by_byte_length ();
  void band_and_mode_codes ();
  void selects_the_entry ();
  void typed_grids ();
  void score_and_text ();
  void several_logs_and_weekends ();
  void review_points ();
  void copies_dates_and_propagation ();
  void save_is_safe ();
  void dialog ();
  void dialog_several_logs ();
  void dialog_keeps_typed_grids ();
  void dialog_keeps_own_days ();
  void buttons_stay_in_view ();
  void real_log ();
  void dummy_logs ();
  void timing ();
  void picture ();
};

// Optional: a picture of the dialog with the dummy logs, to look at it.
// EME_CABRILLO_DUMMY=<folder> EME_CABRILLO_PICTURE=<file.png>
void TestEmeCabrillo::picture ()
{
  auto const folder = qEnvironmentVariable ("EME_CABRILLO_DUMMY"), file = qEnvironmentVariable ("EME_CABRILLO_PICTURE");
  if (folder.isEmpty () || file.isEmpty ()) QSKIP ("EME_CABRILLO_DUMMY or EME_CABRILLO_PICTURE not set");
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  settings.setValue ("EmeCabrillo/Logs", QStringList {QDir {folder}.filePath ("pc10_wsjtx_log.adi"),
                                                      QDir {folder}.filePath ("pc23_wsjtx_log.adi")});
  settings.setValue ("EmeCabrillo/Operator", "MULTI-OP");
  QSet<QDate> days;
  for (auto const& day : {"2026-08-08", "2026-08-09", "2026-09-05", "2026-09-06",
                          "2026-10-31", "2026-11-01", "2026-11-28", "2026-11-29"})
    days << QDate::fromString (day, Qt::ISODate);
  EmeCabrilloDialog dialog {&settings, directory.filePath ("wsjtx_log.adi"), "DL3WDG", "JN68LM", days,
                            directory.path ()};
  dialog.resize (qEnvironmentVariableIntValue ("EME_CABRILLO_PICTURE_WIDTH") > 0
                 ? qEnvironmentVariableIntValue ("EME_CABRILLO_PICTURE_WIDTH") : 720, 860);
  dialog.show ();
  QVERIFY (QTest::qWaitForWindowExposed (&dialog));
  QVERIFY (dialog.grab ().save (file));
}

// Optional: how long a big log takes. EME_CABRILLO_TIMING=<records>.
void TestEmeCabrillo::timing ()
{
  auto const count = qEnvironmentVariableIntValue ("EME_CABRILLO_TIMING");
  if (count <= 0) QSKIP ("EME_CABRILLO_TIMING not set");
  QVector<EmeCabrillo::Record> records;
  QDate const start {2020, 1, 1};
  for (int i = 0; i < count; ++i)
    {
      auto const day = start.addDays (i * 7919 % 2500);   // not in time order
      records << EmeCabrillo::Record {
        {"CALL", QString {"K%1ABC"}.arg (i % 5000)}, {"QSO_DATE", day.toString ("yyyyMMdd")},
        {"TIME_ON", QString {"%1"}.arg (i % 2400, 4, 10, QChar {'0'}).replace (2, 1, QChar ('0' + i % 6))},
        {"BAND", i % 3 ? "2m" : "23cm"}, {"MODE", "MFSK"}, {"SUBMODE", i % 2 ? "FT8" : "Q65"},
        {"GRIDSQUARE", QString {"FN%1"}.arg (i % 100, 2, 10, QChar {'0'})}};
    }
  QSet<QDate> const days {start.addDays (300), start.addDays (301), start.addDays (1000), start.addDays (1001)};
  QElapsedTimer timer;
  timer.start ();
  auto const log = EmeCabrillo::prepare (records);
  auto const sort_ms = timer.restart ();
  EmeCabrillo::Selection s;
  for (int i = 0; i < 3; ++i) s = EmeCabrillo::select (log, days, "DL3WDG", "JN68");
  auto const select_ms = timer.restart ();
  for (int i = 0; i < 3; ++i) s = EmeCabrillo::select (records, days, "DL3WDG", "JN68");
  auto const unsorted_ms = timer.restart ();
  auto const text = EmeCabrillo::cabrillo_text ({}, s.qsos);
  qInfo () << count << "records: made ready once" << sort_ms << "ms; three selects from that" << select_ms
           << "ms; three selects from the records as read" << unsorted_ms << "ms; text of" << s.qsos.size () << "QSOs"
           << timer.elapsed () << "ms";
  QVERIFY (!text.isEmpty ());
}

namespace
{
  // Two PCs of a multi-operator station: the microwave PC (Sep weekend, one
  // 10G QSO on the 50-1296 weekend, a 23cm QSO also in the other log) and the
  // 50-1296 PC (Oct weekend, one 23cm QSO on the microwave weekend), and a
  // weekend with as many QSOs of each group. The same as EMELite's self-test.
  QByteArray pc10_log ()
  {
    return QByteArray {"<EOH>\n"}
      + record ("IT9EQO", "20260905", "0417", "3cm", "JM76", "MFSK", field ("OPERATOR", "DL4KGC"))
      + record ("OK1KIR", "20260906", "0100", "3cm", "JO70", "MFSK", field ("OPERATOR", "DL3WDG"))
      + record ("W5LUA", "20261031", "0200", "3cm", "EM13")             // off weekend
      + record ("K1JT", "20261031", "0300", "23cm", "FN20");            // also in pc23
  }

  QByteArray pc23_log ()
  {
    return QByteArray {"<EOH>\n"}
      + record ("K1JT", "20261031", "0300", "23cm", "FN20")             // a copy
      + record ("G4CCH", "20261031", "0400", "23cm", "IO91")
      + record ("SP6JLW", "20261101", "0500", "23cm", "JO80")
      + record ("OZ4MM", "20260905", "0600", "23cm", "JO55")            // off weekend
      + record ("DF3RU", "20261128", "0700", "2m", "JN58")              // as many of each
      + record ("ES5PC", "20261129", "0800", "3cm", "KO38");
  }

  QSet<QDate> six_days ()
  {
    return {QDate {2026, 9, 5}, QDate {2026, 9, 6}, QDate {2026, 10, 31}, QDate {2026, 11, 1},
            QDate {2026, 11, 28}, QDate {2026, 11, 29}};
  }

  QString write_file (QTemporaryDir const& directory, QString const& name, QByteArray const& content)
  {
    auto const path = directory.filePath (name);
    QFile file {path};
    if (file.open (QIODevice::WriteOnly)) file.write (content);
    return path;
  }
}

void TestEmeCabrillo::several_logs_and_weekends ()
{
  QCOMPARE (EmeCabrillo::band_group ("1.2G"), QString {"50-1296 MHz"});
  QCOMPARE (EmeCabrillo::band_group ("50"), QString {"50-1296 MHz"});
  QCOMPARE (EmeCabrillo::band_group ("2.3G"), QString {"2.3 GHz and up"});
  auto const weekends = EmeCabrillo::weekends ({QDate {2026, 11, 1}, QDate {2026, 10, 31}, QDate {2026, 9, 6},
                                                QDate {2026, 9, 5}, QDate {2026, 11, 28}});
  QCOMPARE (weekends.size (), 3);
  QCOMPARE (weekends[0], (QVector<QDate> {QDate {2026, 9, 5}, QDate {2026, 9, 6}}));
  QCOMPARE (weekends[2], (QVector<QDate> {QDate {2026, 11, 28}}));

  QVector<EmeCabrillo::Record> copies;
  auto const records = EmeCabrillo::merge ({EmeCabrillo::read_adif (pc10_log ()),
                                            EmeCabrillo::read_adif (pc23_log ())}, &copies);
  QCOMPARE (records.size (), 9);
  QCOMPARE (copies.size (), 1);
  QCOMPARE (copies[0].value ("_FILE"), QString {"1"});

  // the repeat inside one log survives the merge: it is the contest's dupe
  auto twice = EmeCabrillo::read_adif (pc23_log () + record ("G4CCH", "20261031", "0410", "23cm", "IO91"));
  QCOMPARE (EmeCabrillo::merge ({twice}).size (), 7);
  // the minute is the key: seconds, or none, make no other QSO of it
  QVector<EmeCabrillo::Record> same;
  EmeCabrillo::merge ({EmeCabrillo::read_adif (record ("K1JT", "20261031", "030059", "23cm", "FN20")),
                       EmeCabrillo::read_adif (record ("K1JT", "20261031", "0300", "23cm", "FN20")
                                               + record ("K1JT", "20261031", "0301", "23cm", "FN20"))}, &same);
  QCOMPARE (same.size (), 1);

  auto const s = EmeCabrillo::select (records, six_days (), "DL3WDG", "JN68", {}, {}, true);
  QCOMPARE (s.weekends.size (), 3);
  QCOMPARE (s.weekends[0].group, QString {"2.3 GHz and up"});
  QCOMPARE (s.weekends[1].group, QString {"50-1296 MHz"});
  QCOMPARE (s.weekends[2].group, QString {});                     // one QSO of each
  QStringList off = calls (s.off_weekend);
  off.sort ();
  QCOMPARE (off, (QStringList {"OZ4MM", "W5LUA"}));
  QCOMPARE (calls (s.qsos), (QStringList {"IT9EQO", "OK1KIR", "K1JT", "G4CCH", "SP6JLW", "DF3RU", "ES5PC"}));
  QCOMPARE (s.operators, (QStringList {"DL3WDG", "DL4KGC"}));

  // by default they are kept and only flagged
  auto const kept = EmeCabrillo::select (records, six_days (), "DL3WDG", "JN68");
  QCOMPARE (kept.qsos.size (), 9);
  QCOMPARE (kept.off_weekend.size (), 2);
}

// Points from the review: a grid that makes a QSO a dupe can still be
// corrected, a typed grid serves the call's other QSOs, modes can be left
// out, the category names of 6 m / 4 m / 2 m, plain ASCII, a cut-off file.
void TestEmeCabrillo::review_points ()
{
  QSet<QDate> const weekend {QDate {2026, 10, 31}, QDate {2026, 11, 1}};
  auto const records = EmeCabrillo::read_adif (
    QByteArray {"<EOH>\n"}
    + record ("W5LUA", "20261031", "0400", "23cm", "EM13")
    + record ("W5LUA", "20261101", "0500", "23cm")                       // really from EM12
    + record ("K1JT", "20261031", "0600", "23cm", "FN20")
    + record ("K1JT", "20261101", "0700", "23cm")                        // FN20 typed: a dupe
    + record ("G4CCH", "20261031", "0800", "23cm")                       // IO91 typed
    + record ("G4CCH", "20261101", "0900", "2m")                         // no grid: gets IO91
    + record ("OK1KIR", "20261031", "1000", "2m", "JO70", "MFSK", field ("SUBMODE", "FT8"))
    + record ("OK1KIR", "20261031", "1100", "2m", "JO70", "MFSK", field ("SUBMODE", "Q65")));
  auto const s = EmeCabrillo::select (records, weekend, "DL3WDG", "JN68");
  QHash<QString, EmeCabrillo::Qso> need;
  for (auto const& qso : s.need_grid) need[qso.call + qso.time] = qso;
  QVERIFY (need.contains ("W5LUA0500"));                // the dupe keeps its row
  QVERIFY (need["W5LUA0500"].dupe);
  QVERIFY (need["W5LUA0500"].filled);
  QVERIFY (need["K1JT0700"].dupe);

  QHash<QString, QString> fixes;
  fixes[EmeCabrillo::record_key (records[1])] = "EM12";
  fixes[EmeCabrillo::record_key (records[3])] = "FN20";
  fixes[EmeCabrillo::record_key (records[4])] = "IO91";
  auto const t = EmeCabrillo::select (records, weekend, "DL3WDG", "JN68", {}, fixes);
  QHash<QString, EmeCabrillo::Qso> in;
  for (auto const& qso : t.qsos) in[qso.call + qso.time] = qso;
  QCOMPARE (in["W5LUA0500"].grid, QString {"EM12"});   // the multiplier is back
  bool typed_dupe_listed {false};
  for (auto const& qso : t.need_grid) typed_dupe_listed |= qso.call == "K1JT" && qso.dupe && qso.entered;
  QVERIFY (typed_dupe_listed);                          // a mistyped grid stays in reach
  QCOMPARE (in["G4CCH0900"].grid, QString {"IO91"});   // from the grid typed for 0800
  QVERIFY (in["G4CCH0900"].filled);

  QCOMPARE (t.modes, (QMap<QString, int> {{"MFSK", 6}, {"FT8", 1}, {"Q65", 1}}));
  QVERIFY (!in.contains ("OK1KIR1100"));                // a dupe of the FT8 QSO
  auto const no_ft8 = EmeCabrillo::select (records, weekend, "DL3WDG", "JN68", {}, fixes, false, {"MFSK", "Q65"});
  QCOMPARE (no_ft8.mode_out, 1);
  QCOMPARE (no_ft8.qsos.size (), t.qsos.size ());       // the Q65 QSO counts instead
  QCOMPARE (EmeCabrillo::mode_name ({{"MODE", "ssb"}}), QString {"SSB"});

  EmeCabrillo::Header header;
  header.callsign = "DL3WDG";
  header.name = QString::fromUtf8 ("Jürgen Łódź ß Я");
  auto const two_m = EmeCabrillo::select (records, weekend, "DL3WDG", "JN68", {"144"}, fixes);
  auto const text = EmeCabrillo::cabrillo_text (header, two_m.qsos);
  QVERIFY2 (text.contains ("CATEGORY-BAND: 2M\n"), qPrintable (text));
  QVERIFY (text.contains ("QSO:   144 "));              // the QSO lines keep 144
  QVERIFY2 (text.contains ("NAME: Jurgen Lodz ss ?\n"), qPrintable (text));
  for (auto const& ch : text) QVERIFY (ch.unicode () < 128);
  auto six = two_m.qsos;
  for (auto& qso : six) qso.band = "50";
  QVERIFY (EmeCabrillo::cabrillo_text (header, six).contains ("CATEGORY-BAND: 6M\n"));
  for (auto& qso : six) qso.band = "70";
  QVERIFY (EmeCabrillo::cabrillo_text (header, six).contains ("CATEGORY-BAND: 4M\n"));

  // the last record of a file cut short, without its <EOR>, still counts
  QCOMPARE (EmeCabrillo::read_adif ("<CALL:4>K1JT<EOR><CALL:5>W5LUA <BAND:2>2m").size (), 2);
  QCOMPARE (EmeCabrillo::read_adif ("<CALL:4>K1JT<EOR>\n").size (), 1);
}

// A copy that knows more, logs that disagree, PROP_MODE, TIME_OFF and
// QSO_DATE_OFF, no time at all, the weekend's group from what is chosen,
// header marks inside a field, records already in time order.
void TestEmeCabrillo::copies_dates_and_propagation ()
{
  QSet<QDate> const day {QDate {2026, 10, 31}};
  // the first log has no grid for K1JT, its copy in the second has; the
  // two logs give different grids for W5LUA
  QVector<EmeCabrillo::Record> copies;
  auto const merged = EmeCabrillo::merge (
    {EmeCabrillo::read_adif (record ("K1JT", "20261031", "0300", "23cm")
                             + record ("W5LUA", "20261031", "0400", "23cm", "EM13")),
     EmeCabrillo::read_adif (record ("K1JT", "20261031", "030030", "23cm", "FN20")
                             + record ("W5LUA", "20261031", "0400", "23cm", "EM12"))}, &copies);
  QCOMPARE (merged.size (), 2);
  QCOMPARE (copies.size (), 2);
  auto const s = EmeCabrillo::select (merged, day, "DL3WDG", "JN68");
  QCOMPARE (s.qsos.size (), 2);
  QCOMPARE (s.qsos[0].grid, QString {"FN20"});          // from the copy
  QVERIFY (s.no_grid.isEmpty ());
  QCOMPARE (calls (s.need_grid), QStringList {"W5LUA"}); // for the user to settle
  QCOMPARE (s.need_grid[0].grid, QString {"EM13"});
  QCOMPARE (s.need_grid[0].grid2, QString {"EM12"});
  QHash<QString, QString> fixes;
  fixes[s.need_grid[0].key] = "EM12";
  auto const settled = EmeCabrillo::select (merged, day, "DL3WDG", "JN68", {}, fixes);
  QCOMPARE (settled.qsos[1].grid, QString {"EM12"});
  QVERIFY (settled.qsos[1].entered);
  // the other way round the same two grids are flagged too
  auto const reversed = EmeCabrillo::merge (
    {EmeCabrillo::read_adif (record ("W5LUA", "20261031", "0400", "23cm", "EM12")),
     EmeCabrillo::read_adif (record ("W5LUA", "20261031", "0400", "23cm", "EM13"))});
  QCOMPARE (EmeCabrillo::select (reversed, day, "DL3WDG", "JN68").need_grid.size (), 1);

  // PROP_MODE: EME or none may be EME; TR is not, SAT is a satellite QSO
  auto const propagation = EmeCabrillo::read_adif (
    record ("K1JT", "20261031", "0300", "23cm", "FN20", "MFSK", field ("PROP_MODE", "EME"))
    + record ("G4CCH", "20261031", "0400", "23cm", "IO91", "MFSK", field ("PROP_MODE", "TR"))
    + record ("ON8BB", "20261031", "0500", "23cm", "JO20", "MFSK", field ("PROP_MODE", "SAT"))
    // only the end of the QSO is logged
    + field ("CALL", "OK1KIR") + field ("QSO_DATE_OFF", "20261031") + field ("TIME_OFF", "0615")
    + field ("BAND", "23cm") + field ("GRIDSQUARE", "JO70") + field ("MODE", "CW") + "<EOR>"
    // no time at all
    + field ("CALL", "SP6JLW") + field ("QSO_DATE", "20261031") + field ("BAND", "23cm")
    + field ("GRIDSQUARE", "JO80") + field ("MODE", "CW") + "<EOR>");
  auto const p = EmeCabrillo::select (propagation, day, "DL3WDG", "JN68");
  QCOMPARE (calls (p.qsos), (QStringList {"K1JT", "OK1KIR"}));
  QCOMPARE (p.qsos[1].time, QString {"0615"});
  QCOMPARE (p.qsos[1].date, QDate (2026, 10, 31));
  QCOMPARE (p.not_eme, 1);
  QCOMPARE (p.satellite, 1);
  QCOMPARE (p.no_time, QStringList {"SP6JLW"});
  QVERIFY (!EmeCabrillo::cabrillo_text ({}, p.qsos).contains (" 0000 "));
  QCOMPARE (EmeCabrillo::record_key (propagation[3]), QString {"OK1KIR|20261031|0615"});

  // a microwave weekend: one 10 GHz Q65 QSO and ten 2 m FT8 ones
  QByteArray log;
  log += record ("OK1KIR", "20260905", "0100", "3cm", "JO70", "MFSK", field ("SUBMODE", "Q65"));
  for (int i = 0; i < 10; ++i)
    log += record (QString {"DL%1XYZ"}.arg (i), "20260905", QString {"02%1"}.arg (i, 2, 10, QChar {'0'}), "2m",
                   "JO62", "MFSK", field ("SUBMODE", "FT8"));
  QSet<QDate> const saturday {QDate {2026, 9, 5}};
  auto const records = EmeCabrillo::read_adif (log);
  auto const with_ft8 = EmeCabrillo::select (records, saturday, "DL3WDG", "JN68", {}, {}, true);
  QCOMPARE (with_ft8.weekends[0].group, QString {"50-1296 MHz"});
  auto const q65_only = EmeCabrillo::select (records, saturday, "DL3WDG", "JN68", {}, {}, true, {"Q65"});
  QCOMPARE (q65_only.weekends[0].group, QString {"2.3 GHz and up"}); // FT8 no longer decides
  QCOMPARE (calls (q65_only.qsos), QStringList {"OK1KIR"});
  QVERIFY (q65_only.off_weekend.isEmpty ());
  auto const only_10g = EmeCabrillo::select (records, saturday, "DL3WDG", "JN68", {"10G"}, {}, true);
  QCOMPARE (calls (only_10g.qsos), QStringList {"OK1KIR"});       // nor does an unticked band

  // <EOH> and <EOR> inside a field are text, not marks
  auto const headerless = EmeCabrillo::read_adif ("<CALL:4>K1JT <COMMENT:5><EOH> <BAND:2>2m <EOR>");
  QCOMPARE (headerless.size (), 1);
  QCOMPARE (headerless[0].value ("COMMENT"), QString {"<EOH>"});
  QCOMPARE (headerless[0].value ("BAND"), QString {"2m"});
  auto const with_header = EmeCabrillo::read_adif (
    "my log <ADIF_VER:5>3.1.4 <eoh>\n<CALL:5>W5LUA <NOTES:5><EOR> <BAND:2>2m <eor>");
  QCOMPARE (with_header.size (), 1);
  QCOMPARE (with_header[0].value ("NOTES"), QString {"<EOR>"});
  QVERIFY (!with_header[0].contains ("ADIF_VER"));

  // a log made ready once gives the same entry as the records as read
  auto const unsorted = EmeCabrillo::read_adif (pc23_log () + pc10_log ());
  QCOMPARE (EmeCabrillo::cabrillo_text ({}, EmeCabrillo::select (EmeCabrillo::prepare (unsorted),
                                                                  six_days (), "DL3WDG", "JN68").qsos),
            EmeCabrillo::cabrillo_text ({}, EmeCabrillo::select (unsorted, six_days (), "DL3WDG", "JN68").qsos));
}

// Save never replaces a log the entry is made from, also not through a
// link; it reports a file it cannot write, and then leaves an earlier
// export as it was.
void TestEmeCabrillo::save_is_safe ()
{
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  auto const adif = write_file (directory, "wsjtx_log.adi", contest_log ());
  auto const other = write_file (directory, "other.adi", pc23_log ());
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  settings.setValue ("EmeCabrillo/Logs", QStringList {EmeCabrilloDialog::own_log (), other});
  EmeCabrilloDialog dialog {&settings, adif, "DL3WDG", "JN68LM", contest_days (), directory.path ()};
  // the message boxes of a refused save are closed as they come up
  QTimer closer;
  connect (&closer, &QTimer::timeout, [] {
      if (auto box = QApplication::activeModalWidget ()) box->close ();
    });
  closer.start (20);
  QVERIFY (!dialog.save_to (adif));                     // WSJT-X's own log
  QVERIFY (!dialog.save_to (other));                    // another input log
  QVERIFY (!dialog.save_to (directory.filePath ("./other.adi")));   // the same file, written differently
  auto const link = directory.filePath ("link.adi");
  if (QFile::link (other, link) && QFileInfo {link}.isSymLink ()
      && QFileInfo {link}.canonicalFilePath () == QFileInfo {other}.canonicalFilePath ())
    QVERIFY (!dialog.save_to (link));                   // a link to it
  for (auto const& path : {adif, other})
    {
      QFile file {path};
      QVERIFY (file.open (QIODevice::ReadOnly));
      QCOMPARE (file.readAll (), path == adif ? contest_log () : pc23_log ());
    }
  // a normal save, then one that cannot be written: the first stays whole
  auto const out = directory.filePath ("entry.log");
  QVERIFY (dialog.save_to (out));
  QFile saved {out};
  QVERIFY (saved.open (QIODevice::ReadOnly | QIODevice::Text));
  auto const first = saved.readAll ();
  saved.close ();
  QCOMPARE (QString::fromLatin1 (first), dialog.cabrillo_text ());
  QVERIFY (!dialog.save_to (directory.filePath ("no_such_folder/entry.log")));
  QCOMPARE (QDir {directory.path ()}.entryList ({"entry.log*"}).size (), 1);   // no temporary file left
}

// Two logs in the dialog, the band-group check, WSJT-X's own log added and a
// log removed; the files are only read.
void TestEmeCabrillo::dialog_several_logs ()
{
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  auto const pc10 = write_file (directory, "pc10.adi", pc10_log ());
  auto const pc23 = write_file (directory, "pc23.adi", pc23_log ());
  auto const own = write_file (directory, "wsjtx_log.adi", QByteArray {"<EOH>\n"}
                               + record ("DL7APV", "20261101", "0900", "23cm", "JO62"));
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  settings.setValue ("EmeCabrillo/Logs", QStringList {pc10, pc23});
  {
    EmeCabrilloDialog dialog {&settings, own, "DL3WDG", "JN68LM", six_days (), directory.path ()};
    QCOMPARE (dialog.logs (), (QStringList {pc10, pc23}));
    QCOMPARE (dialog.selection ().qsos.size (), 9);                 // kept, and flagged
    QCOMPARE (dialog.selection ().off_weekend.size (), 2);
    QCheckBox * box {nullptr};
    for (auto b : dialog.findChildren<QCheckBox *> ()) if (b->text ().startsWith ("Leave out")) box = b;
    QVERIFY (box);
    QVERIFY (box->isVisibleTo (&dialog));
    QVERIFY (!box->isChecked ());
    QVERIFY (!dialog.cabrillo_text ().contains ("OPERATORS:"));     // single operator
    QLineEdit * operators {nullptr};
    for (auto e : dialog.findChildren<QLineEdit *> ()) if (e->text () == "DL3WDG DL4KGC") operators = e;
    QVERIFY (operators);                                            // filled from the logs
    box->setChecked (true);
    QCOMPARE (dialog.selection ().qsos.size (), 7);
    for (auto button : dialog.findChildren<QPushButton *> ())      // Enter never presses one
      QVERIFY2 (!button->autoDefault () && !button->isDefault (), qPrintable (button->text ()));

    dialog.add_log (EmeCabrilloDialog::own_log ());
    QCOMPARE (dialog.logs ().size (), 3);
    QCOMPARE (dialog.selection ().qsos.size (), 8);
    QVERIFY (dialog.cabrillo_text ().contains ("DL7APV        JO62\n"));
    dialog.remove_log (1);                                          // the 50-1296 PC's log
    QCOMPARE (dialog.logs (), (QStringList {pc10, EmeCabrilloDialog::own_log ()}));
    QCOMPARE (dialog.selection ().qsos.size (), 4);                 // IT9EQO OK1KIR K1JT DL7APV
  }
  QCOMPARE (settings.value ("EmeCabrillo/Logs").toStringList (),
            (QStringList {pc10, EmeCabrilloDialog::own_log ()}));
  QCOMPARE (settings.value ("EmeCabrillo/LeaveOutOtherGroup").toBool (), true);
  QFile file {pc23};
  QVERIFY (file.open (QIODevice::ReadOnly));
  QCOMPARE (file.readAll (), pc23_log ());
}

// Typed grids: a dupe keeps its row and can be corrected; they survive a
// log being added and removed, and closing the dialog; a mode unticked.
void TestEmeCabrillo::dialog_keeps_typed_grids ()
{
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  auto const adif = write_file (directory, "wsjtx_log.adi", QByteArray {"<EOH>\n"}
                                + record ("W5LUA", "20261031", "0400", "23cm", "EM13")
                                + record ("W5LUA", "20261101", "0500", "23cm")       // really EM12
                                + record ("G4CCH", "20261031", "0800", "23cm")
                                + record ("K1JT", "20261031", "1100", "2m", "FN20", "MSK144"));
  auto const other = write_file (directory, "other.adi", QByteArray {"<EOH>\n"}
                                 + record ("SP6JLW", "20261101", "1200", "23cm", "JO80"));
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  QSet<QDate> const weekend {QDate {2026, 10, 31}, QDate {2026, 11, 1}};
  auto type = [] (EmeCabrilloDialog& dialog, QString const& call, QString const& grid) {
    auto edit = dialog.findChild<QLineEdit *> ("grid_" + call);
    QVERIFY2 (edit, qPrintable (call));
    edit->setText (grid);
    Q_EMIT edit->editingFinished ();
  };
  {
    EmeCabrilloDialog dialog {&settings, adif, "DL3WDG", "JN68LM", weekend, directory.path ()};
    QCOMPARE (dialog.selection ().qsos.size (), 2);                 // W5LUA EM13, K1JT
    QVERIFY (dialog.findChild<QLineEdit *> ("grid_W5LUA"));         // the dupe has a row
    auto msk = dialog.findChild<QCheckBox *> ("mode_MSK144");
    QVERIFY (msk);
    QCOMPARE (msk->text (), QString {"MSK144 (1)"});
    msk->setChecked (false);                                        // meteor scatter, not EME
    QCOMPARE (dialog.selection ().qsos.size (), 1);
    QCOMPARE (dialog.selection ().mode_out, 1);
    type (dialog, "W5LUA", "EM12");
    type (dialog, "G4CCH", "IO91");
    QCOMPARE (dialog.selection ().qsos.size (), 3);
    QVERIFY (dialog.cabrillo_text ().contains ("W5LUA         EM12\n"));
    dialog.add_log (other);
    QCOMPARE (dialog.selection ().qsos.size (), 4);                 // typed grids still there
    dialog.remove_log (1);
    QCOMPARE (dialog.selection ().qsos.size (), 3);
    QVERIFY (dialog.cabrillo_text ().contains ("G4CCH         IO91\n"));
  }
  QCOMPARE (settings.value ("EmeCabrillo/TypedGrids").toStringList ().size (), 2);
  {
    EmeCabrilloDialog dialog {&settings, adif, "DL3WDG", "JN68LM", weekend, directory.path ()};
    QCOMPARE (dialog.findChild<QLineEdit *> ("grid_W5LUA")->text (), QString {"EM12"});
    QVERIFY (dialog.cabrillo_text ().contains ("W5LUA         EM12\n"));
    QVERIFY (dialog.cabrillo_text ().contains ("G4CCH         IO91\n"));
    type (dialog, "G4CCH", "");                                     // taken out again
    QVERIFY (!dialog.cabrillo_text ().contains ("G4CCH"));
  }
  QCOMPARE (settings.value ("EmeCabrillo/TypedGrids").toStringList ().size (), 1);
}

// The dialog's contest days are its own: the days it is opened with (Active
// Stations' "on days") count only the first time; after that the saved days
// are used, whatever Active Stations has meanwhile.
void TestEmeCabrillo::dialog_keeps_own_days ()
{
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  auto const adif = write_file (directory, "wsjtx_log.adi", QByteArray {"<EOH>\n"}
                                + record ("W5LUA", "20261031", "0400", "23cm", "EM13")
                                + record ("G4CCH", "20261101", "0800", "23cm", "IO91")
                                + record ("K2UYH", "20261128", "0900", "23cm", "FN20"));
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  QSet<QDate> const weekend {QDate {2026, 10, 31}, QDate {2026, 11, 1}};
  QSet<QDate> const other {QDate {2026, 11, 28}};
  {
    EmeCabrilloDialog dialog {&settings, adif, "DL3WDG", "JN68LM", weekend, directory.path ()};
    QCOMPARE (dialog.days (), weekend);                             // first time: as given
    QCOMPARE (dialog.selection ().qsos.size (), 2);
  }
  QCOMPARE (settings.value ("EmeCabrillo/Days").toStringList (),
            (QStringList {"2026-10-31", "2026-11-01"}));
  QVERIFY (!settings.contains ("ActiveStations/WantedDayList"));    // never written
  {
    EmeCabrilloDialog dialog {&settings, adif, "DL3WDG", "JN68LM", other, directory.path ()};
    QCOMPARE (dialog.days (), weekend);                             // its own, not "other"
    QCOMPARE (dialog.selection ().qsos.size (), 2);
  }
}

// Save and Close stay in view in a small window with many missing grids.
void TestEmeCabrillo::buttons_stay_in_view ()
{
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  QByteArray log {"<EOH>\n"};
  for (int i = 0; i < 40; ++i)                                     // 40 QSOs without a grid
    log += record (QString {"DL%1ABC"}.arg (i % 10) + QChar ('A' + i / 10), "20261031",
                   QString {"%1"}.arg (100 + i, 4, 10, QChar {'0'}), "23cm");
  auto const adif = write_file (directory, "wsjtx_log.adi", log);
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  EmeCabrilloDialog dialog {&settings, adif, "DL3WDG", "JN68LM", contest_days (), directory.path ()};
  QCOMPARE (dialog.selection ().need_grid.size (), 40);
  dialog.resize (700, 300);
  dialog.show ();
  QVERIFY (QTest::qWaitForWindowExposed (&dialog));
  for (auto const& text : {QString::fromUtf8 ("Save…"), QString {"Close"}})
    {
      QPushButton * button {nullptr};
      for (auto b : dialog.findChildren<QPushButton *> ()) if (b->text () == text) button = b;
      QVERIFY2 (button, qPrintable (text));
      QVERIFY (button->isVisible ());
      auto const bottom = button->mapTo (&dialog, QPoint {0, button->height ()}).y ();
      QVERIFY2 (bottom <= dialog.height (), qPrintable (QString {"%1 ends at %2, dialog %3x%4"}
                                                         .arg (text).arg (bottom)
                                                         .arg (dialog.width ()).arg (dialog.height ())));
    }
  QVERIFY (dialog.height () <= 400);                                // not grown to fit the 40 rows
}

// Optional: the dummy multi-PC logs (EMELite's testdata\cabrillo, built
// from a real contest log): EME_CABRILLO_DUMMY=<that folder>.
void TestEmeCabrillo::dummy_logs ()
{
  auto const folder = qEnvironmentVariable ("EME_CABRILLO_DUMMY");
  if (folder.isEmpty ()) QSKIP ("EME_CABRILLO_DUMMY not set");
  QVector<QVector<EmeCabrillo::Record>> logs;
  for (auto const& name : {"pc10_wsjtx_log.adi", "pc23_wsjtx_log.adi"})
    {
      QFile file {QDir {folder}.filePath (name)};
      QVERIFY (file.open (QIODevice::ReadOnly));
      logs << EmeCabrillo::read_adif (file.readAll ());
    }
  QVector<EmeCabrillo::Record> copies;
  auto const records = EmeCabrillo::merge (logs, &copies);
  QSet<QDate> days;
  for (auto const& day : {"2026-08-08", "2026-08-09", "2026-09-05", "2026-09-06",
                          "2026-10-31", "2026-11-01", "2026-11-28", "2026-11-29"})
    days << QDate::fromString (day, Qt::ISODate);
  auto const s = EmeCabrillo::select (records, days, "DL3WDG", "JN68LM", {}, {}, true);
  auto const score = EmeCabrillo::score (s.qsos);
  int copies_on_days = 0;
  for (auto const& r : copies) copies_on_days += days.contains (QDate::fromString (r.value ("QSO_DATE"), "yyyyMMdd"));
  qInfo () << "QSOs" << s.qsos.size () << "grids" << score.multipliers << "claimed" << score.claimed
           << "copies" << copies_on_days << "dupes" << s.dupes.size () << "off weekend" << s.off_weekend.size ();
  QCOMPARE (s.qsos.size (), 124);
  QCOMPARE (score.multipliers, 108);
  QCOMPARE (score.claimed, qint64 {1339200});
  QCOMPARE (copies_on_days, 5);
  QCOMPARE (s.dupes.size (), 23);
  QCOMPARE (s.off_weekend.size (), 4);
  QCOMPARE (s.operators, (QStringList {"DL3WDG", "DL4KGC"}));
  QCOMPARE (EmeCabrillo::select (records, days, "DL3WDG", "JN68LM").qsos.size (), 126);   // kept by default
}

// Optional, for checking against a log already accepted by ARRL:
// EME_CABRILLO_ADIF=<adif> EME_CABRILLO_DAYS=2026-09-05,2026-09-06
// EME_CABRILLO_GRIDS=ON4MU=JO20,... EME_CABRILLO_OUT=<file>; skipped without.
void TestEmeCabrillo::real_log ()
{
  auto const adif = qEnvironmentVariable ("EME_CABRILLO_ADIF");
  if (adif.isEmpty ()) QSKIP ("EME_CABRILLO_ADIF not set");
  QFile file {adif};
  QVERIFY (file.open (QIODevice::ReadOnly));
  auto const records = EmeCabrillo::read_adif (file.readAll ());
  QSet<QDate> days;
  for (auto const& day : qEnvironmentVariable ("EME_CABRILLO_DAYS").split (','))
    days << QDate::fromString (day.trimmed (), Qt::ISODate);
  QHash<QString, QString> grids, fixes;
  for (auto const& pair : qEnvironmentVariable ("EME_CABRILLO_GRIDS").split (',', Qt::SkipEmptyParts))
    grids[pair.section ('=', 0, 0).toUpper ()] = pair.section ('=', 1);
  auto s = EmeCabrillo::select (records, days, qEnvironmentVariable ("EME_CABRILLO_CALL", "DL3WDG"), "JN68");
  for (auto const& qso : s.no_grid) if (grids.contains (qso.call)) fixes[qso.key] = grids[qso.call];
  s = EmeCabrillo::select (records, days, qEnvironmentVariable ("EME_CABRILLO_CALL", "DL3WDG"), "JN68", {}, fixes);
  EmeCabrillo::Header header;
  header.callsign = qEnvironmentVariable ("EME_CABRILLO_CALL", "DL3WDG");
  header.grid = "JN68";
  QFile out {qEnvironmentVariable ("EME_CABRILLO_OUT")};
  QVERIFY (out.open (QIODevice::WriteOnly | QIODevice::Text));
  out.write (EmeCabrillo::cabrillo_text (header, s.qsos).toLatin1 ());
  auto const score = EmeCabrillo::score (s.qsos);
  qInfo () << "QSOs" << s.qsos.size () << "grids" << score.multipliers << "claimed" << score.claimed
           << "dupes" << s.dupes.size () << "no grid" << s.no_grid.size ();
}

void TestEmeCabrillo::reads_adif_by_byte_length ()
{
  auto const records = EmeCabrillo::read_adif (
    QByteArray {"header <adif_ver:5>3.1.4 <EOH>"}
    + field ("CALL", "OH2XYZ") + field ("NAME", QString::fromUtf8 ("Jürgen")) + field ("GRIDSQUARE", "kp20lf")
    + "<eor><CALL:4:S>K1JT<eor>");
  QCOMPARE (records.size (), 2);
  QCOMPARE (records[0].value ("NAME"), QString::fromUtf8 ("Jürgen"));
  QCOMPARE (records[0].value ("GRIDSQUARE"), QString {"kp20lf"});
  QCOMPARE (records[1].value ("CALL"), QString {"K1JT"});
  QVERIFY (!records[0].contains ("ADIF_VER"));
}

void TestEmeCabrillo::band_and_mode_codes ()
{
  using EmeCabrillo::Record;
  QCOMPARE (EmeCabrillo::band_code (Record {{"BAND", "3cm"}}), QString {"10G"});
  QCOMPARE (EmeCabrillo::band_code (Record {{"BAND", "23CM"}}), QString {"1.2G"});
  QCOMPARE (EmeCabrillo::band_code (Record {{"BAND", "2m"}}), QString {"144"});
  QCOMPARE (EmeCabrillo::band_code (Record {{"FREQ", "10368.100"}}), QString {"10G"});
  QCOMPARE (EmeCabrillo::band_code (Record {{"BAND", "20m"}}), QString {});
  QCOMPARE (EmeCabrillo::band_code (Record {{"BAND", "13cm"}, {"SAT_NAME", "QO-100"}}), QString {});
  QCOMPARE (EmeCabrillo::band_code (Record {{"BAND", "13cm"}, {"PROP_MODE", "SAT"}}), QString {});
  QCOMPARE (EmeCabrillo::mode_code (Record {{"MODE", "MFSK"}, {"SUBMODE", "Q65"}}), QString {"DG"});
  QCOMPARE (EmeCabrillo::mode_code (Record {{"MODE", "CW"}}), QString {"CW"});
  QCOMPARE (EmeCabrillo::mode_code (Record {{"MODE", "SSB"}}), QString {"PH"});
  QCOMPARE (EmeCabrillo::grid4 ("jn68lm"), QString {"JN68"});
  QCOMPARE (EmeCabrillo::grid4 ("RR73"), QString {});
}

void TestEmeCabrillo::selects_the_entry ()
{
  auto const s = EmeCabrillo::select (EmeCabrillo::read_adif (contest_log ()), contest_days (), "dl3wdg", "JN68LM");
  QCOMPARE (calls (s.qsos), (QStringList {"K1JT", "K1JT", "W5LUA", "OK1KIR", "PA3DZL", "W5LUA"}));
  QStringList bands, modes;
  for (auto const& qso : s.qsos) {bands << qso.band; modes << qso.mode;}
  QCOMPARE (bands, (QStringList {"1.2G", "144", "1.2G", "1.2G", "50", "1.2G"}));
  QCOMPARE (modes, (QStringList {"DG", "CW", "DG", "DG", "PH", "DG"}));
  QCOMPARE (s.qsos[3].grid, QString {"JO70"});          // from the QSO between the weekends
  QVERIFY (s.qsos[3].filled);
  QCOMPARE (s.dupes.size (), 1);
  QCOMPARE (s.dupes[0].time, QString {"0200"});
  QCOMPARE (calls (s.no_grid), QStringList {"G4CCH"});
  QCOMPARE (calls (s.need_grid), (QStringList {"OK1KIR", "G4CCH"}));
  QCOMPARE (s.satellite, 1);
  QCOMPARE (s.bands, (QSet<QString> {"1.2G", "144", "50"}));
  QCOMPARE (s.qsos[0].my_call, QString {"DL3WDG"});
  QCOMPARE (s.qsos[0].my_grid, QString {"JN68"});

  auto const one = EmeCabrillo::select (EmeCabrillo::read_adif (contest_log ()), contest_days (),
                                        "DL3WDG", "JN68", {"1.2G"});
  QCOMPARE (one.qsos.size (), 4);
  QCOMPARE (one.other, 2);
  QCOMPARE (one.bands, (QSet<QString> {"1.2G", "144", "50"}));   // still offered
  QVERIFY (EmeCabrillo::select (EmeCabrillo::read_adif (contest_log ()), {}, "DL3WDG", "JN68").qsos.isEmpty ());
}

void TestEmeCabrillo::typed_grids ()
{
  auto const records = EmeCabrillo::read_adif (contest_log ());
  QHash<QString, QString> fixes;
  fixes[EmeCabrillo::record_key (records[7])] = "io91";      // G4CCH
  fixes[EmeCabrillo::record_key (records[5])] = "JO60";      // OK1KIR: wins over the grid found
  auto const s = EmeCabrillo::select (records, contest_days (), "DL3WDG", "JN68", {}, fixes);
  QCOMPARE (s.qsos.size (), 7);
  QVERIFY (s.no_grid.isEmpty ());
  QHash<QString, EmeCabrillo::Qso> by_call;
  for (auto const& qso : s.qsos) by_call[qso.call] = qso;
  QCOMPARE (by_call["G4CCH"].grid, QString {"IO91"});
  QVERIFY (by_call["G4CCH"].entered);
  QCOMPARE (by_call["OK1KIR"].grid, QString {"JO60"});
  QVERIFY (!by_call["OK1KIR"].filled);
  QCOMPARE (calls (s.need_grid), (QStringList {"OK1KIR", "G4CCH"}));
}

void TestEmeCabrillo::score_and_text ()
{
  auto const s = EmeCabrillo::select (EmeCabrillo::read_adif (contest_log ()), contest_days (), "DL3WDG", "JN68LM");
  auto const score = EmeCabrillo::score (s.qsos);
  QCOMPARE (score.points, 600);
  QCOMPARE (score.multipliers, 6);
  QCOMPARE (score.claimed, qint64 {3600});
  QCOMPARE (score.bands.size (), 3);
  QCOMPARE (score.bands[0].band, QString {"50"});
  QCOMPARE (score.bands[2].band, QString {"1.2G"});
  QCOMPARE (score.bands[2].qsos, 4);
  QCOMPARE (score.bands[2].grids, 4);

  EmeCabrillo::Header header;
  header.callsign = "dl3wdg";
  header.grid = "JN68LM";
  header.location = "DX";
  auto const lines = EmeCabrillo::cabrillo_text (header, s.qsos).split ('\n');
  QCOMPARE (lines.first (), QString {"START-OF-LOG: 3.0"});
  QCOMPARE (lines[lines.size () - 2], QString {"END-OF-LOG:"});
  QVERIFY (lines.contains ("CALLSIGN: DL3WDG"));
  QVERIFY (lines.contains ("CONTEST: ARRL-EME"));
  QVERIFY (lines.contains ("CATEGORY-OPERATOR: SINGLE-OP"));
  QVERIFY (lines.contains ("CATEGORY-BAND: ALL"));
  QVERIFY (lines.contains ("CATEGORY-MODE: MIXED"));
  QVERIFY (lines.contains ("GRID-LOCATOR: JN68"));
  QVERIFY (lines.contains ("CLAIMED-SCORE: 3600"));
  QVERIFY (lines.contains ("LOCATION: DX"));
  QVERIFY (lines.contains ("QSO:  1.2G DG 2026-10-31 0100 DL3WDG        JN68 K1JT          FN20"));
  for (auto const& line : lines) QVERIFY2 (!line.startsWith ("NAME"), qPrintable (line));

  auto const one = EmeCabrillo::select (EmeCabrillo::read_adif (contest_log ()), contest_days (),
                                        "DL3WDG", "JN68", {"1.2G"});
  auto const text = EmeCabrillo::cabrillo_text (header, one.qsos);
  QVERIFY (text.contains ("CATEGORY-BAND: 1.2G\n"));
  QVERIFY (text.contains ("CATEGORY-MODE: DIGI\n"));
}

// The dialog, offscreen: grids typed in, a band unticked, the file saved,
// the ADIF file left as it was.
void TestEmeCabrillo::dialog ()
{
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  auto const adif = directory.filePath ("wsjtx_log.adi");
  {
    QFile file {adif};
    QVERIFY (file.open (QIODevice::WriteOnly));
    file.write (contest_log ());
  }
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  EmeCabrilloDialog dialog {&settings, adif, "DL3WDG", "JN68LM", contest_days (), directory.path ()};
  QVERIFY (dialog.cabrillo_text ().contains ("CLAIMED-SCORE: 3600\n"));
  QCOMPARE (dialog.selection ().no_grid.size (), 1);

  QSet<QDate> changed;
  connect (&dialog, &EmeCabrilloDialog::days_changed, [&changed] (QSet<QDate> const& days) {changed = days;});

  auto type = [&dialog] (QString const& call, QString const& grid) {
    auto edit = dialog.findChild<QLineEdit *> ("grid_" + call);
    QVERIFY2 (edit, qPrintable (call));
    edit->setText (grid);
    Q_EMIT edit->editingFinished ();
  };
  type ("G4CCH", "XX");                 // not a grid: ignored
  QCOMPARE (dialog.selection ().no_grid.size (), 1);
  QVERIFY (dialog.findChild<QLineEdit *> ("grid_G4CCH")->styleSheet ().contains ("#ffd0d0"));
  type ("G4CCH", "io91ab");
  QVERIFY (dialog.selection ().no_grid.isEmpty ());
  QCOMPARE (dialog.selection ().qsos.size (), 7);
  QVERIFY (dialog.cabrillo_text ().contains ("G4CCH         IO91\n"));

  QCheckBox * box {nullptr};
  for (auto b : dialog.findChildren<QCheckBox *> ()) if (b->text () == "144") box = b;
  QVERIFY (box);
  box->setChecked (false);
  QCOMPARE (dialog.selection ().qsos.size (), 6);
  QVERIFY (!dialog.cabrillo_text ().contains ("QSO:   144"));

  auto const out = directory.filePath ("DL3WDG_ARRL-EME_2026.log");
  QVERIFY (dialog.save_to (out));
  QFile saved {out};
  QVERIFY (saved.open (QIODevice::ReadOnly | QIODevice::Text));
  QCOMPARE (QString::fromLatin1 (saved.readAll ()), dialog.cabrillo_text ());
  QFile log {adif};
  QVERIFY (log.open (QIODevice::ReadOnly));
  QCOMPARE (log.readAll (), contest_log ());
  QVERIFY (changed.isEmpty ());         // days not touched
}

QTEST_MAIN (TestEmeCabrillo)
#include "test_eme_cabrillo.moc"
