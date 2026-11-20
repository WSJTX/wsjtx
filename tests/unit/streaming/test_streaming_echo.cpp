// SPDX-License-Identifier: GPL-3.0-or-later
// Echo in the codec against the GUI's measurement: echo_gui_model calls
// save_echo_params_ and avecho_ as MainWindow::dataSink's Echo block does, on
// the samples the codec measures. The audio is the codec's own Echo render,
// shifted in frequency by the Doppler and RIT the rows set, aligned at the
// period's first sample (decode_echo's assumption) in Gaussian noise of a
// fixed seed, or echosim's files; no Echo recording exists.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <functional>
#include <random>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace
{
constexpr int SampleRate = 12000;
// The GUI's Echo period, 3 s, and its stop: 9 blocks of 3456 samples.
constexpr int PeriodSamples = 3 * SampleRate;
constexpr int BlockSamples = 3456;
constexpr int StopSamples = 9 * BlockSamples;
constexpr int LineLimit = 64 * 1024;    // a client that limits a line to 64 KiB
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

QByteArray discontinuity ()
{
  return frame (0x02u, R"({"t":"discontinuity"})");
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

QString label (int period)
{
  auto const seconds = 12 * 3600 + 3 * period;
  return QString {"%1:%2:%3"}.arg (seconds / 3600, 2, 10, QChar {'0'}).arg (seconds / 60 % 60, 2, 10, QChar {'0'})
    .arg (seconds % 60, 2, 10, QChar {'0'});
}

QList<QJsonObject> ofType (QList<QJsonObject> const& events, QString const& type)
{
  QList<QJsonObject> matching;
  for (auto const& event : events)
    if (event.value ("t").toString () == type) matching << event;
  return matching;
}

struct Run
{
  bool started {false};
  bool timedOut {false};
  int exitCode {-1};
  QByteArray out;
  QByteArray err;
  QList<QJsonObject> events;
  int longest {0};

  bool clean () const {return started && !timedOut && 0 == exitCode;}
  QString report () const
  {
    return QString {"started %1, timed out %2, exit %3\nstdout:\n%4\nstderr:\n%5"}
      .arg (started).arg (timedOut).arg (exitCode)
      .arg (QString::fromUtf8 (out.right (3000)), QString::fromUtf8 (err.right (3000)));
  }
};

Run runCodec (QByteArray const& input, QString const& dataDir = {}, QStringList const& environment = {})
{
  Run r;
  QTemporaryDir directory;
  if (!directory.isValid ()) return r;
  auto const data = dataDir.isEmpty () ? directory.path () : dataDir;
  QProcess process;
  auto env = QProcessEnvironment::systemEnvironment ();
  env.insert ("OMP_STACKSIZE", "10M");
  for (auto const& each : environment) env.insert (each.section ('=', 0, 0), each.section ('=', 1));
  process.setProcessEnvironment (env);
  process.setWorkingDirectory (directory.path ());
  QElapsedTimer clock;
  clock.start ();
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), {"-a", data, "-t", directory.path ()});
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
        auto line = pending.left (end);
        pending.remove (0, end + 1);
        r.longest = std::max (r.longest, int (line.size ()) + 1);
        // gfortran ends each record in CR LF on Windows.
        if (line.endsWith ('\r')) line.chop (1);
        r.out += line + '\n';
        if (line.trimmed ().isEmpty ()) continue;
        r.events << QJsonDocument::fromJson (line).object ();
      }
  };
  process.write (input);
  while (process.bytesToWrite () > 0 && QProcess::Running == process.state () && clock.elapsed () < waitMs ())
    {
      process.waitForBytesWritten (5);
      read ();
    }
  process.closeWriteChannel ();
  while (QProcess::Running == process.state () && clock.elapsed () < waitMs ())
    {
      process.waitForReadyRead (5);
      read ();
    }
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

// The codec's Echo render at 12000 Hz.
QByteArray render (QString const& kind, QString const& text, int spacing, double freq)
{
  QJsonObject request {{"id", 1}, {"mode", "ECHO"}, {"kind", kind}, {"freq", freq}, {"rate", SampleRate}};
  if ("tone" != kind) request.insert ("text", text);
  if ("message" == kind) request.insert ("spacing", spacing);
  auto const run = runCodec (sessionHeader () + control (request, "render"));
  QByteArray pcm;
  for (auto const& event : ofType (run.events, "pcm"))
    pcm += QByteArray::fromBase64 (event.value ("data").toString ().toLatin1 ());
  return pcm;
}

std::vector<double> samples (QByteArray const& pcm)
{
  std::vector<double> values (pcm.size () / 2);
  for (std::size_t k = 0; k < values.size (); ++k) values[k] = qFromLittleEndian<qint16> (pcm.constData () + 2 * k);
  return values;
}

void fft (std::vector<std::complex<double>>& a, bool inverse)
{
  auto const n = a.size ();
  for (std::size_t i = 1, j = 0; i < n; ++i)
    {
      auto bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) std::swap (a[i], a[j]);
    }
  for (std::size_t length = 2; length <= n; length <<= 1)
    {
      auto const angle = 2 * Pi / double (length) * (inverse ? 1 : -1);
      std::complex<double> const step {std::cos (angle), std::sin (angle)};
      for (std::size_t i = 0; i < n; i += length)
        {
          std::complex<double> w {1};
          for (std::size_t j = 0; j < length / 2; ++j)
            {
              auto const u = a[i + j];
              auto const v = a[i + j + length / 2] * w;
              a[i + j] = u + v;
              a[i + j + length / 2] = u - v;
              w *= step;
            }
        }
    }
  if (inverse)
    for (auto& value : a) value /= double (n);
}

// signal moved up by hz: the real part of its analytic signal times
// exp(2 pi i hz t).
std::vector<double> shifted (std::vector<double> const& signal, double hz)
{
  std::size_t n = 1;
  while (n < 2 * signal.size ()) n <<= 1;
  std::vector<std::complex<double>> a (n);
  std::copy (signal.begin (), signal.end (), a.begin ());
  fft (a, false);
  for (std::size_t i = 1; i < n / 2; ++i) a[i] *= 2.0;
  for (std::size_t i = n / 2 + 1; i < n; ++i) a[i] = 0;
  fft (a, true);
  std::vector<double> out (signal.size ());
  for (std::size_t k = 0; k < out.size (); ++k)
    out[k] = (a[k] * std::polar (1.0, 2 * Pi * hz * double (k) / SampleRate)).real ();
  return out;
}

// periods periods of 3 s: in each, signal scaled to rms over its length and
// starting at the period's first sample, in Gaussian noise of sigma noise
// (seed fixed); on the periods odd selects (all: -1), others the alternate
// signal instead.
QByteArray periods (std::vector<double> signal, double rms, int count, double noise, unsigned seed,
                    std::vector<double> alternate = {}, double alternateRms = 0, int odd = -1)
{
  auto const scale = [] (std::vector<double>& values, double target) {
    double sum = 0;
    for (auto value : values) sum += value * value;
    auto const now = values.empty () ? 0.0 : std::sqrt (sum / double (values.size ()));
    if (now > 0) for (auto& value : values) value *= target / now;
  };
  scale (signal, rms);
  scale (alternate, alternateRms);
  std::mt19937 random {seed};
  std::normal_distribution<double> gauss {0.0, noise};
  QByteArray pcm (2 * count * PeriodSamples, '\0');
  for (int p = 0; p < count; ++p)
    {
      auto const& source = (odd >= 0 && p % 2 != odd) ? alternate : signal;
      for (int k = 0; k < PeriodSamples; ++k)
        {
          double value = gauss (random);
          if (k < int (source.size ())) value += source[k];
          qToLittleEndian<qint16> (qint16 (std::max (-32767.0, std::min (32767.0, std::round (value)))),
                                   pcm.data () + 2 * (p * PeriodSamples + k));
        }
    }
  return pcm;
}

