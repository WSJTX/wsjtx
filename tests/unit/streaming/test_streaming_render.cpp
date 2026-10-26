// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "DecodeEventLine.hpp"
#include "DecodeOutputPlan.hpp"

namespace
{
using DecodeEventLine::Decode;
using DecodeEventLine::Finished;

Decode decodeOf (QJsonObject const& event)
{
  Decode decode;
  decode.mode = event.value ("mode").toString ().toStdString ();
  decode.time = event.value ("time").toString ().toStdString ();
  decode.snr = event.value ("snr").toInt ();
  decode.dt = static_cast<float> (event.value ("dt").toDouble ());
  decode.freq = event.value ("freq").toInt ();
  decode.message = event.value ("message").toString ().toStdString ();
  decode.ap = event.value ("ap").toInt ();
  decode.sync = event.value ("sync").toString ().toStdString ();
  decode.flags = event.value ("flags").toString ().toStdString ();
  decode.q = event.value ("q").toInt (-1);
  decode.avg = event.value ("avg").toInt ();
  decode.copied = event.value ("copied").toBool ();
  decode.period = event.value ("period").toInt ();
  decode.hasSpread = event.contains ("spread");
  decode.spread = static_cast<float> (event.value ("spread").toDouble ());
  decode.textWidth = event.value ("text_width").toInt (-1);
  decode.pass = event.value ("pass").toInt ();
  return decode;
}

Finished finishedOf (QJsonObject const& event)
{
  return {event.value ("synced").toInt (), event.value ("decoded").toInt (), event.value ("average").toInt ()};
}

QString lineOf (Decode const& decode)
{
  return QString::fromStdString (DecodeEventLine::line (decode));
}

Decode make (std::string mode, std::string time, int snr, float dt, int freq, std::string message)
{
  Decode decode;
  decode.mode = mode;
  decode.time = time;
  decode.snr = snr;
  decode.dt = dt;
  decode.freq = freq;
  decode.message = message;
  return decode;
}

struct Pcm
{
  int rate {0};
  QByteArray samples;
};

Pcm readWav (QString const& path)
{
  Pcm pcm;
  QFile file {path};
  if (!file.open (QIODevice::ReadOnly)) return pcm;
  auto const bytes = file.readAll ();
  if (bytes.left (4) != "RIFF" || bytes.mid (8, 4) != "WAVE") return pcm;
  for (int at = 12; at + 8 <= bytes.size ();)
    {
      auto const size = int (qFromLittleEndian<quint32> (bytes.constData () + at + 4));
      if (bytes.mid (at, 4) == "fmt ") pcm.rate = int (qFromLittleEndian<quint32> (bytes.constData () + at + 12));
      if (bytes.mid (at, 4) == "data")
        {
          pcm.samples = bytes.mid (at + 8, size);
          break;
        }
      at += 8 + size + (size & 1);
    }
  return pcm;
}

// The stream takes 12 kHz only; jt9 decodes the same resampled file.
QByteArray samples12k (Pcm const& pcm)
{
  if (pcm.rate == 12000) return pcm.samples;
  auto const count = pcm.samples.size () / 2;
  auto const sample = [&] (int i) {return double (qFromLittleEndian<qint16> (pcm.samples.constData () + 2 * i));};
  auto const n = int (qint64 (count) * 12000 / pcm.rate);
  QByteArray out (2 * n, '\0');
  for (int i = 0; i < n; ++i)
    {
      double const x = double (i) * pcm.rate / 12000;
      int const k = int (x);
      double const f = x - k;
      double const value = sample (k) * (1 - f) + (k + 1 < count ? sample (k + 1) : sample (k)) * f;
      qToLittleEndian<qint16> (qint16 (qBound (-32768, qRound (value), 32767)), out.data () + 2 * i);
    }
  return out;
}

bool writeWav (QString const& path, QByteArray const& samples)
{
  QByteArray header {"RIFF"};
  auto const u32 = [&header] (quint32 v) {char b[4]; qToLittleEndian (v, b); header.append (b, 4);};
  auto const u16 = [&header] (quint16 v) {char b[2]; qToLittleEndian (v, b); header.append (b, 2);};
  u32 (36 + samples.size ());
  header += "WAVEfmt ";
  u32 (16); u16 (1); u16 (1); u32 (12000); u32 (24000); u16 (2); u16 (16);
  header += "data";
  u32 (samples.size ());
  QFile file {path};
  return file.open (QIODevice::WriteOnly) && file.write (header + samples) == header.size () + samples.size ();
}

QByteArray frame (quint8 type, QByteArray const& body)
{
  QByteArray bytes (5, '\0');
  bytes[0] = char (type);
  qToLittleEndian<quint32> (quint32 (body.size ()), bytes.data () + 1);
  return bytes + body;
}

QByteArray sessionHeader ()
{
  return QByteArray {"WSJT\0\1\x0c\0", 8};
}

QByteArray configure (QJsonObject keys)
{
  keys.insert ("t", "configure");
  return frame (0x02u, QJsonDocument {keys}.toJson (QJsonDocument::Compact));
}

QByteArray audio (QByteArray const& samples)
{
  QByteArray frames;
  for (int offset = 0; offset < samples.size (); offset += 24000) frames += frame (0x01u, samples.mid (offset, 24000));
  return frames;
}

QList<QJsonObject> eventsOf (QByteArray const& output)
{
  QList<QJsonObject> events;
  for (auto const& line : output.split ('\n'))
    if (!line.isEmpty ()) events << QJsonDocument::fromJson (line).object ();
  return events;
}

// A WAV name's time, as the stream's keys: the last six digits HHMMSS, else _HHMM.
QJsonObject timeOf (QString const& name)
{
  auto const stem = name.left (name.lastIndexOf ('.'));
  auto const last6 = stem.right (6);
  if (last6.size () == 6 && std::all_of (last6.begin (), last6.end (), [] (QChar c) {return c.isDigit ();}))
    return {{"utc", last6.left (2) + ':' + last6.mid (2, 2) + ':' + last6.right (2)}};
  return {{"nutc", stem.right (4).toInt ()}};
}

struct Run
{
  bool ok {false};
  QByteArray out;
  QByteArray err;
};

Run run (QString const& program, QStringList const& arguments, QByteArray const& input, QString const& directory)
{
  QProcess process;
  auto environment = QProcessEnvironment::systemEnvironment ();
  environment.insert ("OMP_STACKSIZE", "16M");
  process.setProcessEnvironment (environment);
  process.setWorkingDirectory (directory);
  process.start (program, arguments);
  Run result;
  if (!process.waitForStarted (5000)) return result;
  process.write (input);
  process.closeWriteChannel ();
  bool const finished = process.waitForFinished (-1);
  result.out = process.readAllStandardOutput ().replace ("\r\n", "\n");
  result.err = process.readAllStandardError ();
  result.ok = finished && process.exitStatus () == QProcess::NormalExit && process.exitCode () == 0;
  return result;
}

QString describe (DecodeOutputPlan::PreparedLine const& prepared)
{
  QStringList parts {QString::number (int (prepared.disposition)), QString::fromLatin1 (prepared.normalizedLine),
    prepared.decodedOriginal, prepared.decodedForLogic, prepared.logicMessage.string (),
    QString::number (prepared.averaged), QString::number (prepared.averageCount),
    QString::number (prepared.haveFrequencySpread), QString::number (double (prepared.frequencySpread)),
    QString::number (prepared.batchHasDecodes), QString::number (prepared.q65SingleDecodes),
    QString::number (prepared.q65AveragedDecodes), prepared.reason};
  for (auto const& action : prepared.actions) parts << QString::number (int (action.kind)) + ':' + action.text;
  return parts.join ('|');
}

// The rendered lines must equal jt9's, and so must what the GUI makes of them.
void compareLines (QStringList rendered, QStringList expected, DecodeOutputPlan::PreparationContext const& context,
                   bool anyOrder)
{
  if (anyOrder)
    {
      rendered.sort ();
      expected.sort ();
    }
  QVERIFY2 (rendered == expected, qPrintable ("rendered:\n|" + rendered.join ("|\n|") + "|\njt9:\n|"
                                             + expected.join ("|\n|") + "|"));
  for (int i = 0; i < rendered.size (); ++i)
    QCOMPARE (describe (DecodeOutputPlan::prepareLine (rendered[i].toLatin1 (), context)),
              describe (DecodeOutputPlan::prepareLine (expected[i].toLatin1 (), context)));
}
}

