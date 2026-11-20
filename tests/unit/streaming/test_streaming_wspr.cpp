// SPDX-License-Identifier: GPL-3.0-or-later
// WSPR in the codec against wsprd, the GUI's WSPR decoder, run as the GUI
// runs it (MainWindow::decode: its depth options, -a, -f with the dial as
// %.6f MHz, and a WAV file named for the period, YYMMDD_HHMM.wav, with a
// 44-byte header) on the same audio: samples/WSPR/150426_0918.wav, and
// periods mixed from the codec's own WSPR renders in Gaussian noise of a
// fixed seed (a Mersenne twister and a sum of 12 uniforms, the same on every
// platform). From each WSPR decode event the test rebuilds wsprd's lines
// with C printf, as lib/streaming_wspr.f90 tells a client to.
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <random>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace
{
constexpr int SampleRate = 12000;
constexpr int PeriodSamples = 120 * SampleRate;
// The GUI's WSPR stop (MainWindow::fixStop).
constexpr int Stop = 396;
// The GUI's -f for a 14.0956 MHz dial: QString::arg (dial / 1e6, 0, 'f', 6).
constexpr char Dial[] = "14.095600";
constexpr char Date[] = "261116";
constexpr double Sigma = 1000.0;
// The codec's sample buffer, 30 minutes of audio, in bytes.
constexpr quint32 SampleBufferBytes = 30u * 60u * SampleRate * 2u;
// lib/streaming_decode.f90's NO_DECODED_PERIOD.
constexpr char NoDecodedPeriod[] = "no decoded period to decode again (none yet or since a mode or period change; "
                                   "MSK144, Echo, JTTY, FreqCal and WSPR keep none)";

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

QByteArray halt () {return frame (0x02u, R"({"t":"halt"})");}
QByteArray flush () {return frame (0x02u, R"({"t":"flush"})");}

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

QByteArray audio (QByteArray const& pcm) {return audio (pcm, 0, int (pcm.size () / 2));}

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

// The codec on input, in directory (-a and -t), or in a fresh one, with any
// further command-line arguments.
Run runCodec (QByteArray const& input, QString directory = {}, QStringList const& environment = {},
              QStringList const& arguments = {})
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
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), QStringList {"-a", directory, "-t", directory} + arguments);
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

// --- audio ---------------------------------------------------------------

// The codec's render of each text in WSPR at freq Hz (its lowest tone), one
// session for all; an empty entry where a render failed.
QList<QByteArray> renders (QStringList const& texts, int freq)
{
  QByteArray input = sessionHeader ();
  for (int i = 0; i < texts.size (); ++i)
    input += control ({{"id", i + 1}, {"mode", "WSPR"}, {"text", texts.at (i)}, {"freq", freq},
                       {"rate", SampleRate}}, "render");
  auto const run = runCodec (input);
  QList<QByteArray> pcm;
  for (int i = 0; i < texts.size (); ++i)
    {
      QByteArray samples;
      for (auto const& event : ofType (run.events, "pcm"))
        if (event.value ("id").toInt () == i + 1)
          samples += QByteArray::fromBase64 (event.value ("data").toString ().toLatin1 ());
      pcm << samples;
    }
  return pcm;
}

QByteArray render (QString const& text, int freq)
{
  static QHash<QString, QByteArray> cache;
  auto const key = QString {"%1@%2"}.arg (text).arg (freq);
  if (!cache.contains (key)) cache.insert (key, renders ({text}, freq).value (0));
  return cache.value (key);
}

// The codec's WSPR tones for text: its render's encoded line.
QJsonArray tonesOf (QString const& text)
{
  auto const run = runCodec (sessionHeader () + control ({{"id", 1}, {"mode", "WSPR"}, {"text", text}, {"freq", 1500},
                                                           {"rate", SampleRate}}, "render"));
  auto const encoded = ofType (run.events, "encoded");
  return encoded.isEmpty () ? QJsonArray {} : encoded.first ().value ("tones").toArray ();
}

// text's tones from freq Hz (the lowest tone) drifting `drift` Hz over the
// transmission, as wsprd models drift: symbol i off by drift*(i-81)/162 Hz.
// 8192 samples a symbol, tones 12000/8192 Hz apart, continuous phase, full
// scale as the codec's render.
QByteArray drifting (QString const& text, int freq, double drift)
{
  auto const tones = tonesOf (text);
  if (162 != tones.size ()) return {};
  constexpr int SymbolSamples = 8192;
  QByteArray pcm (2 * 162 * SymbolSamples, '\0');
  double phase = 0;
  for (int i = 0; i < 162; ++i)
    {
      auto const f = freq + tones.at (i).toInt () * double (SampleRate) / SymbolSamples + drift * (i - 81) / 162.0;
      for (int j = 0; j < SymbolSamples; ++j)
        {
          qToLittleEndian<qint16> (qint16 (std::lround (32767.0 * std::sin (phase))),
                                   pcm.data () + 2 * (i * SymbolSamples + j));
          phase = std::fmod (phase + 2.0 * 3.14159265358979323846 * f / SampleRate, 2.0 * 3.14159265358979323846);
        }
    }
  return pcm;
}

// One station in a mixed period: its message, the render's frequency, its SNR
// in WSPR's 2500 Hz, its start in seconds, and its drift (Hz over the
// transmission; 0: the codec's render).
struct Station
{
  char const* text;
  int freq;
  double snr;
  double start;
  double drift {0};
};

// A 120 s period of white Gaussian noise of standard deviation Sigma with the
// stations' renders added: a constant envelope of amplitude A has power
// A^2/2, and the noise's power in 2500 Hz of 6000 is Sigma^2*2500/6000. Each
// noise sample is the sum of 12 uniforms of mt19937's 32-bit output, less 6,
// so every platform draws the same; empty when a render failed.
QByteArray mixed (QList<Station> const& stations, unsigned seed)
{
  std::mt19937 random {seed};
  std::vector<double> x (PeriodSamples);
  for (auto& value : x)
    {
      double sum = 0;
      for (int i = 0; i < 12; ++i) sum += double (random ()) / 4294967296.0;
      value = Sigma * (sum - 6.0);
    }
  for (auto const& station : stations)
    {
      auto const pcm = 0 == station.drift ? render (station.text, station.freq)
        : drifting (station.text, station.freq, station.drift);
      if (pcm.isEmpty ()) return {};
      auto const amplitude = std::sqrt (2.0 * Sigma * Sigma * 2500.0 / 6000.0 * std::pow (10.0, station.snr / 10.0))
        / 32767.0;
      auto const at = int (std::lround (station.start * SampleRate));
      for (int i = 0; 2 * i + 1 < pcm.size () && at + i < PeriodSamples; ++i)
        x[at + i] += amplitude * qFromLittleEndian<qint16> (pcm.constData () + 2 * i);
    }
  QByteArray out (2 * PeriodSamples, '\0');
  for (int k = 0; k < PeriodSamples; ++k)
    qToLittleEndian<qint16> (qint16 (std::max (-32768.0, std::min (32767.0, std::round (x[k])))), out.data () + 2 * k);
  return out;
}