// What the client sets, as configure keys and as the model's script.
struct Echo
{
  QString kind {"tone"};
  QString text;
  int spacing {10};
  double freq {1500};
  int doppler {0};
  int rit {0};
  double spread {0};
  int average {10};
  bool autoOn {false};

  QJsonObject keys () const
  {
    return {{"kind", kind}, {"echo_text", text}, {"echo_spacing", spacing}, {"echo_freq", freq},
            {"doppler", doppler}, {"rit", rit}, {"spread", spread}, {"echo_average", average},
            {"echo_auto", autoOn}};
  }

  QStringList script () const
  {
    return {"kind " + kind, "text " + (text.isEmpty () ? QString {"-"} : text), QString {"spacing %1"}.arg (spacing),
            QString {"freq %1"}.arg (freq, 0, 'g', 17), QString {"doppler %1"}.arg (doppler),
            QString {"rit %1"}.arg (rit), QString {"spread %1"}.arg (spread, 0, 'g', 17),
            QString {"average %1"}.arg (average), QString {"auto %1"}.arg (autoOn ? 1 : 0)};
  }
};

QJsonObject echoMode (Echo const& echo, bool live)
{
  return with (echo.keys (), {{"mode", "ECHO"}, {"live_audio", live}});
}

// The GUI's measurement of one period.
struct Measure
{
  quint32 level {0}, snr {0}, dbErr {0}, dfreq {0}, width {0};
  int quality {0};
  int nsum {0};
  QByteArray call;
  QByteArray blue, red;
  int dopplerTotal {0}, doppler {0}, rit {0}, spacing {0};
  quint32 freq {0}, spread {0};
};

quint32 bitsOf (float value)
{
  quint32 word;
  std::memcpy (&word, &value, sizeof word);
  return word;
}

float floatOf (quint32 word)
{
  float value;
  std::memcpy (&value, &word, sizeof value);
  return value;
}

struct ModelRun
{
  bool ok {false};
  QString failure;
  QList<Measure> measures;
};

ModelRun runModel (QByteArray const& pcm, QStringList script)
{
  ModelRun r;
  QTemporaryDir directory;
  if (!directory.isValid ())
    {
      r.failure = "no directory";
      return r;
    }
  QFile audioFile {directory.filePath ("audio.raw")};
  if (!audioFile.open (QIODevice::WriteOnly) || audioFile.write (pcm) != pcm.size ())
    {
      r.failure = "cannot write the audio";
      return r;
    }
  audioFile.close ();
  script.prepend ("audio " + audioFile.fileName ());
  QFile scriptFile {directory.filePath ("script")};
  if (!scriptFile.open (QIODevice::WriteOnly))
    {
      r.failure = "cannot write the script";
      return r;
    }
  scriptFile.write ((script.join ('\n') + '\n').toUtf8 ());
  scriptFile.close ();
  QProcess process;
  process.setWorkingDirectory (directory.path ());
  process.start (QString::fromUtf8 (ECHO_GUI_MODEL), {scriptFile.fileName ()});
  if (!process.waitForFinished (waitMs ()) || process.exitCode () != 0)
    {
      r.failure = "model failed: " + QString::fromUtf8 (process.readAllStandardError ());
      return r;
    }
  for (auto const& line : QString::fromUtf8 (process.readAllStandardOutput ()).split ('\n'))
    {
      if (line.isEmpty ()) continue;
      auto const words = line.split (' ');
      if (words.value (0) != "echo" || words.size () != 17)
        {
          r.failure = "a malformed model line: " + line.left (200);
          return r;
        }
      Measure m;
      auto const hex = [&] (int i) {return quint32 (words.at (i).toUInt (nullptr, 16));};
      m.level = hex (1);
      m.snr = hex (2);
      m.dbErr = hex (3);
      m.dfreq = hex (4);
      m.quality = words.at (5).toInt ();
      m.width = hex (6);
      m.nsum = words.at (7).toInt ();
      m.call = QByteArray::fromHex (words.at (8).toLatin1 ());
      m.blue = QByteArray::fromBase64 (words.at (9).toLatin1 ());
      m.red = QByteArray::fromBase64 (words.at (10).toLatin1 ());
      m.dopplerTotal = words.at (11).toInt ();
      m.doppler = words.at (12).toInt ();
      m.rit = words.at (13).toInt ();
      m.freq = hex (14);
      m.spread = hex (15);
      m.spacing = words.at (16).toInt ();
      r.measures << m;
    }
  r.ok = true;
  return r;
}

// An echo event against the GUI's measurement, field for field; from a file
// also the in-band parameters read back. Empty when they agree.
QString compare (QJsonObject const& event, Measure const& m, bool file)
{
  auto const text = QString::fromUtf8 (QJsonDocument {event}.toJson (QJsonDocument::Compact)).left (400);
  auto const real = [&] (char const* key, quint32 expected) -> QString {
    if (!event.contains (key) || !event.value (key).isDouble ()) return QString {"no %1 in %2"}.arg (key, text);
    auto const got = float (event.value (key).toDouble ());
    if (bitsOf (got) == expected) return {};
    return QString {"%1 %2 against the GUI's %3: %4"}.arg (key).arg (double (got), 0, 'g', 9)
      .arg (double (floatOf (expected)), 0, 'g', 9).arg (text);
  };
  for (auto const& check : {real ("level", m.level), real ("snr", m.snr), real ("db_err", m.dbErr),
                            real ("dfreq", m.dfreq), real ("width", m.width)})
    if (!check.isEmpty ()) return check;
  if (event.value ("quality").toInt (-1) != m.quality)
    return QString {"quality against the GUI's %1: %2"}.arg (m.quality).arg (text);
  if (event.value ("nsum").toInt (-1) != m.nsum) return QString {"nsum against the GUI's %1: %2"}.arg (m.nsum).arg (text);
  if (event.value ("call").toString ().toLatin1 () != m.call)
    return QString {"call against the GUI's \"%1\": %2"}.arg (QString::fromLatin1 (m.call), text);
  if (event.value ("bins").toInt () != 4096) return "bins: " + text;
  if (bitsOf (float (event.value ("df").toDouble ())) != bitsOf (12000.0f / 32768.0f)) return "df: " + text;
  if (QByteArray::fromBase64 (event.value ("blue").toString ().toLatin1 ()) != m.blue) return "blue differs: " + text;
  if (QByteArray::fromBase64 (event.value ("red").toString ().toLatin1 ()) != m.red) return "red differs: " + text;
  if (file)
    {
      if (event.value ("doppler_total").toInt (-99999) != m.dopplerTotal) return "doppler_total: " + text;
      if (event.value ("doppler").toInt (-99999) != m.doppler) return "doppler: " + text;
      if (event.value ("rit").toInt (-99999) != m.rit) return "rit: " + text;
      if (event.value ("echo_spacing").toInt (-99999) != m.spacing) return "echo_spacing: " + text;
      for (auto const& check : {real ("echo_freq", m.freq), real ("spread", m.spread)})
        if (!check.isEmpty ()) return check;
    }
  else
    for (auto key : {"doppler_total", "doppler", "rit", "echo_freq", "spread", "echo_spacing"})
      if (event.contains (key)) return QString {"%1 in a live event: %2"}.arg (key, text);
  return {};
}

QString compareAll (QList<QJsonObject> const& events, QList<Measure> const& measures, bool file)
{
  if (events.size () != measures.size ())
    return QString {"%1 echo events against the GUI's %2 measurements"}.arg (events.size ()).arg (measures.size ());
  for (int i = 0; i < events.size (); ++i)
    {
      auto const difference = compare (events.at (i), measures.at (i), file);
      if (!difference.isEmpty ()) return QString {"period %1: %2"}.arg (i).arg (difference);
    }
  return {};
}