Q_DECLARE_METATYPE (DecodeEventLine::Decode)

class TestStreamingRender final : public QObject
{
  Q_OBJECT

private slots:
  // Each line as gfortran writes it from decoder_callbacks.f90's formats.
  void rendersJt9Lines_data ()
  {
    QTest::addColumn<Decode> ("event");
    QTest::addColumn<QString> ("expected");
    auto row = [] (char const* name, Decode const& decode, QString const& line) {
      QTest::newRow (name) << decode << line;
    };
    auto const ft8 = make ("FT8", "133430", 15, 0.300000012f, 2571, "W1FC F5BZB -08");
    auto const ft8Line = [] (QString const& dt) {
      return "133430  15" + dt + " 2571 ~  W1FC F5BZB -08                          ";
    };
    row ("ft8", ft8, ft8Line ("  0.3"));
    for (auto const& tie : QList<QPair<float, QString>> {{0.25f, "  0.2"}, {0.75f, "  0.8"}, {-0.25f, " -0.2"},
                                                        {0.254999995f, "  0.3"}, {-0.0f, " -0.0"},
                                                        {-0.04f, " -0.0"}, {999.96f, "*****"}, {-9.96f, "-10.0"}})
      {
        auto decode = ft8;
        decode.dt = tie.first;
        QTest::newRow (qPrintable (QString {"dt %1"}.arg (double (tie.first), 0, 'g', 9)))
          << decode << ft8Line (tie.second);
      }
    auto wide = ft8;
    wide.snr = -1000;
    wide.dt = 0.1f;
    row ("snr too wide", wide, "133430****  0.1 2571 ~  W1FC F5BZB -08                          ");
    auto ap = make ("FT8", "001234", -5, 1.f, 1500, "K1ABC W9XYZ RR73                    ?");
    ap.ap = 2;
    row ("ft8 a2", ap, "001234  -5  1.0 1500 ~  K1ABC W9XYZ RR73                    ? a2");
    auto ft4 = make ("FT4", "000002", -10, -0.2f, 296, "N1TRK N4FKH 569 VA");
    ft4.ap = 11;
    row ("ft4 a*", ft4, "000002 -10 -0.2  296 +  N1TRK N4FKH 569 VA                    a*");
    auto jt65 = make ("JT65", "000100", -30, 5.5f, 1683, "");
    row ("jt65 below min sync", jt65, "0001 -30  5.5 1683");
    jt65 = make ("JT65", "000600", -19, 2.5f, 1699, "K1ABC W9XYZ RRR    OOO");
    jt65.sync = "##";
    jt65.flags = "d2";
    row ("jt65 ## d2", jt65, "0006 -19  2.5 1699 ## K1ABC W9XYZ RRR    OOO d2 ");
    jt65 = make ("JT65", "000400", -28, 2.5f, 1700, "K1ABC G4XYZ IO91");
    jt65.sync = "#*";
    jt65.flags = "a22";
    row ("jt65 #* a22", jt65, "0004 -28  2.5 1700 #* K1ABC G4XYZ IO91       a22");
    jt65.message.clear ();
    jt65.flags.clear ();
    row ("jt65 sync only", jt65, "0004 -28  2.5 1700 #*                           ");
    auto jt4 = make ("JT4", "040700", -26, 2.28299308f, 1481, "");
    row ("jt4 no sync", jt4, "0407 -26  2.3 1481 $");
    jt4 = make ("JT4", "175700", -14, 1.8f, 1199, "G3WDG OK1KIR RRR");
    jt4.sync = "#";
    jt4.flags = "f";
    row ("jt4 $# f", jt4, "1757 -14  1.8 1199 $# G3WDG OK1KIR RRR       f  ");
    jt4.message.clear ();
    jt4.flags.clear ();
    jt4.sync = "*";
    row ("jt4 sync only", jt4, "1757 -14  1.8 1199 $*                           ");
    jt4.message = "CQ OK1KIR JO60       ?";
    jt4.flags = "d*3";
    row ("jt4 d*3", jt4, "1757 -14  1.8 1199 $* CQ OK1KIR JO60       ? d*3");
    auto fst4 = make ("FST4", "000000", 17, 0.4f, 1331, "CQ K9KFR EN71");
    fst4.period = 15;
    fst4.hasSpread = true;
    fst4.spread = 0.123f;
    row ("fst4 15 spread", fst4, "000000  17  0.4 1331 `  CQ K9KFR EN71                            0.123");
    fst4 = make ("FST4", "005800", -29, 0.4f, 1135, "CQ N3HSK FM28                       ?");
    fst4.period = 60;
    fst4.ap = 1;
    row ("fst4 60 a1", fst4, "0058 -29  0.4 1135 `  CQ N3HSK FM28                       ? a1        ");
    fst4.message = "CQ N5TM EL29";
    fst4.ap = 0;
    fst4.hasSpread = true;
    fst4.spread = 1.234f;
    row ("fst4 60 spread", fst4, "0058 -29  0.4 1135 `  CQ N5TM EL29                                1.23");
    // The threshold between f6.3 and f6.2: 0.95 itself, the float below it, and
    // values that round up to it in either format.
    for (auto const& edge : QList<QPair<float, QString>> {{0.95f, "  0.95"}, {0.94999993f, " 0.950"},
                                                         {0.9495f, " 0.950"}, {1.005f, "  1.00"}})
      {
        auto decode = fst4;
        decode.spread = edge.first;
        QTest::newRow (qPrintable (QString {"spread %1"}.arg (double (edge.first), 0, 'g', 9)))
          << decode << "0058 -29  0.4 1135 `  CQ N5TM EL29                              " + edge.second;
      }
    fst4.mode = "FST4W";
    fst4.message = "DL0HOT JO60 30";
    fst4.period = 1800;
    fst4.spread = 0.0625f;
    row ("fst4w spread tie", fst4, "0058 -29  0.4 1135 `  DL0HOT JO60 30                             0.062");
    auto q65 = make ("Q65", "024000", -18, 0.3f, 1008, "K1JT K9AN R-16");
    q65.period = 30;
    q65.q = 0;
    q65.avg = 1;
    row ("q65 30 q0", q65, "024000 -18  0.3 1008 :  K1JT K9AN R-16                        q0 ");
    q65 = make ("Q65", "015100", -26, 0.2f, 1002, "VK7MO VK7PD QE38");
    q65.period = 60;
    q65.q = 0;
    q65.avg = 3;
    row ("q65 q03", q65, "0151 -26  0.2 1002 :  VK7MO VK7PD QE38                      q03");
    q65.q = 3;
    q65.avg = 1;
    q65.copied = true;
    row ("q65 q3#", q65, "0151 -26  0.2 1002 :  VK7MO VK7PD QE38                      q3#");
    q65.avg = 4;
    row ("q65 q34#", q65, "0151 -26  0.2 1002 :  VK7MO VK7PD QE38                      q34#");
    q65.q = 12;
    q65.avg = 12;
    q65.copied = false;
    row ("q65 q**", q65, "0151 -26  0.2 1002 :  VK7MO VK7PD QE38                      q**");
    auto superfox = make ("FT8", "000030", -5, 0.1f, 750, "K1JT AW RR73");
    superfox.textWidth = 12;
    row ("superfox", superfox, "000030  -5  0.1  750 ~  K1JT AW RR73");
    superfox.message = "CQ K1JT";
    superfox.textWidth = 14;
    row ("superfox cq", superfox, "000030  -5  0.1  750 ~  CQ K1JT       ");
    auto replay = make ("FT8", "051115", -12, 0.3f, 1200, "K1JT W9XYZ R-12");
    replay.textWidth = 15;
    row ("rx_messages line", replay, "051115 -12  0.3 1200 ~  K1JT W9XYZ R-12");
    row ("msk144", make ("MSK144", "120800", 2, 0.1f, 1500, "K1ABC W9XYZ R-04"), "");
  }

