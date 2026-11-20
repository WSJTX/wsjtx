// SPDX-License-Identifier: GPL-3.0-or-later
// FreqCal in the codec against the GUI's measurement: freqcal_gui_model calls
// freqcal_ as MainWindow::dataSink does, on the audio the codec measures. The
// audio is carriers at known offsets and levels in Gaussian noise of a fixed
// seed; no FreqCal recording exists. Equalized audio is the GUI's
// (spectrum_gui_model, refspectrum_ as dataSink drives it), and fast JT9's
// equalized row compares with fast_jt9_gui_model on it.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <random>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace
{
constexpr int SampleRate = 12000;
// MainWindow::dataSink's block in FreqCal (initializeFFT(6912)) and the codec's.
constexpr int BlockSamples = 3456;
// freqcal's window, 4.608 s (lib/freqcal.f90).
constexpr int Window = 55296;
// fast JT9's text and frequency, as in test_streaming_fast_jt9.
constexpr char Message[] = "CQ K1ABC FN42";
constexpr int AudioFrequency = 1200;
constexpr double Pi = 3.14159265358979323846;

int waitMs ()
{
  return std::max (120000, qEnvironmentVariableIntValue ("QTEST_FUNCTION_TIMEOUT") / 3);
}

QByteArray frame (quint8 type, QByteArray const& body)
{
  QByteArray bytes;
  bytes.append (char (type));
  auto const length = quint32 (body.size ());
  for (int shift = 0; shift < 32; shift += 8) bytes.append (char ((length >> shift) & 0xffu));
  return bytes + body;
}

QByteArray sessionHeader ()
{
  QByteArray bytes {"WSJT", 4};
  bytes.append ('\0');
  bytes.append ('\1');
  bytes.append ('\x0c');
  bytes.append ('\0');
  return bytes;
}

QByteArray control (QJsonObject keys, char const* type = "configure")
{
  keys.insert ("t", type);
  return frame (0x02u, QJsonDocument {keys}.toJson (QJsonDocument::Compact));
}

QByteArray halt ()
{
  return frame (0x02u, R"({"t":"halt"})");
}

QJsonObject with (QJsonObject keys, QJsonObject const& more)
{
  for (auto it = more.constBegin (); it != more.constEnd (); ++it) keys.insert (it.key (), it.value ());
  return keys;
}

// Samples [from, to) of pcm as audio frames of `each` samples.
QByteArray audio (QByteArray const& pcm, int from, int to, int each = 4096)
{
  QByteArray bytes;
  for (int k = from; k < to; k += each) bytes += frame (0x01u, pcm.mid (2 * k, 2 * std::min (each, to - k)));
  return bytes;
}

QList<QJsonObject> ofType (QList<QJsonObject> const& events, QString const& type)
{
  QList<QJsonObject> matching;
  for (auto const& event : events)
    if (event.value ("t").toString () == type) matching << event;
  return matching;
}

bool writeFile (QString const& path, QByteArray const& bytes)
{
  QFile file {path};
  return file.open (QIODevice::WriteOnly) && file.write (bytes) == bytes.size ();
}

QByteArray readFile (QString const& path)
{
  QFile file {path};
  return file.open (QIODevice::ReadOnly) ? file.readAll () : QByteArray {};
}

struct Run
{
  bool started {false};
  bool timedOut {false};
  int exitCode {-1};
  QByteArray out;
  QByteArray err;
  QList<QJsonObject> events;

  bool clean () const {return started && !timedOut && 0 == exitCode;}
  QString report () const
  {
    return QString {"started %1, timed out %2, exit %3\nstdout:\n%4\nstderr:\n%5"}
      .arg (started).arg (timedOut).arg (exitCode)
      .arg (QString::fromUtf8 (out.right (4000)), QString::fromUtf8 (err.right (4000)));
  }
};

// The codec on input, in directory (-a and -t), or in a fresh one.
Run runCodec (QByteArray const& input, QString directory = {}, QStringList const& environment = {})
{
  Run r;
  QTemporaryDir fresh;
  if (directory.isEmpty ())
    {
      if (!fresh.isValid ()) return r;
      directory = fresh.path ();
    }
  QProcess process;
  auto env = QProcessEnvironment::systemEnvironment ();
  env.insert ("OMP_STACKSIZE", "10M");
  for (auto const& each : environment) env.insert (each.section ('=', 0, 0), each.section ('=', 1));
  process.setProcessEnvironment (env);
  process.setWorkingDirectory (directory);
  QElapsedTimer clock;
  clock.start ();
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), {"-a", directory, "-t", directory});
  r.started = process.waitForStarted (5000);
  if (!r.started) return r;
  QByteArray pending;
  auto const read = [&] {
    QCoreApplication::processEvents ();
    pending += process.readAllStandardOutput ();
    r.err += process.readAllStandardError ();
    int end;
    while ((end = pending.indexOf ('\n')) >= 0)
      {
        auto const line = pending.left (end);
        pending.remove (0, end + 1);
        r.out += line + '\n';
        if (!line.trimmed ().isEmpty ()) r.events << QJsonDocument::fromJson (line).object ();
      }
  };
  auto const pumpUntil = [&] (std::function<bool ()> const& done) {
    while (!done () && QProcess::Running == process.state () && clock.elapsed () < waitMs ())
      {
        if (process.bytesToWrite () > 0) process.waitForBytesWritten (5);
        else process.waitForReadyRead (5);
        read ();
      }
  };
  process.write (input);
  pumpUntil ([&] {return 0 == process.bytesToWrite ();});
  process.closeWriteChannel ();
  pumpUntil ([] {return false;});
  if (QProcess::Running == process.state ())
    {
      r.timedOut = true;
      process.kill ();
      process.waitForFinished (5000);
    }
  read ();
  r.exitCode = process.exitCode ();
  return r;
}