QByteArray wavPcm (QString const& path)
{
  QFile file {path};
  if (path.isEmpty () || !file.open (QIODevice::ReadOnly)) return {};
  auto const bytes = file.readAll ();
  for (int at = 12; at + 8 <= bytes.size ();)
    {
      auto const size = int (qFromLittleEndian<quint32> (bytes.constData () + at + 4));
      if (bytes.mid (at, 4) == "data") return bytes.mid (at + 8, size & ~1);
      at += 8 + size + (size & 1);
    }
  return {};
}

QString inSamples (QString const& relative)
{
  auto const directory = qEnvironmentVariable ("WSJT_SAMPLES_DIR");
  return directory.isEmpty () ? QString {} : directory + '/' + relative;
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

// spectrum_gui_model's output lines on pcm with script, or empty.
QStringList runSpectrumModel (QString const& directory, QByteArray const& pcm, QStringList script)
{
  QDir {}.mkpath (directory);
  if (!writeFile (directory + "/audio.raw", pcm)) return {};
  script.prepend ("audio " + directory + "/audio.raw");
  if (!writeFile (directory + "/script", (script.join ('\n') + '\n').toUtf8 ())) return {};
  QProcess process;
  process.start (QString::fromUtf8 (SPECTRUM_GUI_MODEL), {directory + "/script"});
  if (!process.waitForFinished (waitMs ()) || process.exitCode () != 0) return {};
  QStringList lines;
  for (auto const& line : QString::fromUtf8 (process.readAllStandardOutput ()).split ('\n'))
    if (!line.isEmpty ()) lines << line;
  return lines;
}

// The message the rows send, and the echo they measure it on.
constexpr char Call[] = "K1ABC";
// A dither of +3.25 Hz, as the GUI's Astro window draws one at transmit.
constexpr double Dithered = 1503.25;

// The kind's render at freq, moved by the Doppler and RIT the echo shows:
// avecho looks for it at f1 + doppler - rit.
std::vector<double> echoSignal (QString const& kind, QString const& text, int spacing, double freq, double moved)
{
  auto const pcm = render (kind, text, spacing, freq);
  if (pcm.isEmpty ()) return {};
  return shifted (samples (pcm), moved);
}
}

class TestStreamingEcho : public QObject
{
  Q_OBJECT

  // The codec's measurements of count periods of pcm under echo, labelled
  // from 12:00:00, against the GUI's on the same samples; before[i] goes in
  // period i's configure (clear_average true: the Clear Avg button there).
  void measuresMatch (QByteArray const& pcm, int count, Echo const& echo, bool live,
                      QMap<int, QJsonObject> const& before, Run& run, ModelRun& model)
  {
    QByteArray input = sessionHeader () + control (echoMode (echo, live));
    auto script = echo.script ();
    for (int p = 0; p < count; ++p)
      {
        auto keys = with (before.value (p), {{"utc", label (p)}});
        input += control (keys) + audio (pcm, p * PeriodSamples, (p + 1) * PeriodSamples);
        if (before.value (p).value ("clear_average").toBool ()) script << "clear";
        script << QString {"period %1 %2"}.arg (live ? "live" : "file").arg (p * PeriodSamples);
      }
    run = runCodec (input + halt ());
    model = runModel (pcm, script);
  }

private Q_SLOTS:
  // (m), (n): each period's echo event equals avecho_ called as dataSink calls
  // it on the same samples, field for field and bit for bit, through the
  // averages of echo_average periods, the ring's wrap and a clear_average; and
  // each kind gives the GUI's result for the same audio.
  void measuresMatchTheGui_data ()
  {
    QTest::addColumn<QString> ("audioKind");
    QTest::addColumn<QString> ("kind");
    QTest::addColumn<QString> ("text");
    QTest::newRow ("tone") << QString {"tone"} << QString {"tone"} << QString {};
    QTest::newRow ("cw") << QString {"cw"} << QString {"cw"} << QString {Call};
    QTest::newRow ("message") << QString {"message"} << QString {"message"} << QString {Call};
    QTest::newRow ("message, six characters") << QString {"message"} << QString {"message"} << QString {"W9XYZ7"};
    QTest::newRow ("message audio as tone") << QString {"message"} << QString {"tone"} << QString {Call};
    QTest::newRow ("message audio as cw") << QString {"message"} << QString {"cw"} << QString {Call};
  }

  void measuresMatchTheGui ()
  {
    QFETCH (QString, audioKind);
    QFETCH (QString, kind);
    QFETCH (QString, text);
    Echo echo;
    echo.kind = kind;
    echo.text = text;
    echo.spacing = 20;
    // CW at no spread, which avecho measures with at its least, 0.04 Hz.
    echo.spread = "cw" == audioKind ? 0 : 4.5;
    echo.average = 2;
    // A message keeps its residual Doppler within half a spacing, as
    // decode_echo reads its tones at f1 plus whole spacings.
    echo.freq = "cw" == audioKind ? 1500.0 : Dithered;
    echo.doppler = "message" == audioKind ? 3 : 41;
    echo.rit = "message" == audioKind ? -2 : -6;
    auto const signal = echoSignal (audioKind, text, echo.spacing, echo.freq, echo.doppler - echo.rit);
    QVERIFY2 (!signal.empty (), "no render");
    auto const pcm = periods (signal, 800, 5, 1000, 1729u);
    Run run;
    ModelRun model;
    measuresMatch (pcm, 5, echo, true, {{3, {{"clear_average", true}}}}, run, model);
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    // The GUI's averages: 1, 2, 3 (the ring of 2 wraps), then cleared, 1, 2.
    QList<int> nsums;
    for (auto const& m : model.measures) nsums << m.nsum;
    QCOMPARE (nsums, (QList<int> {1, 2, 3, 1, 2}));
    // The GUI measured the echo of its own kind: its quality is above 0, and
    // a message's call comes back.
    if (audioKind == kind)
      QVERIFY2 (model.measures.last ().quality > 0, qPrintable (QString::number (model.measures.last ().quality)));
    QCOMPARE (model.measures.last ().call, ("message" == kind ? (text + "      ").left (6) : QString {"      "}).toLatin1 ());
    auto const events = ofType (run.events, "echo");
    QVERIFY2 (compareAll (events, model.measures, false).isEmpty (),
              qPrintable (compareAll (events, model.measures, false) + "\n" + run.report ()));
    for (int i = 0; i < events.size (); ++i) QCOMPARE (events.at (i).value ("time").toString (), label (i).remove (':'));
    // Each period ends with its decode_finished, after its echo line, and
    // Echo makes no other decode.
    QCOMPARE (ofType (run.events, "decode_finished").size (), 5);
    QCOMPARE (ofType (run.events, "decode").size (), 0);
    QVERIFY2 (run.longest <= LineLimit, qPrintable (QString::number (run.longest)));
  }

  // Clear Avg starts the message's characters again too, as the GUI's
  // m_nclearave clears decode_echo's sums: a message changed with the clear
  // comes back whole.
  void clearAverageRestartsTheCall ()
  {
    Echo echo;
    echo.kind = "message";
    echo.text = Call;
    echo.spacing = 20;
    echo.spread = 3;
    echo.freq = Dithered;
    echo.average = 10;
    auto const first = echoSignal ("message", Call, echo.spacing, echo.freq, 0.0);
    auto const second = echoSignal ("message", "W9XYZ7", echo.spacing, echo.freq, 0.0);
    QVERIFY2 (!first.empty () && !second.empty (), "no render");
    auto const pcm = periods (first, 800, 3, 1000, 2024u) + periods (second, 800, 3, 1000, 2025u);
    QByteArray input = sessionHeader () + control (echoMode (echo, true));
    auto script = echo.script ();
    for (int p = 0; p < 6; ++p)
      {
        QJsonObject keys {{"utc", label (p)}};
        if (3 == p)
          {
            keys = with (keys, {{"clear_average", true}, {"echo_text", "W9XYZ7"}});
            script << "clear" << "text W9XYZ7";
          }
        input += control (keys) + audio (pcm, p * PeriodSamples, (p + 1) * PeriodSamples);
        script << QString {"period live %1"}.arg (p * PeriodSamples);
      }
    auto const run = runCodec (input + halt ());
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    auto const model = runModel (pcm, script);
    QVERIFY2 (model.ok && 6 == model.measures.size (), qPrintable (model.failure));
    QCOMPARE (model.measures.at (2).call, QByteArray {"K1ABC "});
    QCOMPARE (model.measures.last ().call, QByteArray {"W9XYZ7"});
    auto const events = ofType (run.events, "echo");
    QVERIFY2 (compareAll (events, model.measures, false).isEmpty (), qPrintable (compareAll (events, model.measures, false)));
  }