QString inSamples (QString const& relative)
{
  auto const directory = qEnvironmentVariable ("WSJT_SAMPLES_DIR");
  return directory.isEmpty () ? QString {} : directory + '/' + relative;
}

// A text file wsprd or the codec wrote, its lines ending in LF: C's text mode
// ends them in CR LF on Windows.
QByteArray readText (QString const& path)
{
  return readFile (path).replace ("\r\n", "\n");
}

// A WAV file's samples after its 44-byte header, as wsprd reads them.
QByteArray wavPcm (QString const& path)
{
  auto const bytes = readFile (path);
  return bytes.size () > 44 ? bytes.mid (44) : QByteArray {};
}

// pcm as the GUI's WAV file: a 44-byte header, then the samples.
QByteArray wav (QByteArray const& pcm)
{
  QByteArray bytes;
  auto const put32 = [&] (quint32 v) {for (int i = 0; i < 4; ++i) bytes.append (char (v >> 8 * i));};
  auto const put16 = [&] (quint16 v) {bytes.append (char (v)); bytes.append (char (v >> 8));};
  bytes += "RIFF";
  put32 (quint32 (36 + pcm.size ()));
  bytes += "WAVEfmt ";
  put32 (16);
  put16 (1);
  put16 (1);
  put32 (SampleRate);
  put32 (2 * SampleRate);
  put16 (2);
  put16 (16);
  bytes += "data";
  put32 (quint32 (pcm.size ()));
  return bytes + pcm;
}

QByteArray padded (QByteArray pcm)
{
  pcm = pcm.left (2 * PeriodSamples);
  pcm.append (QByteArray (2 * PeriodSamples - pcm.size (), '\0'));
  return pcm;
}

// Types 1 and 2 and the same stations' type 3, strong, for the hash table rows.
QList<Station> const Callers {{"K1ABC FN42 37", 1450, -15, 1.0}, {"PJ4/W9XYZ 23", 1520, -15, 1.5},
                              {"G4ABC/P 10", 1570, -15, 1.2}};
QList<Station> const Hashed {{"<K1ABC> FN42AX 37", 1450, -15, 1.0}, {"<PJ4/W9XYZ> FK52UD 23", 1520, -15, 1.5},
                             {"<G4ABC/P> IO91WM 10", 1570, -15, 1.2}};

// A period of the rows' audio, its date and HHMM, and the hash table both
// decoders start from (empty: none).
struct Period
{
  QByteArray pcm;
  QString date;
  QString hhmm;
  QByteArray hashtable;
};

QJsonObject label (Period const& p)
{
  if ("0918" == p.hhmm) return {{"nutc", 918}};
  return {{"utc", QString {"%1:%2:00"}.arg (p.hhmm.left (2), p.hhmm.mid (2))}};
}

QStringList depthArgs (int depth)
{
  switch (depth)
    {
    case 1: return {"-qB"};
    case 2: return {"-C", "500", "-o", "4"};
    case 3: return {"-C", "500", "-o", "4", "-d"};
    default: return {};
    }
}

// wsprd's run on one period in directory: its stdout and the files it leaves in -a.
struct Wsprd
{
  bool ok {false};
  QString failure;
  QByteArray out;
  QByteArray all;
  QByteArray spots;
  QByteArray hash;
  bool hashExists {false};
};

Wsprd wsprdOn (QByteArray const& pcm, int depth, QString const& directory, QString const& date, QString const& hhmm,
                char const* dial = Dial)
{
  Wsprd w;
  auto const name = QDir {directory}.filePath (QString {"%1_%2.wav"}.arg (date, hhmm));
  if (!writeFile (name, wav (pcm)))
    {
      w.failure = "cannot write " + name;
      return w;
    }
  QProcess process;
  process.setWorkingDirectory (directory);
  process.start (QString::fromUtf8 (WSPRD_EXECUTABLE), depthArgs (depth) << "-a" << directory << "-f" << dial << name);
  if (!process.waitForFinished (waitMs ()) || process.exitCode () != 0)
    {
      w.failure = "wsprd failed: " + QString::fromUtf8 (process.readAllStandardError ());
      return w;
    }
  w.out = process.readAllStandardOutput ().replace ("\r\n", "\n");
  w.all = readText (directory + "/ALL_WSPR.TXT");
  w.spots = readText (directory + "/wspr_spots.txt");
  w.hashExists = QFileInfo::exists (directory + "/hashtable.txt");
  w.hash = readFile (directory + "/hashtable.txt");
  w.ok = true;
  return w;
}

// wsprd's run in a fresh directory, from p's hash table.
Wsprd wsprdFresh (Period const& p, int depth)
{
  QTemporaryDir directory;
  if (!directory.isValid ()) return {};
  if (!p.hashtable.isEmpty () && !writeFile (directory.filePath ("hashtable.txt"), p.hashtable)) return {};
  return wsprdOn (p.pcm, depth, directory.path (), p.date, p.hhmm);
}

// --- wsprd's lines from the events ------------------------------------------

QStringList const WsprKeys {"audio_freq", "snr_real", "drift", "wspr_sync", "wspr_sync10", "wspr_pass",
                            "wspr_blocksize", "wspr_jitter", "wspr_osd", "wspr_hard_errors", "wspr_cycles",
                            "wspr_metric"};
QStringList const BaseKeys {"v", "t", "mode", "time", "snr", "dt", "freq", "message", "pass"};

QList<QJsonObject> wsprDecodes (QList<QJsonObject> const& events, QString const& time = {})
{
  QList<QJsonObject> decodes;
  for (auto const& event : ofType (events, "decode"))
    if (event.value ("mode").toString () == "WSPR" && (time.isEmpty () || event.value ("time").toString () == time))
      decodes << event;
  return decodes;
}

struct Lines
{
  QByteArray out;
  QByteArray all;
  QByteArray spots;
  QString problem;
};

#if defined (__GNUC__)
__attribute__ ((format (printf, 1, 2)))
#endif
QByteArray printed (char const* format, ...)
{
  char text[256];
  va_list args;
  va_start (args, format);
  std::vsnprintf (text, sizeof text, format, args);
  va_end (args);
  return text;
}

