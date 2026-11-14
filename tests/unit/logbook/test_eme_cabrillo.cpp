#include <QtTest>
#include <QCheckBox>
#include <QFile>
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
  void dialog ();
  void real_log ();
};

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