// A model program on pcm with script; its standard output, or empty on failure.
QString runProgram (char const* program, QByteArray const& pcm, QStringList script, QString* failure)
{
  QTemporaryDir directory;
  if (!directory.isValid ())
    {
      *failure = "no directory";
      return {};
    }
  if (!writeFile (directory.filePath ("audio.raw"), pcm))
    {
      *failure = "cannot write the audio";
      return {};
    }
  script.prepend ("audio " + directory.filePath ("audio.raw"));
  if (!writeFile (directory.filePath ("script"), (script.join ('\n') + '\n').toUtf8 ()))
    {
      *failure = "cannot write the script";
      return {};
    }
  QProcess process;
  process.setWorkingDirectory (directory.path ());
  process.start (QString::fromUtf8 (program), {directory.filePath ("script")});
  if (!process.waitForFinished (waitMs ()) || process.exitCode () != 0)
    {
      *failure = QString {"%1 failed: %2"}.arg (program, QString::fromUtf8 (process.readAllStandardError ()));
      return {};
    }
  return QString::fromUtf8 (process.readAllStandardOutput ());
}

// carriers "freq:amplitude,..." in Gaussian noise of sigma, seconds long.
QByteArray carrierAudio (int samples, QString const& carriers, double sigma, unsigned seed)
{
  struct Carrier {double freq, amplitude, phase;};
  QList<Carrier> list;
  for (auto const& each : carriers.split (',', Qt::SkipEmptyParts))
    list << Carrier {each.section (':', 0, 0).toDouble (), each.section (':', 1, 1).toDouble (), 0.3 * (list.size () + 1)};
  std::mt19937 random {seed};
  std::normal_distribution<double> noise {0.0, sigma > 0 ? sigma : 1.0};
  QByteArray pcm (2 * samples, '\0');
  for (int k = 0; k < samples; ++k)
    {
      double value = sigma > 0 ? noise (random) : 0.0;
      for (auto const& c : list) value += c.amplitude * std::sin (2.0 * Pi * c.freq * k / SampleRate + c.phase);
      qToLittleEndian<qint16> (qint16 (std::max (-32767.0, std::min (32767.0, std::round (value)))), pcm.data () + 2 * k);
    }
  return pcm;
}

// Low-passed noise, whose spectrum a reference measurement makes uneven.
QByteArray colouredNoise (int samples, unsigned seed)
{
  std::mt19937 random {seed};
  std::normal_distribution<double> noise {0.0, 1500.0};
  QByteArray pcm (2 * samples, '\0');
  double y = 0;
  for (int k = 0; k < samples; ++k)
    {
      y = 0.8 * y + noise (random);
      qToLittleEndian<qint16> (qint16 (std::max (-32767.0, std::min (32767.0, std::round (0.5 * y)))), pcm.data () + 2 * k);
    }
  return pcm;
}

// a + b, sample by sample, clipped.
QByteArray mix (QByteArray const& a, QByteArray const& b)
{
  QByteArray pcm (std::max (a.size (), b.size ()), '\0');
  for (int k = 0; 2 * k + 1 < pcm.size (); ++k)
    {
      int sum = 0;
      if (2 * k + 1 < a.size ()) sum += qFromLittleEndian<qint16> (a.constData () + 2 * k);
      if (2 * k + 1 < b.size ()) sum += qFromLittleEndian<qint16> (b.constData () + 2 * k);
      qToLittleEndian<qint16> (qint16 (std::max (-32767, std::min (32767, sum))), pcm.data () + 2 * k);
    }
  return pcm;
}

// freqcal's measurements a period, at the GUI's stop ((int(TR/0.288))/8)*8.
int measurements (int trperiod)
{
  auto const stop = (int (trperiod / 0.288) / 8) * 8;
  return (stop - 16) / 8 + 1;
}

struct Measure
{
  int k;
  QString line;
};

// freqcal_gui_model's measurements, one list a period, or empty on failure.
QList<QList<Measure>> runModel (QByteArray const& pcm, QStringList const& script, QString* failure)
{
  auto const output = runProgram (FREQCAL_GUI_MODEL, pcm, script, failure);
  QList<QList<Measure>> periods;
  if (output.isEmpty ()) return periods;
  QList<Measure> current;
  for (auto const& line : output.split ('\n'))
    {
      if (line.startsWith ("measure "))
        {
          auto const rest = line.mid (8);
          current << Measure {rest.section (' ', 0, 0).toInt (), rest.mid (rest.indexOf (' ') + 1)};
        }
      else if ("end" == line)
        {
          periods << current;
          current.clear ();
        }
    }
  return periods;
}

// The line's fields, (i2.2,':',i2.2,':',i2.2,i7,i3,i6,2f10.3,2f7.1,2x,a1,a1)
// (lib/freqcal.f90).
QString lineFreq (QString const& line) {return line.mid (24, 10).trimmed ();}
QString lineError (QString const& line) {return line.mid (34, 10).trimmed ();}
QString lineLevel (QString const& line) {return line.mid (44, 7).trimmed ();}
QString lineSnr (QString const& line) {return line.mid (51, 7).trimmed ();}
bool lineLow (QString const& line) {return line.mid (60, 1) == "*";}

// An event against the GUI's line, field for field but the time; empty when they agree.
QString compareMeasure (QJsonObject const& event, QString const& line)
{
  auto const show = QString::fromUtf8 (QJsonDocument {event}.toJson (QJsonDocument::Compact));
  auto const number = [&] (char const* key, QString const& text) -> QString {
    bool ok = false;
    auto const expected = text.toDouble (&ok);
    if (!ok) return QString {"%1: the GUI's %2 is no number"}.arg (key, text);
    if (!event.value (key).isDouble () || event.value (key).toDouble () != expected)
      return QString {"%1 %2 against the GUI's %3 in %4"}.arg (key, show, text, line);
    return {};
  };
  for (auto const& difference : {number ("freq", lineFreq (line)), number ("error", lineError (line)),
                                 number ("level", lineLevel (line)), number ("snr", lineSnr (line))})
    if (!difference.isEmpty ()) return difference;
  if (!event.value ("low_snr").isBool () || event.value ("low_snr").toBool () != lineLow (line))
    return QString {"low_snr %1 against the GUI's %2"}.arg (show, line);
  QStringList keys = event.keys ();
  keys.sort ();
  if (keys != QStringList ({"error", "freq", "level", "low_snr", "snr", "t", "time", "v"}))
    return QString {"fields %1"}.arg (keys.join (','));
  return {};
}