  void rendersJt9Lines ()
  {
    QFETCH (Decode, event);
    QFETCH (QString, expected);
    QCOMPARE (lineOf (event), expected);
  }

  void rendersDecodeFinished ()
  {
    QCOMPARE (QString::fromStdString (DecodeEventLine::line (Finished {0, 14, 0})),
              QString {"<DecodeFinished>   0  14        0"});
    QCOMPARE (QString::fromStdString (DecodeEventLine::line (Finished {0, 1, 1000})),
              QString {"<DecodeFinished>   0   1     1000"});
    QCOMPARE (QString::fromStdString (DecodeEventLine::line (Finished {0, 1, 1234567890})),
              QString {"<DecodeFinished>   0   1*********"});
    DecodeOutputPlan::PreparationContext context;
    context.mode = "Q65";
    auto const prepared = DecodeOutputPlan::prepareLine ("<DecodeFinished>   0   1     1000", context);
    QCOMPARE (prepared.disposition, DecodeOutputPlan::LineDisposition::Finish);
    QVERIFY (prepared.batchHasDecodes);
    QCOMPARE (prepared.q65SingleDecodes, 1);
  }

  // jt9 decoding a WAV file and the stream decoding the same audio with a
  // configure equal to jt9's options: the stream's events, rendered, are
  // jt9's lines.
  void matchesJt9_data ()
  {
    QTest::addColumn<QStringList> ("files");
    QTest::addColumn<QStringList> ("options");
    QTest::addColumn<double> ("period");
    QTest::addColumn<QJsonObject> ("keys");
    // Rows whose dt values a 2-decimal dt would render wrong.
    QTest::addColumn<bool> ("exposesRounding");
    // With plotspec in the working directory FST4 measures the spread.
    QTest::addColumn<bool> ("doppler");
    // Each period labelled with utc, whatever the file name's form.
    QTest::addColumn<bool> ("iso");
    auto row = [] (char const* name, QStringList files, QStringList options, double period,
                   QJsonObject keys, bool exposes = false, bool doppler = false, bool iso = false) {
      QTest::newRow (name) << files << options << period << keys << exposes << doppler << iso;
    };
    auto const range = [] (QString const& pattern, QStringList const& values) {
      QStringList files;
      for (auto const& value : values) files << pattern.arg (value);
      return files;
    };
    auto const jt65Files = range ("JT65/JT65B/000000_000%1.wav", {"1", "2", "3", "4", "5", "6", "7"});
    QString const q65 {"Q65/"};
    row ("ft8", {"FT8/210703_133430.wav"}, {"-8"}, 15, {{"mode", "FT8"}, {"ntol", 20}}, true);
    row ("ft8-d3", {"FT8/210703_133430.wav"}, {"-8", "-d", "3"}, 15,
         {{"mode", "FT8"}, {"depth", 3}, {"ntol", 20}}, true);
    row ("ft8-mtd", {"FT8/210703_133430.wav"}, {"-8", "-d", "3", "-M", "-N", "4", "-D", "3"}, 15,
         {{"mode", "FT8"}, {"depth", 3}, {"ntol", 20}, {"multithreaded_ft8", true}, {"ft8_threads", 4},
          {"ft8_decoder_start", 3}});
    row ("ft4", {"FT4/000000_000002.wav"}, {"--ft4", "-d", "3"}, 7.5,
         {{"mode", "FT4"}, {"depth", 3}, {"ntol", 20}}, true);
    row ("jt9", {"JT9/130418_1742.wav"}, {"-9"}, 60, {{"mode", "JT9"}, {"ntol", 20}}, true);
    row ("jt9-d3", {"JT9/130418_1742.wav"}, {"-9", "-d", "3"}, 60, {{"mode", "JT9"}, {"depth", 3}, {"ntol", 20}});
    auto with = [] (QJsonObject keys, QJsonObject const& more) {
      for (auto it = more.begin (); it != more.end (); ++it) keys.insert (it.key (), it.value ());
      return keys;
    };
    QJsonObject const jt65 {{"mode", "JT65"}, {"submode", 1}, {"rxfreq", 1700}, {"ntol", 20}};
    QStringList const jt65Options {"--jt65", "-b", "B", "-f", "1700", "-d"};
    row ("jt65", {"JT65/JT65B/000000_0004.wav"}, jt65Options + QStringList {"3"}, 60, with (jt65, {{"depth", 3}}));
    row ("jt65-seq", jt65Files, jt65Options + QStringList {"3"}, 60, with (jt65, {{"depth", 3}}));
    row ("jt65-seq-avg", jt65Files, jt65Options + QStringList {"51"}, 60, with (jt65, {{"depth", 51}}));
    // JT65 averages odd and even minutes apart by an HHMM label, whichever form comes in.
    row ("jt65-seq-avg-utc", jt65Files, jt65Options + QStringList {"51"}, 60, with (jt65, {{"depth", 51}}),
         false, false, true);
    row ("jt65-seq-vhf-avg", jt65Files, jt65Options + QStringList {"51", "-X", "64"}, 60,
         with (jt65, {{"depth", 51}, {"vhf_features", true}}));
    row ("jt65jt9", {"JT65/JT65B/000000_0004.wav"}, {"-6", "-9", "-T", "-b", "B", "-f", "1700", "-d", "3"}, 60,
         {{"mode", "JT65+JT9"}, {"tx_mode", "JT9"}, {"submode", 1}, {"rxfreq", 1700}, {"depth", 3}});
    row ("jt4f", {"JT4/JT4F/OK1KIR_141105_175700.WAV"}, {"--jt4", "-b", "F", "-f", "1200", "-d", "3"}, 60,
         {{"mode", "JT4"}, {"submode", 5}, {"rxfreq", 1200}, {"depth", 3}, {"ntol", 20}});
    row ("jt4a", {"JT4/JT4A/DF2ZC_070926_040700.WAV"}, {"--jt4", "-b", "A", "-d", "3"}, 60,
         {{"mode", "JT4"}, {"submode", 0}, {"depth", 3}, {"ntol", 20}});
    row ("jt4a-avg", {"JT4/JT4A/DF2ZC_070926_040700.WAV"}, {"--jt4", "-b", "A", "-d", "51"}, 60,
         {{"mode", "JT4"}, {"submode", 0}, {"depth", 51}, {"ntol", 20}});
    QJsonObject const q65_30 {{"mode", "Q65"}, {"trperiod", 30}, {"rxfreq", 1000}};
    QStringList const q65_30Options {"--q65", "-p", "30", "-f", "1000", "-d"};
    auto const q65Seq30 = range (q65 + "30A_Ionoscatter_6m/201203_02%1.wav", {"2700", "2800", "2900"});
    row ("q65-30a", {q65 + "30A_Ionoscatter_6m/201203_024000.wav"}, q65_30Options + QStringList {"3"}, 30,
         with (q65_30, {{"depth", 3}}));
    // The QSO's calls let a priori decoding decode each period.
    QStringList const k9an {"-c", "K1JT", "-x", "K9AN", "-Q", "2"};
    QJsonObject const k9anKeys {{"mycall", "K1JT"}, {"his_call", "K9AN"}, {"qso_progress_state", 2}};
    row ("q65-30a-seq", q65Seq30, q65_30Options + QStringList {"3"} + k9an, 30,
         with (with (q65_30, {{"depth", 3}}), k9anKeys));
    row ("q65-30a-seq-avg", q65Seq30, q65_30Options + QStringList {"19"} + k9an, 30,
         with (with (q65_30, {{"depth", 19}}), k9anKeys));
    // jt9 decodes Q65 at 60 s after a 2.5 s EME delay, the GUI with "Decode after EME delay".
    row ("q65-30a-ap", {q65 + "30A_Ionoscatter_6m/201203_024000.wav"},
         q65_30Options + QStringList {"3", "-c", "K1JT", "-x", "K9AN", "-Q", "2"}, 30,
         with (q65_30, {{"depth", 3}, {"mycall", "K1JT"}, {"his_call", "K9AN"}, {"qso_progress_state", 2}}));
    row ("q65-60a", {q65 + "60A_EME_6m/210106_1621.wav"}, {"--q65", "-b", "A", "-p", "60", "-f", "1000", "-d", "3"}, 60,
         {{"mode", "Q65"}, {"trperiod", 60}, {"submode", 0}, {"rxfreq", 1000}, {"depth", 3},
          {"decode_after_eme_delay", true}});
    row ("q65-60b-seq-avg", range (q65 + "60B_1296_Troposcatter/210109_%1.wav", {"0007", "0147", "0151"}),
         {"--q65", "-b", "B", "-p", "60", "-f", "1000", "-d", "19"}, 60,
         {{"mode", "Q65"}, {"trperiod", 60}, {"submode", 1}, {"rxfreq", 1000}, {"depth", 19},
          {"decode_after_eme_delay", true}});
    row ("q65-60d", {q65 + "60D_EME_10GHz/201212_1838.wav"}, {"--q65", "-b", "D", "-p", "60", "-f", "1000", "-d", "3"},
         60, {{"mode", "Q65"}, {"trperiod", 60}, {"submode", 3}, {"rxfreq", 1000}, {"depth", 3},
              {"decode_after_eme_delay", true}});
    row ("q65-120d", {q65 + "120D_Rainscatter_10_GHz/210117_0920.wav"},
         {"--q65", "-b", "D", "-p", "120", "-f", "1000", "-d", "3"}, 120,
         {{"mode", "Q65"}, {"trperiod", 120}, {"submode", 3}, {"rxfreq", 1000}, {"depth", 3}});
    // No settings tried decode these two; the row compares the counts only.
    row ("q65-120e-seq-avg-counts", range (q65 + "120E_Ionoscatter_6m/210130_%1.wav", {"1438", "1442"}),
         {"--q65", "-b", "E", "-p", "120", "-f", "1000", "-d", "19"}, 120,
         {{"mode", "Q65"}, {"trperiod", 120}, {"submode", 4}, {"rxfreq", 1000}, {"depth", 19}});
    row ("q65-300a", {q65 + "300A_Optical_Scatter/201210_0505.wav"}, {"--q65", "-p", "300", "-f", "1000", "-d", "3"},
         300, {{"mode", "Q65"}, {"trperiod", 300}, {"rxfreq", 1000}, {"depth", 3}});
    row ("fst4", {"FST4+FST4W/210115_0058.wav"},
         {"--fst4", "-p", "60", "-f", "1331", "-L", "1000", "-H", "1400", "-d", "3"}, 60,
         {{"mode", "FST4"}, {"trperiod", 60}, {"rxfreq", 1331}, {"nfa", 1000}, {"nfb", 1400}, {"depth", 3},
          {"ntol", 20}, {"noise_blanker_level", 0}});
    row ("fst4-doppler", {"FST4+FST4W/210115_0058.wav"},
         {"--fst4", "-p", "60", "-f", "1331", "-L", "1000", "-H", "1400", "-d", "3"}, 60,
         {{"mode", "FST4"}, {"trperiod", 60}, {"rxfreq", 1331}, {"nfa", 1000}, {"nfb", 1400}, {"depth", 3},
          {"ntol", 20}, {"noise_blanker_level", 0}}, false, true);
    // The rows below decode the build's fixtures (fixture:<path under tests/unit>).
    QJsonObject const fst4_15 {{"mode", "FST4"}, {"trperiod", 15}, {"rxfreq", 1500}, {"nfa", 1300}, {"nfb", 1700},
                               {"depth", 1}, {"ntol", 20}, {"noise_blanker_level", 0}};
    QStringList const fst4_15Options {"--fst4", "-p", "15", "-f", "1500", "-L", "1300", "-H", "1700", "-d", "1"};
    row ("fst4-15", {"fixture:decoder_engine/fst4-engine.wav"}, fst4_15Options, 15, fst4_15);
    row ("fst4-15-doppler", {"fixture:decoder_engine/fst4-engine.wav"}, fst4_15Options, 15, fst4_15, false, true);
    row ("q65-15", {"fixture:decoder_engine/q65-engine.wav"},
         {"--q65", "-p", "15", "-f", "1500", "-L", "1300", "-H", "1700", "-d", "1"}, 15,
         {{"mode", "Q65"}, {"trperiod", 15}, {"submode", 0}, {"rxfreq", 1500}, {"nfa", 1300}, {"nfb", 1700},
          {"depth", 1}});
    QJsonObject const station {{"mycall", "K1ABC"}, {"his_call", "W9XYZ"}, {"qso_progress_state", 3}};
    QStringList const stationOptions {"-c", "K1ABC", "-x", "W9XYZ", "-Q", "3"};
    auto const simulated = [&range] (QString const& sim) {
      return range ("fixture:streaming/render/" + sim + "/000000_00000%1.wav", {"1", "2", "3", "4", "5", "6", "7", "8"});
    };
    row ("ft8-ap", simulated ("ft8ap"), QStringList {"-8", "-d", "3"} + stationOptions, 15,
         with (station, {{"mode", "FT8"}, {"depth", 3}, {"ntol", 20}}));
    row ("ft4-ap", simulated ("ft4ap"), QStringList {"--ft4", "-d", "3"} + stationOptions, 7.5,
         with (station, {{"mode", "FT4"}, {"depth", 3}, {"ntol", 20}}));
    // The GUI's Q65 Pileup activity: contest 1 and the pileup bit, whose a priori
    // decoding leaves the copied bit free, which these signals set (q3#).
    auto const pileup = range ("fixture:streaming/render/q65pileup/000000_000%1.wav", {"0", "1", "2", "3"});
    QJsonObject const pileupKeys {{"mode", "Q65"}, {"trperiod", 30}, {"rxfreq", 1500}, {"depth", 3},
                                  {"mycall", "K1ABC"}, {"his_call", "W9XYZ"}, {"qso_progress_state", 2},
                                  {"contest_type", "q65_pileup"}};
    QStringList const pileupOptions {"--q65", "-p", "30", "-f", "1500", "-d", "3", "-c", "K1ABC", "-x", "W9XYZ",
                                     "-Q", "2"};
    row ("q65-pileup-ap", pileup, pileupOptions + QStringList {"-X", "129"}, 30, pileupKeys, false, false, true);
    // q65_pileup in the frame wins over the activity.
    row ("q65-pileup-off-counts", pileup, pileupOptions + QStringList {"-X", "1"}, 30,
         with (pileupKeys, {{"q65_pileup", false}}), false, false, true);
    row ("fst4w", {"FST4+FST4W/201230_0300.wav"}, {"--fst4w", "-p", "1800", "-f", "1433", "-F", "100", "-d", "3"},
         1800, {{"mode", "FST4W"}, {"trperiod", 1800}, {"rxfreq", 1433}, {"ntol", 100}, {"depth", 3},
                {"noise_blanker_level", 0}});
  }