// wsprd's stdout, ALL_WSPR.TXT and wspr_spots.txt lines for decodes, with the
// client's dial (the string wsprd's -f reads) and date: lib/streaming_wspr.f90's
// recipe, and each event's own fields checked on the way.
Lines rebuild (QList<QJsonObject> const& decodes, QString const& date, char const* dial)
{
  Lines lines;
  auto const d = std::strtod (dial, nullptr);
  auto keys = BaseKeys + WsprKeys;
  keys.sort ();
  for (auto const& e : decodes)
    {
      auto const show = QString::fromUtf8 (QJsonDocument {e}.toJson (QJsonDocument::Compact));
      auto got = e.keys ();
      got.sort ();
      if (got != keys)
        {
          lines.problem = "fields of " + show;
          return lines;
        }
      for (auto const& key : WsprKeys + QStringList {"snr", "dt", "freq", "pass"})
        if (!e.value (key).isDouble ())
          {
            lines.problem = key + " is no number in " + show;
            return lines;
          }
      auto const time4 = e.value ("time").toString ().left (4).toStdString ();
      auto const date6 = date.toStdString ();
      auto const message = e.value ("message").toString ().toStdString ();
      auto const snr = float (e.value ("snr_real").toDouble ());
      auto const dt = float (e.value ("dt").toDouble ());
      auto const sync = float (e.value ("wspr_sync").toDouble ());
      auto const audioFreq = e.value ("audio_freq").toDouble ();
      auto const freq = d + audioFreq / 1e6;
      auto const drift = e.value ("drift").toInt ();
      auto const cycles = unsigned (e.value ("wspr_cycles").toInt ());
      // The integers every decode has: wsprd's snr and audio frequency, rounded half to even.
      if (e.value ("snr").toInt () != int (std::lrint (double (snr)))
          || e.value ("freq").toInt () != int (std::lrint (double (float (audioFreq))))
          || double (float (audioFreq)) != audioFreq)
        {
          lines.problem = "snr, freq or audio_freq in " + show;
          return lines;
        }
      lines.out += printed ("%4s %3.0f %4.1f %10.6f %2d  %-s \n", time4.c_str (), double (snr), double (dt), freq, drift,
                            message.c_str ());
      lines.all += printed ("%6s %4s %3.0f %5.2f %11.7f  %-22s %2d %5.2f %2d %2d %4d %2d %3d %5u %5d\n",
                            date6.c_str (), time4.c_str (), double (snr), double (dt), freq, message.c_str (), drift,
                            double (sync), e.value ("wspr_pass").toInt (), e.value ("wspr_blocksize").toInt (),
                            e.value ("wspr_jitter").toInt (), e.value ("wspr_osd").toInt (),
                            e.value ("wspr_hard_errors").toInt (), cycles, e.value ("wspr_metric").toInt ());
      lines.spots += printed ("%6s %4s %3d %3.0f %4.1f %10.6f  %-22s %2d %5u %4d\n", date6.c_str (), time4.c_str (),
                              e.value ("wspr_sync10").toInt (), double (snr), double (dt), freq, message.c_str (),
                              drift, cycles, e.value ("wspr_jitter").toInt ());
    }
  lines.out += "<DecodeFinished>\n";
  return lines;
}

// The codec's period labelled time against wsprd's run: the rebuilt stdout
// (messages, snr, dt, dial + audio frequency, drift), and with files
// ALL_WSPR.TXT and wspr_spots.txt byte for byte (allBefore: the lines an
// earlier run appended); each decode's time and pass, and the period's
// decode_finished. Empty when they agree.
QString compare (Run const& codec, Wsprd const& w, Period const& p, int pass, bool files,
                 QByteArray const& allBefore = {})
{
  if (!w.ok) return w.failure;
  auto const time = p.hhmm + "00";
  auto const decodes = wsprDecodes (codec.events, time);
  auto const lines = rebuild (decodes, p.date, Dial);
  if (!lines.problem.isEmpty ()) return lines.problem;
  if (lines.out != w.out)
    return QString {"stdout\ncodec:\n%1wsprd:\n%2"}.arg (QString::fromUtf8 (lines.out), QString::fromUtf8 (w.out));
  if (files && allBefore + lines.all != w.all)
    return QString {"ALL_WSPR.TXT\ncodec:\n%1wsprd:\n%2"}.arg (QString::fromUtf8 (allBefore + lines.all),
                                                               QString::fromUtf8 (w.all));
  if (files && lines.spots != w.spots)
    return QString {"wspr_spots.txt\ncodec:\n%1wsprd:\n%2"}.arg (QString::fromUtf8 (lines.spots),
                                                                 QString::fromUtf8 (w.spots));
  for (auto const& e : decodes)
    if (e.value ("pass").toInt () != pass)
      return "pass in " + QString::fromUtf8 (QJsonDocument {e}.toJson (QJsonDocument::Compact));
  int finished = 0;
  for (auto const& f : ofType (codec.events, "decode_finished"))
    if (f.value ("period_end").toString () == time)
      {
        ++finished;
        if (f.value ("synced").toInt () != 0 || f.value ("decoded").toInt () != decodes.size ()
            || f.value ("average").toInt () != 0)
          return "counts in " + QString::fromUtf8 (QJsonDocument {f}.toJson (QJsonDocument::Compact));
      }
  if (1 != finished) return QString {"%1 decode_finished for %2"}.arg (finished).arg (time);
  return {};
}

QJsonObject wsprKeys (int depth, bool live) {return {{"mode", "WSPR"}, {"depth", depth}, {"live_audio", live}};}

// The rows' audio: the recording, and mixes of renders.
Period audioCase (QString const& name, QString* failure)
{
  Period p;
  p.date = Date;
  if ("golden" == name)
    {
      p.pcm = wavPcm (inSamples ("WSPR/150426_0918.wav"));
      if (p.pcm.isEmpty ()) *failure = "no recording";
      p.pcm = padded (p.pcm);
      p.date = "150426";
      p.hhmm = "0918";
      return p;
    }
  if ("mix" == name)
    {
      // Types 1, 2 (prefix and suffix) and 3, six stations.
      p.pcm = mixed ({{"W5XYZ EM10 33", 1420, -14, 1.0}, {"PJ4/W9XYZ 23", 1455, -18, 1.5},
                      {"<K1ABC> FN42AX 37", 1490, -16, 1.0}, {"W1XYZ/12 30", 1525, -20, 1.2},
                      {"JA1ABC PM95 40", 1560, -22, 2.0}, {"<K7DEF> DN31AB 27", 1590, -19, 1.0}}, 107u);
      p.hhmm = "0014";
    }
  else if ("dense" == name)
    {
      p.pcm = mixed ({{"K1ABC FN42 37", 1405, -21, 1.0}, {"<PJ4/W9XYZ> FK52UD 23", 1430, -23, 1.3},
                      {"G4ABC/P 10", 1455, -24, 1.6}, {"W5XYZ EM10 33", 1480, -20, 1.0},
                      {"VK2XYZ QF56 20", 1505, -25, 2.0}, {"DL1ABC JO62 30", 1530, -26, 1.1},
                      {"<K7DEF> DN31AB 27", 1555, -22, 1.0}, {"JA1ABC PM95 40", 1580, -27, 1.5}}, 109u);
      p.hhmm = "0018";
    }
  else if ("close" == name)
    {
      // Stations 3 Hz apart, where wsprd's -q (no time jitter) and -d (more
      // candidates) change what decodes: the GUI's depths decode differently.
      p.pcm = mixed ({{"K1ABC FN42 37", 1500, -22, 1.0}, {"W5XYZ EM10 33", 1503, -24, 1.3},
                      {"JA1ABC PM95 40", 1506, -25, 1.6}, {"VK2XYZ QF56 20", 1509, -24, 1.1},
                      {"W5XYZ EM10 33", 1440, -26, 1.0}, {"VK2XYZ QF56 20", 1445, -26, 1.4}}, 6u);
      p.hhmm = "0026";
    }
  else if ("drift" == name)
    {
      // Drift in half steps, which wsprd prints truncated.
      p.pcm = mixed ({{"K1ABC FN42 37", 1420, -14, 1.0, 1.5}, {"W5XYZ EM10 33", 1470, -14, 1.0, -1.5},
                      {"JA1ABC PM95 40", 1520, -14, 1.0, 2.5}, {"VK2XYZ QF56 20", 1570, -14, 1.0, -2.5}}, 41u);
      p.hhmm = "0022";
    }
  else if ("weak" == name)
    {
      // Near the threshold, with a hash table made as a station's would be:
      // wsprd at the GUI's depth 2 on the same stations' strong period. On
      // this audio K1ABC decodes at depths 2 and 3 only by OSD, which
      // accepts a call the table knows.
      Period strong {mixed (Callers, 1u), Date, "1200", {}};
      auto const seed = wsprdFresh (strong, 2);
      if (!seed.ok || seed.hash.isEmpty ()) *failure = "no seed table: " + seed.failure;
      p.hashtable = seed.hash;
      p.pcm = mixed ({{"K1ABC FN42 37", 1450, -30, 1.0}, {"PJ4/W9XYZ 23", 1520, -30, 1.5},
                      {"G4ABC/P 10", 1570, -30, 1.2}}, 1u);
      p.hhmm = "1202";
    }
  if (p.pcm.isEmpty () && failure->isEmpty ()) *failure = "no render";
  return p;
}