// FreqCal's time: the period label plus k/12000 s, whole seconds, HHMMSS.
QString labelPlus (int labelSeconds, int k)
{
  auto const t = (labelSeconds + k / SampleRate) % 86400;
  return QString {"%1%2%3"}.arg (t / 3600, 2, 10, QChar {'0'}).arg (t / 60 % 60, 2, 10, QChar {'0'})
    .arg (t % 60, 2, 10, QChar {'0'});
}

QString utc (int seconds)
{
  seconds %= 86400;
  return QString {"%1:%2:%3"}.arg (seconds / 3600, 2, 10, QChar {'0'}).arg (seconds / 60 % 60, 2, 10, QChar {'0'})
    .arg (seconds % 60, 2, 10, QChar {'0'});
}

QJsonObject freqcal (int trperiod, bool live, int rxfreq = 1500, int ntol = 50)
{
  return {{"mode", "FREQCAL"}, {"trperiod", trperiod}, {"rxfreq", rxfreq}, {"ntol", ntol}, {"live_audio", live}};
}

// The decoder's lines, which FreqCal never sends.
int decoderLines (Run const& run)
{
  int n = 0;
  for (auto const* type : {"decode", "decode_finished", "busy", "pass_progress", "pass_skipped"})
    n += ofType (run.events, type).size ();
  return n;
}

// The codec's render of text in fast JT9, or of FT8 (submode empty).
QByteArray render (QString const& mode, QString const& submode, int trperiod, QString const& text, int freq)
{
  QJsonObject request {{"id", 1}, {"mode", mode}, {"text", text}, {"freq", freq}, {"rate", SampleRate}};
  if (!submode.isEmpty ()) request = with (request, {{"submode", submode}, {"trperiod", trperiod}, {"fast", true}});
  auto const run = runCodec (sessionHeader () + control (request, "render"));
  QByteArray pcm;
  for (auto const& event : ofType (run.events, "pcm"))
    pcm += QByteArray::fromBase64 (event.value ("data").toString ().toLatin1 ());
  return pcm;
}

// One period of samples with signal from `at` at a fifth of its scale, in noise of 1500.
QByteArray inNoise (QByteArray const& signal, int samples, int at, unsigned seed)
{
  std::mt19937 random {seed};
  std::normal_distribution<double> noise {0.0, 1500.0};
  QByteArray pcm (2 * samples, '\0');
  for (int k = 0; k < samples; ++k)
    {
      double value = noise (random);
      auto const i = k - at;
      if (i >= 0 && 2 * i + 1 < signal.size ()) value += 0.2 * qFromLittleEndian<qint16> (signal.constData () + 2 * i);
      qToLittleEndian<qint16> (qint16 (std::max (-32767.0, std::min (32767.0, std::round (value)))), pcm.data () + 2 * k);
    }
  return pcm;
}

// A refspec.dat the GUI's measurement writes from coloured noise, or empty.
QByteArray measuredReference (QString* failure)
{
  QTemporaryDir directory;
  if (!directory.isValid ()) return {};
  auto const samples = 15 * SampleRate;
  auto const output = runProgram (SPECTRUM_GUI_MODEL, colouredNoise (samples, 99u),
                                  {"datadir " + directory.path (), "reference measure",
                                   QString {"period live 0 %1"}.arg (samples)}, failure);
  if (output.isEmpty ()) return {};
  return readFile (directory.filePath ("refspec.dat"));
}

// The GUI's equalized samples of pcm, one live period as `kind` ("period live" or "fastjt9 ... trperiod")
// takes it, with reference applied from refspec.dat.
QByteArray guiEqualized (QByteArray const& pcm, QByteArray const& reference, QString const& kind, QString* failure)
{
  QTemporaryDir directory;
  if (!directory.isValid () || !writeFile (directory.filePath ("refspec.dat"), reference)) return {};
  auto const out = directory.filePath ("equalized.pcm");
  auto const output = runProgram (SPECTRUM_GUI_MODEL, pcm, {"datadir " + directory.path (), "reference apply",
                                                            "equalized " + out, kind}, failure);
  if (output.isEmpty ()) return {};
  return readFile (out);
}

// The fast JT9 decode lines fast_jt9_gui_model gives for pcm decoded at k, or {"failed: ..."}.
QStringList guiFastJt9 (QByteArray const& pcm, QString const& submode, int trperiod, int nutc, int k)
{
  QString failure;
  auto const output = runProgram (FAST_JT9_GUI_MODEL, pcm,
                                  {QString {"nutc %1"}.arg (nutc), QString {"ntr %1"}.arg (trperiod),
                                   QString {"submode %1"}.arg (submode.at (0).unicode () - 'A'), "minsync 0",
                                   QString {"rxfreq %1"}.arg (AudioFrequency), "ntol 100",
                                   QString {"decode 0 %1 1"}.arg (k)}, &failure);
  if (output.isEmpty ()) return {"failed: " + failure};
  QStringList lines;
  for (auto const& line : output.split ('\n'))
    if (line.startsWith ("line ")) lines << line.mid (5);
  return lines;
}
}