  void matchesJt9 ()
  {
    QFETCH (QStringList, files);
    QFETCH (QStringList, options);
    QFETCH (double, period);
    QFETCH (QJsonObject, keys);
    QFETCH (bool, exposesRounding);
    QFETCH (bool, doppler);
    QFETCH (bool, iso);
    auto const fixture = files.first ().startsWith ("fixture:");
    auto const root = qEnvironmentVariable (fixture ? "WSJT_RENDER_FIXTURES" : "WSJT_SAMPLES_DIR");
    if (root.isEmpty ()) QSKIP ("run by test_streaming_render_<row>");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    for (auto const& side : {"jt9", "codec"})
      {
        QDir {directory.path ()}.mkpath (side);
        if (doppler) QVERIFY (QFile {directory.filePath (side) + "/plotspec"}.open (QIODevice::WriteOnly));
      }

    QStringList wavs;
    auto input = sessionHeader ();
    keys.insert ("live_audio", false);
    for (auto const& file : files)
      {
        auto const pcm = readWav (root + '/' + QString {file}.remove ("fixture:"));
        QVERIFY2 (pcm.rate > 0 && !pcm.samples.isEmpty (), qPrintable (file));
        auto samples12 = samples12k (pcm);
        auto const name = file.mid (file.lastIndexOf ('/') + 1);
        wavs << directory.filePath (name);
        QVERIFY (writeWav (wavs.last (), samples12));
        auto frameKeys = files.size () > 1 && file != files.first () ? QJsonObject {} : keys;
        auto time = timeOf (name);
        if (iso && time.contains ("nutc"))
          {
            auto const hhmm = time.take ("nutc").toInt ();
            time.insert ("utc", QString {"%1:%2:00"}.arg (hhmm / 100, 2, 10, QChar {'0'}).arg (hhmm % 100, 2, 10, QChar {'0'}));
          }
        for (auto it = time.begin (); it != time.end (); ++it) frameKeys.insert (it.key (), it.value ());
        // Each file of a sequence is one period.
        auto const periodBytes = int (period * 12000) * 2;
        if (files.size () > 1) samples12 = samples12.left (periodBytes).leftJustified (periodBytes, '\0');
        input += configure (frameKeys) + audio (samples12);
      }
    QStringList const paths {"-a", ".", "-t", ".", "-r", qEnvironmentVariable ("WSJT_SHARE_DIR")};
    auto const jt9 = run (QString::fromUtf8 (JT9_EXECUTABLE), options + paths + wavs, {}, directory.filePath ("jt9"));
    QVERIFY2 (jt9.ok, jt9.err.constData ());
    auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), paths, input,
                            directory.filePath ("codec"));
    QVERIFY2 (codec.ok, codec.err.constData ());

    QStringList expected, expectedFinished;
    for (auto const& line : jt9.out.split ('\n'))
      {
        if (line.isEmpty ()) continue;
        (line.startsWith ("<DecodeFinished>") ? expectedFinished : expected) << QString::fromLatin1 (line);
      }
    QStringList rendered, renderedFinished;
    int exposures = 0;
    for (auto const& line : codec.out.split ('\n'))
      {
        if (line.isEmpty ()) continue;
        QJsonParseError error;
        auto const event = QJsonDocument::fromJson (line, &error).object ();
        QVERIFY2 (error.error == QJsonParseError::NoError, line.constData ());
        QVERIFY2 (event.value ("t").toString () != "error", line.constData ());
        if (event.value ("t").toString () == "decode")
          {
            auto decode = decodeOf (event);
            rendered << lineOf (decode);
            decode.dt = QString::number (double (decode.dt), 'f', 2).toFloat ();
            if (lineOf (decode) != rendered.last ()) ++exposures;
          }
        if (event.value ("t").toString () == "decode_finished")
          renderedFinished << QString::fromStdString (DecodeEventLine::line (finishedOf (event)));
      }
    QVERIFY2 (!expectedFinished.isEmpty (), jt9.out.constData ());
    // A row compares lines unless it says it compares the counts only.
    if (QString::fromLatin1 (QTest::currentDataTag ()).endsWith ("-counts")) QVERIFY (expected.isEmpty ());
    else QVERIFY2 (!expected.isEmpty (), jt9.out.constData ());

    DecodeOutputPlan::PreparationContext context;
    context.mode = keys.value ("mode").toString ();
    context.myCall = "K1JT";
    context.diskData = true;
    context.multithreadFt8 = keys.value ("multithreaded_ft8").toBool ();
    context.ft8DecoderStart = keys.value ("ft8_decoder_start").toInt (3);
    // The multithreaded decoder's threads finish in any order.
    compareLines (rendered, expected, context, context.multithreadFt8);
    if (QTest::currentTestFailed ()) return;
    compareLines (renderedFinished, expectedFinished, context, false);
    if (exposesRounding) QVERIFY2 (exposures > 0, "no dt here tells 2 decimals from the decoder's value");
    // A row for a field proves nothing unless its lines show the field.
    auto const all = rendered.join ('\n');
    if (doppler)
      QVERIFY2 (all.contains (QRegularExpression {"\\d\\.\\d\\d\\d?$", QRegularExpression::MultilineOption}),
                qPrintable (all));
    if (QString::fromLatin1 (QTest::currentDataTag ()).endsWith ("-ap"))
      QVERIFY2 (all.contains (QRegularExpression {" (a\\d|q[1-9])"}), qPrintable (all));
  }

  // Settings jt9's command line cannot give (SuperFox; the station flags its
  // WAV mode leaves unset, which the multithreaded decoder's a priori passes
  // read): the worker's lines come from ft8_worker_model, the stream's params
  // through multimode_decoder.
  void matchesTheWorker_data ()
  {
    QTest::addColumn<QStringList> ("wavs");
    QTest::addColumn<QJsonObject> ("keys");
    QTest::addColumn<int> ("finalPass");
    QTest::addColumn<bool> ("superfox");
#ifdef SUPERFOX_WAV
    QTest::newRow ("superfox") << QStringList {QString::fromUtf8 (SUPERFOX_WAV)}
      << QJsonObject {{"mode", "FT8"}, {"utc", "00:00:30"}, {"contest_type", "hound"}, {"superfox_mode", true},
                      {"even_sequence", true}, {"rxfreq", 750}, {"ntol", 100}}
      << 50 << true;
    auto const fixtures = qEnvironmentVariable ("WSJT_RENDER_FIXTURES");
    // SuperFox pads its CQ and free-text lines with blanks.
    QJsonObject const hound {{"mode", "FT8"}, {"utc", "00:00:30"}, {"contest_type", "hound"}, {"superfox_mode", true},
                             {"even_sequence", true}, {"rxfreq", 750}, {"ntol", 100}};
    QTest::newRow ("superfox-cq") << QStringList {fixtures + "/streaming/superfox-cq/000000_000000.wav"}
      << hound << 50 << true;
    QTest::newRow ("superfox-text") << QStringList {fixtures + "/streaming/superfox-text/000000_000000.wav"}
      << hound << 50 << true;
    QStringList simulated;
    for (int n = 1; n <= 8; ++n)
      simulated << QString {"%1/streaming/render/ft8ap/000000_00000%2.wav"}
        .arg (qEnvironmentVariable ("WSJT_RENDER_FIXTURES")).arg (n);
    QTest::newRow ("mtd-ap") << simulated
      << QJsonObject {{"mode", "FT8"}, {"utc", "00:00:01"}, {"depth", 3}, {"ntol", 20}, {"multithreaded_ft8", true},
                      {"ft8_threads", 4}, {"ft8_decoder_start", 3}, {"mycall", "K1ABC"}, {"his_call", "W9XYZ"},
                      {"qso_progress_state", 3}}
      << 49 << false;
#endif
  }

  void matchesTheWorker ()
  {
#ifndef SUPERFOX_WAV
    QSKIP ("the simulators and ft8_worker_model are built only with WSJT_BUILD_UTILS");
#else
    QFETCH (QStringList, wavs);
    QFETCH (QJsonObject, keys);
    QFETCH (int, finalPass);
    QFETCH (bool, superfox);
    keys.insert ("t", "configure");
    keys.insert ("live_audio", false);
    auto const json = QJsonDocument {keys}.toJson (QJsonDocument::Compact);
    QStringList expected, expectedFinished, rendered, renderedFinished;
    // One process each, as the model decodes one period.
    for (auto const& wav : wavs)
      {
        QTemporaryDir directory;
        QVERIFY (directory.isValid ());
        auto const worker = run (QString::fromUtf8 (FT8_WORKER_MODEL),
                                 {wav, QString::fromUtf8 (json), QString::number (finalPass),
                                  qEnvironmentVariable ("WSJT_SHARE_DIR")}, {}, directory.path ());
        QVERIFY2 (worker.ok, worker.err.constData ());
        auto const pcm = readWav (wav);
        QVERIFY (pcm.rate == 12000);
        QStringList const paths {"-a", directory.path (), "-t", directory.path (), "-r",
                                 qEnvironmentVariable ("WSJT_SHARE_DIR")};
        auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), paths,
                                sessionHeader () + frame (0x02u, json) + audio (pcm.samples), directory.path ());
        QVERIFY2 (codec.ok, codec.err.constData ());
        for (auto const& line : worker.out.split ('\n'))
          if (!line.isEmpty ())
            (line.startsWith ("<DecodeFinished>") ? expectedFinished : expected) << QString::fromLatin1 (line);
        for (auto const& event : eventsOf (codec.out))
          {
            if (event.value ("t").toString () == "decode")
              {
                QCOMPARE (event.value ("superfox").toBool (), superfox);
                rendered << lineOf (decodeOf (event));
              }
            if (event.value ("t").toString () == "decode_finished")
              renderedFinished << QString::fromStdString (DecodeEventLine::line (finishedOf (event)));
          }
      }
    QVERIFY (!expected.isEmpty ());
    if (!superfox)
      QVERIFY2 (expected.join ('\n').contains (QRegularExpression {" a\\d$", QRegularExpression::MultilineOption}),
                qPrintable (expected.join ('\n')));
    if (QByteArray {QTest::currentDataTag ()} == "superfox-cq")
      QVERIFY2 (std::any_of (expected.begin (), expected.end (), [] (QString const& line) {return line.endsWith (' ');}),
                qPrintable (expected.join ('\n')));
    DecodeOutputPlan::PreparationContext context;
    context.mode = "FT8";
    context.specOp = superfox ? SpecialOperatingActivity::HOUND : SpecialOperatingActivity::NONE;
    context.diskData = true;
    context.multithreadFt8 = keys.value ("multithreaded_ft8").toBool ();
    context.ft8DecoderStart = keys.value ("ft8_decoder_start").toInt (3);
    compareLines (rendered, expected, context, context.multithreadFt8);
    compareLines (renderedFinished, expectedFinished, context, false);