  // With tone the GUI packs nothing, so decode_echo reads whatever the
  // period's first 15 samples hold: here a message's parameters, left in
  // them as in a saved message file sent as live audio. It shifts the tones
  // and recovers the call, as the codec's tone does.
  void toneLeavesTheFirstSamplesAsSent ()
  {
    Echo sent;
    sent.kind = "message";
    sent.text = Call;
    sent.spacing = 20;
    sent.freq = Dithered;
    sent.spread = 3;
    auto const signal = echoSignal ("message", Call, sent.spacing, sent.freq, 0.0);
    QVERIFY2 (!signal.empty (), "no render");
    auto const live = periods (signal, 800, 2, 1000, 77u);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto script = sent.script ();
    for (int p = 0; p < 2; ++p)
      script << QString {"pack %1 %2"}.arg (p * PeriodSamples).arg (directory.filePath (QString {"packed%1.raw"}.arg (p)));
    auto const packed = runModel (live, script);
    QVERIFY2 (packed.ok, qPrintable (packed.failure));
    auto const pcm = readFile (directory.filePath ("packed0.raw")) + readFile (directory.filePath ("packed1.raw"));
    QCOMPARE (pcm.size (), 2 * 2 * PeriodSamples);
    Echo echo = sent;
    echo.kind = "tone";
    Run run;
    ModelRun model;
    measuresMatch (pcm, 2, echo, true, {}, run, model);
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    QVERIFY2 (model.ok && 2 == model.measures.size (), qPrintable (model.failure));
    QCOMPARE (model.measures.last ().call, QByteArray {"K1ABC "});
    auto const events = ofType (run.events, "echo");
    QVERIFY2 (compareAll (events, model.measures, false).isEmpty (), qPrintable (compareAll (events, model.measures, false)));
  }

  // The values in force at a period's 9th half-symbol are the ones measured:
  // a change before it applies to the period, after it to the next. Live,
  // doppler and echo_freq; from a file, which carries those, echo_auto.
  void keysInForceAtTheNinthHalfSymbol_data ()
  {
    QTest::addColumn<bool> ("live");
    QTest::newRow ("live") << true;
    QTest::newRow ("file") << false;
  }