class TestStreamingFreqcal : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  // Each measurement equals freqcal_ called as dataSink calls it on the same
  // samples, every field but the time; the count a period is the GUI's
  // (1, 3, 5 or 12), its time the label plus k/12000, from a file and live.
  // nkhz, the dial the codec does not have, changes only the line's text.
  void measurementsMatchTheGui_data ()
  {
    QTest::addColumn<int> ("trperiod");
    QTest::addColumn<bool> ("live");
    QTest::addColumn<int> ("rxfreq");
    QTest::addColumn<int> ("ntol");
    QTest::addColumn<QString> ("carriers");   // freq:amplitude,...
    QTest::addColumn<double> ("sigma");
    QTest::addColumn<double> ("expected");    // the carrier measured; 0: none, -1: no peak
    QTest::addColumn<bool> ("low");           // S/N below 20
    for (int trperiod : {5, 10, 15, 30})
      for (bool live : {false, true})
        QTest::newRow (QString {"%1 s %2"}.arg (trperiod).arg (live ? "live" : "file").toUtf8 ().constData ())
          << trperiod << live << 1500 << 50 << QString {"1500.37:2000"} << 300.0 << 1500.37 << false;
    QTest::newRow ("15 s weak") << 15 << false << 1500 << 50 << QString {"1503.21:12"} << 1000.0 << 0.0 << true;
    QTest::newRow ("15 s ntol above rxfreq") << 15 << false << 400 << 500 << QString {"612.3:2000"} << 300.0 << 612.3 << false;
    QTest::newRow ("15 s strong one outside") << 15 << false << 1500 << 100 << QString {"1700:8000,1523.4:1000"}
                                              << 300.0 << 1523.4 << false;
    QTest::newRow ("15 s silence") << 15 << false << 1500 << 50 << QString {} << 0.0 << -1.0 << true;
  }

  void measurementsMatchTheGui ()
  {
    QFETCH (int, trperiod);
    QFETCH (bool, live);
    QFETCH (int, rxfreq);
    QFETCH (int, ntol);
    QFETCH (QString, carriers);
    QFETCH (double, sigma);
    QFETCH (double, expected);
    QFETCH (bool, low);
    auto const periodSamples = trperiod * SampleRate;
    auto const pcm = carrierAudio (2 * periodSamples, carriers, sigma, 1u + unsigned (trperiod));
    auto const label = 12 * 3600 + 30;
    auto const codec = runCodec (sessionHeader ()
                                 + control (with (freqcal (trperiod, live, rxfreq, ntol), {{"utc", utc (label)}, {"busy_events", true}}))
                                 + audio (pcm, 0, periodSamples) + control ({{"utc", utc (label + trperiod)}})
                                 + audio (pcm, periodSamples, 2 * periodSamples) + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (ofType (codec.events, "error").isEmpty (), qPrintable (codec.report ()));
    auto const events = ofType (codec.events, "freqcal");
    QCOMPARE (events.size (), 2 * measurements (trperiod));
    QCOMPARE (decoderLines (codec), 0);
    QStringList script {QString {"ntr %1"}.arg (trperiod), QString {"rxfreq %1"}.arg (rxfreq), QString {"ntol %1"}.arg (ntol)};
    script << QString {"period 0 %1"}.arg (periodSamples) << QString {"period %1 %1"}.arg (periodSamples);
    QString failure;
    auto const model = runModel (pcm, script, &failure);
    QVERIFY2 (model.size () == 2, qPrintable (failure));
    // The dial in the line changes only its seven columns.
    auto const dialled = runModel (pcm, QStringList {"nkhz 14076"} + script, &failure);
    QVERIFY2 (dialled.size () == 2, qPrintable (failure));
    int i = 0;
    for (int p = 0; p < 2; ++p)
      {
        QCOMPARE (model.at (p).size (), measurements (trperiod));
        for (int m = 0; m < model.at (p).size (); ++m, ++i)
          {
            auto const& gui = model.at (p).at (m);
            QCOMPARE (gui.k, Window + 8 * BlockSamples * m);
            auto const& line = gui.line;
            auto const& withDial = dialled.at (p).at (m).line;
            QCOMPARE (withDial.mid (8, 7).trimmed (), QString {"14076"});
            QCOMPARE (withDial.mid (15), line.mid (15));
            // The GUI's routine measures the generated audio.
            if (expected > 0) QVERIFY2 (std::abs (lineFreq (line).toDouble () - expected) < 0.05, qPrintable (line));
            if (expected < 0) QCOMPARE (lineFreq (line), QString {"-99.900"});
            QVERIFY2 (lineLow (line) == low, qPrintable (line));
            auto const difference = compareMeasure (events.at (i), line);
            QVERIFY2 (difference.isEmpty (), qPrintable (QString {"period %1 measurement %2: %3"}.arg (p).arg (m).arg (difference)));
            QCOMPARE (events.at (i).value ("time").toString (), labelPlus (label + p * trperiod, gui.k));
          }
      }
  }

  // The time is the period's label plus k/12000, whole seconds, whatever the
  // clock says: past midnight it wraps, and an nutc label counts as a utc one.
  void timeIsTheLabelPlusTheSamples_data ()
  {
    QTest::addColumn<int> ("trperiod");
    QTest::addColumn<int> ("label");   // seconds of the day
    QTest::addColumn<bool> ("numeric");
    QTest::newRow ("30 s across midnight") << 30 << 23 * 3600 + 59 * 60 + 45 << false;
    QTest::newRow ("10 s nutc") << 10 << 7 * 3600 + 5 * 60 + 10 << true;
  }

  void timeIsTheLabelPlusTheSamples ()
  {
    QFETCH (int, trperiod);
    QFETCH (int, label);
    QFETCH (bool, numeric);
    auto const periodSamples = trperiod * SampleRate;
    auto const pcm = carrierAudio (periodSamples, "1500.37:2000", 300.0, 7u);
    QJsonObject keys = numeric ? QJsonObject {{"nutc", label / 3600 * 10000 + label / 60 % 60 * 100 + label % 60}}
                               : QJsonObject {{"utc", utc (label)}};
    auto const codec = runCodec (sessionHeader () + control (with (freqcal (trperiod, true), keys))
                                 + audio (pcm, 0, periodSamples) + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (ofType (codec.events, "error").isEmpty (), qPrintable (codec.report ()));
    QStringList times, expected;
    for (auto const& event : ofType (codec.events, "freqcal")) times << event.value ("time").toString ();
    for (int k = Window; k <= (int (trperiod / 0.288) / 8) * 8 * BlockSamples; k += 8 * BlockSamples)
      expected << labelPlus (label, k);
    QCOMPARE (times, expected);
  }

  // FREQCAL, case aside, takes trperiod 5, 10, 15 or 30, rxfreq 200 to 5000
  // and ntol 1 to 1000, the GUI's ranges; a frame that would leave it outside
  // them is declined (configure_range_error). It has no integer code, and
  // fast is JT9's.
  void periodRules_data ()
  {
    QTest::addColumn<QByteArray> ("frames");   // control JSON lines
    QTest::addColumn<QString> ("key");          // the refused key; empty: accepted
    QTest::addColumn<QString> ("detail");
    auto const period = QString {"trperiod must be 5, 10, 15 or 30 s in FREQCAL"};
    auto const accepted = QByteArray {R"({"mode":"FREQCAL","trperiod":15})"};
    QTest::newRow ("trperiod 60") << QByteArray {R"({"mode":"FREQCAL","trperiod":60})"} << "trperiod" << period;
    QTest::newRow ("trperiod 20") << QByteArray {R"({"mode":"FREQCAL","trperiod":20})"} << "trperiod" << period;
    QTest::newRow ("trperiod 7.5") << QByteArray {R"({"mode":"FREQCAL","trperiod":7.5})"} << "trperiod" << period;
    QTest::newRow ("no trperiod") << QByteArray {R"({"mode":"FREQCAL"})"} << "trperiod" << period;
    QTest::newRow ("trperiod 45 later") << accepted + "\n" + R"({"trperiod":45})" << "trperiod" << period;
    QTest::newRow ("fast") << QByteArray {R"({"mode":"FREQCAL","trperiod":15,"fast":true})"} << "fast"
                           << "fast applies to JT9";
    QTest::newRow ("rxfreq 199") << QByteArray {R"({"mode":"FREQCAL","trperiod":15,"rxfreq":199})"} << "rxfreq"
                                 << "rxfreq must be from 200 to 5000 Hz in FREQCAL";
    QTest::newRow ("rxfreq 5001") << QByteArray {R"({"mode":"FREQCAL","trperiod":15,"rxfreq":5001})"} << "rxfreq"
                                  << "rxfreq must be from 200 to 5000 Hz in FREQCAL";
    QTest::newRow ("ntol 0") << QByteArray {R"({"mode":"FREQCAL","trperiod":15,"ntol":0})"} << "ntol"
                             << "ntol must be from 1 to 1000 Hz in FREQCAL";
    QTest::newRow ("ntol 1001 later") << accepted + "\n" + R"({"ntol":1001})" << "ntol"
                                      << "ntol must be from 1 to 1000 Hz in FREQCAL";
    QTest::newRow ("integer code") << QByteArray {R"({"mode":1006,"trperiod":15})"} << "unknown_mode" << QString {};
    QTest::newRow ("lower case") << QByteArray {R"({"mode":"freqcal","trperiod":5})"} << QString {} << QString {};
    QTest::newRow ("mixed case") << QByteArray {R"({"mode":"FreqCal","trperiod":30})"} << QString {} << QString {};
    QTest::newRow ("edges") << QByteArray {R"({"mode":"FREQCAL","trperiod":10,"rxfreq":5000,"ntol":1000})"} + "\n"
                               + R"({"rxfreq":200,"ntol":1})" << QString {} << QString {};
    QTest::newRow ("ntol 2000 with mode, capped") << QByteArray {R"({"mode":"FREQCAL","trperiod":15,"ntol":2000})"}
                                                  << QString {} << QString {};
  }

  void periodRules ()
  {
    QFETCH (QByteArray, frames);
    QFETCH (QString, key);
    QFETCH (QString, detail);
    QByteArray input = sessionHeader ();
    for (auto const& line : frames.split ('\n')) input += frame (0x02u, R"({"t":"configure",)" + line.mid (1));
    // A carrier, for an accepted FREQCAL to measure.
    auto const pcm = carrierAudio (30 * SampleRate, "1500.37:2000", 300.0, 3u);
    auto const codec = runCodec (input + audio (pcm, 0, 30 * SampleRate) + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const errors = ofType (codec.events, "error");
    if (key.isEmpty ())
      {
        QVERIFY2 (errors.isEmpty (), qPrintable (codec.report ()));
        QVERIFY2 (!ofType (codec.events, "freqcal").isEmpty (), qPrintable (codec.report ()));
        return;
      }
    QCOMPARE (errors.size (), 1);
    if ("unknown_mode" == key)
      {
        QCOMPARE (errors.first ().value ("code").toString (), QString {"unknown_mode"});
        QVERIFY (ofType (codec.events, "freqcal").isEmpty ());
        return;
      }
    QCOMPARE (errors.first ().value ("code").toString (), QString {"configure_range_error"});
    QCOMPARE (errors.first ().value ("key").toString (), key);
    QCOMPARE (errors.first ().value ("mode").toString (), QString {"FREQCAL"});
    QCOMPARE (errors.first ().value ("detail").toString (), detail);
    // A declined frame changes nothing: an earlier accepted FREQCAL measures on, any other mode does not.
    auto const stayed = frames.startsWith (R"({"mode":"FREQCAL","trperiod":15})") && frames.contains ('\n');
    QCOMPARE (ofType (codec.events, "freqcal").isEmpty (), !stayed);
  }

  // Entering FREQCAL, rxfreq is 1500 and ntol 50 unless the frame sets them,
  // as WSJT-X sets Rx Freq to 1500 on choosing FreqCal and keeps JT9's F Tol
  // (50 unless changed). Like every configure that carries mode or trperiod,
  // a FreqCal one resets ntol, to 50; one that carries only trperiod keeps
  // rxfreq. Carriers at 1520.3, 1580.7 and 1680.4 Hz, the last the strongest,
  // tell the windows apart.
  void entryDefaults_data ()
  {
    QTest::addColumn<QByteArray> ("frames");   // control JSON lines
    QTest::addColumn<int> ("trperiod");         // FreqCal's period after them
    QTest::addColumn<int> ("rxfreq");           // the window the GUI's routine gets
    QTest::addColumn<int> ("ntol");
    QTest::addColumn<double> ("expected");      // the carrier measured there
    QTest::newRow ("from FT8 with rxfreq 100") << QByteArray {R"({"mode":"FT8","rxfreq":100,"ntol":10})"} + "\n"
                                                    + R"({"mode":"FREQCAL","trperiod":15})"
                                                 << 15 << 1500 << 50 << 1520.3;
    QTest::newRow ("from fast JT9 with rxfreq 700")
      << QByteArray {R"({"mode":"JT9","fast":true,"submode":"E","trperiod":15,"rxfreq":700,"ntol":100})"} + "\n"
           + R"({"mode":"FREQCAL","trperiod":10})" << 10 << 1500 << 50 << 1520.3;
    QTest::newRow ("trperiod alone keeps rxfreq")
      << QByteArray {R"({"mode":"FREQCAL","trperiod":15,"rxfreq":1600,"ntol":100})"} + "\n" + R"({"trperiod":10})"
      << 10 << 1600 << 50 << 1580.7;
    QTest::newRow ("mode again") << QByteArray {R"({"mode":"FREQCAL","trperiod":15,"rxfreq":1600,"ntol":100})"} + "\n"
                                      + R"({"mode":"FREQCAL","trperiod":10})" << 10 << 1500 << 50 << 1520.3;
  }

  void entryDefaults ()
  {
    QFETCH (QByteArray, frames);
    QFETCH (int, trperiod);
    QFETCH (int, rxfreq);
    QFETCH (int, ntol);
    QFETCH (double, expected);
    QByteArray input = sessionHeader ();
    for (auto const& line : frames.split ('\n')) input += frame (0x02u, R"({"t":"configure",)" + line.mid (1));
    auto const periodSamples = trperiod * SampleRate;
    auto const pcm = carrierAudio (periodSamples, "1520.3:1000,1580.7:4000,1680.4:6000", 300.0, 37u);
    auto const codec = runCodec (input + audio (pcm, 0, periodSamples) + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (ofType (codec.events, "error").isEmpty (), qPrintable (codec.report ()));
    auto const events = ofType (codec.events, "freqcal");
    QCOMPARE (events.size (), measurements (trperiod));
    QString failure;
    auto const model = runModel (pcm, {QString {"ntr %1"}.arg (trperiod), QString {"rxfreq %1"}.arg (rxfreq),
                                       QString {"ntol %1"}.arg (ntol), QString {"period 0 %1"}.arg (periodSamples)},
                                 &failure);
    QVERIFY2 (model.size () == 1, qPrintable (failure));
    QCOMPARE (model.first ().size (), events.size ());
    for (int m = 0; m < events.size (); ++m)
      {
        auto const& line = model.first ().at (m).line;
        QVERIFY2 (std::abs (lineFreq (line).toDouble () - expected) < 0.05, qPrintable (line));
        auto const difference = compareMeasure (events.at (m), line);
        QVERIFY2 (difference.isEmpty (), qPrintable (QString {"measurement %1: %2"}.arg (m).arg (difference)));
      }
  }

  // FreqCal sends no decoder line, busy_events on: no decode, decode_finished,
  // busy, pass_progress or pass_skipped, as the GUI's FreqCal publishes no
  // decode request; a decode again finds no decoded period; a flush, halt or
  // end of input measures no part block.
  void noDecoderLines ()
  {
    auto const periodSamples = 10 * SampleRate;
    auto const pcm = carrierAudio (2 * periodSamples, "1500.37:2000", 300.0, 5u);
    // The second period stops a block short of its third measurement (32 blocks).
    auto const cut = 32 * BlockSamples - 100;
    auto const codec = runCodec (sessionHeader () + control (with (freqcal (10, true), {{"busy_events", true}, {"utc", "12:00:00"}}))
                                 + audio (pcm, 0, periodSamples) + control ({{"nagain_flag", true}, {"utc", "12:00:10"}})
                                 + audio (pcm, periodSamples, periodSamples + cut) + frame (0x02u, R"({"t":"flush"})")
                                 + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (ofType (codec.events, "freqcal").size (), measurements (10) + 2);
    QCOMPARE (decoderLines (codec), 0);
    auto const errors = ofType (codec.events, "error");
    QCOMPARE (errors.size (), 1);
    QCOMPARE (errors.first ().value ("code").toString (), QString {"decode_again_unavailable"});
  }

  // With spectrum on, FreqCal sends the rows a period mode sends: live every
  // block, from a file those up to its stop, each the GUI's waterfall row
  // (spectrum_gui_model's symspec_ as dataSink calls it). From a file each
  // block's row comes before its measurement. Live, a measurement goes out
  // ahead of the live rows queued before it, as every other line does, and
  // live rows can be discarded: no later block's row comes before it, and
  // every row comes or is counted in the rows_dropped line before the next
  // row that comes.
  void rowsAsThePeriodModes_data ()
  {
    QTest::addColumn<bool> ("live");
    QTest::newRow ("live") << true;
    QTest::newRow ("file") << false;
  }

  void rowsAsThePeriodModes ()
  {
    QFETCH (bool, live);
    auto const trperiod = 15;
    auto const periodSamples = trperiod * SampleRate;
    auto const stop = (int (trperiod / 0.288) / 8) * 8;
    auto const pcm = carrierAudio (periodSamples, "1500.37:2000,1800:500", 300.0, 11u);
    auto const codec = runCodec (sessionHeader () + control (with (freqcal (trperiod, live), {{"spectrum", true}, {"utc", "12:00:00"}}))
                                 + audio (pcm, 0, periodSamples) + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (ofType (codec.events, "error").isEmpty (), qPrintable (codec.report ()));
    // The rows (hsym), measurements (F) and rows_dropped lines (D) in stream order.
    QStringList order;
    for (auto const& event : codec.events)
      {
        auto const type = event.value ("t").toString ();
        if ("spectrum" == type) order << QString::number (event.value ("hsym").toInt ());
        else if ("freqcal" == type) order << "F";
        else if ("rows_dropped" == type) order << QString {"D%1"}.arg (event.value ("lines").toInt ());
      }
    auto const shownOrder = order.join (' ').toUtf8 ();
    auto const shown = shownOrder.constData ();
    auto const rows = ofType (codec.events, "spectrum");
    int dropped = 0;
    for (auto const& line : ofType (codec.events, "rows_dropped")) dropped += line.value ("lines").toInt ();
    QVERIFY2 (live || 0 == dropped, shown);
    auto const blocks = live ? periodSamples / BlockSamples : stop;
    QVERIFY2 (rows.size () + dropped == blocks, shown);
    // The queue keeps the last 20 live rows.
    QVERIFY2 (rows.size () >= std::min (20, blocks), shown);
    QString failure;
    auto const output = runProgram (SPECTRUM_GUI_MODEL, pcm,
                                    {QString {"stop %1"}.arg (stop),
                                     QString {"period %1 0 %2"}.arg (live ? "live" : "file").arg (periodSamples)}, &failure);
    QVERIFY2 (!output.isEmpty (), qPrintable (failure));
    QMap<int, QStringList> guiRows;
    for (auto const& line : output.split ('\n'))
      if (line.startsWith ("spectrum ")) guiRows.insert (line.section (' ', 1, 1).toInt (), line.split (' '));
    QCOMPARE (guiRows.size (), blocks);
    auto const asFloat = [] (QString const& hex) {
      auto const word = hex.toUInt (nullptr, 16);
      float value;
      std::memcpy (&value, &word, sizeof value);
      return value;
    };
    // Each row that comes is the GUI's row for its block, in block order.
    int previous = 0;
    for (int r = 0; r < rows.size (); ++r)
      {
        auto const& row = rows.at (r);
        auto const k = row.value ("k").toInt ();
        QVERIFY2 (k > previous && guiRows.contains (k), shown);
        previous = k;
        auto const& words = guiRows[k];
        QCOMPARE (row.value ("hsym").toInt (), words.at (2).toInt ());
        QCOMPARE (float (row.value ("power").toDouble ()), asFloat (words.at (3)));
        QCOMPARE (float (row.value ("peak").toDouble ()), asFloat (words.at (4)));
        QCOMPARE (row.value ("bins").toInt (), words.at (6).toInt ());
        QVERIFY2 (row.value ("data").toString () == words.at (7), qPrintable (QString {"row %1's data"}.arg (r)));
      }
    // Each rows_dropped line counts the rows between the row before it and the row after it.
    int lastK = 0, pending = 0;
    for (auto const& event : codec.events)
      {
        auto const type = event.value ("t").toString ();
        if ("rows_dropped" == type) pending += event.value ("lines").toInt ();
        else if ("spectrum" == type)
          {
            auto const k = event.value ("k").toInt ();
            QVERIFY2 (k == lastK + (1 + pending) * BlockSamples, shown);
            lastK = k;
            pending = 0;
          }
      }
    QVERIFY2 (0 == pending, shown);
    // Measurement m is at the end of block 16 + 8m: from a file right after
    // its block's row, live after no later block's row.
    int lastRowK = 0, measured = 0;
    for (auto const& event : codec.events)
      {
        auto const type = event.value ("t").toString ();
        if ("spectrum" == type) lastRowK = event.value ("k").toInt ();
        else if ("freqcal" == type)
          {
            auto const k = Window + 8 * BlockSamples * measured++;
            QCOMPARE (event.value ("time").toString (), QString {"1200%1"}.arg (k / SampleRate, 2, 10, QChar {'0'}));
            QVERIFY2 (live ? lastRowK <= k : lastRowK == k, shown);
          }
      }
    QCOMPARE (measured, measurements (trperiod));
  }

  // FreqCal equalized: with ref_spectrum "apply" on live audio, each measurement is
  // freqcal_'s on the GUI's equalized samples (refspectrum_ as dataSink
  // drives it), and the filter changed what it measures.
  void equalizedMeasurementsMatchTheGui ()
  {
    QString failure;
    auto const reference = measuredReference (&failure);
    QVERIFY2 (!reference.isEmpty (), qPrintable ("no reference spectrum: " + failure));
    auto const trperiod = 15;
    auto const periodSamples = trperiod * SampleRate;
    auto const pcm = mix (carrierAudio (periodSamples, "1500.37:600,2500.11:600", 0.0, 1u), colouredNoise (periodSamples, 13u));
    auto const equalized = guiEqualized (pcm, reference, QString {"period live 0 %1"}.arg (periodSamples), &failure);
    QVERIFY2 (equalized.size () == pcm.size (), qPrintable ("no equalized audio: " + failure));
    QVERIFY2 (equalized != pcm, "the filter changed nothing");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QVERIFY (writeFile (directory.filePath ("refspec.dat"), reference));
    auto const codec = runCodec (sessionHeader () + control (with (freqcal (trperiod, true, 2500, 100), {{"ref_spectrum", "apply"}, {"utc", "12:00:00"}}))
                                 + audio (pcm, 0, periodSamples) + halt (), directory.path ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (ofType (codec.events, "error").isEmpty (), qPrintable (codec.report ()));
    auto const events = ofType (codec.events, "freqcal");
    QStringList script {QString {"ntr %1"}.arg (trperiod), "rxfreq 2500", "ntol 100", QString {"period 0 %1"}.arg (periodSamples)};
    auto const model = runModel (equalized, script, &failure);
    QVERIFY2 (model.size () == 1, qPrintable (failure));
    auto const plain = runModel (pcm, script, &failure);
    QVERIFY2 (plain.size () == 1, qPrintable (failure));
    QCOMPARE (events.size (), measurements (trperiod));
    QCOMPARE (model.first ().size (), events.size ());
    for (int m = 0; m < events.size (); ++m)
      {
        auto const& line = model.first ().at (m).line;
        QVERIFY2 (std::abs (lineFreq (line).toDouble () - 2500.11) < 0.05, qPrintable (line));
        auto const difference = compareMeasure (events.at (m), line);
        QVERIFY2 (difference.isEmpty (), qPrintable (difference));
      }
    QVERIFY2 (lineLevel (model.first ().first ().line) != lineLevel (plain.first ().first ().line),
              "equalization left the level as it was");
  }

  // Fast JT9 equalized: with ref_spectrum "apply" on live audio, the decode is
  // fast_decode_'s on the GUI's equalized samples at the GUI's live try.
  void fastJt9DecodesTheEqualizedAudio ()
  {
    QString failure;
    auto const reference = measuredReference (&failure);
    QVERIFY2 (!reference.isEmpty (), qPrintable ("no reference spectrum: " + failure));
    auto const trperiod = 15;
    auto const periodSamples = trperiod * SampleRate;
    auto const signal = render ("JT9", "E", trperiod, Message, AudioFrequency);
    QVERIFY2 (!signal.isEmpty (), "no render");
    auto const pcm = mix (inNoise (signal, periodSamples, 0, 15u), colouredNoise (periodSamples, 17u));
    auto const equalized = guiEqualized (pcm, reference, QString {"fastjt9 0 %1 %2"}.arg (periodSamples).arg (trperiod),
                                         &failure);
    QVERIFY2 (equalized.size () == pcm.size (), qPrintable ("no equalized audio: " + failure));
    QVERIFY2 (equalized != pcm, "the filter changed nothing");
    // The first block end with less than 0.35 s left, as test_streaming_fast_jt9's liveTry.
    auto const k = (int (std::lround ((trperiod - 0.35) * SampleRate)) + BlockSamples - 1) / BlockSamples * BlockSamples;
    auto const gui = guiFastJt9 (equalized, "E", trperiod, 120000, k);
    QVERIFY2 (gui.join ('\n').contains (Message), qPrintable ("the GUI decodes nothing: " + gui.join (" | ")));
    auto const plain = guiFastJt9 (pcm, "E", trperiod, 120000, k);
    QVERIFY2 (plain != gui, qPrintable ("equalization left the decode as it was: " + gui.join (" | ")));
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QVERIFY (writeFile (directory.filePath ("refspec.dat"), reference));
    auto const codec = runCodec (sessionHeader ()
                                 + control ({{"mode", "JT9"}, {"fast", true}, {"submode", "E"}, {"trperiod", trperiod},
                                             {"rxfreq", AudioFrequency}, {"ntol", 100}, {"live_audio", true},
                                             {"ref_spectrum", "apply"}, {"utc", "12:00:00"}})
                                 + audio (pcm, 0, periodSamples) + halt (), directory.path ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const decodes = ofType (codec.events, "decode");
    QCOMPARE (decodes.size (), gui.size ());
    for (int i = 0; i < gui.size (); ++i)
      {
        auto const& line = gui.at (i);
        auto const& decode = decodes.at (i);
        QCOMPARE (decode.value ("time").toString (), line.left (6));
        QCOMPARE (decode.value ("snr").toInt (), line.mid (6, 4).trimmed ().toInt ());
        QVERIFY (std::abs (decode.value ("dt").toDouble () - double (line.mid (10, 5).trimmed ().toFloat ())) <= 1e-6);
        QCOMPARE (decode.value ("freq").toInt (), line.mid (15, 5).trimmed ().toInt ());
        QCOMPARE (decode.value ("message").toString (), line.mid (24).trimmed ());
        QCOMPARE (decode.value ("pass").toInt (), k / BlockSamples);
      }
  }

  // FreqCal's lines and the errors for frames sent in FreqCal come out in
  // input order, from the stream's own thread, while the previous mode's
  // decode still runs: here an FT8 final held 3 s on the decode thread, whose
  // lines can follow them. (Decoding inline, the final ends first.)
  void linesKeepInputOrder ()
  {
    auto const ft8 = carrierAudio (15 * SampleRate, "1200:500", 1500.0, 29u);
    auto const tones = carrierAudio (5 * SampleRate, "1500.37:2000", 300.0, 31u);
    auto const codec = runCodec (sessionHeader ()
                                 + control ({{"mode", "FT8"}, {"trperiod", 15}, {"utc", "12:00:00"}})
                                 + audio (ft8, 0, 15 * SampleRate)
                                 + control (with (freqcal (5, false), {{"utc", "12:00:15"}}))
                                 + control ({{"ntol", 0}}) + control ({{"nagain_flag", true}})
                                 + audio (tones, 0, 5 * SampleRate) + halt (),
                                 {}, {"WSJT_CODEC_TEST_HOLD_PASS=50:3000"});
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (codec.err.contains ("hold done nzhsym=50"), codec.err.constData ());
    QStringList order;
    for (auto const& event : codec.events)
      {
        auto const type = event.value ("t").toString ();
        if ("error" == type) order << event.value ("code").toString ();
        else if ("freqcal" == type) order << type;
      }
    QCOMPARE (order, (QStringList {"configure_range_error", "decode_again_unavailable", "freqcal"}));
    QCOMPARE (ofType (codec.events, "decode_finished").size (), 1);
  }

  // A client that ignores line types it does not know reads, around a FreqCal
  // interlude, what it reads without one: FreqCal adds only freqcal lines.
  void clientsIgnoringFreqcalStillWork ()
  {
    auto const periodSamples = 15 * SampleRate;
    auto const signal = render ("FT8", {}, 0, Message, AudioFrequency);
    QVERIFY2 (!signal.isEmpty (), "no render");
    auto const ft8 = inNoise (signal, periodSamples, SampleRate / 2, 21u);
    auto const tones = carrierAudio (periodSamples, "1500.37:2000", 300.0, 23u);
    QJsonObject const ft8Keys {{"mode", "FT8"}, {"trperiod", 15}, {"rxfreq", 1500}, {"ntol", 1000}, {"nfa", 200}, {"nfb", 4000}};
    auto const session = [&] (bool interlude) {
      QByteArray input = sessionHeader () + control (with (ft8Keys, {{"utc", "12:00:00"}})) + audio (ft8, 0, periodSamples);
      if (interlude)
        input += control (with (freqcal (15, false), {{"utc", "12:00:15"}})) + audio (tones, 0, periodSamples);
      return runCodec (input + control (with (ft8Keys, {{"utc", "12:00:30"}})) + audio (ft8, 0, periodSamples) + halt ());
    };
    auto const interlude = session (true);
    QVERIFY2 (interlude.clean (), qPrintable (interlude.report ()));
    QVERIFY2 (!ofType (interlude.events, "freqcal").isEmpty (), qPrintable (interlude.report ()));
    auto const without = session (false);
    QVERIFY2 (without.clean (), qPrintable (without.report ()));
    auto const known = [] (Run const& run) {
      static QSet<QString> const types {"ready", "decode", "decode_finished", "error", "pass_skipped"};
      QStringList lines;
      for (auto const& line : run.out.split ('\n'))
        {
          if (line.trimmed ().isEmpty ()) continue;
          auto const event = QJsonDocument::fromJson (line).object ();
          if (event.isEmpty () || event.value ("v").toInt () != 1) return QStringList {"not a v1 object: " + line};
          if (types.contains (event.value ("t").toString ())) lines << QString::fromUtf8 (line);
        }
      return lines;
    };
    QVERIFY2 (!ofType (without.events, "decode").isEmpty (), qPrintable (without.report ()));
    QCOMPARE (known (interlude), known (without));
  }
};

QTEST_GUILESS_MAIN (TestStreamingFreqcal)

#include "test_streaming_freqcal.moc"