#endif
  }

  // clear_average clears the averages for the next decode that runs only, as
  // the GUI's Clear Avg does: a later period averages again.
  void clearAverageClearsOnce ()
  {
    auto const samples = qEnvironmentVariable ("WSJT_SAMPLES_DIR");
    if (samples.isEmpty ()) QSKIP ("run with WSJT_SAMPLES_DIR, by test_streaming_render_clear_average");
    QList<QByteArray> periods;
    for (int n = 1; n <= 6; ++n)
      {
        auto const pcm = readWav (QString {"%1/JT65/JT65B/000000_000%2.wav"}.arg (samples).arg (n));
        QVERIFY (pcm.rate == 12000);
        periods << pcm.samples.left (60 * 24000).leftJustified (60 * 24000, '\0');
      }
    // again: the clear comes with a decode again of the period before, and
    // clearOff in a frame of its own after it.
    auto const session = [&] (int clearAt, int clearOffAt, bool again = false) {
      QJsonObject const keys {{"mode", "JT65"}, {"submode", 1}, {"rxfreq", 1700}, {"ntol", 20}, {"depth", 19}};
      auto input = sessionHeader ();
      for (int n = 1; n <= periods.size (); ++n)
        {
          auto frameKeys = n == 1 ? keys : QJsonObject {};
          if (n == clearAt) frameKeys.insert ("clear_average", true);
          if (n == clearAt && again) frameKeys.insert ("nagain_flag", true);
          if (n == clearOffAt && again)
            {
              input += configure (frameKeys);
              frameKeys = {};
            }
          if (n == clearOffAt) frameKeys.insert ("clear_average", false);
          frameKeys.insert ("nutc", n);
          input += configure (frameKeys) + audio (periods[n - 1]);
        }
      QTemporaryDir directory;
      auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                              {"-a", directory.path (), "-t", directory.path ()}, input, directory.path ());
      QList<int> decodes {0};
      for (auto const& event : eventsOf (codec.out))
        {
          if (event.value ("t").toString () == "decode") ++decodes.last ();
          if (event.value ("t").toString () == "decode_finished") decodes << 0;
        }
      decodes.removeLast ();
      return decodes;
    };
    // Period 4 decodes alone and from the average, period 6 likewise.
    auto const none = session (0, 0);
    QCOMPARE (none, (QList<int> {0, 0, 2, 2, 1, 2}));
    auto const cleared = session (4, 0);
    QCOMPARE (cleared, (QList<int> {0, 0, 2, 1, 0, 2}));
    QCOMPARE (session (4, 5), cleared);
    // A decode again takes the clear: the next period is not cleared.
    auto const clearedAgain = session (5, 0, true);
    QCOMPARE (clearedAgain.size (), none.size () + 1);
    QCOMPARE (session (5, 5, true), clearedAgain);
  }

  // Q65's clear_average, one-shot as for JT65: the periods averaged, which
  // decode_finished's average counts, start again from the cleared period.
  void q65ClearAverageClearsOnce ()
  {
    auto const samples = qEnvironmentVariable ("WSJT_SAMPLES_DIR");
    if (samples.isEmpty ()) QSKIP ("run with WSJT_SAMPLES_DIR, by test_streaming_render_clear_average_q65");
    QStringList const times {"0007", "0147", "0151"};
    QList<QByteArray> periods;
    for (auto const& time : times)
      {
        auto const pcm = readWav (samples + "/Q65/60B_1296_Troposcatter/210109_" + time + ".wav");
        QVERIFY (pcm.rate == 12000);
        periods << pcm.samples.left (60 * 24000).leftJustified (60 * 24000, '\0');
      }
    auto const session = [&] (int clearAt, int clearOffAt) {
      QJsonObject const keys {{"mode", "Q65"}, {"trperiod", 60}, {"submode", 1}, {"rxfreq", 1000}, {"depth", 19},
                              {"decode_after_eme_delay", true}};
      auto input = sessionHeader ();
      for (int n = 1; n <= periods.size (); ++n)
        {
          auto frameKeys = n == 1 ? keys : QJsonObject {};
          frameKeys.insert ("nutc", times.at (n - 1).toInt ());
          if (n == clearAt) frameKeys.insert ("clear_average", true);
          if (n == clearOffAt) frameKeys.insert ("clear_average", false);
          input += configure (frameKeys) + audio (periods[n - 1]);
        }
      QTemporaryDir directory;
      auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                              {"-a", directory.path (), "-t", directory.path ()}, input, directory.path ());
      QStringList found;
      int decodes = 0;
      for (auto const& event : eventsOf (codec.out))
        {
          if (event.value ("t").toString () == "decode") ++decodes;
          if (event.value ("t").toString () == "decode_finished")
            {
              found << QString {"%1/%2"}.arg (event.value ("average").toInt ()).arg (decodes);
              decodes = 0;
            }
        }
      return found;
    };
    // The third period decodes only from three averaged.
    QCOMPARE (session (0, 0), (QStringList {"1/0", "2/0", "3/1"}));
    auto const cleared = session (2, 0);
    QCOMPARE (cleared, (QStringList {"1/0", "1/0", "2/0"}));
    QCOMPARE (session (2, 3), cleared);
  }

  // Each decode carries the half-symbol stop of the pass that made it, which
  // the GUI's early-decode dedupe goes by; MSK144's, decoded block by block,
  // carry none.
  void decodesCarryTheirPass ()
  {
    auto const samples = qEnvironmentVariable ("WSJT_SAMPLES_DIR");
    if (samples.isEmpty ()) QSKIP ("run with WSJT_SAMPLES_DIR, by test_streaming_render_pass");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const ft8 = readWav (samples + "/FT8/210703_133430.wav");
    auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                            {"-a", directory.path (), "-t", directory.path ()},
                            sessionHeader () + configure ({{"mode", "FT8"}, {"utc", "13:34:30"}, {"depth", 3}})
                              + audio (ft8.samples), directory.path ());
    QVERIFY2 (codec.ok, codec.err.constData ());
    QList<int> passes;
    for (auto const& event : eventsOf (codec.out))
      if (event.value ("t").toString () == "decode") passes << event.value ("pass").toInt ();
    QVERIFY (passes.size () > 2);
    QVERIFY2 (std::is_sorted (passes.begin (), passes.end ()), codec.out.constData ());
    QCOMPARE (passes.first (), 41);
    for (auto const pass : passes) QVERIFY (pass == 41 || pass == 47 || pass == 50);
    QVERIFY2 (passes.contains (47) || passes.contains (50), codec.out.constData ());

    auto const msk144 = readWav (samples + "/MSK144/181211_120800.wav");
    auto const period = msk144.samples.left (15 * 24000).leftJustified (15 * 24000, '\0');
    auto const blocks = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                             {"-a", directory.path (), "-t", directory.path ()},
                             sessionHeader () + configure ({{"mode", "MSK144"}, {"trperiod", 15}, {"rxfreq", 1500},
                                                            {"ntol", 50}, {"depth", 3}, {"utc", "12:08:00"}})
                               + audio (period) + configure ({{"utc", "12:08:15"}}) + audio (period),
                             directory.path ());
    QVERIFY2 (blocks.ok, blocks.err.constData ());
    int decodes = 0;
    for (auto const& event : eventsOf (blocks.out))
      if (event.value ("t").toString () == "decode")
        {
          ++decodes;
          QVERIFY2 (!event.contains ("pass"), blocks.out.constData ());
        }
    QVERIFY2 (decodes > 1, blocks.out.constData ());
  }
};

QTEST_GUILESS_MAIN (TestStreamingRender)
#include "test_streaming_render.moc"