// The codec on p alone, in a fresh -a holding p's hash table.
Run codecOn (Period const& p, QJsonObject const& keys, QStringList const& environment = {})
{
  QTemporaryDir directory;
  if (!directory.isValid ()) return {};
  if (!p.hashtable.isEmpty () && !writeFile (directory.filePath ("hashtable.txt"), p.hashtable)) return {};
  return runCodec (sessionHeader () + control (with (keys, label (p))) + audio (p.pcm) + halt (), directory.path (),
                   environment);
}

QStringList messages (QList<QJsonObject> const& decodes)
{
  QStringList list;
  for (auto const& e : decodes) list << e.value ("message").toString ();
  return list;
}

// The hash table rows' two periods, labelled 12:00 and 12:02.
Period callersPeriod () {return {mixed (Callers, 21u), Date, "1200", {}};}
Period hashedPeriod () {return {mixed (Hashed, 22u), Date, "1202", {}};}

QStringList const Resolved {"<K1ABC> FN42AX 37", "<PJ4/W9XYZ> FK52UD 23", "<G4ABC/P> IO91WM 10"};
QStringList const Unresolved {"<...> FN42AX 37", "<...> FK52UD 23", "<...> IO91WM 10"};

// --- the GUI's equalization (as test_streaming_freqcal) ---------------------

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

// spectrum_gui_model on pcm with script; its standard output, or empty on failure.
QString runModel (QByteArray const& pcm, QStringList script, QString const& directory, QString* failure)
{
  if (!writeFile (directory + "/audio.raw", pcm))
    {
      *failure = "cannot write the audio";
      return {};
    }
  script.prepend ("audio " + directory + "/audio.raw");
  if (!writeFile (directory + "/script", (script.join ('\n') + '\n').toUtf8 ()))
    {
      *failure = "cannot write the script";
      return {};
    }
  QProcess process;
  process.setWorkingDirectory (directory);
  process.start (QString::fromUtf8 (SPECTRUM_GUI_MODEL), {directory + "/script"});
  if (!process.waitForFinished (waitMs ()) || process.exitCode () != 0)
    {
      *failure = "spectrum_gui_model failed: " + QString::fromUtf8 (process.readAllStandardError ());
      return {};
    }
  return QString::fromUtf8 (process.readAllStandardOutput ());
}

} // namespace