  void keysInForceAtTheNinthHalfSymbol ()
  {
    QFETCH (bool, live);
    Echo echo;
    echo.spread = 2;
    echo.freq = Dithered;
    echo.doppler = 20;
    echo.average = 2;
    auto const signal = echoSignal ("tone", {}, 10, echo.freq, 33);
    QVERIFY2 (!signal.empty (), "no render");
    auto pcm = periods (signal, 800, 2, 1000, 4242u);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    if (!live)
      {
        // Saved as the GUI saves a CW period, which packs its parameters.
        Echo sent = echo;
        sent.kind = "cw";
        sent.doppler = 33;
        // A spread other than the key's, which echo_auto measures with.
        sent.spread = 6;
        auto script = sent.script ();
        for (int p = 0; p < 2; ++p)
          script << QString {"pack %1 %2"}.arg (p * PeriodSamples).arg (directory.filePath (QString {"saved%1.raw"}.arg (p)));
        auto const packed = runModel (pcm, script);
        QVERIFY2 (packed.ok, qPrintable (packed.failure));
        pcm = readFile (directory.filePath ("saved0.raw")) + readFile (directory.filePath ("saved1.raw"));
        QCOMPARE (pcm.size (), 2 * 2 * PeriodSamples);
      }
    QJsonObject const before = live ? QJsonObject {{"doppler", 33}, {"echo_freq", 1501.5}} : QJsonObject {{"echo_auto", true}};
    QJsonObject const after = live ? QJsonObject {{"doppler", 7}} : QJsonObject {{"echo_auto", false}};
    auto const input = sessionHeader () + control (with (echoMode (echo, live), {{"utc", label (0)}}))
      + audio (pcm, 0, 5 * BlockSamples) + control (before)
      + audio (pcm, 5 * BlockSamples, StopSamples) + control (after)
      + audio (pcm, StopSamples, PeriodSamples) + control ({{"utc", label (1)}})
      + audio (pcm, PeriodSamples, 2 * PeriodSamples) + halt ();
    auto const run = runCodec (input);
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    auto const how = live ? QString {"live"} : QString {"file"};
    auto script = echo.script ();
    if (live) script << "doppler 33" << "freq 1501.5";
    else script << "auto 1";
    script << "period " + how + " 0" << (live ? "doppler 7" : "auto 0")
           << QString {"period %1 %2"}.arg (how).arg (PeriodSamples);
    auto const model = runModel (pcm, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const events = ofType (run.events, "echo");
    QVERIFY2 (compareAll (events, model.measures, !live).isEmpty (), qPrintable (compareAll (events, model.measures, !live)));
    // With the first values the GUI measures the first period otherwise (and
    // live the second too), so a change the codec took late would show.
    auto const first = runModel (pcm, echo.script () << "period " + how + " 0" << QString {"period %1 %2"}.arg (how).arg (PeriodSamples));
    QVERIFY2 (first.ok && 2 == first.measures.size (), qPrintable (first.failure));
    QVERIFY (compare (events.first (), first.measures.first (), !live).size ());
    if (live) QVERIFY (compare (events.last (), first.measures.last (), !live).size ());
  }

  // (o): from a file (live_audio false) the codec reads the parameters the
  // file's first 15 samples carry, as the GUI does for a WAV file, whatever
  // the configure says, and reports them.
  void fileInputReadsInBandParameters_data ()
  {
    QTest::addColumn<QString> ("source");
    QTest::addColumn<bool> ("autoOn");
    QTest::newRow ("a saved message") << QString {"saved message"} << false;
    // With echo_auto avecho measures with the self spread, the client's,
    // not the file's.
    QTest::newRow ("a saved message, echo_auto") << QString {"saved message"} << true;
    // A spacing slot past 30, which the GUI's line shows as 0.
    QTest::newRow ("a spacing past 30") << QString {"spacing past 30"} << false;
#if defined (ECHOSIM)
    QTest::newRow ("echosim") << QString {"echosim"} << false;
#endif
  }

  void fileInputReadsInBandParameters ()
  {
    QFETCH (QString, source);
    QFETCH (bool, autoOn);
    // The configure's values differ from every one the file carries.
    Echo echo;
    echo.kind = "tone";
    echo.freq = 1490.5;
    echo.doppler = 99;
    echo.rit = 7;
    echo.spread = 1;
    echo.average = 5;
    echo.autoOn = autoOn;
    QByteArray pcm;
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    if ("echosim" == source)
      {
#if defined (ECHOSIM)
        QProcess process;
        process.setWorkingDirectory (directory.path ());
        // f0 1500, Doppler 37 Hz, spread 4 Hz, 3 files, SNR -10 dB in 2500 Hz.
        process.start (QString::fromUtf8 (ECHOSIM), {"1500", "37.0", "4.0", "3", "-10"});
        QVERIFY2 (process.waitForFinished (waitMs ()) && 0 == process.exitCode (),
                  qPrintable (QString::fromUtf8 (process.readAll ())));
        for (auto const& name : {"000000_000000.wav", "000000_000003.wav", "000000_000006.wav"})
          {
            auto const one = wavPcm (directory.filePath (name));
            QCOMPARE (one.size (), 2 * PeriodSamples);
            pcm += one;
          }
#endif
      }
    else
      {
        // A file of the GUI's saved Echo WAV's layout: a live message period
        // with the parameters packed into its first 15 samples, the total
        // Doppler apart from the audio shift, as m_fDop and m_fAudioShift.
        Echo sent;
        sent.kind = "message";
        sent.text = Call;
        sent.spacing = 15;
        sent.freq = Dithered;
        sent.doppler = 3;
        sent.rit = -1;
        sent.spread = 3.5;
        auto const signal = echoSignal ("message", Call, sent.spacing, sent.freq, sent.doppler - sent.rit);
        QVERIFY2 (!signal.empty (), "no render");
        auto const live = periods (signal, 800, 3, 1000, 99u);
        auto script = sent.script () << "doppler_total 4";
        for (int p = 0; p < 3; ++p)
          script << QString {"pack %1 %2"}.arg (p * PeriodSamples).arg (directory.filePath (QString {"saved%1.raw"}.arg (p)));
        auto const packed = runModel (live, script);
        QVERIFY2 (packed.ok, qPrintable (packed.failure));
        for (int p = 0; p < 3; ++p) pcm += readFile (directory.filePath (QString {"saved%1.raw"}.arg (p)));
        QCOMPARE (pcm.size (), 2 * 3 * PeriodSamples);
        // The spacing slot, samples 11 and 12, as the int32 40.
        if ("spacing past 30" == source)
          for (int p = 0; p < 3; ++p) qToLittleEndian<qint32> (40, pcm.data () + 2 * (p * PeriodSamples + 10));
      }
    Run run;
    ModelRun model;
    measuresMatch (pcm, 3, echo, false, {}, run, model);
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    QVERIFY2 (model.ok && 3 == model.measures.size (), qPrintable (model.failure));
    auto const& last = model.measures.last ();
    if ("spacing past 30" != source) QVERIFY2 (last.quality > 0, qPrintable (QString::number (last.quality)));
    if ("echosim" == source)
      {
        // echosim packs its Doppler as the total, no audio Doppler or RIT, f1
        // with the Doppler and its spread.
        QCOMPARE (last.dopplerTotal, 37);
        QCOMPARE (last.doppler, 0);
        QCOMPARE (last.rit, 0);
        QCOMPARE (floatOf (last.freq), 1537.0f);
        QCOMPARE (floatOf (last.spread), 4.0f);
      }
    else
      {
        QCOMPARE (last.dopplerTotal, 4);
        QCOMPARE (last.doppler, 3);
        QCOMPARE (last.rit, -1);
        QCOMPARE (floatOf (last.freq), float (Dithered));
        QCOMPARE (floatOf (last.spread), 3.5f);
        // Past 30 decode_echo reads no message.
        QCOMPARE (last.spacing, "spacing past 30" == source ? 0 : 15);
        QCOMPARE (last.call, "spacing past 30" == source ? QByteArray {"      "} : QByteArray {"K1ABC "});
      }
    auto const events = ofType (run.events, "echo");
    QVERIFY2 (compareAll (events, model.measures, true).isEmpty (),
              qPrintable (compareAll (events, model.measures, true) + "\n" + run.report ()));
  }

  // A clear_average sent in ECHO is Echo's: the GUI's Echo block takes
  // m_nclearave and zeroes it, so JT65's next decode after the interlude
  // does not clear, as it does when the clear is sent in JT65.
  void clearAverageSentInEchoStaysInEcho ()
  {
    QList<QByteArray> jt65;
    for (int n = 1; n <= 6; ++n)
      {
        auto pcm = wavPcm (inSamples (QString {"JT65/JT65B/000000_000%1.wav"}.arg (n))).left (2 * 60 * SampleRate);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        pcm.append (QByteArray (2 * 60 * SampleRate - pcm.size (), '\0'));
        jt65 << pcm;
      }
    QJsonObject const keys {{"mode", "JT65"}, {"trperiod", 60}, {"submode", 1}, {"rxfreq", 1700}, {"ntol", 20},
                            {"depth", 19}};
    auto const noise = periods ({}, 0, 1, 1000, 12u);
    // clearIn: "echo", "jt65" or none.
    auto const session = [&] (QString const& clearIn) {
      auto input = sessionHeader ();
      for (int n = 1; n <= 6; ++n)
        {
          if (4 == n)
            {
              // Live CW packs its parameters, so nothing is read from noise.
              QJsonObject interlude {{"mode", "ECHO"}, {"utc", "00:03:30"}, {"live_audio", true}, {"kind", "cw"}};
              if ("echo" == clearIn) interlude.insert ("clear_average", true);
              input += control (interlude) + audio (noise, 0, PeriodSamples);
            }
          auto frameKeys = 1 == n ? keys : 4 == n ? with (keys, {{"live_audio", false}}) : QJsonObject {};
          if (4 == n && "jt65" == clearIn) frameKeys.insert ("clear_average", true);
          frameKeys.insert ("nutc", n);
          input += control (frameKeys) + audio (jt65.at (n - 1), 0, 60 * SampleRate);
        }
      auto const run = runCodec (input + halt ());
      QList<int> decodes {0};
      for (auto const& event : run.events)
        {
          if ("decode" == event.value ("t").toString () && "JT65" == event.value ("mode").toString ()) ++decodes.last ();
          if ("decode_finished" == event.value ("t").toString ()
              && "000330" != event.value ("period_end").toString ())
            decodes << 0;
        }
      decodes.removeLast ();
      return qMakePair (run.clean () && 1 == ofType (run.events, "echo").size (), decodes);
    };
    auto const none = session ({});
    auto const there = session ("jt65");
    auto const inEcho = session ("echo");
    QVERIFY (none.first && there.first && inEcho.first);
    qInfo () << "decodes per JT65 period: none" << none.second << "sent in JT65" << there.second << "sent in ECHO"
             << inEcho.second;
    QVERIFY2 (none.second != there.second, "the clear sent with the JT65 period changes nothing");
    QCOMPARE (inEcho.second, none.second);
  }

  // doppler and rit take a JSON number, truncated toward zero as the GUI's
  // int nDop = m_fAudioShift truncates its audio shift.
  void fractionalDopplerTruncates ()
  {
    Echo echo;
    echo.spread = 2;
    echo.freq = Dithered;
    echo.average = 5;
    auto const signal = echoSignal ("tone", {}, 10, echo.freq, -523 - 2);
    QVERIFY2 (!signal.empty (), "no render");
    auto const pcm = periods (signal, 800, 2, 1000, 523u);
    auto const input = sessionHeader () + control (with (echoMode (echo, true), {{"doppler", -523.6}, {"rit", 2.7}, {"utc", label (0)}}))
      + audio (pcm, 0, PeriodSamples) + control ({{"utc", label (1)}}) + audio (pcm, PeriodSamples, 2 * PeriodSamples) + halt ();
    auto const run = runCodec (input);
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    QCOMPARE (ofType (run.events, "error").size (), 0);
    auto const model = runModel (pcm, echo.script () << "doppler -523" << "rit 2" << "period live 0"
                                 << QString {"period live %1"}.arg (PeriodSamples));
    QVERIFY2 (model.ok && 2 == model.measures.size (), qPrintable (model.failure));
    QVERIFY2 (model.measures.last ().quality > 0, qPrintable (QString::number (model.measures.last ().quality)));
    auto const events = ofType (run.events, "echo");
    QVERIFY2 (compareAll (events, model.measures, false).isEmpty (), qPrintable (compareAll (events, model.measures, false)));
  }

  // An expected echo frequency outside 750 to 2250 Hz stops avecho before it
  // measures: the period's line keeps the last graph and nsum, or after a
  // clear their zeros, as the GUI's Echo block leaves echocom.
  void abortedMeasurementsMatchTheGui_data ()
  {
    QTest::addColumn<bool> ("clear");
    QTest::newRow ("without a clear") << false;
    QTest::newRow ("with a clear") << true;
  }

  void abortedMeasurementsMatchTheGui ()
  {
    QFETCH (bool, clear);
    Echo echo;
    echo.spread = 2;
    echo.freq = Dithered;
    echo.doppler = 12;
    echo.average = 5;
    auto const signal = echoSignal ("tone", {}, 10, echo.freq, echo.doppler);
    QVERIFY2 (!signal.empty (), "no render");
    auto const pcm = periods (signal, 800, 3, 1000, 900u);
    auto script = echo.script () << "period live 0";
    QJsonObject away {{"utc", label (1)}, {"doppler", 1000}};
    if (clear)
      {
        away.insert ("clear_average", true);
        script << "clear";
      }
    script << "doppler 1000" << QString {"period live %1"}.arg (PeriodSamples) << "doppler 12"
           << QString {"period live %1"}.arg (2 * PeriodSamples);
    auto const input = sessionHeader () + control (with (echoMode (echo, true), {{"utc", label (0)}}))
      + audio (pcm, 0, PeriodSamples) + control (away) + audio (pcm, PeriodSamples, 2 * PeriodSamples)
      + control ({{"utc", label (2)}, {"doppler", 12}}) + audio (pcm, 2 * PeriodSamples, 3 * PeriodSamples) + halt ();
    auto const run = runCodec (input);
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    auto const model = runModel (pcm, script);
    QVERIFY2 (model.ok && 3 == model.measures.size (), qPrintable (model.failure));
    // The GUI measured nothing in the second period.
    auto const& aborted = model.measures.at (1);
    QCOMPARE (floatOf (aborted.snr), 0.0f);
    QCOMPARE (aborted.nsum, clear ? 0 : 1);
    if (clear) QCOMPARE (aborted.blue, QByteArray (4 * 4096, '\0'));
    else QCOMPARE (aborted.blue, model.measures.at (0).blue);
    auto const events = ofType (run.events, "echo");
    QVERIFY2 (compareAll (events, model.measures, false).isEmpty (), qPrintable (compareAll (events, model.measures, false)));
  }

  // In ECHO, which measures on the stream's own thread, an error and a
  // refused decode again come in the order of the frames that caused them,
  // ahead of the echo line that later audio brings, while the last FT4
  // pass is still held on the decode thread (WSJT_CODEC_TEST_HOLD_PASS).
  // Decoding inline (WSJT_CODEC_TEST_INLINE=1), the held pass ends first.
  void errorsKeepInputOrderInEcho ()
  {
    Echo echo;
    auto const signal = echoSignal ("tone", {}, 10, 1500.0, 0.0);
    QVERIFY2 (!signal.empty (), "no render");
    auto const pcm = periods (signal, 800, 1, 1000, 21u);
    auto const input = sessionHeader () + control ({{"mode", "FT4"}, {"depth", 1}, {"utc", "00:00:00"}})
      + frame (0x01u, QByteArray (2 * 21 * BlockSamples, '\0')) + control (with (echoMode (echo, true), {{"utc", label (0)}}))
      + control ({{"echo_average", 3}}) + control ({{"nagain_flag", true}}) + audio (pcm, 0, PeriodSamples) + halt ();
    auto const run = runCodec (input, {}, {"WSJT_CODEC_TEST_HOLD_PASS=21:4000"});
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    QVERIFY2 (run.err.contains ("wsjt-codec: hold done nzhsym=21"), run.err.constData ());
    QStringList order;
    for (auto const& event : run.events)
      {
        auto const type = event.value ("t").toString ();
        if ("error" == type) order << event.value ("code").toString ();
        else if ("echo" == type || "decode_finished" == type)
          order << type + ' ' + event.value ("time").toString () + event.value ("period_end").toString ();
      }
    // The FT4 period's decode_finished comes last: its pass was still held
    // while the Echo period was measured.
    QStringList expected {"configure_range_error", "decode_again_unavailable", "echo 120000",
                          "decode_finished 120000", "decode_finished 000000"};
    if ("1" == qEnvironmentVariable ("WSJT_CODEC_TEST_INLINE")) expected.move (4, 0);
    QCOMPARE (order, expected);
    auto const again = ofType (run.events, "error").value (1);
    QVERIFY2 (again.value ("detail").toString ().contains ("Echo"), qPrintable (again.value ("detail").toString ()));
  }

  // (p): two 6 s cycles. Half duplex, the client sends no audio while it
  // transmits, as the GUI's input stops, and a discontinuity at the next
  // period's start: only the receive halves are measured. Full duplex, it
  // sends every sample: the transmit halves are measured too, as the GUI's
  // are with Full Duplex on.
  void transmitHalvesFollowTheInput_data ()
  {
    QTest::addColumn<bool> ("fullDuplex");
    QTest::newRow ("half duplex") << false;
    QTest::newRow ("full duplex") << true;
  }

  void transmitHalvesFollowTheInput ()
  {
    QFETCH (bool, fullDuplex);
    Echo echo;
    echo.spread = 3;
    echo.freq = Dithered;
    echo.doppler = 12;
    echo.average = 5;
    auto const signal = echoSignal ("tone", {}, 10, echo.freq, echo.doppler);
    auto const ownSignal = echoSignal ("tone", {}, 10, 1500.0, 0.0);
    QVERIFY2 (!signal.empty () && !ownSignal.empty (), "no render");
    // Periods 0 and 2 are the transmit halves, loud with the station's own
    // signal; 1 and 3 receive the echo.
    auto const pcm = periods (signal, 800, 4, 1000, 777u, ownSignal, 6000, 1);
    QByteArray input = sessionHeader () + control (echoMode (echo, true));
    auto script = echo.script ();
    for (int p = 0; p < 4; ++p)
      {
        auto const from = p * PeriodSamples;
        input += control ({{"utc", label (p)}});
        if (fullDuplex || 1 == p % 2)
          {
            input += audio (pcm, from, from + PeriodSamples);
            script << QString {"period live %1"}.arg (from);
          }
        else
          {
            // The input runs until the transmission starts, stops, and
            // resumes 2.6 s in; the next period starts on its boundary.
            input += audio (pcm, from, from + 1200) + audio (pcm, from + 31200, from + PeriodSamples)
              + discontinuity ();
          }
      }
    auto const run = runCodec (input + halt ());
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    auto const model = runModel (pcm, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const events = ofType (run.events, "echo");
    QCOMPARE (events.size (), fullDuplex ? 4 : 2);
    QVERIFY2 (compareAll (events, model.measures, false).isEmpty (), qPrintable (compareAll (events, model.measures, false)));
    QStringList times;
    for (auto const& event : events) times << event.value ("time").toString ();
    QCOMPARE (times, fullDuplex ? (QStringList {"120000", "120003", "120006", "120009"})
              : (QStringList {"120003", "120009"}));
    QCOMPARE (ofType (run.events, "decode_finished").size (), events.size ());
  }

  // With ref_spectrum "apply" on live audio the codec measures the
  // audio as equalized: its events equal the GUI's on the samples its
  // ReferenceSpectrumInput equalized (spectrum_gui_model).
  void equalizedMeasuresMatchTheGui ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const reference = directory.path () + "/reference";
    auto const ft8 = wavPcm (inSamples ("FT8/210703_133430.wav")).left (2 * 15 * SampleRate);
    QVERIFY2 (ft8.size () == 2 * 15 * SampleRate, "no FT8 recording");
    QVERIFY2 (!runSpectrumModel (reference, ft8, {"datadir " + reference, "reference measure",
                                                 QString {"period live 0 %1"}.arg (15 * SampleRate)}).isEmpty (),
              "the GUI's measurement failed");
    auto const refspec = readFile (reference + "/refspec.dat");
    QVERIFY2 (!refspec.isEmpty (), "no refspec.dat");
    Echo echo;
    echo.spread = 2.5;
    echo.freq = Dithered;
    echo.doppler = 25;
    echo.average = 5;
    auto const signal = echoSignal ("tone", {}, 10, echo.freq, echo.doppler);
    QVERIFY2 (!signal.empty (), "no render");
    auto const pcm = periods (signal, 800, 2, 1000, 31337u);
    auto const equalizing = directory.path () + "/equalizing";
    QVERIFY (QDir {}.mkpath (equalizing) && writeFile (equalizing + "/refspec.dat", refspec));
    auto const equalizedPath = equalizing + "/equalized.pcm";
    QVERIFY2 (!runSpectrumModel (equalizing, pcm, {"datadir " + equalizing, "reference apply",
                                                  "equalized " + equalizedPath,
                                                  QString {"period live 0 %1"}.arg (PeriodSamples),
                                                  QString {"period live %1 %1"}.arg (PeriodSamples)}).isEmpty (),
              "the GUI's equalization failed");
    auto const equalized = readFile (equalizedPath);
    QCOMPARE (equalized.size (), pcm.size ());
    QVERIFY2 (equalized != pcm, "the filter changed nothing");
    auto const codecData = directory.path () + "/codec";
    QVERIFY (QDir {}.mkpath (codecData) && writeFile (codecData + "/refspec.dat", refspec));
    auto const input = sessionHeader () + control (with (echoMode (echo, true), {{"ref_spectrum", "apply"}, {"utc", label (0)}}))
      + audio (pcm, 0, PeriodSamples) + control ({{"utc", label (1)}}) + audio (pcm, PeriodSamples, 2 * PeriodSamples) + halt ();
    auto const run = runCodec (input, codecData);
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    auto const script = echo.script () << "period live 0" << QString {"period live %1"}.arg (PeriodSamples);
    auto const model = runModel (equalized, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const events = ofType (run.events, "echo");
    QVERIFY2 (compareAll (events, model.measures, false).isEmpty (), qPrintable (compareAll (events, model.measures, false)));
    auto const plain = runModel (pcm, script);
    QVERIFY2 (plain.ok, qPrintable (plain.failure));
    QVERIFY2 (!compareAll (events, plain.measures, false).isEmpty (), "the equalized measurements equal the plain ones");
  }

  // With spectrum on, Echo sends a period mode's rows, the GUI's symspec_
  // rows: live every block of the period, from a file up to the stop.
  void spectrumRowsAsThePeriodModes_data ()
  {
    QTest::addColumn<bool> ("live");
    QTest::newRow ("live") << true;
    QTest::newRow ("file") << false;
  }

  void spectrumRowsAsThePeriodModes ()
  {
    QFETCH (bool, live);
    Echo echo;
    auto const signal = echoSignal ("tone", {}, 10, 1500.0, 0.0);
    QVERIFY2 (!signal.empty (), "no render");
    auto const pcm = periods (signal, 800, 1, 1000, 5u);
    auto const run = runCodec (sessionHeader () + control (with (echoMode (echo, live), {{"spectrum", true}, {"utc", label (0)}}))
                               + audio (pcm, 0, PeriodSamples) + halt ());
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const lines = runSpectrumModel (directory.path (), pcm, {"stop 9", QString {"period %1 0 %2"}
                                                                  .arg (live ? "live" : "file").arg (PeriodSamples)});
    QStringList expected;
    for (auto const& line : lines)
      if (line.startsWith ("spectrum ")) expected << line;
    QCOMPARE (expected.size (), live ? 10 : 9);
    QStringList got;
    for (auto const& row : ofType (run.events, "spectrum"))
      {
        QStringList words {"spectrum", QString::number (row.value ("k").toInt ()), QString::number (row.value ("hsym").toInt ())};
        for (auto key : {"power", "peak", "df"})
          words << QString {"%1"}.arg (bitsOf (float (row.value (key).toDouble ())), 8, 16, QChar {'0'});
        words << QString::number (row.value ("bins").toInt ()) << row.value ("data").toString ();
        got << words.join (' ');
      }
    QCOMPARE (got, expected);
    QCOMPARE (ofType (run.events, "echo").size (), 1);
  }

  // No JT9 decode in Echo, where the GUI's dataSink goes on to request one:
  // no decode, busy, pass or pass_skipped line, with busy_events on.
  void noJt9DecodeInEcho ()
  {
    Echo echo;
    auto const signal = echoSignal ("tone", {}, 10, 1500.0, 0.0);
    QVERIFY2 (!signal.empty (), "no render");
    auto const pcm = periods (signal, 800, 2, 1000, 6u);
    auto const run = runCodec (sessionHeader () + control (with (echoMode (echo, true), {{"busy_events", true}, {"utc", label (0)}}))
                               + audio (pcm, 0, PeriodSamples) + control ({{"utc", label (1)}})
                               + audio (pcm, PeriodSamples, 2 * PeriodSamples) + halt ());
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    // ready lists ECHO among the modes configure decodes.
    QVERIFY (ofType (run.events, "ready").value (0).value ("modes").toArray ().contains (QJsonValue {"ECHO"}));
    for (auto type : {"decode", "busy", "pass_progress", "pass_skipped"}) QCOMPARE (ofType (run.events, type).size (), 0);
    QVERIFY2 (!run.err.contains ("wsjt-codec: pass "), run.err.constData ());
    QCOMPARE (ofType (run.events, "echo").size (), 2);
    QCOMPARE (ofType (run.events, "decode_finished").size (), 2);
    // Each period's lines: its echo, then its decode_finished.
    QStringList order;
    for (auto const& event : run.events)
      if (event.value ("t").toString () == "echo" || event.value ("t").toString () == "decode_finished")
        order << event.value ("t").toString () + ' ' + event.value ("time").toString ()
          + event.value ("period_end").toString ();
    QCOMPARE (order, (QStringList {"echo 120000", "decode_finished 120000", "echo 120003", "decode_finished 120003"}));
    // Nor a decode again: the GUI's Decode button is off in Echo.
    auto const again = runCodec (sessionHeader () + control (with (echoMode (echo, true), {{"utc", label (0)}}))
                                 + audio (pcm, 0, PeriodSamples) + control ({{"nagain_flag", true}}) + halt ());
    QVERIFY2 (again.clean (), qPrintable (again.report ()));
    auto const errors = ofType (again.events, "error");
    QCOMPARE (errors.size (), 1);
    QCOMPARE (errors.first ().value ("code").toString (), QString {"decode_again_unavailable"});
    QCOMPARE (ofType (again.events, "echo").size (), 1);
    QCOMPARE (ofType (again.events, "decode").size (), 0);
  }

  // A period that ends before its 9th block is not measured, as the GUI
  // never reaches its Echo block there; ECHO's period is 3 s.
  void shortPeriodsAreNotMeasured_data ()
  {
    QTest::addColumn<QString> ("end");
    QTest::newRow ("discontinuity") << QString {"discontinuity"};
    QTest::newRow ("flush") << QString {"flush"};
    QTest::newRow ("halt") << QString {"halt"};
  }

  void shortPeriodsAreNotMeasured ()
  {
    QFETCH (QString, end);
    Echo echo;
    auto const signal = echoSignal ("tone", {}, 10, 1500.0, 0.0);
    QVERIFY2 (!signal.empty (), "no render");
    auto const pcm = periods (signal, 800, 2, 1000, 8u);
    // A whole period (mode ECHO alone sets 3 s), then one short of its stop.
    auto input = sessionHeader () + control ({{"mode", "ECHO"}, {"live_audio", true}, {"utc", label (0)}})
      + audio (pcm, 0, PeriodSamples) + control ({{"utc", label (1)}}) + audio (pcm, PeriodSamples, PeriodSamples + StopSamples - 1);
    if ("halt" != end) input += frame (0x02u, QString {R"({"t":"%1"})"}.arg (end).toUtf8 ());
    auto const run = runCodec (input + halt ());
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    auto const events = ofType (run.events, "echo");
    QCOMPARE (events.size (), 1);
    QCOMPARE (events.first ().value ("time").toString (), QString {"120000"});
    // flush, halt and the end of input end the short period with its
    // decode_finished, as in every period mode; a discontinuity drops it.
    QCOMPARE (ofType (run.events, "decode_finished").size (), "discontinuity" == end ? 1 : 2);
  }

  // The keys' values are checked: one out of range declines the frame with
  // configure_range_error, one of the wrong type with configure_type_error.
  // ECHO takes only a trperiod of 3. The keys are accepted in every mode.
  void configureChecksTheEchoKeys_data ()
  {
    QTest::addColumn<QByteArray> ("frame");
    QTest::addColumn<QString> ("code");
    QTest::addColumn<QString> ("key");
    auto const row = [] (char const* name, char const* json, char const* code, char const* key) {
      QTest::newRow (name) << QByteArray {json} << QString {code} << QString {key};
    };
    row ("trperiod 6", R"({"t":"configure","mode":"ECHO","trperiod":6})", "configure_range_error", "trperiod");
    row ("kind beep", R"({"t":"configure","mode":"ECHO","kind":"beep"})", "configure_range_error", "kind");
    row ("kind 1", R"({"t":"configure","mode":"ECHO","kind":1})", "configure_type_error", "kind");
    row ("echo_spacing 12", R"({"t":"configure","mode":"ECHO","echo_spacing":12})", "configure_range_error", "echo_spacing");
    row ("echo_average 3", R"({"t":"configure","mode":"ECHO","echo_average":3})", "configure_range_error", "echo_average");
    row ("echo_average 0", R"({"t":"configure","mode":"ECHO","echo_average":0})", "configure_range_error", "echo_average");
    row ("echo_freq 1510", R"({"t":"configure","mode":"ECHO","echo_freq":1510})", "configure_range_error", "echo_freq");
    row ("echo_freq 1489.9", R"({"t":"configure","mode":"ECHO","echo_freq":1489.9})", "configure_range_error", "echo_freq");
    row ("echo_text K1/ABC", R"({"t":"configure","mode":"ECHO","echo_text":"K1/ABC"})", "configure_range_error", "echo_text");
    row ("echo_text seven", R"({"t":"configure","mode":"ECHO","echo_text":"K1ABCDE"})", "configure_range_error", "echo_text");
    row ("doppler string", R"({"t":"configure","mode":"ECHO","doppler":"5"})", "configure_type_error", "doppler");
    row ("doppler 3000000000", R"({"t":"configure","mode":"ECHO","doppler":3000000000})", "configure_range_error", "doppler");
    row ("rit -2147483648.5", R"({"t":"configure","mode":"ECHO","rit":-2147483648.5})", "configure_range_error", "rit");
    row ("rit bool", R"({"t":"configure","mode":"ECHO","rit":true})", "configure_type_error", "rit");
    row ("spread string", R"({"t":"configure","mode":"ECHO","spread":"4"})", "configure_type_error", "spread");
    row ("echo_auto 1", R"({"t":"configure","mode":"ECHO","echo_auto":1})", "configure_type_error", "echo_auto");
    row ("in FT8", R"({"t":"configure","mode":"FT8","echo_spacing":13})", "configure_range_error", "echo_spacing");
    row ("accepted in FT8", R"({"t":"configure","mode":"FT8","doppler":5,"kind":"message","echo_text":"k1abc","echo_freq":1500})", "", "");
    row ("accepted", R"({"t":"configure","mode":"ECHO","doppler":-250,"rit":30,"spread":0,"echo_spacing":30,"echo_average":100,"echo_auto":true,"echo_text":"","kind":"CW"})", "", "");
  }

  void configureChecksTheEchoKeys ()
  {
    QFETCH (QByteArray, frame);
    QFETCH (QString, code);
    QFETCH (QString, key);
    auto const run = runCodec (sessionHeader () + ::frame (0x02u, frame) + halt ());
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    auto const errors = ofType (run.events, "error");
    if (code.isEmpty ())
      {
        QVERIFY2 (errors.isEmpty (), qPrintable (run.report ()));
        return;
      }
    QCOMPARE (errors.size (), 1);
    QCOMPARE (errors.first ().value ("code").toString (), code);
    QCOMPARE (errors.first ().value ("key").toString (), key);
    if ("configure_range_error" == code && frame.contains ("ECHO"))
      QCOMPARE (errors.first ().value ("mode").toString (), QString {"ECHO"});
  }

  // A client that ignores line types it does not know still works: an Echo
  // session's other lines are one JSON object a line, each period ends with
  // its decode_finished, and the FT4 period after it decodes as alone.
  void clientsIgnoringEchoStillWork ()
  {
    auto const ft4 = wavPcm (inSamples ("FT4/000000_000002.wav"));
    QVERIFY2 (!ft4.isEmpty (), "no FT4 recording");
    QJsonObject const ft4Settings {{"mode", "FT4"}, {"utc", "00:00:00"}, {"depth", 3}, {"live_audio", false}};
    Echo echo;
    auto const signal = echoSignal ("message", Call, 10, 1500.0, 0.0);
    QVERIFY2 (!signal.empty (), "no render");
    auto const pcm = periods (signal, 800, 2, 1000, 9u);
    auto const ft4Part = control (ft4Settings) + audio (ft4, 0, int (ft4.size () / 2)) + halt ();
    auto const withEcho = runCodec (sessionHeader () + control (with (echoMode (echo, true), {{"utc", label (0)}}))
                                    + audio (pcm, 0, PeriodSamples) + control ({{"utc", label (1)}})
                                    + audio (pcm, PeriodSamples, 2 * PeriodSamples) + ft4Part);
    auto const alone = runCodec (sessionHeader () + ft4Part);
    QVERIFY2 (withEcho.clean () && alone.clean (), qPrintable (withEcho.report () + alone.report ()));
    QCOMPARE (ofType (withEcho.events, "echo").size (), 2);
    QVERIFY2 (withEcho.longest <= LineLimit, qPrintable (QString::number (withEcho.longest)));
    QStringList known, knownAlone;
    for (auto const& line : withEcho.out.split ('\n'))
      {
        if (line.isEmpty ()) continue;
        auto const object = QJsonDocument::fromJson (line).object ();
        QVERIFY2 (1 == object.value ("v").toInt () && object.contains ("t"), line.constData ());
        if ("echo" != object.value ("t").toString ()) known << QString::fromUtf8 (line);
      }
    for (auto const& line : alone.out.split ('\n'))
      if (!line.isEmpty ()) knownAlone << QString::fromUtf8 (line);
    // The Echo periods' decode_finished lines, then the FT4 session's lines.
    QCOMPARE (known.mid (0, 3), (QStringList {knownAlone.first (),
                                              R"({"v":1,"t":"decode_finished","period_end":"120000","synced":0,"decoded":0,"average":0})",
                                              R"({"v":1,"t":"decode_finished","period_end":"120003","synced":0,"decoded":0,"average":0})"}));
    QCOMPARE (known.mid (3), knownAlone.mid (1));
    QVERIFY (ofType (alone.events, "decode").size () > 0);
  }
};

QTEST_GUILESS_MAIN (TestStreamingEcho)

#include "test_streaming_echo.moc"