class TestStreamingWspr final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  // DONE (c): each period streamed through the codec in a fresh -a (the weak
  // one from its seeded hash table, as wsprd), live (live_audio true at file
  // speed: the decode at the stop, 1,368,576 samples) and from a file (at the
  // period's end), at the GUI's three depths, gives wsprd's messages, snr,
  // dt, dial + audio frequency and drift: its stdout, rebuilt from the
  // events, byte for byte; "pass":396 on each.
  void decodesMatchWsprd_data ()
  {
    QTest::addColumn<QString> ("name");
    QTest::addColumn<bool> ("live");
    QTest::addColumn<int> ("depth");
    for (auto const* name : {"golden", "mix", "dense", "close", "drift", "weak"})
      for (auto live : {true, false})
        for (int depth = 1; depth <= 3; ++depth)
          QTest::newRow (qPrintable (QString {"%1 %2 depth %3"}.arg (name, live ? "live" : "file").arg (depth)))
            << QString {name} << live << depth;
  }

  void decodesMatchWsprd ()
  {
    QFETCH (QString, name);
    QFETCH (bool, live);
    QFETCH (int, depth);
    QString failure;
    auto const p = audioCase (name, &failure);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    auto const codec = codecOn (p, wsprKeys (depth, live));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (ofType (codec.events, "error").isEmpty (), qPrintable (codec.report ()));
    auto const w = wsprdFresh (p, depth);
    auto const difference = compare (codec, w, p, Stop, false);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    auto const decodes = wsprDecodes (codec.events);
    QVERIFY2 (!decodes.isEmpty () || ("weak" == name && 1 == depth), qPrintable (codec.report ()));
    if ("weak" == name && depth > 1)
      {
        // The near-threshold case: the seeded table lets OSD decode.
        int osd = 0;
        for (auto const& e : decodes) osd += e.value ("wspr_osd").toInt ();
        QVERIFY2 (osd > 0, qPrintable (codec.report ()));
      }
  }

  // DONE (d): ALL_WSPR.TXT and wspr_spots.txt, rebuilt from the events with
  // C printf, the dial and the period's date, equal wsprd's byte for byte, in
  // a fresh -a. Live and from a file the decoder reads the same samples.
  void linesRebuildWsprdsFiles_data ()
  {
    QTest::addColumn<QString> ("name");
    QTest::addColumn<int> ("depth");
    for (auto const* name : {"golden", "mix", "dense", "close", "drift", "weak"})
      for (int depth = 1; depth <= 3; ++depth)
        QTest::newRow (qPrintable (QString {"%1 depth %2"}.arg (name).arg (depth))) << QString {name} << depth;
  }

  void linesRebuildWsprdsFiles ()
  {
    QFETCH (QString, name);
    QFETCH (int, depth);
    QString failure;
    auto const p = audioCase (name, &failure);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    auto const codec = codecOn (p, wsprKeys (depth, true));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const w = wsprdFresh (p, depth);
    auto const difference = compare (codec, w, p, Stop, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    QVERIFY2 (!w.all.isEmpty () || ("weak" == name && 1 == depth), "wsprd decoded nothing");
  }

  // The rows can tell the depth mapping apart: on the close stations wsprd
  // decodes differently with -qB than with -B alone (time jitter), and at
  // depth 3 than at depth 2 (more candidates).
  void depthsDecodeDifferently ()
  {
    QString failure;
    auto const p = audioCase ("close", &failure);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    QTemporaryDir a, b;
    QVERIFY (a.isValid () && b.isValid ());
    auto const name = QString {"%1_%2.wav"}.arg (p.date, p.hhmm);
    QVERIFY (writeFile (a.filePath (name), wav (p.pcm)));
    QProcess process;
    process.start (QString::fromUtf8 (WSPRD_EXECUTABLE), {"-B", "-a", a.path (), "-f", Dial, a.filePath (name)});
    QVERIFY (process.waitForFinished (waitMs ()) && 0 == process.exitCode ());
    auto const jitter = process.readAllStandardOutput ().replace ("\r\n", "\n");
    auto const depth1 = wsprdFresh (p, 1), depth2 = wsprdFresh (p, 2), depth3 = wsprdFresh (p, 3);
    QVERIFY2 (depth1.ok && depth2.ok && depth3.ok, qPrintable (depth1.failure + depth2.failure + depth3.failure));
    QVERIFY2 (depth1.out != jitter, depth1.out.constData ());
    QVERIFY2 (depth2.out != depth3.out, depth2.out.constData ());
  }

  // DONE (e): a type 3 call resolves through the hash table the session
  // carries from one period to the next, as wsprd's through hashtable.txt
  // across two runs in one -a: the second period's lines and the table
  // equal wsprd's. Without the first period it stays <...>.
  void typeThreeResolvesAcrossPeriods ()
  {
    auto const callers = callersPeriod ();
    auto const hashed = hashedPeriod ();
    QVERIFY2 (!callers.pcm.isEmpty () && !hashed.pcm.isEmpty (), "no render");
    QTemporaryDir codecDir, wsprdDir;
    QVERIFY (codecDir.isValid () && wsprdDir.isValid ());
    auto const codec = runCodec (sessionHeader () + control (with (wsprKeys (2, true), label (callers)))
                                 + audio (callers.pcm) + control (label (hashed)) + audio (hashed.pcm) + halt (),
                                 codecDir.path ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const first = wsprdOn (callers.pcm, 2, wsprdDir.path (), callers.date, callers.hhmm);
    QVERIFY2 (first.ok, qPrintable (first.failure));
    auto const second = wsprdOn (hashed.pcm, 2, wsprdDir.path (), hashed.date, hashed.hhmm);
    QVERIFY2 (second.ok, qPrintable (second.failure));
    auto difference = compare (codec, first, callers, Stop, false);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    difference = compare (codec, second, hashed, Stop, true, first.all);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    QCOMPARE (messages (wsprDecodes (codec.events, "120200")), Resolved);
    QCOMPARE (readFile (codecDir.filePath ("hashtable.txt")), second.hash);
    auto const alone = codecOn (hashed, wsprKeys (2, true));
    QVERIFY2 (alone.clean (), qPrintable (alone.report ()));
    QCOMPARE (messages (wsprDecodes (alone.events)), Unresolved);
  }

  // DONE (e): and across two sessions through -a's hashtable.txt, which each
  // session writes after every WSPR period as wsprd writes it.
  void typeThreeResolvesAcrossSessions ()
  {
    auto const callers = callersPeriod ();
    auto const hashed = hashedPeriod ();
    QVERIFY2 (!callers.pcm.isEmpty () && !hashed.pcm.isEmpty (), "no render");
    QTemporaryDir codecDir, wsprdDir;
    QVERIFY (codecDir.isValid () && wsprdDir.isValid ());
    auto const first = wsprdOn (callers.pcm, 3, wsprdDir.path (), callers.date, callers.hhmm);
    QVERIFY2 (first.ok, qPrintable (first.failure));
    auto const one = runCodec (sessionHeader () + control (with (wsprKeys (3, false), label (callers)))
                               + audio (callers.pcm) + halt (), codecDir.path ());
    QVERIFY2 (one.clean (), qPrintable (one.report ()));
    QVERIFY2 (!first.hash.isEmpty (), "wsprd's table is empty");
    QCOMPARE (readFile (codecDir.filePath ("hashtable.txt")), first.hash);
    auto const second = wsprdOn (hashed.pcm, 3, wsprdDir.path (), hashed.date, hashed.hhmm);
    QVERIFY2 (second.ok, qPrintable (second.failure));
    auto const two = runCodec (sessionHeader () + control (with (wsprKeys (3, false), label (hashed)))
                               + audio (hashed.pcm) + halt (), codecDir.path ());
    QVERIFY2 (two.clean (), qPrintable (two.report ()));
    auto const difference = compare (two, second, hashed, Stop, false);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    QCOMPARE (messages (wsprDecodes (two.events)), Resolved);
    QCOMPARE (readFile (codecDir.filePath ("hashtable.txt")), second.hash);
  }

  // DONE (e): wspr_hash_clear true empties the session's table, as the GUI's
  // Erase WSPR hashtable, and deletes <-a>/hashtable.txt, at its place in the
  // stream: the next period's type 3 calls stay <...>, as wsprd's from a
  // fresh -a, which leaves an empty table too. false changes nothing.
  void hashClear_data ()
  {
    QTest::addColumn<QString> ("row");
    QTest::newRow ("between periods") << QString {"between periods"};
    QTest::newRow ("false keeps it") << QString {"false keeps it"};
    QTest::newRow ("deletes the file") << QString {"deletes the file"};
    QTest::newRow ("before the first decode") << QString {"before the first decode"};
  }

  void hashClear ()
  {
    QFETCH (QString, row);
    auto const callers = callersPeriod ();
    auto const hashed = hashedPeriod ();
    QVERIFY2 (!callers.pcm.isEmpty () && !hashed.pcm.isEmpty (), "no render");
    QTemporaryDir codecDir;
    QVERIFY (codecDir.isValid ());
    auto const table = codecDir.filePath ("hashtable.txt");
    auto const fresh = wsprdFresh (hashed, 2);
    QVERIFY2 (fresh.ok, qPrintable (fresh.failure));
    if ("before the first decode" == row)
      {
        // A table a previous session left.
        auto const seed = wsprdFresh (callers, 2);
        QVERIFY2 (seed.ok && !seed.hash.isEmpty (), qPrintable (seed.failure));
        QVERIFY (writeFile (table, seed.hash));
        auto const codec = runCodec (sessionHeader () + control (with (with (wsprKeys (2, true), label (hashed)),
                                                                       {{"wspr_hash_clear", true}}))
                                     + audio (hashed.pcm) + halt (), codecDir.path ());
        QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
        auto const difference = compare (codec, fresh, hashed, Stop, false);
        QVERIFY2 (difference.isEmpty (), qPrintable (difference));
        QCOMPARE (messages (wsprDecodes (codec.events)), Unresolved);
        QCOMPARE (readFile (table), fresh.hash);
        return;
      }
    QByteArray input = sessionHeader () + control (with (wsprKeys (2, true), label (callers))) + audio (callers.pcm);
    if ("deletes the file" == row)
      {
        auto const kept = runCodec (input + halt (), codecDir.path ());
        QVERIFY2 (kept.clean (), qPrintable (kept.report ()));
        QVERIFY2 (QFileInfo::exists (table) && !readFile (table).isEmpty (), "the first period left no table");
        QTemporaryDir other;
        QVERIFY (other.isValid ());
        auto const cleared = runCodec (input + control ({{"wspr_hash_clear", true}}) + halt (), other.path ());
        QVERIFY2 (cleared.clean (), qPrintable (cleared.report ()));
        QCOMPARE (messages (wsprDecodes (cleared.events)), messages (wsprDecodes (kept.events)));
        QVERIFY2 (!QFileInfo::exists (other.filePath ("hashtable.txt")), "hashtable.txt is still there");
        return;
      }
    auto const clear = "between periods" == row;
    input += control (with (label (hashed), {{"wspr_hash_clear", clear}})) + audio (hashed.pcm) + halt ();
    auto const codec = runCodec (input, codecDir.path ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (ofType (codec.events, "error").isEmpty (), qPrintable (codec.report ()));
    if (clear)
      {
        auto const difference = compare (codec, fresh, hashed, Stop, false);
        QVERIFY2 (difference.isEmpty (), qPrintable (difference));
        QCOMPARE (messages (wsprDecodes (codec.events, "120200")), Unresolved);
        QCOMPARE (readFile (table), fresh.hash);
      }
    else QCOMPARE (messages (wsprDecodes (codec.events, "120200")), Resolved);
  }

  // WSPR takes a trperiod of 120 s only, its default (configure_range_error,
  // key trperiod), by name only, case aside.
  void periodRules_data ()
  {
    QTest::addColumn<QByteArray> ("frames");
    QTest::addColumn<QString> ("error");      // its code, or empty
    QTest::addColumn<bool> ("decodes");
    auto const keys = [] (QJsonObject const& more) {return control (with ({{"utc", "09:18:00"}, {"depth", 2}}, more));};
    QTest::newRow ("trperiod 60") << keys ({{"mode", "WSPR"}, {"trperiod", 60}}) << QString {"configure_range_error"}
                                  << false;
    QTest::newRow ("trperiod 30 later") << keys ({{"mode", "WSPR"}}) + control ({{"trperiod", 30}})
                                        << QString {"configure_range_error"} << true;
    QTest::newRow ("trperiod 120") << keys ({{"mode", "WSPR"}, {"trperiod", 120}}) << QString {} << true;
    QTest::newRow ("no trperiod") << keys ({{"mode", "WSPR"}}) << QString {} << true;
    QTest::newRow ("lower case") << keys ({{"mode", "wspr"}}) << QString {} << true;
    QTest::newRow ("integer code") << keys ({{"mode", 1001}}) << QString {"unknown_mode"} << false;
  }

  void periodRules ()
  {
    QFETCH (QByteArray, frames);
    QFETCH (QString, error);
    QFETCH (bool, decodes);
    QString failure;
    auto const pcm = audioCase ("golden", &failure).pcm;
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    auto const codec = runCodec (sessionHeader () + frames + audio (pcm) + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const errors = ofType (codec.events, "error");
    if (error.isEmpty ()) QVERIFY2 (errors.isEmpty (), qPrintable (codec.report ()));
    else
      {
        QCOMPARE (errors.size (), 1);
        QCOMPARE (errors.first ().value ("code").toString (), error);
        if ("configure_range_error" == error)
          {
            QCOMPARE (errors.first ().value ("key").toString (), QString {"trperiod"});
            QCOMPARE (errors.first ().value ("mode").toString (), QString {"WSPR"});
            QCOMPARE (errors.first ().value ("detail").toString (), QString {"trperiod must be 120 s in WSPR"});
          }
      }
    auto const wspr = wsprDecodes (codec.events);
    QCOMPARE (!wspr.isEmpty (), decodes);
    for (auto const& e : wspr) QCOMPARE (e.value ("pass").toInt (), Stop);
  }

  // WSPR keeps no period to decode again, as the GUI's Decode button does
  // nothing in WSPR: nagain_flag is refused and decodes nothing more.
  void decodeAgainIsRefused ()
  {
    QString failure;
    auto const p = audioCase ("golden", &failure);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    auto const codec = runCodec (sessionHeader () + control (with (wsprKeys (1, true), label (p))) + audio (p.pcm)
                                 + control ({{"nagain_flag", true}}) + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const errors = ofType (codec.events, "error");
    QCOMPARE (errors.size (), 1);
    QCOMPARE (errors.first ().value ("code").toString (), QString {"decode_again_unavailable"});
    QCOMPARE (errors.first ().value ("detail").toString (), QString {NoDecodedPeriod});
    auto const w = wsprdFresh (p, 1);
    auto const difference = compare (codec, w, p, Stop, false);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  void readyListsWspr ()
  {
    auto const codec = runCodec (sessionHeader () + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY (!codec.events.isEmpty ());
    QCOMPARE (codec.events.first ().value ("t").toString (), QString {"ready"});
    QVERIFY (codec.events.first ().value ("modes").toArray ().contains ("WSPR"));
  }

  // A frame longer than the sample buffer ends a WSPR session, its period
  // in flight undecoded, as it ends any (frame_too_large, exit status 1).
  void oversizedFrameEndsTheSession_data ()
  {
    QTest::addColumn<quint32> ("declaredLength");
    QTest::newRow ("over capacity") << SampleBufferBytes + 2u;
    QTest::newRow ("uint32 max") << std::numeric_limits<quint32>::max ();
  }

  void oversizedFrameEndsTheSession ()
  {
    QFETCH (quint32, declaredLength);
    QByteArray oversized (1, char (0x01u));
    for (int shift = 0; shift < 32; shift += 8) oversized.append (char ((declaredLength >> shift) & 0xffu));
    auto const silence = QByteArray (2 * 10 * SampleRate, '\0');
    auto const codec = runCodec (sessionHeader () + control (wsprKeys (2, false)) + audio (silence) + oversized);
    QVERIFY2 (codec.started && !codec.timedOut && 1 == codec.exitCode, qPrintable (codec.report ()));
    auto const errors = ofType (codec.events, "error");
    QCOMPARE (errors.size (), 1);
    QCOMPARE (errors.first ().value ("code").toString (), QString {"frame_too_large"});
    QVERIFY (ofType (codec.events, "decode").isEmpty ());
    QVERIFY (ofType (codec.events, "decode_finished").isEmpty ());
  }

  // A control frame is a halt by its "t" alone: a WSPR configure whose
  // mycall is "halt" configures, and the session reads on (here a trperiod
  // WSPR refuses).
  void haltAsAValueConfigures ()
  {
    auto const configure = frame (0x02u, R"({"t":"configure","mode":"WSPR","mycall":"halt","mygrid":"FN20"})");
    auto const codec = runCodec (sessionHeader () + configure + control ({{"trperiod", 60}}) + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const errors = ofType (codec.events, "error");
    QCOMPARE (errors.size (), 1);
    QCOMPARE (errors.first ().value ("code").toString (), QString {"configure_range_error"});
    QCOMPARE (errors.first ().value ("mode").toString (), QString {"WSPR"});
    QCOMPARE (errors.first ().value ("key").toString (), QString {"trperiod"});
  }

  // -2 with no file starts the session in WSPR, as -8 starts one in FT8:
  // with no configure, a period decodes as WSPR at the command line's depth.
  void commandLineStartsWspr ()
  {
    QString failure;
    auto const p = audioCase ("golden", &failure);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    auto const codec = runCodec (sessionHeader () + audio (p.pcm) + halt (), {}, {}, {"-2", "-d", "3"});
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const w = wsprdFresh (p, 3);
    QVERIFY2 (w.ok, qPrintable (w.failure));
    // wsprd's "%4s %3.0f %4.1f %10.6f %2d  %-s \n": the message from column 29.
    QStringList expected;
    for (auto const& line : QString::fromUtf8 (w.out).split ('\n', Qt::SkipEmptyParts))
      if (!line.startsWith ("<DecodeFinished>")) expected << line.mid (29).trimmed ();
    auto const decodes = wsprDecodes (codec.events);
    QVERIFY (!expected.isEmpty ());
    QCOMPARE (messages (decodes), expected);
    for (auto const& d : decodes) QCOMPARE (d.value ("pass").toInt (), Stop);
    auto const finished = ofType (codec.events, "decode_finished");
    QCOMPARE (finished.size (), 1);
    QCOMPARE (finished.first ().value ("decoded").toInt (), int (decodes.size ()));
  }

  // With busy_events, the decode on the decode thread says busy first, with
  // its mode and stop, and busy false after its decode_finished; a hold of
  // the pass at 396 (WSJT_CODEC_TEST_HOLD_PASS) holds the WSPR decode.
  void busyLinesFrameTheDecode ()
  {
    QString failure;
    auto const p = audioCase ("golden", &failure);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    auto const codec = codecOn (p, with (wsprKeys (2, true), {{"busy_events", true}}),
                                {"WSJT_CODEC_TEST_HOLD_PASS=396:500"});
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (codec.err.contains ("hold done nzhsym=396"), codec.err.constData ());
    QStringList order;
    for (auto const& e : codec.events)
      {
        auto const type = e.value ("t").toString ();
        if ("busy" == type)
          {
            if (e.value ("busy").toBool ())
              {
                QCOMPARE (e.value ("mode").toString (), QString {"WSPR"});
                QCOMPARE (e.value ("nzhsym").toInt (), Stop);
                QCOMPARE (e.value ("period_end").toString (), QString {"091800"});
              }
            order << (e.value ("busy").toBool () ? "busy" : "idle");
          }
        else if ("decode" == type && (order.isEmpty () || order.last () != "decode")) order << "decode";
        else if ("decode_finished" == type) order << "finished";
      }
    QCOMPARE (order, (QStringList {"busy", "decode", "finished", "idle"}));
  }

  // A period cut short (here a flush 100 s in) decodes what it has, as the
  // codec's other period modes do, and as wsprd decodes a short WAV file,
  // zeros after it. Its pass is the half-symbols the stream counted, which
  // stop at 181 as for every period mode but FT8.
  void cutShortPeriodDecodesWhatItHas ()
  {
    QString failure;
    auto const p = audioCase ("mix", &failure);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    auto const cut = 100 * SampleRate;
    auto const codec = runCodec (sessionHeader () + control (with (wsprKeys (2, true), label (p))) + audio (p.pcm, 0, cut)
                                 + flush () + halt ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    Period shortened = p;
    shortened.pcm = p.pcm.left (2 * cut);
    auto const w = wsprdFresh (shortened, 2);
    auto const difference = compare (codec, w, p, 181, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    QVERIFY2 (!wsprDecodes (codec.events).isEmpty (), qPrintable (codec.report ()));
  }

  // Plan §9: a client that reads only the fields every decode has, and
  // ignores the rest, as both current clients do, gets from each WSPR decode
  // a whole decode of the usual types, and reads around a WSPR interlude what
  // it reads without one.
  void clientsIgnoringWsprFieldsStillWork ()
  {
    QString failure;
    auto const p = audioCase ("mix", &failure);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    auto const ft8Samples = 15 * SampleRate;
    auto const signal = [] {
      auto const run = runCodec (sessionHeader () + control ({{"id", 1}, {"mode", "FT8"}, {"text", "CQ K1ABC FN42"},
                                                               {"freq", 1200}, {"rate", SampleRate}}, "render"));
      QByteArray pcm;
      for (auto const& event : ofType (run.events, "pcm"))
        pcm += QByteArray::fromBase64 (event.value ("data").toString ().toLatin1 ());
      return pcm;
    } ();
    QVERIFY2 (!signal.isEmpty (), "no FT8 render");
    QByteArray ft8 (2 * ft8Samples, '\0');
    std::mt19937 random {5u};
    std::normal_distribution<double> noise {0.0, 1500.0};
    for (int k = 0; k < ft8Samples; ++k)
      {
        auto value = noise (random);
        auto const i = k - SampleRate / 2;
        if (i >= 0 && 2 * i + 1 < signal.size ()) value += 0.2 * qFromLittleEndian<qint16> (signal.constData () + 2 * i);
        qToLittleEndian<qint16> (qint16 (std::max (-32767.0, std::min (32767.0, std::round (value)))), ft8.data () + 2 * k);
      }
    QJsonObject const ft8Keys {{"mode", "FT8"}, {"trperiod", 15}, {"rxfreq", 1500}, {"ntol", 1000}, {"depth", 2}};
    auto const session = [&] (bool interlude) {
      QByteArray input = sessionHeader () + control (with (ft8Keys, {{"utc", "12:00:00"}})) + audio (ft8);
      if (interlude) input += control (with (wsprKeys (2, false), label (p))) + audio (p.pcm);
      return runCodec (input + control (with (ft8Keys, {{"utc", "12:04:00"}})) + audio (ft8) + halt ());
    };
    auto const interlude = session (true);
    QVERIFY2 (interlude.clean (), qPrintable (interlude.report ()));
    auto const without = session (false);
    QVERIFY2 (without.clean (), qPrintable (without.report ()));
    auto const wspr = wsprDecodes (interlude.events);
    QVERIFY2 (!wspr.isEmpty (), qPrintable (interlude.report ()));
    for (auto const& e : wspr)
      {
        auto const show = QJsonDocument {e}.toJson (QJsonDocument::Compact);
        QVERIFY2 (e.value ("v").toInt () == 1 && e.value ("mode").isString () && e.value ("message").isString ()
                  && e.value ("time").toString ().size () == 6 && e.value ("dt").isDouble (), show.constData ());
        for (auto const* key : {"snr", "freq", "pass"})
          QVERIFY2 (e.value (key).isDouble () && e.value (key).toDouble () == e.value (key).toInt (), show.constData ());
      }
    // What the client keeps: every line it knows, decodes by their usual
    // fields alone, less the WSPR period's.
    auto const known = [] (Run const& run) {
      static QSet<QString> const types {"ready", "decode", "decode_finished", "error", "pass_skipped"};
      QStringList lines;
      for (auto const& line : run.out.split ('\n'))
        {
          if (line.trimmed ().isEmpty ()) continue;
          auto event = QJsonDocument::fromJson (line).object ();
          if (event.isEmpty () || event.value ("v").toInt () != 1) return QStringList {"not a v1 object: " + line};
          if (!types.contains (event.value ("t").toString ())) continue;
          if (event.value ("mode").toString () == "WSPR" || event.value ("period_end").toString () == "001400") continue;
          for (auto const& key : WsprKeys) event.remove (key);
          lines << QString::fromUtf8 (QJsonDocument {event}.toJson (QJsonDocument::Compact));
        }
      return lines;
    };
    QVERIFY2 (!ofType (without.events, "decode").isEmpty (), qPrintable (without.report ()));
    QCOMPARE (known (interlude), known (without));
  }

  // Equalized: with ref_spectrum "apply" on live audio, the codec decodes
  // what wsprd decodes from the GUI's equalized audio (spectrum_gui_model:
  // refspectrum_ as dataSink drives it, the samples the GUI saves to WSPR's
  // WAV file), which the filter changed.
  void equalizedDecodesMatchWsprd ()
  {
    QTemporaryDir modelDir, codecDir;
    QVERIFY (modelDir.isValid () && codecDir.isValid ());
    QString failure;
    auto const measured = runModel (colouredNoise (15 * SampleRate, 99u),
                                    {"datadir " + modelDir.path (), "reference measure",
                                     QString {"period live 0 %1"}.arg (15 * SampleRate)}, modelDir.path (), &failure);
    QVERIFY2 (!measured.isEmpty (), qPrintable (failure));
    auto const reference = readFile (modelDir.filePath ("refspec.dat"));
    QVERIFY2 (!reference.isEmpty (), "no reference spectrum");
    QList<Station> const stations {{"K1ABC FN42 37", 1450, -18, 1.0}, {"PJ4/W9XYZ 23", 1520, -21, 1.5},
                                   {"W5XYZ EM10 33", 1570, -24, 1.2}};
    auto const signal = mixed (stations, 31u);
    QVERIFY2 (!signal.isEmpty (), "no render");
    // Coloured noise over the white: the reference flattens it.
    QByteArray pcm (signal.size (), '\0');
    auto const coloured = colouredNoise (PeriodSamples, 13u);
    for (int k = 0; k < PeriodSamples; ++k)
      {
        auto const sum = int (qFromLittleEndian<qint16> (signal.constData () + 2 * k))
          + int (qFromLittleEndian<qint16> (coloured.constData () + 2 * k));
        qToLittleEndian<qint16> (qint16 (std::max (-32767, std::min (32767, sum))), pcm.data () + 2 * k);
      }
    auto const out = modelDir.filePath ("equalized.pcm");
    auto const output = runModel (pcm, {"datadir " + modelDir.path (), "reference apply", "equalized " + out,
                                        QString {"period live 0 %1"}.arg (PeriodSamples)}, modelDir.path (), &failure);
    QVERIFY2 (!output.isEmpty (), qPrintable (failure));
    auto const equalized = readFile (out);
    QCOMPARE (equalized.size (), pcm.size ());
    QVERIFY2 (equalized != pcm, "the filter changed nothing");
    QVERIFY (writeFile (codecDir.filePath ("refspec.dat"), reference));
    Period p {pcm, Date, "0030", {}};
    auto const codec = runCodec (sessionHeader () + control (with (with (wsprKeys (2, true), label (p)),
                                                                   {{"ref_spectrum", "apply"}}))
                                 + audio (pcm) + halt (), codecDir.path ());
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (ofType (codec.events, "error").isEmpty (), qPrintable (codec.report ()));
    Period guiSaved {equalized, Date, "0030", {}};
    auto const w = wsprdFresh (guiSaved, 2);
    auto const difference = compare (codec, w, p, Stop, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    QVERIFY2 (!wsprDecodes (codec.events).isEmpty (), qPrintable (codec.report ()));
    auto const plain = wsprdFresh (p, 2);
    QVERIFY2 (plain.ok, qPrintable (plain.failure));
    QVERIFY2 (plain.all != w.all, "equalization left wsprd's decodes as they were");
  }

  // Plan §8: in a -a whose jt9_wisdom.dat a previous codec session exported
  // (here one that decoded WSPR, with FFTW's wisdom for WSPR's own problems,
  // and JTTY, whose plans are measured), the WSPR decodes still equal wsprd's
  // from a fresh -a, at each depth, on audio whose depths decode differently.
  // Parity is claimed for fresh sessions; this row shows only that this
  // wisdom changes nothing here.
  void populatedWisdom ()
  {
    QString failure;
    auto const p = audioCase ("close", &failure);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    QTemporaryDir earlier, directory;
    QVERIFY (earlier.isValid () && directory.isValid ());
    QByteArray jtty (2 * 3 * SampleRate, '\0');
    auto const previous = runCodec (sessionHeader () + control (with (wsprKeys (3, false), label (p))) + audio (p.pcm)
                                    + control ({{"mode", "JTTY"}, {"rxfreq", 1500}}) + audio (jtty) + halt (),
                                    earlier.path ());
    QVERIFY2 (previous.clean (), qPrintable (previous.report ()));
    auto const wisdom = readFile (earlier.filePath ("jt9_wisdom.dat"));
    QVERIFY2 (wisdom.contains ("fftw"), "no wisdom exported");
    QVERIFY (writeFile (directory.filePath ("jt9_wisdom.dat"), wisdom));
    for (int depth = 1; depth <= 3; ++depth)
      {
        auto const codec = runCodec (sessionHeader () + control (with (wsprKeys (depth, true), label (p))) + audio (p.pcm)
                                     + halt (), directory.path ());
        QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
        QFile::remove (directory.filePath ("hashtable.txt"));
        auto const w = wsprdFresh (p, depth);
        auto const difference = compare (codec, w, p, Stop, true);
        QVERIFY2 (difference.isEmpty (), qPrintable (QString {"depth %1: %2"}.arg (depth).arg (difference)));
      }
  }
};

QTEST_GUILESS_MAIN (TestStreamingWspr)

#include "test_streaming_wspr.moc"
