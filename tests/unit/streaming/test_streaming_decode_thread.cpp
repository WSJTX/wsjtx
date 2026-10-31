// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QTime>
#include <QtEndian>
#include <QtTest>

#include "Ft8MtdDecodeScheduler.hpp"

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace
{
using Stage = Ft8MtdDecodeScheduler::Stage;
using Action = Ft8MtdDecodeScheduler::Action;

constexpr int SampleRate = 12000;
constexpr int HsymSamples = 3456;
constexpr int Ft8PeriodSamples = 15 * SampleRate;
constexpr int Ft4PeriodSamples = 90000;
// A paced client's frame, 0.1 s of audio.
constexpr int FrameSamples = 1200;
// MSK144's block and its interval, rounded up.
constexpr int BlockSamples = 3584;
constexpr qint64 BlockIntervalMs = (1000 * BlockSamples + SampleRate - 1) / SampleRate;
// Late audio is due this long before it is sent.
constexpr int DueMarginMs = 500;
// ft8-engine.wav's one signal.
constexpr char EngineMessage[] = "CQ K1JT FN20";
// The cap on audio queued for the decode thread, and the stdin buffer's.
constexpr qint64 QueueCapBytes = qint64 {64} << 20;
constexpr qint64 StdinCapBytes = qint64 {16} << 20;

// One wait covers a decode or two; sanitizer builds raise QtTest's limit and this with it.
int waitMs ()
{
  return std::max (60000, qEnvironmentVariableIntValue ("QTEST_FUNCTION_TIMEOUT") / 6);
}

// How long a row holds a pass so that its requests land inside it.
int holdMs ()
{
  return waitMs () / 12;
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

QByteArray control (QJsonObject const& keys)
{
  return frame (0x02u, QJsonDocument {keys}.toJson (QJsonDocument::Compact));
}

QByteArray configure (QJsonObject keys)
{
  keys.insert ("t", "configure");
  return control (keys);
}

// A control frame without "t": its configure_parse_error shows the stream has
// read everything before it.
QByteArray marker ()
{
  return frame (0x02u, "{}");
}

QByteArray halt ()
{
  return control ({{"t", "halt"}});
}

QJsonObject with (QJsonObject keys, QJsonObject const& more)
{
  for (auto it = more.constBegin (); it != more.constEnd (); ++it) keys.insert (it.key (), it.value ());
  return keys;
}

QByteArray audio (QByteArray const& pcm, int frameSamples = 4096)
{
  QByteArray bytes;
  for (int at = 0; at < pcm.size (); at += 2 * frameSamples) bytes += frame (0x01u, pcm.mid (at, 2 * frameSamples));
  return bytes;
}

QByteArray samples (QByteArray const& pcm, int from, int to)
{
  return pcm.mid (2 * from, 2 * (to - from));
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

QString fixture (QString const& name)
{
  auto const directory = qEnvironmentVariable ("WSJT_ENGINE_FIXTURES");
  return directory.isEmpty () ? QString {} : directory + '/' + name;
}

// A recording cut, or padded with silence, to one period.
QByteArray period (QByteArray pcm, int count)
{
  if (pcm.isEmpty ()) return pcm;
  pcm = pcm.left (2 * count);
  pcm.append (QByteArray (2 * count - pcm.size (), '\0'));
  return pcm;
}

QString utcOf (int p, QTime first = {13, 34, 30}, int seconds = 15)
{
  return first.addSecs (p * seconds).toString ("HH:mm:ss");
}

QString labelOf (QString utc)
{
  return utc.remove (':');
}

// A mode's recording and settings, as the codec's measurements and the
// stream's other rows decode them, depth 3.
struct Mode
{
  QString wav;
  QJsonObject settings;
  int samples {0};       // its period
  QString name;          // its decodes' mode
  double trperiod {15};
  bool sorted {false};   // its decodes come in thread order
};

Mode modeOf (QString const& name)
{
  QJsonObject const ft8 {{"mode", "FT8"}, {"utc", "13:34:30"}, {"depth", 3}};
  QJsonObject const mtd {{"multithreaded_ft8", true}, {"ft8_threads", 2}, {"ft8_decoder_start", 0}};
  auto const ft8Sample = inSamples ("FT8/210703_133430.wav");
  auto const fst4Sample = inSamples ("FST4+FST4W/210115_0058.wav");
  QJsonObject const fst4 {{"mode", "FST4"}, {"trperiod", 60}, {"rxfreq", 1331}, {"nutc", 5800}, {"depth", 3}};
  if ("ft8" == name) return {ft8Sample, ft8, Ft8PeriodSamples, "FT8", 15, false};
  if ("ft8-mtd" == name) return {ft8Sample, with (ft8, mtd), Ft8PeriodSamples, "FT8", 15, true};
  if ("ft4" == name)
    return {inSamples ("FT4/000000_000002.wav"), {{"mode", "FT4"}, {"utc", "00:00:00"}, {"depth", 3}},
            Ft4PeriodSamples, "FT4", 7.5, false};
  if ("fst4" == name)
    return {fixture ("fst4-engine.wav"), {{"mode", "FST4"}, {"trperiod", 15}, {"depth", 3}, {"nfa", 1300},
                                           {"nfb", 1700}, {"rxfreq", 1500}, {"utc", "00:15:00"}},
            15 * SampleRate, "FST4", 15, false};
  if ("q65" == name)
    return {fixture ("q65-engine.wav"), {{"mode", "Q65"}, {"trperiod", 15}, {"submode", 0}, {"depth", 3},
                                          {"nfa", 1300}, {"nfb", 1700}, {"rxfreq", 1500}, {"utc", "00:15:00"}},
            15 * SampleRate, "Q65", 15, false};
  if ("jt4" == name)
    return {fixture ("jt4-engine.wav"), {{"mode", "JT4"}, {"trperiod", 60}, {"submode", 0}, {"depth", 3},
                                          {"rxfreq", 1000}, {"ntol", 50}, {"utc", "12:34:00"}},
            60 * SampleRate, "JT4", 60, false};
  if ("jt9" == name)
    return {inSamples ("JT9/130418_1742.wav"), {{"mode", "JT9"}, {"trperiod", 60}, {"nutc", 1742}, {"depth", 3}},
            60 * SampleRate, "JT9", 60, false};
  if ("jt65" == name)
    return {inSamples ("JT65/JT65B/000000_0004.wav"), {{"mode", "JT65"}, {"trperiod", 60}, {"submode", 1},
                                                        {"rxfreq", 1700}, {"nutc", 4}, {"depth", 3}},
            60 * SampleRate, "JT65", 60, false};
  if ("jt65jt9" == name)
    return {inSamples ("JT9/130418_1742.wav"), {{"mode", "JT65+JT9"}, {"trperiod", 60}, {"nutc", 1742},
                                                 {"jt65_jt9_split_hz", 1000}, {"tx_mode", "JT9"}, {"depth", 3}},
            60 * SampleRate, "JT65+JT9", 60, false};
  if ("fst4-60" == name)
    return {fst4Sample, with (fst4, {{"nfa", 1000}, {"nfb", 1400}}), 60 * SampleRate, "FST4", 60, false};
  if ("fst4-60-wide" == name)
    return {fst4Sample, with (fst4, {{"nfa", 200}, {"nfb", 4000}}), 60 * SampleRate, "FST4", 60, false};
  if ("fst4w-1800" == name)
    return {inSamples ("FST4+FST4W/201230_0300.wav"), {{"mode", "FST4W"}, {"trperiod", 1800}, {"rxfreq", 1433},
                                                        {"ntol", 100}, {"nutc", 30000}, {"depth", 3}},
            1800 * SampleRate, "FST4W", 1800, false};
  if ("q65-300a" == name)
    return {inSamples ("Q65/300A_Optical_Scatter/201210_0505.wav"), {{"mode", "Q65"}, {"trperiod", 300},
                                                                      {"submode", 0}, {"rxfreq", 1000},
                                                                      {"nutc", 50500}, {"depth", 3}},
            300 * SampleRate, "Q65", 300, false};
  return {};
}

QJsonObject const Msk144Settings {{"mode", "MSK144"}, {"trperiod", 15}, {"rxfreq", 1500}, {"ntol", 50},
                                  {"depth", 3}, {"utc", "12:08:00"}};
QJsonObject const JttySettings {{"mode", "JTTY"}, {"rxfreq", 1500}, {"ntol", 50}, {"nfa", 200}, {"nfb", 2800}};

// --- what the codec writes ----------------------------------------------------

struct Line
{
  qint64 ms;         // when the test read it
  bool error;        // stderr
  QByteArray text;   // without its line end; a dropped pcm line keeps what precedes its data, a hashed one
                     // that and its data's SHA-1
};

using Match = std::function<bool (Line const&)>;

bool isPcm (QByteArray const& text)
{
  return text.startsWith ("{\"v\":1,\"t\":\"pcm\"");
}

QByteArray typeOf (QByteArray const& text)
{
  auto at = text.indexOf ("\"t\":\"");
  if (at < 0) return {};
  at += 5;
  return text.mid (at, text.indexOf ('"', at) - at);
}

QJsonObject json (QByteArray const& text)
{
  return QJsonDocument::fromJson (text).object ();
}

Match outType (QByteArray const& type)
{
  return [type] (Line const& line) {return !line.error && typeOf (line.text) == type;};
}

Match outHas (QByteArray const& text)
{
  return [text] (Line const& line) {return !line.error && line.text.contains (text);};
}

Match errHas (QByteArray const& text)
{
  return [text] (Line const& line) {return line.error && line.text.contains (text);};
}

Match passLine (int nzhsym, bool multithreaded = false)
{
  auto const head = QByteArray {"pass nzhsym="} + QByteArray::number (nzhsym) + ' ';
  return [head, multithreaded] (Line const& line) {
    return line.error && line.text.contains (head) && (!multithreaded || line.text.contains ("multithreaded=T"));
  };
}

// WSJT_CODEC_TEST_HOLD_PASS's line when its hold of the pass at nzhsym ends.
Match holdDoneLine (int nzhsym)
{
  auto const text = QByteArray {"wsjt-codec: hold done nzhsym="} + QByteArray::number (nzhsym);
  return [text] (Line const& line) {return line.error && line.text.trimmed () == text;};
}

Match busyLine (bool busy)
{
  return [busy] (Line const& line) {
    return !line.error && "busy" == typeOf (line.text) && json (line.text).value ("busy").toBool () == busy;
  };
}

int find (QList<Line> const& lines, Match const& match, int from = 0)
{
  for (int i = std::max (0, from); i < lines.size (); ++i)
    if (match (lines.at (i))) return i;
  return -1;
}

int findLast (QList<Line> const& lines, Match const& match)
{
  for (int i = lines.size () - 1; i >= 0; --i)
    if (match (lines.at (i))) return i;
  return -1;
}

qint64 msOf (QList<Line> const& lines, int index)
{
  return index >= 0 && index < lines.size () ? lines.at (index).ms : -1;
}

int count (QList<Line> const& lines, Match const& match)
{
  return int (std::count_if (lines.begin (), lines.end (), match));
}

// Matches each line once however often it is polled, so that a client's
// cost per line stays the same as its lines pile up.
class Tally
{
public:
  explicit Tally (Match match, int from = 0)
    : match_ {std::move (match)}
    , next_ {from}
  {
  }

  int operator() (QList<Line> const& lines)
  {
    for (; next_ < lines.size (); ++next_)
      {
        if (!match_ (lines.at (next_))) continue;
        if (first_ < 0) first_ = next_;
        ++count_;
      }
    return count_;
  }

  int first () const {return first_;}

private:
  Match match_;
  int next_;
  int count_ {0};
  int first_ {-1};
};

bool isDecision (Line const& line)
{
  return line.error && (line.text.contains ("wsjt-codec: pass nzhsym=") || line.text.contains ("wsjt-codec: skip nzhsym=")
                        || line.text.contains ("wsjt-codec: defer nzhsym="));
}

// The stream's decisions on stderr: "pass 41 F", "pass 50 F again", "skip 46 early busy", "defer 50".
QStringList decisions (QList<Line> const& lines)
{
  static QRegularExpression const line {
    "wsjt-codec: (?:pass nzhsym=(\\d+) samples=\\d+ depth=\\d+ cycles=\\d+ multithreaded=([TF]) live=[TF] "
    "again=([TF]) emedelay=-?[0-9.]+|skip nzhsym=(\\d+) stage=(\\w+) reason=(\\w+)|defer nzhsym=(\\d+))$"};
  QStringList found;
  for (auto const& each : lines)
    {
      if (!each.error) continue;
      auto const m = line.match (QString::fromUtf8 (each.text));
      if (!m.hasMatch ()) continue;
      if (m.capturedLength (1) > 0)
        found << QString {"pass %1 %2%3"}.arg (m.captured (1), m.captured (2),
                                              "T" == m.captured (3) ? QString {" again"} : QString {});
      else if (m.capturedLength (4) > 0) found << QString {"skip %1 %2 %3"}.arg (m.captured (4), m.captured (5), m.captured (6));
      else found << "defer " + m.captured (7);
    }
  return found;
}

// The lines a decode writes, each pass's decodes as a multiset when they
// come in thread order, every other line as written.
QByteArrayList decodeLines (QList<Line> const& lines, bool sorted = false)
{
  QByteArrayList found;
  QByteArrayList decodes;
  int pass = -1;
  auto const flush = [&] {
    if (sorted) std::sort (decodes.begin (), decodes.end ());
    found << decodes;
    decodes.clear ();
  };
  for (auto const& line : lines)
    {
      if (line.error) continue;
      auto const type = typeOf (line.text);
      if ("decode" == type)
        {
          auto const of = json (line.text).value ("pass").toInt (-1);
          if (of != pass) flush ();
          pass = of;
          decodes << line.text;
        }
      else if ("decode_finished" == type || "pass_skipped" == type || "msk144_trained" == type
               || ("error" == type && (line.text.contains ("\"decode_rejected\"")
                                       || line.text.contains ("\"decode_again_unavailable\""))))
        {
          flush ();
          pass = -1;
          found << line.text;
        }
    }
  flush ();
  return found;
}

QStringList periodEnds (QList<Line> const& lines)
{
  QStringList found;
  for (auto const& line : lines)
    if (outType ("decode_finished") (line)) found << json (line.text).value ("period_end").toString ();
  return found;
}

QStringList skips (QList<Line> const& lines)
{
  QStringList found;
  for (auto const& line : lines)
    {
      if (!outType ("pass_skipped") (line)) continue;
      auto const event = json (line.text);
      found << QString {"%1 %2 %3 %4"}.arg (event.value ("period_end").toString ())
        .arg (event.value ("nzhsym").toInt ()).arg (event.value ("stage").toString (), event.value ("reason").toString ());
    }
  return found;
}

QStringList messagesOf (QByteArrayList const& lines)
{
  QStringList found;
  for (auto const& line : lines)
    if ("decode" == typeOf (line)) found << json (line).value ("message").toString ().trimmed ();
  return found;
}

// The half-symbol of the last pass a session ran, or 0.
int lastPass (QList<Line> const& lines)
{
  static QRegularExpression const pass {"wsjt-codec: pass nzhsym=(\\d+) "};
  int found = 0;
  for (auto const& line : lines)
    {
      if (!line.error) continue;
      auto const m = pass.match (QString::fromUtf8 (line.text));
      if (m.hasMatch ()) found = m.captured (1).toInt ();
    }
  return found;
}

// A process's resident memory in bytes, or -1.
qint64 residentBytes (qint64 pid)
{
#ifdef Q_OS_UNIX
  QFile statm {QString {"/proc/%1/statm"}.arg (pid)};
  if (statm.open (QIODevice::ReadOnly))
    {
      auto const fields = statm.readAll ().split (' ');
      auto const page = sysconf (_SC_PAGESIZE);
      if (fields.size () > 1 && page > 0) return fields.at (1).toLongLong () * page;
    }
#endif
  QProcess ps;
  ps.start ("ps", {"-o", "rss=", "-p", QString::number (pid)});
  if (!ps.waitForFinished (10000) || 0 != ps.exitCode ()) return -1;
  bool ok = false;
  auto const kib = ps.readAllStandardOutput ().trimmed ().toLongLong (&ok);
  return ok ? kib * 1024 : -1;
}

// "41 133430", "50 133430 deferred", "50 133430 again" or "false", per busy line.
QStringList busySequence (QList<Line> const& lines, QString const& mode = {})
{
  QStringList found;
  for (auto const& line : lines)
    {
      if (!outType ("busy") (line)) continue;
      auto const event = json (line.text);
      if (!event.value ("busy").toBool ())
        {
          found << "false";
          continue;
        }
      auto text = QString {"%1 %2"}.arg (event.value ("nzhsym").toInt ()).arg (event.value ("period_end").toString ());
      if (event.value ("again").toBool ()) text += " again";
      if (event.value ("deferred").toBool ()) text += " deferred";
      if (!mode.isEmpty () && event.value ("mode").toString () != mode) text += " in " + event.value ("mode").toString ();
      found << text;
    }
  return found;
}

QString show (QList<Line> const& lines, int from = 0)
{
  QStringList text;
  for (int i = from; i < lines.size (); ++i)
    {
      auto const& line = lines.at (i);
      if (isPcm (line.text)) continue;
      text << QString {"%1 %2 %3"}.arg (line.ms, 7).arg (line.error ? "err" : "out")
        .arg (QString::fromUtf8 (line.text.left (200)));
    }
  return text.join ('\n');
}

// --- one codec process ----------------------------------------------------------

bool isReply (QByteArray const& type)
{
  return "encoded" == type || "pcm" == type || "segment" == type || "packed" == type || "rendered" == type
    || "rejected" == type;
}

bool isTerminal (QByteArray const& type)
{
  return "packed" == type || "rendered" == type || "rejected" == type;
}

qint64 idOf (QByteArray const& text)
{
  static QRegularExpression const id {"\"id\":(-?\\d+)"};
  auto const m = id.match (QString::fromUtf8 (text.left (96)));
  return m.hasMatch () ? m.captured (1).toLongLong () : std::numeric_limits<qint64>::min ();
}


struct Options
{
  QStringList environment;   // NAME=value, added to the test's
  QString directory;         // its -a and -t, a temporary one when empty
  QStringList arguments;     // before -a and -t
  bool dropPcm {false};      // keep no pcm data
  bool hashPcm {false};      // keep only a hash of pcm data
  QString program;           // another build of wsjt-codec
};

class Codec
{
public:
  explicit Codec (Options const& options = {})
    : dropPcm_ {options.dropPcm}
    , hashPcm_ {options.hashPcm}
  {
    directory = options.directory.isEmpty () ? temporary_.path () : options.directory;
    auto environment = QProcessEnvironment::systemEnvironment ();
    for (auto const& setting : options.environment)
      environment.insert (setting.section ('=', 0, 0), setting.section ('=', 1));
    process_.setProcessEnvironment (environment);
    process_.setProcessChannelMode (QProcess::SeparateChannels);
    process_.setWorkingDirectory (directory);
    clock_.start ();
    auto const program = options.program.isEmpty () ? QString::fromUtf8 (WSJT_CODEC_EXECUTABLE) : options.program;
    process_.start (program, options.arguments + QStringList {"-a", directory, "-t", directory});
    started = process_.waitForStarted (5000);
    if (!started) failure = process_.errorString ();
  }

  Codec (Codec const&) = delete;
  Codec& operator= (Codec const&) = delete;

  ~Codec ()
  {
    if (QProcess::NotRunning != process_.state ())
      {
        process_.kill ();
        process_.waitForFinished (5000);
      }
  }

  qint64 now () const {return clock_.elapsed ();}

  // Writes and waits until the pipe has taken it all; returns when.
  qint64 write (QByteArray const& bytes)
  {
    post (bytes);
    while (process_.bytesToWrite () > 0 && QProcess::Running == process_.state ()) pump ();
    return now ();
  }

  // Queues bytes for the pipe without waiting.
  void post (QByteArray const& bytes)
  {
    process_.write (bytes);
    written += bytes.size ();
  }

  qint64 request (qint64 id, QJsonObject keys, QString const& verb = "render")
  {
    keys.insert ("t", verb);
    keys.insert ("id", id);
    auto const at = write (control (keys));
    sent[id] = at;
    return at;
  }

  void pump (int ms = 1)
  {
    if (process_.bytesToWrite () > 0) process_.waitForBytesWritten (ms);
    else process_.waitForReadyRead (ms);
    read ();
  }

  bool waitFor (std::function<bool ()> const& done, int ms, QString const& what)
  {
    QElapsedTimer timer;
    timer.start ();
    while (!done ())
      {
        if (timer.elapsed () > ms || QProcess::Running != process_.state ())
          {
            read ();
            if (done ()) return true;
            if (failure.isEmpty ()) failure = "waiting for " + what;
            return false;
          }
        pump ();
      }
    return true;
  }

  bool waitForLine (Match const& match, int ms, QString const& what, int from = 0)
  {
    return waitForCount (match, 1, ms, what, from);
  }

  bool waitForCount (Match const& match, int n, int ms, QString const& what, int from = 0)
  {
    Tally seen {match, from};
    return waitFor ([&] {return seen (lines) >= n;}, ms, what);
  }

  // Whether the request's last reply line has come.
  bool answered (qint64 id) const {return answered_.contains (id);}

  // Reads nothing for ms, so that the codec's lines back up in the pipe.
  void stall (int ms)
  {
    QThread::msleep (ulong (ms));
  }

  bool waitUntil (qint64 ms)
  {
    return waitFor ([&] {return now () >= ms;}, int (ms - now ()) + 1000, "the clock");
  }

  // Ends the input and waits for the exit.
  bool finish (int ms = waitMs ())
  {
    process_.closeWriteChannel ();
    auto const ended = waitFor ([this] {return QProcess::NotRunning == process_.state ();}, ms, "the codec to exit");
    if (!ended)
      {
        process_.kill ();
        process_.waitForFinished (5000);
      }
    read ();
    exitCode = process_.exitCode ();
    exitStatus = process_.exitStatus ();
    return ended;
  }

  bool clean () const {return QProcess::NormalExit == exitStatus && 0 == exitCode && failure.isEmpty ();}
  QString report () const {return QString {"%1 (exit %2)\n%3"}.arg (failure).arg (exitCode).arg (show (lines));}
  bool running () const {return QProcess::Running == process_.state ();}
  qint64 processId () const {return process_.processId ();}
  qint64 consumed () const {return written - process_.bytesToWrite ();}

  QString directory;
  bool started {false};
  QString failure;
  QList<Line> lines;
  QMap<qint64, qint64> sent;   // each request's id and when the pipe took it
  qint64 written {0};
  int exitCode {-1};
  QProcess::ExitStatus exitStatus {QProcess::CrashExit};

private:
  void read ()
  {
    // Qt 5 on Windows returns on stdout's data with stderr's readyRead still queued.
    QCoreApplication::processEvents ();
    take (out_, process_.readAllStandardOutput (), false);
    take (err_, process_.readAllStandardError (), true);
  }

  void take (QByteArray& buffer, QByteArray const& more, bool error)
  {
    if (more.isEmpty ()) return;
    auto const ms = now ();
    buffer += more;
    int from = 0;
    for (int end; (end = buffer.indexOf ('\n', from)) >= 0; from = end + 1)
      {
        // A dropped pcm line is copied only up to its data, a hashed one with its data's hash.
        auto const pcm = (dropPcm_ || hashPcm_) && !error
          && isPcm (QByteArray::fromRawData (buffer.constData () + from, end - from));
        auto text = buffer.mid (from, pcm && !hashPcm_ ? std::min (end - from, 128) : end - from);
        if (text.endsWith ('\r')) text.chop (1);
        if (pcm)
          {
            auto const data = text.indexOf ("\"data\"");
            auto const hash = hashPcm_ ? QCryptographicHash::hash (text.mid (data), QCryptographicHash::Sha1).toHex ()
                                       : QByteArray {};
            text.truncate (data);
            text += hash;
          }
        if (!error && isTerminal (typeOf (text))) answered_.insert (idOf (text));
        lines.append ({ms, error, text});
      }
    buffer.remove (0, from);
  }

  QTemporaryDir temporary_;
  QProcess process_;
  QElapsedTimer clock_;
  QByteArray out_;
  QByteArray err_;
  QSet<qint64> answered_;
  bool dropPcm_;
  bool hashPcm_;
};

// Runs a whole input and returns the finished codec.
std::unique_ptr<Codec> runInput (QByteArray const& input, Options const& options = {})
{
  std::unique_ptr<Codec> codec {new Codec {options}};
  if (!codec->started) return codec;
  codec->post (input);
  // The slowest decodes take a minute natively, several on a capped runner.
  codec->finish (5 * waitMs ());
  return codec;
}

bool ready (Codec& codec)
{
  return codec.waitForLine (outType ("ready"), waitMs (), "ready");
}

// --- replies -----------------------------------------------------------------

struct Reply
{
  qint64 sent {-1};
  qint64 first {-1};       // its first line's arrival
  qint64 last {-1};        // its terminal line's
  int firstIndex {-1};
  int lastIndex {-1};
  QByteArray terminal;     // the terminal line's type
  QByteArrayList lines;
};

QMap<qint64, Reply> repliesOf (Codec const& codec)
{
  QMap<qint64, Reply> replies;
  for (int i = 0; i < codec.lines.size (); ++i)
    {
      auto const& line = codec.lines.at (i);
      if (line.error) continue;
      auto const type = typeOf (line.text);
      if (!isReply (type)) continue;
      auto& reply = replies[idOf (line.text)];
      if (reply.first < 0)
        {
          reply.first = line.ms;
          reply.firstIndex = i;
        }
      reply.lines << line.text;
      if (isTerminal (type))
        {
          reply.last = line.ms;
          reply.lastIndex = i;
          reply.terminal = type;
        }
    }
  for (auto it = codec.sent.constBegin (); it != codec.sent.constEnd (); ++it) replies[it.key ()].sent = it.value ();
  return replies;
}

// A reply's lines under another request id.
QByteArrayList withId (QByteArrayList lines, qint64 id)
{
  static QRegularExpression const field {"\"id\":-?\\d+"};
  for (auto& line : lines) line = QString::fromUtf8 (line).replace (field, QString {"\"id\":%1"}.arg (id)).toUtf8 ();
  return lines;
}

// A render of each kind the codec encodes, at 12 kHz.
QList<QJsonObject> encodeKinds ()
{
  return {
    {{"mode", "FT8"}, {"text", "CQ K1ABC FN42"}, {"freq", 1500}},
    {{"mode", "FT4"}, {"text", "CQ K1ABC FN42"}, {"freq", 1500}},
    {{"mode", "JT4"}, {"text", "CQ K1ABC FN42"}, {"submode", "A"}, {"freq", 1500}},
    {{"mode", "JT9"}, {"text", "CQ K1ABC FN42"}, {"submode", "A"}, {"freq", 1500}},
    {{"mode", "JT65"}, {"text", "CQ K1ABC FN42"}, {"submode", "A"}, {"freq", 1500}},
    {{"mode", "Q65"}, {"text", "CQ K1ABC FN42"}, {"submode", "A"}, {"trperiod", 30}, {"freq", 1500}},
    {{"mode", "FST4"}, {"text", "CQ K1ABC FN42"}, {"trperiod", 60}, {"freq", 1500}},
    {{"mode", "FST4W"}, {"text", "K1ABC FN42 37"}, {"trperiod", 120}, {"freq", 1500}},
    {{"mode", "WSPR"}, {"text", "K1ABC FN42 37"}, {"freq", 1500}},
    {{"mode", "MSK144"}, {"text", "K1ABC W9XYZ R-07"}, {"trperiod", 15}, {"freq", 1000}},
    {{"mode", "ECHO"}, {"kind", "message"}, {"text", "K1JT"}, {"spacing", 10}, {"freq", 1500}},
    {{"mode", "TUNE"}, {"seconds", 2}, {"freq", 1500}},
    {{"mode", "FOX"}, {"slots", QJsonArray {"W9XYZ K1ABC -12"}}, {"freq", 300}},
    {{"mode", "SUPERFOX"}, {"slots", QJsonArray {"W9XYZ K1ABC RR73"}}, {"freq", 750}},
    {{"text", "CQ CQ K1ABC"}, {"freq", 1500}},
  };
}

QJsonObject const FoxRender {{"mode", "FOX"}, {"slots", QJsonArray {"W9XYZ K1ABC -12"}}, {"freq", 300},
                             {"rate", 12000}};
QJsonObject const Ft8Render {{"mode", "FT8"}, {"text", "CQ K1ABC FN42"}, {"freq", 1500}, {"rate", 12000}};
QJsonObject const Fst4LongRender {{"mode", "FST4"}, {"text", "CQ K1ABC FN42"}, {"trperiod", 300}, {"freq", 1500},
                                  {"rate", 12000}};

// --- a client's liveness deadlines -------------------------------------------

struct SentPeriod
{
  qint64 ms;      // the client had sent its whole audio
  QString label;  // its period_end
};

struct Rules
{
  QString mode;
  double trperiod;
  bool live;
};

struct Expiry
{
  qint64 at;
  QString what;   // "decode" or "period <label>"
};

// The deadlines that ran out for a client that, as the GUI does, allows a
// busy pass so long between progress lines and an idle sent period so long
// before its decode_finished or a busy line naming it. A reply's lines and
// planning pause them; they go on from where they were afterwards.
QList<Expiry> liveness (QList<Line> const& lines, QList<SentPeriod> const& sent, Rules const& rules, qint64 end)
{
  struct Timer
  {
    qint64 start;
    qint64 limit;
    qint64 elapsed;   // while paused; -1 while running
  };
  std::optional<Timer> decode;
  QMap<QString, Timer> periods;
  QStringList held;               // sent periods a busy line suspended
  QSet<QString> done;
  bool busy {false};
  bool sawBusy {false};
  bool completed {false};
  bool again {false};
  bool inReply {false};
  bool planning {false};
  QString busyMode {rules.mode};
  QList<Expiry> expired;
  auto const limit = [&] (QString const& mode, bool automatic) -> qint64 {
    auto const longest = qint64 (4000 * rules.trperiod);
    if (!completed) return std::max<qint64> (300000, longest);
    if (automatic && "FT8" == mode) return 10000;
    if (automatic && "FT4" == mode) return 6000;
    return std::max<qint64> (60000, longest);
  };
  auto const paused = [&] {return inReply || planning;};
  auto const timer = [&] (qint64 t, qint64 ms) {return Timer {t, ms, paused () ? 0 : -1};};
  auto const check = [&] (qint64 t) {
    if (decode && decode->elapsed < 0 && decode->start + decode->limit <= t)
      {
        expired << Expiry {decode->start + decode->limit, "decode"};
        decode.reset ();
      }
    for (auto it = periods.begin (); it != periods.end ();)
      {
        if (it->elapsed < 0 && it->start + it->limit <= t)
          {
            expired << Expiry {it->start + it->limit, "period " + it.key ()};
            it = periods.erase (it);
          }
        else
          {
            ++it;
          }
      }
  };
  auto const pause = [&] (qint64 t) {
    if (decode && decode->elapsed < 0) decode->elapsed = t - decode->start;
    for (auto& p : periods)
      if (p.elapsed < 0) p.elapsed = t - p.start;
  };
  auto const resume = [&] (qint64 t) {
    if (decode && decode->elapsed >= 0) decode = Timer {t - decode->elapsed, decode->limit, -1};
    for (auto& p : periods)
      if (p.elapsed >= 0) p = Timer {t - p.elapsed, p.limit, -1};
  };
  auto const progress = [&] (qint64 t) {
    if (busy) decode = timer (t, limit (busyMode, rules.live && !again));
  };
  auto const answer = [&] (QString const& label) {
    done.insert (label);
    periods.remove (label);
    held.removeAll (label);
  };

  struct Event
  {
    qint64 ms;
    int line;      // -1 for a sent period
    int period;
  };
  QList<Event> events;
  for (int i = 0; i < lines.size (); ++i)
    if (!lines.at (i).error) events << Event {lines.at (i).ms, i, -1};
  for (int i = 0; i < sent.size (); ++i) events << Event {sent.at (i).ms, -1, i};
  std::stable_sort (events.begin (), events.end (), [] (Event const& a, Event const& b) {return a.ms < b.ms;});
  for (auto const& event : events)
    {
      auto const t = event.ms;
      check (t);
      if (event.line < 0)
        {
          auto const& label = sent.at (event.period).label;
          if (done.contains (label)) continue;
          if (busy) held << label;
          else periods[label] = timer (t, limit (rules.mode, rules.live));
          continue;
        }
      auto const& text = lines.at (event.line).text;
      auto const type = typeOf (text);
      if (isReply (type))
        {
          if (!inReply && !isTerminal (type))
            {
              if (!paused ()) pause (t);
              inReply = true;
            }
          else if (isTerminal (type))
            {
              inReply = false;
              if (!paused ()) resume (t);
            }
          continue;
        }
      if ("planning" == type)
        {
          auto const now = json (text).value ("planning").toBool ();
          if (now && !paused ()) pause (t);
          planning = now;
          if (!paused ()) resume (t);
          continue;
        }
      if ("busy" == type)
        {
          auto const e = json (text);
          if (sawBusy) completed = true;
          if (e.value ("busy").toBool ())
            {
              if (!busy)
                {
                  held << periods.keys ();
                  periods.clear ();
                }
              busy = true;
              sawBusy = true;
              busyMode = e.value ("mode").toString ();
              again = e.value ("again").toBool ();
              answer (e.value ("period_end").toString ());
              progress (t);
            }
          else
            {
              busy = false;
              decode.reset ();
              for (auto const& label : held)
                if (!done.contains (label)) periods[label] = timer (t, limit (rules.mode, rules.live));
              held.clear ();
            }
          continue;
        }
      if ("decode_finished" == type)
        {
          completed = true;
          answer (json (text).value ("period_end").toString ());
          progress (t);
        }
      else if ("pass_progress" == type || "pass_skipped" == type)
        {
          progress (t);
        }
      else if ("decode" == type)
        {
          auto const mode = json (text).value ("mode").toString ();
          if (mode == busyMode || ("JT65+JT9" == busyMode && ("JT9" == mode || "JT65" == mode))) progress (t);
        }
    }
  check (end);
  return expired;
}

QString show (QList<Expiry> const& expired)
{
  QStringList text;
  for (auto const& each : expired) text << QString {"%1 at %2 ms"}.arg (each.what).arg (each.at);
  return text.join ("; ");
}

// --- live multithreaded FT8 behind by a period ----------------------------------

// Start 0: period 1 arrives while period 0's final decodes, so its final waits
// (test_streaming_backlog's mtdBehindOnePeriodSkipsAndDefers). Renders go one
// after another from the anchor on, or burstRenders of them with period 1, so
// the main thread is busy as the anchor ends; this returns at the first
// decision after the anchor, with a render in flight. dueMs: all of the audio
// sent here is due before the anchor.
struct Behind
{
  QByteArray busy;     // the sample, periods 0 and 2
  QByteArray engine;   // ft8-engine.wav, period 1
  QJsonObject settings {{"mode", "FT8"}, {"depth", 3}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                        {"ft8_decoder_start", 0}, {"ft8_cycles", 1}, {"live_audio", true}};
  int burstRenders {0};
  qint64 nextId {0};
  qint64 inFlight {-1};
  int anchorAt {-1};
  int endedAt {-1};
};

QString runBehind (Codec& codec, Behind& b, qint64 dueMs)
{
  if (!codec.started) return codec.failure;
  codec.write (sessionHeader () + configure (with (b.settings, {{"utc", utcOf (0)}})) + frame (0x01u, b.busy.left (2)));
  auto const t0 = codec.now ();
  if (!codec.waitUntil (t0 + dueMs + DueMarginMs)) return codec.failure;
  codec.write (frame (0x01u, samples (b.busy, 1, 41 * HsymSamples)) + marker ());
  if (!codec.waitForLine (outHas ("configure_parse_error"), waitMs (), "the gate")) return codec.failure;
  auto const before = codec.lines.size ();
  codec.write (frame (0x01u, samples (b.busy, 41 * HsymSamples, 49 * HsymSamples)));
  if (!codec.waitForLine (passLine (49, true), waitMs (), "the anchor's pass line", before)) return codec.failure;
  b.anchorAt = find (codec.lines, passLine (49, true), before);
  QByteArray renders;
  for (int id = 1; id <= b.burstRenders; ++id) renders += control (with (Fst4LongRender, {{"t", "render"}, {"id", id}}));
  codec.write (frame (0x01u, samples (b.busy, 49 * HsymSamples, Ft8PeriodSamples)) + configure ({{"utc", utcOf (1)}})
               + frame (0x01u, b.engine) + renders);
  if (b.burstRenders > 0) b.inFlight = b.nextId = b.burstRenders;
  Tally ended {isDecision, b.anchorAt + 1};
  while (!ended (codec.lines) && codec.running () && codec.now () - t0 < dueMs + waitMs ())
    {
      if (0 == b.burstRenders && (b.inFlight < 0 || codec.answered (b.inFlight)))
        {
          b.inFlight = ++b.nextId;
          codec.request (b.inFlight, Fst4LongRender);
        }
      codec.pump ();
    }
  b.endedAt = ended.first ();
  if (b.endedAt < 0) return "the anchor did not end";
  if (codec.answered (b.inFlight))
    {
      // Lines read together share a time: a reply read with the anchor's
      // end may have ended after it, so another render keeps the main
      // thread busy; one read earlier left it idle.
      auto const replied = repliesOf (codec).value (b.inFlight).last;
      if (replied < msOf (codec.lines, b.endedAt) || b.burstRenders > 0)
        return "no render kept the main thread busy when the anchor ended";
      b.inFlight = ++b.nextId;
      codec.request (b.inFlight, Fst4LongRender);
    }
  return {};
}
}

// Period-mode decodes on the codec's decode thread, while the main thread
// reads, renders and decodes MSK144 and JTTY.
class TestStreamingDecodeThread final : public QObject
{
  Q_OBJECT

private:
  // Each kind's idle render time on a fresh session: the median of five after a first.
  QMap<QString, qint64> idleRenderMs (QMap<QString, QJsonObject> const& kinds)
  {
    QMap<QString, qint64> idle;
    Options options;
    options.dropPcm = true;
    Codec codec {options};
    if (!codec.started) return idle;
    codec.write (sessionHeader () + configure ({{"mode", "FT8"}}));
    if (!ready (codec)) return idle;
    qint64 id = 0;
    for (auto it = kinds.constBegin (); it != kinds.constEnd (); ++it)
      {
        QList<qint64> times;
        for (int n = 0; n < 6; ++n)
          {
            auto const sentAt = codec.request (++id, it.value ());
            if (!codec.waitFor ([&] {return codec.answered (id);}, waitMs (), "an idle render")) return {};
            times << repliesOf (codec).value (id).last - sentAt;
          }
        times.removeFirst ();
        std::sort (times.begin (), times.end ());
        idle[it.key ()] = times.at (2);
      }
    codec.write (halt ());
    codec.finish ();
    return idle;
  }

private slots:
  // A render during the slowest decodes, while audio comes in real
  // time, is answered within its idle time plus 20 ms natively (twice its
  // idle time plus 100 ms with WSJT_DECODE_THREAD_CAPPED set, on the CPU-
  // capped image); the first of each kind is reported, not bound. One render
  // is outstanding at a time, so that none waits behind another.
  void rendersDuringTheSlowestDecodes_data ()
  {
    QTest::addColumn<QString> ("decode");
    QTest::newRow ("fst4-60-wide") << QString {"fst4-60-wide"};
    QTest::newRow ("multithreaded ft8") << QString {"ft8-mtd"};
  }

  void rendersDuringTheSlowestDecodes ()
  {
    QFETCH (QString, decode);
    auto const mode = modeOf (decode);
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QMap<QString, QJsonObject> const kinds {
      {"FT8", Ft8Render},
      {"Tune 600 s", {{"mode", "TUNE"}, {"seconds", 600}, {"freq", 1500}, {"rate", 12000}}},
      {"FST4-300", Fst4LongRender}};
    auto const idle = idleRenderMs (kinds);
    QVERIFY2 (idle.size () == kinds.size (), "the idle renders");
    auto const capped = qEnvironmentVariableIsSet ("WSJT_DECODE_THREAD_CAPPED");
    auto const bound = [&] (QString const& kind) {return capped ? 2 * idle[kind] + 100 : idle[kind] + 20;};
    auto const multithreaded = "ft8-mtd" == decode;
    auto const periods = multithreaded ? 12 : 1;
    auto const stop = 49 * HsymSamples;

    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    // The decodes alone first; the session with renders loads their FFTW wisdom.
    QByteArray plainInput = sessionHeader () + configure (mode.settings);
    for (int p = 0; p < periods; ++p)
      plainInput += (p > 0 ? configure ({{"utc", utcOf (p)}}) : QByteArray {}) + audio (pcm, 12000);
    Options shared;
    shared.directory = directory.path ();
    auto const plain = runInput (plainInput + halt (), shared);
    QVERIFY2 (plain->clean (), qPrintable (plain->report ()));

    shared.dropPcm = true;
    Codec codec {shared};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (mode.settings));
    QVERIFY2 (ready (codec), qPrintable (codec.report ()));
    QList<QPair<qint64, qint64>> windows;   // each decode's pass line to its decode_finished
    QMap<qint64, QString> kindOf;
    QStringList const names = kinds.keys ();
    qint64 id = 0;
    Tally finishedLines {outType ("decode_finished")};
    for (int p = 0; p < periods; ++p)
      {
        QByteArray head = p > 0 ? configure ({{"utc", utcOf (p)}}) : QByteArray {};
        Match const pass = multithreaded ? passLine (49, true) : errHas ("pass nzhsym=");
        auto const before = codec.lines.size ();
        codec.write (head + audio (multithreaded ? pcm.left (2 * stop) : pcm, 12000));
        QVERIFY2 (codec.waitForLine (pass, waitMs (), "the decode's pass line", before), qPrintable (codec.report ()));
        auto const start = msOf (codec.lines, find (codec.lines, pass, before));
        // Paced audio, and a render every half second (a quarter in the short
        // multithreaded final) once the last is answered, until the decode ends.
        auto const rest = multithreaded ? pcm.mid (2 * stop) : QByteArray (2 * 50 * SampleRate, '\0');
        auto const cadence = multithreaded ? 250 : 500;
        int paced = 0;
        auto nextFrame = codec.now ();
        auto nextRender = codec.now ();
        auto const finished = [&] {return finishedLines (codec.lines) > p;};
        while (!finished () && codec.running () && codec.now () - start < 10 * waitMs ())
          {
            if (codec.now () >= nextFrame && 2 * paced < rest.size ())
              {
                codec.write (frame (0x01u, rest.mid (2 * paced, 2 * FrameSamples)));
                paced += FrameSamples;
                nextFrame += 100;
              }
            if (codec.now () >= nextRender && (0 == id || codec.answered (id)))
              {
                auto const kind = names.at (int (id % names.size ()));
                kindOf[++id] = kind;
                codec.request (id, kinds[kind]);
                nextRender += cadence;
              }
            codec.pump ();
          }
        QVERIFY2 (finished (), qPrintable (codec.report ()));
        windows << qMakePair (start, msOf (codec.lines, find (codec.lines, outType ("decode_finished"), before)));
        if (multithreaded && 2 * paced < rest.size ()) codec.write (frame (0x01u, rest.mid (2 * paced)));
      }
    // The paced audio after the last decode is a period short of its stop.
    codec.write (control ({{"t", "discontinuity"}}) + halt ());
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (decodeLines (codec.lines, mode.sorted), decodeLines (plain->lines, mode.sorted));

    auto const replies = repliesOf (codec);
    QMap<QString, int> measured;
    QMap<QString, qint64> worst;
    QMap<QString, QList<qint64>> times;
    QSet<QString> seen;
    QStringList over;
    QStringList firsts;
    for (auto it = kindOf.constBegin (); it != kindOf.constEnd (); ++it)
      {
        auto const& reply = replies.value (it.key ());
        QCOMPARE (reply.terminal, QByteArray {"rendered"});
        auto const during = std::any_of (windows.begin (), windows.end (), [&] (QPair<qint64, qint64> const& w) {
          return reply.sent >= w.first && reply.sent < w.second;
        });
        if (!during) continue;
        auto const ms = reply.last - reply.sent;
        if (!seen.contains (it.value ()))
          {
            seen.insert (it.value ());
            firsts << QString {"%1 %2 ms"}.arg (it.value ()).arg (ms);
            continue;
          }
        ++measured[it.value ()];
        worst[it.value ()] = std::max (worst.value (it.value ()), ms);
        times[it.value ()] << ms;
        if (ms > bound (it.value ()))
          over << QString {"%1 #%2 %3 ms"}.arg (it.value ()).arg (it.key ()).arg (ms);
      }
    QStringList summary;
    for (auto const& kind : names)
      {
        auto sorted = times.value (kind);
        std::sort (sorted.begin (), sorted.end ());
        auto const at = [&] (double q) {return sorted.isEmpty () ? -1 : sorted.at (int (q * (sorted.size () - 1)));};
        summary << QString {"%1: idle %2 ms, bound %3 ms, %4 during the decodes, median %5 ms, p95 %6 ms, worst %7 ms"}
          .arg (kind).arg (idle[kind]).arg (bound (kind)).arg (measured.value (kind)).arg (at (0.5)).arg (at (0.95))
          .arg (worst.value (kind));
      }
    qInfo ().noquote () << "first renders:" << firsts.join ("; ");
    qInfo ().noquote () << summary.join ("\n");
    QVERIFY2 (over.isEmpty (), qPrintable (summary.join ("; ") + "\nover the bound: " + over.join ("; ")));
    for (auto const& kind : names)
      QVERIFY2 (measured.value (kind) >= 2, qPrintable ("too few renders during the decodes: " + kind));
  }

  // Each encode kind during each period mode's decode: from the hold of the
  // decode's last pass until its decode_finished, the kinds are sent in
  // turn, each request when the last is answered, so that requests run
  // beside the decoder after the hold. The first round is answered before the
  // decode_finished, a request sent after the hold is answered before the
  // decode_finished (not checked for q65 and jt4, whose decodes end about
  // 25 ms after the hold, within one round), the decodes are those of a run
  // without them, and each reply is that of an idle session in the same -a
  // after them.
  void encodesDuringEachDecode_data ()
  {
    QTest::addColumn<QString> ("decode");
    QTest::addColumn<bool> ("answeredDuring");
    for (auto const name : {"ft8", "ft8-mtd", "ft4", "fst4", "q65", "jt4", "jt9", "jt65", "jt65jt9"})
      QTest::newRow (name) << QString {name} << (QString {name} != "q65" && QString {name} != "jt4");
  }

  void encodesDuringEachDecode ()
  {
    QFETCH (QString, decode);
    QFETCH (bool, answeredDuring);
    auto const mode = modeOf (decode);
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const kinds = encodeKinds ();
    auto const round = [&] (int r) {
      QByteArray requests;
      for (int i = 0; i < kinds.size (); ++i)
        {
          auto const id = r * kinds.size () + i + 1;
          requests += control (with (kinds.at (i), {{"t", "render"}, {"id", id}, {"rate", 12000}}));
        }
      return requests;
    };
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    shared.hashPcm = true;
    auto const plain = runInput (sessionHeader () + configure (mode.settings) + audio (pcm) + halt (), shared);
    QVERIFY2 (plain->clean (), qPrintable (plain->report ()));
    auto const held = lastPass (plain->lines);
    QVERIFY2 (held > 0, qPrintable (plain->report ()));
    auto hold = shared;
    hold.environment << QString {"WSJT_CODEC_TEST_HOLD_PASS=%1:%2"}.arg (held).arg (holdMs ());
    Codec run {hold};
    QVERIFY2 (run.started, qPrintable (run.failure));
    run.write (sessionHeader () + configure (mode.settings) + audio (pcm));
    QVERIFY2 (run.waitForLine (passLine (held), waitMs (), "the held pass"), qPrintable (run.report ()));
    int sent = 0;
    auto const answered = [&] {
      for (int id = 1; id <= sent; ++id)
        if (!run.answered (id)) return false;
      return true;
    };
    auto const send = [&] {
      auto const id = ++sent;
      auto const& kind = kinds.at ((id - 1) % kinds.size ());
      run.sent[id] = run.write (control (with (kind, {{"t", "render"}, {"id", id}, {"rate", 12000}})));
    };
    Tally finishedLines {outType ("decode_finished")};
    auto const heldAt = run.now ();
    while (0 == finishedLines (run.lines) && run.running () && run.now () - heldAt < 5 * waitMs ())
      {
        if (answered ()) send ();
        run.pump ();
      }
    while (sent % kinds.size ()) send ();
    int const rounds = sent / kinds.size ();
    QVERIFY2 (finishedLines (run.lines) > 0, qPrintable (run.report ()));
    QVERIFY2 (run.waitFor (answered, waitMs (), "the replies"), qPrintable (run.report ()));
    run.write (halt ());
    QVERIFY2 (run.finish (), qPrintable (run.report ()));
    QVERIFY2 (run.clean (), qPrintable (run.report ()));
    auto const decodes = decodeLines (run.lines, mode.sorted);
    QVERIFY2 (!messagesOf (decodes).isEmpty (), qPrintable (run.report ()));
    QCOMPARE (decodes, decodeLines (plain->lines, mode.sorted));
    auto const idle = runInput (sessionHeader () + configure (mode.settings) + round (0) + halt (), shared);
    QVERIFY2 (idle->clean (), qPrintable (idle->report ()));
    auto const before = repliesOf (*idle);
    auto const during = repliesOf (run);
    auto const finished = find (run.lines, outType ("decode_finished"));
    // The hold can end later than holdMs after the pass line; its own line says when.
    auto const holdEnds = msOf (run.lines, find (run.lines, holdDoneLine (held), find (run.lines, passLine (held))));
    QVERIFY2 (holdEnds >= 0, qPrintable (run.report ()));
    int beside = 0;
    for (int r = 0; r < rounds; ++r)
      for (int i = 0; i < kinds.size (); ++i)
        {
          auto const& a = before.value (i + 1);
          auto const& b = during.value (r * kinds.size () + i + 1);
          auto const what = QString {"round %1, %2"}.arg (r + 1).arg (QString::fromUtf8 (a.lines.value (0).left (60)));
          QVERIFY2 (a.terminal == "rendered", qPrintable (what));
          if (0 == r)
            QVERIFY2 (b.lastIndex >= 0 && b.lastIndex < finished, qPrintable ("answered after the decode: " + what));
          QVERIFY2 (withId (b.lines, i + 1) == a.lines, qPrintable ("the reply to " + what));
          beside += b.sent > holdEnds && b.lastIndex < finished;
        }
    qInfo ().noquote () << QString {"%1 rounds of %2, %3 sent after the hold and answered before decode_finished"}
      .arg (rounds).arg (kinds.size ()).arg (beside);
    if (answeredDuring)
      QVERIFY2 (beside > 0, "no request sent after the hold was answered before decode_finished");
  }

  // Fox renders, which plan their FFTs at patience 0, during live decodes
  // in a fresh -a, from the hold of the decode's pass until its
  // decode_finished: some are answered before it, and the decodes are
  // those of a run without them.
  void foxRendersLeaveLiveDecodes_data ()
  {
    QTest::addColumn<QString> ("decode");
    for (auto const name : {"jt9", "jt65", "q65", "fst4"}) QTest::newRow (name) << QString {name};
  }

  void foxRendersLeaveLiveDecodes ()
  {
    QFETCH (QString, decode);
    auto const mode = modeOf (decode);
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const settings = with (mode.settings, {{"live_audio", true}});
    auto const plain = runInput (sessionHeader () + configure (settings) + audio (pcm) + halt ());
    QVERIFY2 (plain->clean (), qPrintable (plain->report ()));
    auto const held = lastPass (plain->lines);
    QVERIFY2 (held > 0, qPrintable (plain->report ()));
    Options options;
    options.environment << QString {"WSJT_CODEC_TEST_HOLD_PASS=%1:%2"}.arg (held).arg (holdMs ());
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (settings) + audio (pcm));
    QVERIFY2 (codec.waitForLine (passLine (held), waitMs (), "the held pass"), qPrintable (codec.report ()));
    qint64 id = 0;
    Tally finishedLines {outType ("decode_finished")};
    auto const finished = [&] {return finishedLines (codec.lines) > 0;};
    while (!finished () && codec.running () && codec.now () < waitMs ())
      {
        if (0 == id || codec.answered (id)) codec.request (++id, FoxRender);
        codec.pump ();
      }
    QVERIFY2 (finished (), qPrintable (codec.report ()));
    codec.write (halt ());
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const decodes = decodeLines (codec.lines);
    QVERIFY2 (!messagesOf (decodes).isEmpty (), qPrintable (codec.report ()));
    QCOMPARE (decodes, decodeLines (plain->lines));
    auto const end = find (codec.lines, outType ("decode_finished"));
    int during = 0;
    for (auto const& reply : repliesOf (codec)) during += reply.lastIndex >= 0 && reply.lastIndex < end;
    qInfo ().noquote () << QString {"%1 of %2 Fox renders answered before the decode_finished"}.arg (during).arg (id);
    QVERIFY2 (during > 0, "no Fox render was answered during the decode");
  }

  // Multithreaded FT8 and JT65+JT9 decode on the decode thread as they
  // do inline, and as the build WSJT_CODEC_REFERENCE names does (develop's).
  void decodesMatchTheReference_data ()
  {
    QTest::addColumn<QString> ("decode");
    QTest::addColumn<int> ("periods");
    QTest::newRow ("ft8-mtd") << QString {"ft8-mtd"} << 2;
    QTest::newRow ("jt65jt9") << QString {"jt65jt9"} << 1;
  }

  void decodesMatchTheReference ()
  {
    QFETCH (QString, decode);
    QFETCH (int, periods);
    auto const mode = modeOf (decode);
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QByteArray input = sessionHeader () + configure (mode.settings);
    for (int p = 0; p < periods; ++p) input += (p > 0 ? configure ({{"utc", utcOf (p)}}) : QByteArray {}) + audio (pcm);
    input += halt ();
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    auto const threaded = runInput (input, shared);
    QVERIFY2 (threaded->clean (), qPrintable (threaded->report ()));
    auto const decodes = decodeLines (threaded->lines, mode.sorted);
    QVERIFY2 (messagesOf (decodes).size () > 1, qPrintable (threaded->report ()));
    auto inlined = shared;
    inlined.environment << "WSJT_CODEC_TEST_INLINE=1";
    auto const onMain = runInput (input, inlined);
    QVERIFY2 (onMain->clean (), qPrintable (onMain->report ()));
    QCOMPARE (decodeLines (onMain->lines, mode.sorted), decodes);
    QCOMPARE (decisions (onMain->lines), decisions (threaded->lines));
    auto const reference = qEnvironmentVariable ("WSJT_CODEC_REFERENCE");
    if (reference.isEmpty ())
      {
        qInfo () << "WSJT_CODEC_REFERENCE is not set: compared with the inline handler only";
        return;
      }
    auto other = shared;
    other.program = reference;
    auto const develop = runInput (input, other);
    QVERIFY2 (develop->clean (), qPrintable (develop->report ()));
    QCOMPARE (decodeLines (develop->lines, mode.sorted), decodes);
  }

  // The multithreaded decoder's workers print the period's time and pass.
  void multithreadedDecodesCarryTheirPassAndTime ()
  {
    auto const mode = modeOf ("ft8-mtd");
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const run = runInput (sessionHeader () + configure (with (mode.settings, {{"utc", "00:23:45"}})) + audio (pcm)
                               + halt ());
    QVERIFY2 (run->clean (), qPrintable (run->report ()));
    QSet<int> passes;
    for (auto const& line : run->lines)
      {
        if (outType ("decode") (line))
          {
            auto const event = json (line.text);
            QCOMPARE (event.value ("time").toString (), QString {"002345"});
            QVERIFY2 (event.contains ("pass"), line.text.constData ());
            passes.insert (event.value ("pass").toInt ());
          }
      }
    QCOMPARE (periodEnds (run->lines), QStringList {"002345"});
    QVERIFY2 (passes.contains (49), "no decode from the multithreaded final");
    QVERIFY2 ((passes - QSet<int> {41, 46, 49}).isEmpty (), qPrintable (run->report ()));
  }

  // The deepest decode of each mode on the decode thread decodes as inline does.
  void deepestDecodeOnTheDecodeThread_data ()
  {
    QTest::addColumn<QString> ("decode");
    for (auto const name : {"ft8", "ft8-mtd", "ft4", "fst4-60", "fst4w-1800", "q65-300a", "jt4", "jt9", "jt65",
                            "jt65jt9"})
      QTest::newRow (name) << QString {name};
  }

  void deepestDecodeOnTheDecodeThread ()
  {
    QFETCH (QString, decode);
    auto const mode = modeOf (decode);
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const input = sessionHeader () + configure (mode.settings) + audio (pcm, 12000) + halt ();
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    auto const threaded = runInput (input, shared);
    QVERIFY2 (threaded->clean (), qPrintable (threaded->report ()));
    QCOMPARE (count (threaded->lines, outType ("decode_finished")), 1);
    auto const decodes = decodeLines (threaded->lines, mode.sorted);
    QVERIFY2 (!messagesOf (decodes).isEmpty (), qPrintable (threaded->report ()));
    auto inlined = shared;
    inlined.environment << "WSJT_CODEC_TEST_INLINE=1";
    auto const onMain = runInput (input, inlined);
    QVERIFY2 (onMain->clean (), qPrintable (onMain->report ()));
    QCOMPARE (decodes, decodeLines (onMain->lines, mode.sorted));
  }

  // A marker sent during a decode is answered after its decode_finished.
  void markerDuringADecodeFollowsIt ()
  {
    auto const mode = modeOf ("ft8");
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    Codec codec;
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (mode.settings) + audio (pcm));
    QVERIFY2 (codec.waitForLine (passLine (50), waitMs (), "the final's pass line"), qPrintable (codec.report ()));
    codec.write (marker ());
    auto const during = find (codec.lines, outType ("decode_finished")) < 0;
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (during, "the decode ended before the marker was sent");
    auto const finished = find (codec.lines, outType ("decode_finished"));
    auto const answer = find (codec.lines, outHas ("configure_parse_error"));
    QVERIFY2 (finished >= 0 && answer > finished, qPrintable (codec.report ()));
  }

  // A configure that cancels, arriving after the decode ends while
  // renders keep the main thread busy, finds the waiting final run first:
  // it arrived after the decoder freed.
  void cancelAfterTheDecodeRunsTheWaitingFinal ()
  {
    Behind b;
    b.busy = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    b.engine = period (wavPcm (fixture ("ft8-engine.wav")), Ft8PeriodSamples);
    QVERIFY2 (!b.busy.isEmpty () && !b.engine.isEmpty (), "no recording");
    Options options;
    options.dropPcm = true;
    Codec codec {options};
    auto const failed = runBehind (codec, b, 2 * Ft8PeriodSamples / 12);
    QVERIFY2 (failed.isEmpty (), qPrintable (failed + "\n" + codec.report ()));
    codec.write (configure ({{"depth", 2}}));
    auto const busy = !codec.answered (b.inFlight);
    QVERIFY2 (codec.waitFor ([&] {return codec.answered (b.inFlight);}, waitMs (), "the last render"),
              qPrintable (codec.report ()));
    codec.write (marker ());
    QVERIFY2 (codec.waitForCount (outHas ("configure_parse_error"), 2, waitMs (), "the marker"),
              qPrintable (codec.report ()));
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (busy, "the render ended before the configure was sent");
    QCOMPARE (decisions (codec.lines),
              (QStringList {"pass 41 F", "pass 49 T", "skip 41 early busy", "defer 49", "pass 49 T"}));
    QVERIFY2 (count (codec.lines, outHas ("\"cancelled\"")) == 0, qPrintable (codec.report ()));
    QCOMPARE (periodEnds (codec.lines), (QStringList {labelOf (utcOf (0)), labelOf (utcOf (1))}));
    auto const lines = decodeLines (codec.lines);
    auto const first = std::find_if (lines.begin (), lines.end (), [] (QByteArray const& line) {
      return "decode_finished" == typeOf (line);
    });
    QVERIFY2 (messagesOf (QByteArrayList {first, lines.end ()}).contains (EngineMessage), qPrintable (codec.report ()));
  }

  // A deferred final with no further input runs when the main thread
  // waits for input, every time; "take held" holds the decode thread before it
  // takes each operation, so that the main thread waits first.
  void deferredFinalRunsWithNoFurtherInput_data ()
  {
    QTest::addColumn<QStringList> ("environment");
    QTest::newRow ("plain") << QStringList {"WSJT_CODEC_TEST_HOLD_PASS=41:1500"};
    QTest::newRow ("take held") << QStringList {"WSJT_CODEC_TEST_HOLD_PASS=41:1500",
                                                "WSJT_CODEC_TEST_HOLD_TAKE_MS=300"};
  }

  void deferredFinalRunsWithNoFurtherInput ()
  {
    QFETCH (QStringList, environment);
    auto const pcm = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QJsonObject const settings {{"mode", "FT8"}, {"depth", 3}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                                {"ft8_decoder_start", 1}, {"ft8_cycles", 1}, {"live_audio", true}};
    Options options;
    options.environment = environment;
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (with (settings, {{"utc", utcOf (0)}})) + frame (0x01u, pcm.left (2)));
    auto const t0 = codec.now ();
    int constexpr iterations = 4;
    QStringList expected;
    Tally finals {passLine (50, true)};
    Tally ends {outType ("decode_finished")};
    Tally markers {outHas ("configure_parse_error")};
    for (int i = 0; i < iterations; ++i)
      {
        // Each period whole is due before it is sent, so that its triggers
        // fire on arrival: the held early pass runs, the second is busy and
        // the final defers.
        QVERIFY (codec.waitUntil (t0 + (i + 1) * 15000 + DueMarginMs));
        auto const head = i > 0 ? configure ({{"utc", utcOf (i)}}) + frame (0x01u, pcm.left (2)) : QByteArray {};
        codec.write (head + frame (0x01u, samples (pcm, 1, 41 * HsymSamples))
                     + frame (0x01u, samples (pcm, 41 * HsymSamples, Ft8PeriodSamples)));
        QVERIFY2 (codec.waitFor ([&] {return finals (codec.lines) > i && ends (codec.lines) > i;}, waitMs (),
                                 QString {"period %1's deferred final"}.arg (i)), qPrintable (codec.report ()));
        // A decoder setting cancels the scheduler's backoff for the next period.
        codec.write (configure ({{"depth", i % 2 ? 3 : 2}}) + marker ());
        QVERIFY2 (codec.waitFor ([&] {return markers (codec.lines) > i;}, waitMs (), "the marker"),
                  qPrintable (codec.report ()));
        expected << "pass 41 F" << "skip 46 early busy" << "defer 50" << "pass 50 T";
      }
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (decisions (codec.lines), expected);
    QVERIFY2 (count (codec.lines, outHas ("\"cancelled\"")) == 0, qPrintable (codec.report ()));
  }

  // During a decode a reply comes at once and a declined configure's
  // error after the decode's lines; in MSK144 a decode again's refusal and a
  // reply come at once, in that order.
  void repliesComeAtOnce_data ()
  {
    QTest::addColumn<bool> ("msk144");
    QTest::newRow ("declined configure") << false;
    QTest::newRow ("msk144 refusal") << true;
  }

  void repliesComeAtOnce ()
  {
    QFETCH (bool, msk144);
    auto const mode = modeOf ("jt9");
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    Codec codec;
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (mode.settings) + audio (pcm));
    QVERIFY2 (codec.waitForLine (errHas ("pass nzhsym="), waitMs (), "the pass line"), qPrintable (codec.report ()));
    auto const frames = msk144 ? configure (Msk144Settings) + configure ({{"nagain_flag", true}})
                               : configure ({{"mode", "NOT-A-MODE"}});
    codec.write (frames);
    codec.request (1, Ft8Render);
    auto const during = find (codec.lines, outType ("decode_finished")) < 0;
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (during, "the decode ended before the requests were sent");
    auto const reply = repliesOf (codec).value (1);
    auto const finished = find (codec.lines, outType ("decode_finished"));
    auto const error = find (codec.lines, outHas (msk144 ? "\"decode_again_unavailable\"" : "\"unknown_mode\""));
    QVERIFY2 (reply.terminal == "rendered" && finished >= 0 && error >= 0, qPrintable (codec.report ()));
    qInfo ().noquote () << QString {"reply in %1 ms"}.arg (reply.last - reply.sent);
    QVERIFY2 (reply.lastIndex < finished, qPrintable ("the reply waited for the decode\n" + codec.report ()));
    if (msk144)
      {
        QVERIFY2 (error < reply.firstIndex, qPrintable (codec.report ()));
      }
    else
      {
        QVERIFY2 (error > finished, qPrintable (codec.report ()));
      }
  }

  // A final waits while renders keep the main thread busy; a render
  // that arrives after the decoder freed, with no other frame, starts the
  // final before its reply ends, and the next trigger is decided by the
  // GUI's scheduler.
  void renderAfterTheDecodeStartsTheWaitingFinal ()
  {
    Behind b;
    b.busy = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    b.engine = period (wavPcm (fixture ("ft8-engine.wav")), Ft8PeriodSamples);
    QVERIFY2 (!b.busy.isEmpty () && !b.engine.isEmpty (), "no recording");
    Options options;
    options.dropPcm = true;
    Codec codec {options};
    // Period 2's first early pass is due before the anchor too.
    auto const failed = runBehind (codec, b, (2 * Ft8PeriodSamples + 41 * HsymSamples) / 12);
    QVERIFY2 (failed.isEmpty (), qPrintable (failed + "\n" + codec.report ()));
    auto const r2 = ++b.nextId;
    codec.request (r2, Fst4LongRender);
    auto const deferral = find (codec.lines, errHas ("defer nzhsym=49"));
    QVERIFY2 (codec.waitForLine (passLine (49, true), waitMs (), "the waiting final", std::max (deferral, b.endedAt)),
              qPrintable (codec.report ()));
    auto const final = codec.lines.value (find (codec.lines, passLine (49, true), std::max (deferral, b.endedAt)));
    codec.write (configure ({{"utc", utcOf (2)}}) + frame (0x01u, b.busy.left (2 * 41 * HsymSamples)));
    Tally decided {isDecision};
    QVERIFY2 (codec.waitFor ([&] {return codec.answered (r2) && decided (codec.lines) >= 6;}, waitMs (),
                             "the render and period 2's early pass"), qPrintable (codec.report ()));
    codec.write (marker ());
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const reply = repliesOf (codec).value (r2);
    qInfo ().noquote () << QString {"render sent %1 ms, final's pass line %2 ms, render's last line %3 ms"}
      .arg (reply.sent).arg (final.ms).arg (reply.last);
    QVERIFY2 (deferral >= 0 && deferral < find (codec.lines, passLine (49, true), b.endedAt),
              qPrintable (codec.report ()));
    QVERIFY2 (final.ms < reply.last, "the waiting final started after the render's reply");
    QCOMPARE (decisions (codec.lines).mid (0, 6),
              (QStringList {"pass 41 F", "pass 49 T", "skip 41 early busy", "defer 49", "pass 49 T",
                            "skip 41 early busy"}));
  }

  // A fast feeder: every pass runs, threaded and inline; and while the
  // decode thread is held the main thread reads ahead to the queue's cap and
  // no further.
  void fastFeederRunsEveryPass ()
  {
    auto const busy = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    auto const engine = period (wavPcm (fixture ("ft8-engine.wav")), Ft8PeriodSamples);
    QVERIFY2 (!busy.isEmpty () && !engine.isEmpty (), "no recording");
    QJsonObject const settings {{"mode", "FT8"}, {"depth", 3}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                                {"ft8_decoder_start", 0}, {"live_audio", true}, {"utc", utcOf (0)}};
    int constexpr periods = 12;
    QByteArray input = sessionHeader () + configure (settings);
    QStringList expected;
    QStringList labels;
    for (int p = 0; p < periods; ++p)
      {
        input += (p > 0 ? configure ({{"utc", utcOf (p)}}) : QByteArray {}) + audio (p % 2 ? engine : busy);
        expected << "pass 41 F" << "pass 49 T";
        labels << labelOf (utcOf (p));
      }
    input += halt ();
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    auto const threaded = runInput (input, shared);
    QVERIFY2 (threaded->clean (), qPrintable (threaded->report ()));
    auto inlined = shared;
    inlined.environment << "WSJT_CODEC_TEST_INLINE=1";
    auto const onMain = runInput (input, inlined);
    QVERIFY2 (onMain->clean (), qPrintable (onMain->report ()));
    // Live FT8 passes stop on the wall clock, so the decodes vary; the passes may not.
    QCOMPARE (decisions (threaded->lines), expected);
    QCOMPARE (decisions (onMain->lines), expected);
    QCOMPARE (skips (threaded->lines), skips (onMain->lines));
    QCOMPARE (periodEnds (threaded->lines), labels);
    QCOMPARE (periodEnds (onMain->lines), labels);
  }

  void fastFeederQueuesAudioUpToTheCap ()
  {
    // FT4's one pass is held; FT8's silent periods follow, each queuing its
    // three passes' audio: 283,392, 324,864 and 360,000 bytes.
    qint64 constexpr perPeriod = 2 * (41 + 47) * HsymSamples + 2 * Ft8PeriodSamples;
    int constexpr periods = 300;
    QByteArray const silence (2 * Ft8PeriodSamples, '\0');
    QByteArray head = sessionHeader () + configure ({{"mode", "FT4"}, {"depth", 1}, {"utc", "00:00:00"}})
      + frame (0x01u, QByteArray (2 * Ft4PeriodSamples, '\0'));
    QByteArray rest;
    qint64 wire = 0;
    for (int p = 0; p < periods; ++p)
      {
        auto const one = configure (p > 0 ? QJsonObject {{"utc", utcOf (p, {0, 0, 15})}}
                                          : QJsonObject {{"mode", "FT8"}, {"utc", utcOf (0, {0, 0, 15})}})
          + frame (0x01u, silence);
        wire = std::max (wire, qint64 (one.size ()));
        rest += one;
      }
    Options options;
    options.environment << "WSJT_CODEC_TEST_HOLD_PASS=21:8000";
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (head);
    codec.post (rest);
    auto peak = codec.consumed ();
    auto changed = codec.now ();
    Tally finished {outType ("decode_finished")};
    auto const held = [&] {return 0 == finished (codec.lines);};
    while (held () && codec.running () && codec.now () - changed < 2000 && codec.consumed () < codec.written)
      {
        codec.pump ();
        if (codec.consumed () != peak)
          {
            peak = codec.consumed ();
            changed = codec.now ();
          }
      }
    auto const stalled = held ();
    QVERIFY2 (codec.finish (4 * waitMs ()), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const queued = (QueueCapBytes + perPeriod - 1) / perPeriod + 2;
    auto const most = head.size () + queued * wire + StdinCapBytes + (1 << 20);
    auto const least = head.size () + (QueueCapBytes / perPeriod - 8) * wire + StdinCapBytes;
    qInfo ().noquote () << QString {"read %1 bytes while FT4's pass was held; at most %2, at least %3"}
      .arg (peak).arg (most).arg (least);
    QVERIFY2 (stalled, "the main thread read everything before FT4's pass ended");
    QVERIFY2 (peak <= most, "the main thread read past the cap");
    QVERIFY2 (peak >= least, "the main thread did not read ahead to the cap");
    QCOMPARE (count (codec.lines, outType ("decode_finished")), periods + 1);
  }

  // A final waits behind long renders sent before the decoder freed, which
  // keep the main thread busy after the decode thread has deferred the
  // final: with no further input the final runs once the main thread waits
  // for input; a marker sent meanwhile is answered after it, whose drain it
  // triggers.
  void waitingFinalBehindLongRenders_data ()
  {
    QTest::addColumn<bool> ("withMarker");
    QTest::newRow ("reader blocks") << false;
    QTest::newRow ("marker") << true;
  }

  void waitingFinalBehindLongRenders ()
  {
    QFETCH (bool, withMarker);
    Behind b;
    b.busy = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    b.engine = period (wavPcm (fixture ("ft8-engine.wav")), Ft8PeriodSamples);
    QVERIFY2 (!b.busy.isEmpty () && !b.engine.isEmpty (), "no recording");
    b.burstRenders = 40;
    Options options;
    options.dropPcm = true;
    Codec codec {options};
    auto const failed = runBehind (codec, b, 2 * Ft8PeriodSamples / 12);
    QVERIFY2 (failed.isEmpty (), qPrintable (failed + "\n" + codec.report ()));
    QVERIFY2 (codec.waitForLine (errHas ("defer nzhsym=49"), waitMs (), "the deferral", b.endedAt),
              qPrintable (codec.report ()));
    auto const deferral = find (codec.lines, errHas ("defer nzhsym=49"), b.endedAt);
    auto rendering = true;
    if (withMarker)
      {
        codec.write (marker ());
        rendering = !codec.answered (b.burstRenders);
      }
    QVERIFY2 (codec.waitFor ([&] {return codec.answered (b.burstRenders);}, 2 * waitMs (), "the renders"),
              qPrintable (codec.report ()));
    auto const last = msOf (codec.lines, repliesOf (codec).value (b.burstRenders).lastIndex);
    // Nothing more is sent: the final must not wait for input.
    auto const ran = codec.waitForLine (passLine (49, true), 5000, "the waiting final", deferral);
    auto const final = find (codec.lines, passLine (49, true), deferral);
    if (withMarker) codec.waitForCount (outHas ("configure_parse_error"), 2, waitMs (), "the marker");
    codec.write (halt ());
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    qInfo ().noquote () << QString {"deferred at %1 ms, last reply %2 ms, the final's pass line %3 ms"}
      .arg (msOf (codec.lines, deferral)).arg (last).arg (msOf (codec.lines, final));
    QVERIFY2 (ran, "the waiting final did not run when the input stopped");
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (rendering, "the renders ended before the marker was sent");
    QVERIFY2 (msOf (codec.lines, deferral) < last, "the final was deferred after the renders ended");
    QCOMPARE (decisions (codec.lines),
              (QStringList {"pass 41 F", "pass 49 T", "skip 41 early busy", "defer 49", "pass 49 T"}));
    QCOMPARE (periodEnds (codec.lines), (QStringList {labelOf (utcOf (0)), labelOf (utcOf (1))}));
    if (withMarker)
      {
        auto const answer = findLast (codec.lines, outHas ("configure_parse_error"));
        QVERIFY2 (answer > findLast (codec.lines, outType ("decode_finished")),
                  qPrintable ("the marker came before the waiting final's lines\n" + show (codec.lines)));
      }
  }

  // A flush with a final waiting runs that final before the period it cuts
  // short, whose remaining pass then runs on the audio in.
  void flushRunsTheWaitingFinalFirst ()
  {
    auto const busy = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    auto const engine = period (wavPcm (fixture ("ft8-engine.wav")), Ft8PeriodSamples);
    QVERIFY2 (!busy.isEmpty () && !engine.isEmpty (), "no recording");
    QJsonObject const settings {{"mode", "FT8"}, {"depth", 3}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                                {"ft8_decoder_start", 0}, {"ft8_cycles", 1}, {"live_audio", true}};
    Codec codec;
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (with (settings, {{"utc", utcOf (0)}})) + frame (0x01u, busy.left (2)));
    auto const t0 = codec.now ();
    // Everything the burst carries is due before the anchor.
    QVERIFY (codec.waitUntil (t0 + (2 * Ft8PeriodSamples + 45 * HsymSamples) / 12 + DueMarginMs));
    codec.write (frame (0x01u, samples (busy, 1, 41 * HsymSamples)) + marker ());
    QVERIFY2 (codec.waitForLine (outHas ("configure_parse_error"), waitMs (), "the gate"), qPrintable (codec.report ()));
    auto const before = codec.lines.size ();
    codec.write (frame (0x01u, samples (busy, 41 * HsymSamples, 49 * HsymSamples)));
    QVERIFY2 (codec.waitForLine (passLine (49, true), waitMs (), "the anchor", before), qPrintable (codec.report ()));
    codec.write (frame (0x01u, samples (busy, 49 * HsymSamples, Ft8PeriodSamples)) + configure ({{"utc", utcOf (1)}})
                 + frame (0x01u, engine) + configure ({{"utc", utcOf (2)}})
                 + frame (0x01u, busy.left (2 * 45 * HsymSamples)) + control ({{"t", "flush"}}) + marker ());
    QVERIFY2 (codec.waitForCount (outHas ("configure_parse_error"), 2, waitMs (), "the flush"),
              qPrintable (codec.report ()));
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (decisions (codec.lines),
              (QStringList {"pass 41 F", "pass 49 T", "skip 41 early busy", "defer 49", "skip 41 early busy",
                            "pass 49 T", "pass 49 T"}));
    QCOMPARE (periodEnds (codec.lines), (QStringList {labelOf (utcOf (0)), labelOf (utcOf (1)), labelOf (utcOf (2))}));
    auto const lines = decodeLines (codec.lines);
    auto const first = std::find_if (lines.begin (), lines.end (), [] (QByteArray const& line) {
      return "decode_finished" == typeOf (line);
    });
    auto const second = std::find_if (first + 1, lines.end (), [] (QByteArray const& line) {
      return "decode_finished" == typeOf (line);
    });
    QVERIFY2 (messagesOf (QByteArrayList {first, second}).contains (EngineMessage), qPrintable (codec.report ()));
  }

  // A reply's lines stay together while decodes write their own beside it:
  // replies to renders sent behind four periods' audio interleave with the
  // decodes only between replies.
  void repliesStayWholeDuringDecodes ()
  {
    auto const mode = modeOf ("ft8");
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    int constexpr renders = 40;
    QByteArray input = sessionHeader () + configure (mode.settings);
    for (int p = 0; p < 4; ++p) input += (p > 0 ? configure ({{"utc", utcOf (p)}}) : QByteArray {}) + audio (pcm);
    for (int id = 1; id <= renders; ++id)
      input += control ({{"t", "render"}, {"id", id}, {"mode", "TUNE"}, {"seconds", 300}, {"freq", 1500},
                         {"rate", 12000}});
    Options options;
    options.dropPcm = true;
    auto const run = runInput (input + halt (), options);
    QVERIFY2 (run->clean (), qPrintable (run->report ()));
    auto const replies = repliesOf (*run);
    QCOMPARE (replies.size (), renders);
    int inside = 0;
    for (auto it = replies.constBegin (); it != replies.constEnd (); ++it)
      {
        QCOMPARE (it->terminal, QByteArray {"rendered"});
        for (int i = it->firstIndex; i <= it->lastIndex; ++i)
          {
            auto const& line = run->lines.at (i);
            if (!line.error && (!isReply (typeOf (line.text)) || idOf (line.text) != it.key ())) ++inside;
          }
      }
    // Decodes written between the first reply and the last show the two overlapped.
    int between = 0;
    for (int i = replies.first ().firstIndex; i < replies.last ().lastIndex; ++i)
      between += outType ("decode") (run->lines.at (i));
    qInfo ().noquote () << QString {"%1 lines inside replies; %2 decodes between the first reply and the last"}
      .arg (inside).arg (between);
    QCOMPARE (inside, 0);
    QVERIFY2 (between > 0, "no decode overlapped the replies");
  }

  // The audio clock takes up only the waits during which no decode runs: a
  // client that waits while the first early pass is held, then sends the
  // final's audio ahead of its due time, finds the decoder free.
  void waitsDuringADecodeLeaveTheClock ()
  {
    auto const pcm = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QJsonObject const settings {{"mode", "FT8"}, {"depth", 3}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                                {"ft8_decoder_start", 0}, {"ft8_cycles", 1}, {"live_audio", true}, {"utc", utcOf (0)}};
    Options options;
    options.environment << "WSJT_CODEC_TEST_HOLD_PASS=41:12000";
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    auto const t0 = codec.write (sessionHeader () + configure (settings) + frame (0x01u, pcm.left (2 * 41 * HsymSamples)));
    QVERIFY2 (codec.waitForLine (passLine (41), waitMs (), "the early pass"), qPrintable (codec.report ()));
    // The final falls due 14.1 s in, after the held pass ends; sent at 8 s.
    QVERIFY (codec.waitUntil (t0 + 8000));
    codec.write (frame (0x01u, samples (pcm, 41 * HsymSamples, 49 * HsymSamples)));
    QVERIFY2 (codec.waitForLine (outType ("decode_finished"), waitMs (), "the final"), qPrintable (codec.report ()));
    codec.write (frame (0x01u, samples (pcm, 49 * HsymSamples, Ft8PeriodSamples)) + halt ());
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (decisions (codec.lines), (QStringList {"pass 41 F", "pass 49 T"}));
  }

  // A snapshot that does not fit waits for the operations queued before it
  // to be taken, then tries again and finds the buffers they finished with.
  void aSnapshotRetriesOnceTheQueueDrains ()
  {
    auto const pcm = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    // Period 0's three passes fit (968,256 bytes); period 1's first early pass
    // does not while the first is held, and does once it has run.
    Options options;
    options.environment << "WSJT_CODEC_TEST_HOLD_PASS=41:3000" << "WSJT_CODEC_TEST_ALLOC_LIMIT=1000000";
    auto const run = runInput (sessionHeader () + configure ({{"mode", "FT8"}, {"depth", 3}, {"utc", utcOf (0)}})
                               + audio (pcm) + configure ({{"utc", utcOf (1)}}) + audio (pcm.left (2 * 48 * HsymSamples))
                               + control ({{"t", "discontinuity"}}) + halt (), options);
    QVERIFY2 (run->clean (), qPrintable (run->report ()));
    QCOMPARE (decisions (run->lines),
              (QStringList {"pass 41 F", "pass 47 F", "pass 50 F", "pass 41 F", "pass 47 F"}));
    QCOMPARE (skips (run->lines), QStringList {});
    QCOMPARE (periodEnds (run->lines), QStringList {labelOf (utcOf (0))});
  }

  // The test switch runs the handler on the main thread: a render sent while
  // a pass is held waits for that pass inline, and does not threaded.
  void theInlineSwitchTakesEffect ()
  {
    auto const mode = modeOf ("ft8");
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    for (auto const inlined : {false, true})
      {
        Options options;
        options.environment << QString {"WSJT_CODEC_TEST_HOLD_PASS=50:%1"}.arg (holdMs ());
        if (inlined) options.environment << "WSJT_CODEC_TEST_INLINE=1";
        Codec codec {options};
        QVERIFY2 (codec.started, qPrintable (codec.failure));
        codec.write (sessionHeader () + configure (mode.settings) + audio (pcm));
        QVERIFY2 (codec.waitForLine (passLine (50), waitMs (), "the held pass"), qPrintable (codec.report ()));
        codec.request (1, Ft8Render);
        QVERIFY2 (codec.waitFor ([&] {return codec.answered (1);}, waitMs (), "the reply"), qPrintable (codec.report ()));
        QVERIFY2 (codec.waitForLine (outType ("decode_finished"), waitMs (), "the pass"), qPrintable (codec.report ()));
        codec.write (halt ());
        QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
        QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
        auto const reply = repliesOf (codec).value (1);
        auto const finished = find (codec.lines, outType ("decode_finished"));
        if (inlined)
          QVERIFY2 (reply.firstIndex > finished, qPrintable ("inline, the render did not wait\n" + show (codec.lines)));
        else
          QVERIFY2 (reply.lastIndex < finished, qPrintable ("threaded, the render waited\n" + show (codec.lines)));
      }
  }

  // Every operation counts toward the queue's cap: while the decode thread
  // is held, a flood of configure frames is read only until the queued
  // operations reach it, so the codec's memory grows by at most its stdin
  // buffer and the cap.
  void controlFramesQueueUpToTheCap ()
  {
    int constexpr frames = 1000000;
    auto const one = configure ({{"utc", "00:00:07"}});
    QByteArray flood;
    flood.reserve (frames * one.size ());
    for (int i = 0; i < frames; ++i) flood += one;
    Options options;
    options.environment << "WSJT_CODEC_TEST_HOLD_PASS=21:10000";
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure ({{"mode", "FT4"}, {"depth", 1}, {"utc", "00:00:00"}})
                 + frame (0x01u, QByteArray (2 * Ft4PeriodSamples, '\0')));
    QVERIFY2 (codec.waitForLine (passLine (21), waitMs (), "the held pass"), qPrintable (codec.report ()));
    auto const before = residentBytes (codec.processId ());
    codec.post (flood);
    Tally finished {outType ("decode_finished")};
    auto consumed = codec.consumed ();
    auto changed = codec.now ();
    while (0 == finished (codec.lines) && codec.running () && codec.now () - changed < 2000
           && codec.consumed () < codec.written)
      {
        codec.pump ();
        if (codec.consumed () != consumed)
          {
            consumed = codec.consumed ();
            changed = codec.now ();
          }
      }
    auto const after = residentBytes (codec.processId ());
    auto const held = 0 == finished (codec.lines);
    QVERIFY2 (codec.finish (4 * waitMs ()), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const most = StdinCapBytes + QueueCapBytes + (qint64 {24} << 20);
    qInfo ().noquote () << QString {"read %1 of %2 bytes while the pass was held; memory grew %3 bytes, "
                                    "from %4 to %5, at most %6"}
      .arg (consumed).arg (codec.written).arg (after - before).arg (before).arg (after).arg (most);
    QVERIFY2 (before > 0 && after > 0, "no resident memory figure");
    QVERIFY2 (held, "the main thread read everything before the pass ended");
    QVERIFY2 (after - before <= most, "the queued operations passed the cap");
  }

  // A busy false the decode thread owes goes out before a line the main
  // thread routes after it: renders sent with the period keep the main
  // thread busy as its last pass ends, and a declined configure sent then
  // is answered after the busy false.
  void busyFalseComesBeforeALaterLine ()
  {
    auto const mode = modeOf ("ft8");
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    int constexpr renders = 40;
    QByteArray burst;
    for (int id = 1; id <= renders; ++id) burst += control (with (Fst4LongRender, {{"t", "render"}, {"id", id}}));
    Options options;
    options.dropPcm = true;
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (with (mode.settings, {{"busy_events", true}})) + audio (pcm) + burst);
    QVERIFY2 (codec.waitForLine (outType ("decode_finished"), waitMs (), "the period"), qPrintable (codec.report ()));
    codec.write (configure ({{"mode", "NOT-A-MODE"}}));
    auto const rendering = !codec.answered (renders);
    QVERIFY2 (codec.waitFor ([&] {return codec.answered (renders);}, 2 * waitMs (), "the renders"),
              qPrintable (codec.report ()));
    QVERIFY2 (codec.waitForLine (outHas ("\"unknown_mode\""), waitMs (), "the error"), qPrintable (codec.report ()));
    codec.write (halt ());
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (rendering, "the renders ended before the configure was sent");
    auto const finished = find (codec.lines, outType ("decode_finished"));
    auto const idle = find (codec.lines, busyLine (false), finished);
    auto const error = find (codec.lines, outHas ("\"unknown_mode\""));
    QVERIFY2 (idle >= 0 && idle < error, qPrintable ("the error came before busy false\n" + show (codec.lines)));
  }

  // A busy false the decode thread owes goes out before the decode thread's
  // own refusal of an operation read after the decode ended. Two markers
  // read with an early pass's audio are taken after the pass, each after a
  // held take; a decode again sent once the first marker's error is out,
  // with no period decoded yet in the session, is queued while the second
  // waits, and is refused after the busy false.
  void busyFalseComesBeforeARefusal ()
  {
    auto const mode = modeOf ("ft8");
    auto const pcm = samples (wavPcm (mode.wav), 0, 41 * HsymSamples);
    QVERIFY2 (pcm.size () == 2 * 41 * HsymSamples, "no recording");
    auto const take = holdMs () / 5;
    Options options;
    options.environment << QString {"WSJT_CODEC_TEST_HOLD_TAKE_MS=%1"}.arg (take);
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (with (mode.settings, {{"busy_events", true}})) + audio (pcm) + marker ()
                 + marker ());
    auto const markerError = outHas ("\"configure_parse_error\"");
    QVERIFY2 (codec.waitForLine (markerError, 2 * waitMs (), "the first marker"), qPrintable (codec.report ()));
    auto const sent = codec.write (configure ({{"nagain_flag", true}}));
    auto const first = msOf (codec.lines, find (codec.lines, markerError));
    auto const waiting = 1 == count (codec.lines, markerError);
    QVERIFY2 (codec.waitForLine (outHas ("\"decode_again_unavailable\""), 2 * waitMs (), "the refusal"),
              qPrintable (codec.report ()));
    codec.write (halt ());
    QVERIFY2 (codec.finish (2 * waitMs ()), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (waiting && sent - first < take,
              qPrintable (QString {"the decode again went out %1 ms after the first marker's error, past the "
                                   "second's %2 ms take"}.arg (sent - first).arg (take)));
    auto const pass = find (codec.lines, passLine (41));
    auto const idle = find (codec.lines, busyLine (false));
    auto const refused = find (codec.lines, outHas ("\"decode_again_unavailable\""));
    QVERIFY2 (pass >= 0 && find (codec.lines, markerError) > pass,
              qPrintable ("a marker was taken before the early pass\n" + show (codec.lines)));
    QVERIFY2 (idle >= 0 && idle < refused, qPrintable ("the refusal came before busy false\n" + show (codec.lines)));
  }

  // The plan gate, armed with no JTTY session to open it, lets the session
  // end at the end of its input.
  void planGateOpensAtTheEnd ()
  {
    auto const mode = modeOf ("ft8");
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    Options options;
    options.environment << "WSJT_CODEC_TEST_PLAN_GATE=1";
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (mode.settings) + audio (pcm) + halt ());
    QVERIFY2 (codec.finish (), qPrintable ("the session did not end\n" + codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (count (codec.lines, outType ("decode_finished")), 1);
  }

  // jt9 saves its wisdom at its clean end in place: through a link to the
  // file, which stays a link.
  void jt9SavesItsWisdomInPlace ()
  {
    auto const wav = inSamples ("FT8/210703_133430.wav");
    QVERIFY2 (QFileInfo::exists (wav), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const target = directory.filePath ("kept_wisdom.dat");
    auto const name = directory.filePath ("jt9_wisdom.dat");
    QFile file {target};
    QVERIFY (file.open (QIODevice::WriteOnly));
    file.close ();
    QVERIFY (QFile::link (target, name));
    QProcess jt9;
    jt9.setWorkingDirectory (directory.path ());
    jt9.start (QString::fromUtf8 (JT9_EXECUTABLE), {"-8", "-a", directory.path (), "-t", directory.path (), wav});
    QVERIFY2 (jt9.waitForStarted (5000), qPrintable (jt9.errorString ()));
    QVERIFY2 (jt9.waitForFinished (waitMs ()), "jt9 did not end");
    auto const err = jt9.readAllStandardError ();
    QCOMPARE (jt9.exitStatus (), QProcess::NormalExit);
    QCOMPARE (jt9.exitCode (), 0);
    QVERIFY2 (QFileInfo {name}.isSymLink (), qPrintable ("the link became a file\n" + QString::fromUtf8 (err)));
    QVERIFY2 (QFileInfo {target}.size () > 0, qPrintable ("no wisdom in the linked file\n" + QString::fromUtf8 (err)));
  }

  // A halt during a decode: every line, exit 0, the decode thread joined.
  void haltDuringADecode_data ()
  {
    QTest::addColumn<QString> ("decode");
    QTest::addColumn<int> ("stop");
    QTest::newRow ("ft8") << QString {"ft8"} << 50;
    QTest::newRow ("ft8-mtd") << QString {"ft8-mtd"} << 49;
  }

  void haltDuringADecode ()
  {
    QFETCH (QString, decode);
    QFETCH (int, stop);
    auto const mode = modeOf (decode);
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    auto const plain = runInput (sessionHeader () + configure (mode.settings) + audio (pcm) + halt (), shared);
    QVERIFY2 (plain->clean (), qPrintable (plain->report ()));
    Codec codec {shared};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (mode.settings) + audio (pcm));
    QVERIFY2 (codec.waitForLine (passLine (stop), waitMs (), "the final's pass line"), qPrintable (codec.report ()));
    codec.write (halt ());
    auto const during = find (codec.lines, outType ("decode_finished")) < 0;
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (during, "the decode ended before the halt");
    QCOMPARE (decodeLines (codec.lines, mode.sorted), decodeLines (plain->lines, mode.sorted));
  }

  // MSK144 or JTTY entered while a period decode runs starts at once:
  // its first block (MSK144) or session line (JTTY) within a block interval
  // of its audio, its output as with no decode beside it, and the period's
  // lines as without the switch.
  void modeEnteredDuringADecode_data ()
  {
    QTest::addColumn<QString> ("decode");
    QTest::addColumn<bool> ("jtty");
    QTest::addColumn<bool> ("timed");
    QTest::newRow ("msk144") << QString {"fst4-60"} << false << true;
    QTest::newRow ("jtty") << QString {"fst4-60"} << true << true;
    // The sanitizer legs' rows: a shorter decode beside, no timing.
    QTest::newRow ("msk144 beside jt9") << QString {"jt9"} << false << false;
    QTest::newRow ("jtty beside jt9") << QString {"jt9"} << true << false;
  }

  void modeEnteredDuringADecode ()
  {
    QFETCH (QString, decode);
    QFETCH (bool, jtty);
    QFETCH (bool, timed);
    auto const mode = modeOf (decode);
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    auto const entered = jtty ? wavPcm (inSamples ("JTTY/260807_134110.wav"))
                              : period (wavPcm (inSamples ("MSK144/181211_120800.wav")), 15 * SampleRate);
    QVERIFY2 (!pcm.isEmpty () && !entered.isEmpty (), "no recording");
    auto const settings = jtty ? JttySettings : Msk144Settings;
    auto const step = jtty ? FrameSamples : BlockSamples;
    QList<QByteArray> chunks;
    for (int at = 0; at < entered.size (); at += 2 * step)
      chunks << frame (0x01u, entered.mid (at, 2 * step)) + (jtty ? QByteArray {} : marker ());
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    shared.environment << "OMP_STACKSIZE=16M";
    // Each alone first, which leaves the FFTW wisdom both measure in the directory.
    auto const alone = runInput (sessionHeader () + configure (mode.settings) + audio (pcm) + halt (), shared);
    QVERIFY2 (alone->clean (), qPrintable (alone->report ()));
    QByteArray enteredInput = sessionHeader () + configure (settings);
    for (auto const& chunk : chunks) enteredInput += chunk;
    auto const enteredAlone = runInput (enteredInput + halt (), shared);
    QVERIFY2 (enteredAlone->clean (), qPrintable (enteredAlone->report ()));

    Codec codec {shared};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (mode.settings) + audio (pcm));
    QVERIFY2 (codec.waitForLine (errHas ("pass nzhsym="), waitMs (), "the period's pass line"),
              qPrintable (codec.report ()));
    auto const switched = codec.lines.size ();
    codec.write (configure (settings));
    auto const first = codec.write (chunks.first ());
    auto const during = find (codec.lines, outType ("decode_finished")) < 0;
    QList<qint64> writes {first};
    for (int i = 1; i < chunks.size (); ++i)
      {
        codec.waitUntil (first + qint64 {1000} * i * step / SampleRate);
        writes << codec.write (chunks.at (i));
      }
    codec.write (halt ());
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));

    // The period's lines.
    QByteArrayList period, periodAlone;
    auto const label = periodEnds (alone->lines).value (0);
    for (auto const& line : decodeLines (codec.lines))
      {
        auto const event = json (line);
        if (event.value ("mode").toString () == mode.name || event.value ("period_end").toString () == label)
          period << line;
      }
    periodAlone = decodeLines (alone->lines);
    QVERIFY2 (!messagesOf (periodAlone).isEmpty (), qPrintable (alone->report ()));
    QCOMPARE (period, periodAlone);

    // The entered mode's lines; JTTY's are numbered from the stream's first sample.
    auto const offset = double (mode.samples) / SampleRate;
    auto const enteredLines = [&] (QList<Line> const& lines, double shift) {
      QList<QJsonObject> found;
      for (auto const& line : lines)
        {
          if (line.error) continue;
          auto event = json (line.text);
          auto const type = event.value ("t").toString ();
          if (jtty && ("session" == type || "jtty_update" == type))
            {
              if (event.contains ("origin")) event["origin"] = event.value ("origin").toInt () - int (shift * SampleRate);
              for (auto const key : {"start", "latest"})
                if (event.value (key).isDouble ()) event[key] = event.value (key).toDouble () - shift;
              found << event;
            }
          else if (!jtty && (("decode" == type && "MSK144" == event.value ("mode").toString ())
                             || ("decode_finished" == type && "120800" == event.value ("period_end").toString ())
                             || "msk144_trained" == type))
            {
              found << event;
            }
        }
      return found;
    };
    auto const want = enteredLines (enteredAlone->lines, 0);
    QVERIFY2 (want.size () > 1, qPrintable (enteredAlone->report ()));
    auto got = enteredLines (codec.lines, offset);
    QCOMPARE (got.size (), want.size ());
    for (int i = 0; i < got.size (); ++i)
      {
        // JTTY's times, in stream samples / 12000, shifted by the period before it.
        for (auto const key : {"start", "latest"})
          if (got[i].value (key).isDouble () && std::abs (got[i].value (key).toDouble ()
                                                          - want[i].value (key).toDouble ()) < 1e-6)
            got[i][key] = want[i].value (key);
        QCOMPARE (got[i], want[i]);
      }

    if (!timed) return;
    QVERIFY2 (during, "the period's decode ended before the switch");
    auto const answer = jtty ? find (codec.lines, outType ("session"), switched)
                             : find (codec.lines, outHas ("configure_parse_error"), switched);
    QVERIFY2 (answer >= 0, qPrintable (codec.report ()));
    auto const latency = msOf (codec.lines, answer) - first;
    qint64 worst = 0;
    if (!jtty)
      {
        int at = switched;
        for (int i = 0; i < writes.size (); ++i)
          {
            at = find (codec.lines, outHas ("configure_parse_error"), at) + 1;
            if (at <= 0) break;
            worst = std::max (worst, codec.lines.at (at - 1).ms - writes.at (i));
          }
      }
    qInfo ().noquote () << QString {"first %1 ms after its audio (bound %2 ms); worst block %3 ms; the period's "
                                    "decode_finished at %4 ms"}
      .arg (latency).arg (BlockIntervalMs).arg (worst)
      .arg (msOf (codec.lines, find (codec.lines, outType ("decode_finished"))) - first);
    QVERIFY2 (latency <= BlockIntervalMs, "the entered mode waited for the period's decode");
  }

  // JTTY entered with a fresh -a while a period decode runs, the
  // decode held before it decodes until JTTY's measuring has begun:
  // nothing is decoded in three times a run's wait for its first decode
  // without the gate, planning brackets the measured step, the wisdom is
  // saved after it, and a client meets no expired deadline; the stall and
  // the period's decodes against a run without the switch are reported.
  void jttyMeasuresBesideADecode_data ()
  {
    QTest::addColumn<bool> ("live");
    QTest::newRow ("from a file") << false;
    QTest::newRow ("live ft8") << true;
  }

  void jttyMeasuresBesideADecode ()
  {
    QFETCH (bool, live);
    auto const mode = modeOf ("ft8");
    auto const pcm = period (wavPcm (mode.wav), mode.samples);
    auto const jtty = wavPcm (inSamples ("JTTY/260807_134110.wav"));
    QVERIFY2 (!pcm.isEmpty () && !jtty.isEmpty (), "no recording");
    auto const settings = with (mode.settings, {{"live_audio", live}, {"busy_events", true}});
    auto const plain = runInput (sessionHeader () + configure (settings) + audio (pcm) + halt ());
    QVERIFY2 (plain->clean (), qPrintable (plain->report ()));
    // Without the gate, the time from the early pass's line to the first decode.
    auto const decoded = find (plain->lines, outType ("decode"));
    QVERIFY2 (decoded > find (plain->lines, passLine (41)), qPrintable (plain->report ()));
    auto const unheld = msOf (plain->lines, decoded) - msOf (plain->lines, find (plain->lines, passLine (41)));
    auto const quiet = 3 * std::max<qint64> (unheld, 100);
    Options options;
    options.environment << "WSJT_CODEC_TEST_PLAN_GATE=1";
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    auto const wisdom = codec.directory + "/jt9_wisdom.dat";
    auto const sentAt = codec.write (sessionHeader () + configure (settings) + audio (pcm));
    QVERIFY2 (codec.waitForLine (passLine (41), waitMs (), "the early pass"), qPrintable (codec.report ()));
    QVERIFY2 (codec.waitUntil (codec.now () + quiet), qPrintable (codec.report ()));
    QVERIFY2 (0 == count (codec.lines, outType ("decode")),
              qPrintable (QString {"a decode within %1 ms of the early pass, before JTTY's measuring\n"}.arg (quiet)
                          + show (codec.lines)));
    codec.write (configure (JttySettings) + audio (jtty, FrameSamples));
    // planning false comes before the measured step's updates.
    Tally closing {outHas ("\"planning\":false")};
    Tally updates {outType ("jtty_update")};
    codec.waitFor ([&] {return closing (codec.lines) > 0 || updates (codec.lines) > 0;}, 2 * waitMs (),
                   "the measured step's end");
    QVERIFY2 (codec.waitForLine (outHas ("\"planning\":false"), 2000, "planning false"), qPrintable (codec.report ()));
    auto const saved = codec.waitFor ([&] {return QFileInfo::exists (wisdom);}, 5000, "the wisdom");
    codec.write (halt ());
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.exitCode == 0 && codec.exitStatus == QProcess::NormalExit, qPrintable (codec.report ()));
    auto const opened = find (codec.lines, outHas ("\"planning\":true"));
    auto const closed = find (codec.lines, outHas ("\"planning\":false"));
    QVERIFY2 (opened >= 0 && closed > opened, qPrintable (codec.report ()));
    // The gate held the period's first pass at its start until JTTY
    // began measuring: the pass's next line follows planning true. Live, a
    // measurement longer than the period has the later passes skipped as
    // busy, and the held pass decodes only if it runs before FT8's 13.4 s
    // wall-clock cut-off.
    auto const early = std::max (find (codec.lines, passLine (41)), find (codec.lines, [] (Line const& line) {
      return outType ("busy") (line) && line.text.contains ("\"nzhsym\":41");
    }));
    auto const next = find (codec.lines, [] (Line const& line) {
      static QByteArrayList const types {"decode", "pass_progress", "pass_skipped", "decode_finished", "busy"};
      return !line.error && types.contains (typeOf (line.text));
    }, early + 1);
    QVERIFY2 (next > opened,
              qPrintable ("the early pass went on before JTTY's measuring began\n" + show (codec.lines)));
    QVERIFY2 (saved, "no wisdom saved after the measured step");
    auto const expired = liveness (codec.lines, {{sentAt, labelOf (mode.settings.value ("utc").toString ())}},
                                   {"FT8", 15, live}, codec.lines.last ().ms);
    QVERIFY2 (expired.isEmpty (), qPrintable (show (expired) + "\n" + show (codec.lines)));
    auto const finished = find (codec.lines, outType ("decode_finished"));
    qInfo ().noquote () << QString {"the first decode %1 ms after the early pass without the gate; planning %2 ms; "
                                    "the period's decode_finished %3 ms after planning began; decodes %4 with the "
                                    "switch, %5 without"}
      .arg (unheld).arg (msOf (codec.lines, closed) - msOf (codec.lines, opened))
      .arg (finished >= 0 ? msOf (codec.lines, finished) - msOf (codec.lines, opened) : -1)
      .arg (messagesOf (decodeLines (codec.lines)).size ()).arg (messagesOf (decodeLines (plain->lines)).size ());
  }

  // busy_events: busy at every pass start and deferral with the pass's
  // fields, true across back-to-back passes and from an early pass to the
  // final it defers, false once idle and at the end; pass_progress inside
  // busy passes at most once a second; planning around a measured JTTY step;
  // nothing without the key.
  void busyEvents_data ()
  {
    QTest::addColumn<QString> ("scenario");
    for (auto const name : {"passes", "again", "deferral", "discontinuity", "cancelled", "replaced", "memory",
                            "key mid-session", "after a drained final", "progress", "generation", "planning",
                            "planning warm", "without the key"})
      QTest::newRow (name) << QString {name};
  }

  void busyEvents ()
  {
    QFETCH (QString, scenario);
    auto const ft8 = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    QVERIFY2 (!ft8.isEmpty (), "no recording");
    QJsonObject const on {{"busy_events", true}};
    QJsonObject const standard {{"mode", "FT8"}, {"depth", 3}, {"utc", utcOf (0)}};
    QJsonObject const live {{"mode", "FT8"}, {"depth", 3}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                            {"ft8_cycles", 1}, {"live_audio", true}, {"utc", utcOf (0)}};
    auto const p0 = labelOf (utcOf (0));
    auto const p1 = labelOf (utcOf (1));
    auto const p2 = labelOf (utcOf (2));
    Options options;
    std::unique_ptr<Codec> run;
    QStringList want;
    QStringList decided;   // the stream's decisions, as the GUI's scheduler makes them
    QStringList const deferral {"pass 41 F", "skip 46 early busy", "defer 50"};

    // A start 1 early pass held, the rest of its period sent while it runs:
    // the final defers and, with no further input, drains.
    auto const deferred = [&] (QJsonObject const& first, QByteArray const& after) {
      options.environment << "WSJT_CODEC_TEST_HOLD_PASS=41:1500";
      run.reset (new Codec {options});
      run->write (sessionHeader () + configure (with (with (live, {{"ft8_decoder_start", 1}}), first))
                  + frame (0x01u, ft8.left (2)));
      run->waitUntil (Ft8PeriodSamples / 12 + DueMarginMs);
      run->write (frame (0x01u, samples (ft8, 1, 41 * HsymSamples))
                  + frame (0x01u, samples (ft8, 41 * HsymSamples, Ft8PeriodSamples)) + after);
      run->waitForLine (outType ("decode_finished"), waitMs (), "the final");
      run->write (marker ());
      run->waitForLine (outHas ("configure_parse_error"), waitMs (), "the marker");
      run->finish ();
    };
    // Start 0 or 2 behind: an anchor at the stop, the next periods during
    // it, each in two frames split at the hsym given.
    auto const behind = [&] (int start, int behindBy, int split) {
      auto const stop = 0 == start ? 49 : 48;
      run.reset (new Codec {options});
      run->write (sessionHeader () + configure (with (with (live, {{"ft8_decoder_start", start}}), on))
                  + frame (0x01u, ft8.left (2)));
      run->waitUntil ((behindBy + 1) * Ft8PeriodSamples / 12 + DueMarginMs);
      if (0 == start)
        {
          run->write (frame (0x01u, samples (ft8, 1, 41 * HsymSamples)) + marker ());
          run->waitForLine (outHas ("configure_parse_error"), waitMs (), "the gate");
        }
      run->write (frame (0x01u, samples (ft8, 0 == start ? 41 * HsymSamples : 1, stop * HsymSamples)));
      run->waitForLine (passLine (stop, true), waitMs (), "the anchor");
      QByteArray burst = frame (0x01u, samples (ft8, stop * HsymSamples, Ft8PeriodSamples));
      for (int p = 1; p <= behindBy; ++p)
        burst += configure ({{"utc", utcOf (p)}}) + frame (0x01u, ft8.left (2 * split * HsymSamples))
          + frame (0x01u, ft8.mid (2 * split * HsymSamples));
      run->write (burst);
      run->waitForCount (outType ("decode_finished"), behindBy + 1, waitMs (), "the periods");
      run->write (marker ());
      run->waitForLine (outHas ("configure_parse_error"), waitMs (), "the marker");
      run->finish ();
    };

    if ("passes" == scenario)
      {
        QByteArray input = sessionHeader () + configure (with (standard, on));
        for (int p = 0; p < 3; ++p)
          {
            input += (p > 0 ? configure ({{"utc", utcOf (p)}}) : QByteArray {}) + audio (ft8);
            for (auto const n : {41, 47, 50}) want << QString {"%1 %2"}.arg (n).arg (labelOf (utcOf (p)));
          }
        run = runInput (input + halt ());
        want << "false";
        for (int p = 0; p < 3; ++p) decided << "pass 41 F" << "pass 47 F" << "pass 50 F";
      }
    else if ("again" == scenario)
      {
        run = runInput (sessionHeader () + configure (with (standard, on)) + audio (ft8)
                        + configure ({{"nagain_flag", true}}) + halt ());
        want = QStringList {"41 " + p0, "47 " + p0, "50 " + p0, "50 " + p0 + " again", "false"};
        decided = QStringList {"pass 41 F", "pass 47 F", "pass 50 F", "pass 50 F again"};
      }
    else if ("deferral" == scenario)
      {
        deferred (on, {});
        want = QStringList {"41 " + p0, "50 " + p0 + " deferred", "50 " + p0, "false"};
        decided = deferral + QStringList {"pass 50 T"};
      }
    else if ("discontinuity" == scenario)
      {
        run = runInput (sessionHeader () + configure (with (standard, on)) + audio (ft8.left (2 * 48 * HsymSamples))
                        + control ({{"t", "discontinuity"}}) + halt ());
        QCOMPARE (count (run->lines, outType ("decode_finished")), 0);
        want = QStringList {"41 " + p0, "47 " + p0, "false"};
        decided = QStringList {"pass 41 F", "pass 47 F"};
      }
    else if ("cancelled" == scenario)
      {
        deferred (on, configure ({{"depth", 2}}));
        QVERIFY2 (count (run->lines, outHas ("\"cancelled\"")) == 1, qPrintable (run->report ()));
        want = QStringList {"41 " + p0, "50 " + p0 + " deferred", "false"};
        decided = deferral + QStringList {"skip 50 final cancelled"};
      }
    else if ("replaced" == scenario)
      {
        behind (0, 2, 49);
        QVERIFY2 (count (run->lines, outHas ("\"replaced\"")) == 1, qPrintable (run->report ()));
        // Idle after the gate's early pass.
        want = QStringList {"41 " + p0, "false", "49 " + p0, "49 " + p1 + " deferred", "49 " + p2 + " deferred",
                            "49 " + p2, "false"};
        // A replaced final's skip and its successor's deferral, in either order.
        decided = QStringList {"pass 41 F", "pass 49 T", "skip 41 early busy", "defer 49", "skip 41 early busy",
                               "defer 49", "skip 49 final replaced", "pass 49 T"};
      }
    else if ("memory" == scenario)
      {
        // Period 0's final, split at its trigger, keeps 331,776 bytes; period
        // 1's, split later, more than the limit.
        options.environment << "WSJT_CODEC_TEST_ALLOC_LIMIT=333000";
        behind (2, 1, 49);
        QVERIFY2 (count (run->lines, outHas ("\"memory\"")) == 1, qPrintable (run->report ()));
        want = QStringList {"48 " + p0, "false"};
        decided = QStringList {"pass 48 T", "skip 48 final memory"};
      }
    else if ("key mid-session" == scenario)
      {
        deferred ({}, configure (on));
        want = QStringList {"50 " + p0 + " deferred", "50 " + p0, "false"};
        decided = deferral + QStringList {"pass 50 T"};
      }
    else if ("after a drained final" == scenario)
      {
        // The next period, sent as the drained final starts, is queued when
        // that final ends: busy stays true until its first pass.
        options.environment << "WSJT_CODEC_TEST_HOLD_PASS=41:1500";
        run.reset (new Codec {options});
        run->write (sessionHeader () + configure (with (with (live, {{"ft8_decoder_start", 1}}), on))
                    + frame (0x01u, ft8.left (2)));
        run->waitUntil (Ft8PeriodSamples / 12 + DueMarginMs);
        run->write (frame (0x01u, samples (ft8, 1, 41 * HsymSamples))
                    + frame (0x01u, samples (ft8, 41 * HsymSamples, Ft8PeriodSamples)));
        auto const drained = [&p0] (Line const& line) {
          auto const event = json (line.text);
          return busyLine (true) (line) && 50 == event.value ("nzhsym").toInt ()
            && p0 == event.value ("period_end").toString () && !event.value ("deferred").toBool ();
        };
        QVERIFY2 (run->waitForLine (drained, waitMs (), "the drained final"), qPrintable (run->report ()));
        run->write (configure ({{"utc", utcOf (1)}}) + frame (0x01u, ft8));
        QVERIFY2 (run->waitForCount (outType ("decode_finished"), 2, waitMs (), "period 1"), qPrintable (run->report ()));
        run->write (marker ());
        run->waitForLine (outHas ("configure_parse_error"), waitMs (), "the marker");
        run->finish ();
        QVERIFY2 (run->clean (), qPrintable (run->report ()));
        auto const seen = busySequence (run->lines, "FT8");
        auto const at = seen.indexOf ("50 " + p0);
        QVERIFY2 (at >= 0 && at + 1 < seen.size () && seen.at (at + 1).contains (p1),
                  qPrintable ("busy lines: " + seen.join (" | ") + "\n" + show (run->lines)));
        return;
      }
    else if ("progress" == scenario)
      {
        auto const fst4 = modeOf ("fst4");
        auto const mtd = modeOf ("ft8-mtd");
        auto const fst4Pcm = period (wavPcm (fst4.wav), fst4.samples);
        QVERIFY2 (!fst4Pcm.isEmpty (), "no recording");
        run = runInput (sessionHeader () + configure (with (fst4.settings, on)) + audio (fst4Pcm)
                        + configure (mtd.settings) + audio (ft8) + configure ({{"utc", utcOf (1)}}) + audio (ft8)
                        + halt ());
        QVERIFY2 (run->clean (), qPrintable (run->report ()));
        QJsonObject running;
        qint64 last = -1;
        int found = 0;
        for (auto const& line : run->lines)
          {
            if (outType ("busy") (line))
              {
                auto const event = json (line.text);
                running = event.value ("busy").toBool () && !event.value ("deferred").toBool () ? event : QJsonObject {};
              }
            if (!outType ("pass_progress") (line)) continue;
            ++found;
            auto const event = json (line.text);
            QVERIFY2 (!running.isEmpty (), qPrintable ("pass_progress while not busy\n" + show (run->lines)));
            for (auto const key : {"mode", "period_end", "nzhsym"})
              QVERIFY2 (event.value (key) == running.value (key), qPrintable (QString::fromUtf8 (line.text)));
            QVERIFY2 (last < 0 || line.ms - last >= 900, qPrintable ("two pass_progress lines within a second\n"
                                                                     + show (run->lines)));
            last = line.ms;
          }
        QVERIFY2 (found > 0, qPrintable (show (run->lines)));
        return;
      }
    else if ("generation" == scenario)
      {
        // Each pass reports progress afresh: FST4, which reports at its
        // attempt's end, three passes more than a second apart.
        auto const fst4 = modeOf ("fst4");
        auto const fst4Pcm = period (wavPcm (fst4.wav), fst4.samples);
        QVERIFY2 (!fst4Pcm.isEmpty (), "no recording");
        run.reset (new Codec {options});
        run->write (sessionHeader () + configure (with (fst4.settings, on)));
        QStringList want;
        for (int p = 0; p < 3; ++p)
          {
            auto const utc = utcOf (p, {0, 15, 0}, 60);
            run->write ((p > 0 ? configure ({{"utc", utc}}) : QByteArray {}) + audio (fst4Pcm));
            QVERIFY2 (run->waitForCount (outType ("decode_finished"), p + 1, waitMs (), "the pass"),
                      qPrintable (run->report ()));
            run->waitUntil (run->now () + 1300);
            want << labelOf (utc);
          }
        run->write (halt ());
        run->finish ();
        QVERIFY2 (run->clean (), qPrintable (run->report ()));
        QStringList seen;
        for (auto const& line : run->lines)
          if (outType ("pass_progress") (line)) seen << json (line.text).value ("period_end").toString ();
        QVERIFY2 (seen == want, qPrintable ("pass_progress for " + seen.join (' ') + "\n" + show (run->lines)));
        return;
      }
    else if ("planning warm" == scenario)
      {
        // A second session in the same -a finds every plan in the saved wisdom.
        auto const jtty = wavPcm (inSamples ("JTTY/260807_134110.wav"));
        QVERIFY2 (!jtty.isEmpty (), "no recording");
        QTemporaryDir directory;
        QVERIFY (directory.isValid ());
        options.directory = directory.path ();
        auto const input = sessionHeader () + configure (with (JttySettings, on)) + audio (jtty, FrameSamples) + halt ();
        auto const cold = runInput (input, options);
        QVERIFY2 (cold->clean (), qPrintable (cold->report ()));
        auto const warm = runInput (input, options);
        QVERIFY2 (warm->clean (), qPrintable (warm->report ()));
        QVERIFY2 (count (cold->lines, outHas ("\"planning\":true")) > 0, qPrintable (show (cold->lines)));
        QVERIFY2 (count (warm->lines, outType ("planning")) == 0, qPrintable (show (warm->lines)));
        QVERIFY2 (count (warm->lines, outType ("jtty_update")) > 0, qPrintable (show (warm->lines)));
        return;
      }
    else if ("planning" == scenario)
      {
        auto const jtty = wavPcm (inSamples ("JTTY/260807_134110.wav"));
        QVERIFY2 (!jtty.isEmpty (), "no recording");
        run = runInput (sessionHeader () + configure (with (JttySettings, on)) + audio (jtty, FrameSamples) + halt ());
        QVERIFY2 (run->clean (), qPrintable (run->report ()));
        QStringList seen;
        for (auto const& line : run->lines)
          if (outType ("planning") (line)) seen << (json (line.text).value ("planning").toBool () ? "true" : "false");
        QVERIFY2 (!seen.isEmpty () && seen.size () % 2 == 0, qPrintable (show (run->lines)));
        for (int i = 0; i < seen.size (); ++i) QCOMPARE (seen.at (i), QString {i % 2 ? "false" : "true"});
        QVERIFY2 (count (run->lines, outType ("jtty_update")) > 0, qPrintable (show (run->lines)));
        return;
      }
    else if ("without the key" == scenario)
      {
        auto const jtty = wavPcm (inSamples ("JTTY/260807_134110.wav"));
        QVERIFY2 (!jtty.isEmpty (), "no recording");
        run = runInput (sessionHeader () + configure (standard) + audio (ft8) + configure ({{"nagain_flag", true}})
                        + configure (JttySettings) + audio (jtty, FrameSamples) + halt ());
        QVERIFY2 (run->clean (), qPrintable (run->report ()));
        QVERIFY2 (count (run->lines, outType ("decode")) > 0 && count (run->lines, outType ("jtty_update")) > 0,
                  qPrintable (show (run->lines)));
        for (auto const type : {"busy", "pass_progress", "planning"})
          QVERIFY2 (count (run->lines, outType (type)) == 0, qPrintable (show (run->lines)));
        return;
      }
    QVERIFY2 (run && run->clean (), qPrintable (run ? run->report () : QString {}));
    auto made = decisions (run->lines);
    if ("replaced" == scenario && made.size () > 6) std::sort (made.begin () + 5, made.begin () + 7);
    QCOMPARE (made, decided);
    auto const seen = busySequence (run->lines, "FT8");
    QVERIFY2 (seen == want, qPrintable ("busy lines: " + seen.join (" | ") + "\nwanted:     " + want.join (" | ")
                                        + "\n" + show (run->lines)));
    // The last busy line, false, follows every other decode line.
    auto const lastFalse = findLast (run->lines, busyLine (false));
    for (int i = lastFalse + 1; i < run->lines.size (); ++i)
      {
        auto const type = typeOf (run->lines.at (i).text);
        QVERIFY2 (run->lines.at (i).error || ("decode" != type && "decode_finished" != type && "pass_skipped" != type),
                  qPrintable (show (run->lines)));
      }
  }

  // A client's deadlines: a decode held stops pass_progress
  // while replies go on, and the decode deadline fires; the decode thread
  // held between operations with busy false lets the period deadline fire; a
  // fast feeder meets no deadline; a long reply during a multithreaded final
  // holds the final's lines back, drops pass_progress rather than queuing it,
  // and meets no deadline.
  void clientDeadlines_data ()
  {
    QTest::addColumn<QString> ("scenario");
    for (auto const name : {"held decode", "held between operations", "fast feeder", "long reply", "long hold"})
      QTest::newRow (name) << QString {name};
  }

  void clientDeadlines ()
  {
    QFETCH (QString, scenario);
    auto const ft4 = modeOf ("ft4");
    auto const ft4Pcm = period (wavPcm (ft4.wav), ft4.samples);
    auto const ft8 = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    auto const engine = period (wavPcm (fixture ("ft8-engine.wav")), Ft8PeriodSamples);
    QVERIFY2 (!ft4Pcm.isEmpty () && !ft8.isEmpty () && !engine.isEmpty (), "no recording");
    QJsonObject const on {{"busy_events", true}};
    auto const ft4Live = with (with (ft4.settings, {{"live_audio", true}}), on);
    Options options;
    options.dropPcm = true;
    QList<SentPeriod> sent;

    if ("held decode" == scenario)
      {
        options.environment << "WSJT_CODEC_TEST_HOLD_PASS=21:8000";
        Codec codec {options};
        QVERIFY2 (codec.started, qPrintable (codec.failure));
        auto const at = codec.write (sessionHeader () + configure (ft4Live) + audio (ft4Pcm)
                                     + configure ({{"utc", "00:00:07"}}) + audio (ft4Pcm));
        sent << SentPeriod {at, "000000"} << SentPeriod {at, "000007"};
        Tally busySecond {[] (Line const& line) {
          return busyLine (true) (line) && json (line.text).value ("period_end").toString () == "000007";
        }};
        Tally finished {outType ("decode_finished")};
        auto const second = [&] {
          busySecond (codec.lines);
          return busySecond.first ();
        };
        codec.waitFor ([&] {return second () >= 0 || finished (codec.lines) >= 2;}, 3 * waitMs (),
                       "the second period's pass");
        QVERIFY2 (second () >= 0, qPrintable ("no busy line for the second period\n" + codec.report ()));
        auto const start = msOf (codec.lines, second ());
        qint64 id = 0;
        while (finished (codec.lines) < 2 && codec.running () && codec.now () - start < waitMs ())
          {
            if (codec.now () >= start + 1000 * (id + 1) && id < 5) codec.request (++id, Ft8Render);
            codec.pump ();
          }
        QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
        QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
        auto const ended = find (codec.lines, outType ("decode_finished"), second ());
        auto const end = msOf (codec.lines, ended);
        // Lines read together share a time, so the hold begins at the held pass's busy line by order.
        for (int i = second () + 1; i < codec.lines.size (); ++i)
          QVERIFY2 (!outType ("pass_progress") (codec.lines.at (i)) || codec.lines.at (i).ms >= start + 7000,
                    qPrintable ("pass_progress during the hold\n" + show (codec.lines)));
        auto const replies = repliesOf (codec);
        QVERIFY2 (replies.size () >= 4, qPrintable (show (codec.lines)));
        for (auto const& reply : replies)
          QVERIFY2 (reply.terminal == "rendered" && reply.lastIndex < ended && reply.last - reply.sent < 1000,
                    qPrintable ("a reply waited for the held decode\n" + show (codec.lines)));
        auto const expired = liveness (codec.lines, sent, {"FT4", 7.5, true}, codec.lines.last ().ms);
        QVERIFY2 (1 == expired.size () && "decode" == expired.first ().what && expired.first ().at > start + 5900
                    && expired.first ().at < end,
                  qPrintable ("expired: " + show (expired) + "\n" + show (codec.lines)));
      }
    else if ("held between operations" == scenario)
      {
        options.environment << "WSJT_CODEC_TEST_HOLD_TAKE_MS=10000";
        Codec codec {options};
        QVERIFY2 (codec.started, qPrintable (codec.failure));
        sent << SentPeriod {codec.write (sessionHeader () + configure (ft4Live) + audio (ft4Pcm)), "000000"};
        QVERIFY2 (codec.waitForLine (outType ("decode_finished"), 3 * waitMs (), "the first period"),
                  qPrintable (codec.report ()));
        QVERIFY2 (codec.waitForLine (busyLine (false), 5000, "busy false"), qPrintable (codec.report ()));
        auto const at = codec.write (configure ({{"utc", "00:00:07"}}) + audio (ft4Pcm));
        sent << SentPeriod {at, "000007"};
        QVERIFY2 (codec.waitForCount (outType ("decode_finished"), 2, 3 * waitMs (), "the second period"),
                  qPrintable (codec.report ()));
        QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
        QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
        auto const expired = liveness (codec.lines, sent, {"FT4", 7.5, true}, codec.lines.last ().ms);
        QVERIFY2 (1 == expired.size () && "period 000007" == expired.first ().what
                    && expired.first ().at >= at + 6000 && expired.first ().at < at + 7000,
                  qPrintable ("expired: " + show (expired) + "\n" + show (codec.lines)));
      }
    else if ("fast feeder" == scenario)
      {
        QJsonObject const settings {{"mode", "FT8"}, {"depth", 3}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                                    {"ft8_decoder_start", 0}, {"live_audio", true}, {"busy_events", true},
                                    {"utc", utcOf (0)}};
        QByteArray input = sessionHeader () + configure (settings);
        for (int p = 0; p < 12; ++p)
          input += (p > 0 ? configure ({{"utc", utcOf (p)}}) : QByteArray {}) + audio (p % 2 ? engine : ft8);
        Codec codec {options};
        QVERIFY2 (codec.started, qPrintable (codec.failure));
        auto const at = codec.write (input);
        for (int p = 0; p < 12; ++p) sent << SentPeriod {at, labelOf (utcOf (p))};
        QVERIFY2 (codec.waitForCount (outType ("decode_finished"), 12, 20 * waitMs (), "the periods"),
                  qPrintable (codec.report ()));
        QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
        QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
        QVERIFY2 (count (codec.lines, outType ("busy")) > 0, qPrintable (show (codec.lines)));
        auto const expired = liveness (codec.lines, sent, {"FT8", 15, true}, codec.lines.last ().ms);
        QVERIFY2 (expired.isEmpty (), qPrintable ("expired: " + show (expired) + "\n" + show (codec.lines)));
      }
    else if ("long hold" == scenario)
      {
        // The client reads nothing for 4 s after a long render, so the
        // reply's hold outlasts the final, whose progress reports in the
        // meantime are dropped rather than queued behind the reply.
        auto const mtd = modeOf ("ft8-mtd");
        Codec codec {options};
        QVERIFY2 (codec.started, qPrintable (codec.failure));
        sent << SentPeriod {codec.write (sessionHeader () + configure (with (mtd.settings, on))
                                         + audio (ft8.left (2 * 49 * HsymSamples))), "133430"};
        QVERIFY2 (codec.waitForLine (passLine (49, true), waitMs (), "the final"), qPrintable (codec.report ()));
        codec.request (1, {{"mode", "TUNE"}, {"seconds", 600}, {"freq", 1500}, {"rate", 48000}});
        codec.stall (4000);
        QVERIFY2 (codec.waitFor ([&] {return codec.answered (1);}, waitMs (), "the reply"), qPrintable (codec.report ()));
        codec.write (audio (ft8.mid (2 * 49 * HsymSamples)) + halt ());
        QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
        QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
        auto const reply = repliesOf (codec).value (1);
        auto const finished = find (codec.lines, outType ("decode_finished"));
        QVERIFY2 (reply.terminal == "rendered" && finished > reply.lastIndex,
                  qPrintable ("the final's lines did not wait behind the reply\n" + show (codec.lines)));
        int queued = 0;
        for (int i = reply.lastIndex + 1; i < finished; ++i) queued += outType ("pass_progress") (codec.lines.at (i));
        QVERIFY2 (0 == queued, qPrintable ("pass_progress queued behind the reply\n" + show (codec.lines)));
        auto const expired = liveness (codec.lines, sent, {"FT8", 15, false}, codec.lines.last ().ms);
        QVERIFY2 (expired.isEmpty (), qPrintable ("expired: " + show (expired) + "\n" + show (codec.lines)));
      }
    else if ("long reply" == scenario)
      {
        auto const mtd = modeOf ("ft8-mtd");
        Codec codec {options};
        QVERIFY2 (codec.started, qPrintable (codec.failure));
        sent << SentPeriod {codec.write (sessionHeader () + configure (with (mtd.settings, on))
                                         + audio (ft8.left (2 * 49 * HsymSamples))), "133430"};
        QVERIFY2 (codec.waitForLine (passLine (49, true), waitMs (), "the final"), qPrintable (codec.report ()));
        codec.request (1, {{"mode", "TUNE"}, {"seconds", 600}, {"freq", 1500}, {"rate", 48000}});
        auto const during = find (codec.lines, outType ("decode_finished")) < 0;
        codec.write (audio (ft8.mid (2 * 49 * HsymSamples)) + halt ());
        QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
        QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
        QVERIFY2 (during, "the final ended before the render was sent");
        auto const reply = repliesOf (codec).value (1);
        auto const finished = find (codec.lines, outType ("decode_finished"));
        QVERIFY2 (reply.terminal == "rendered" && finished > reply.lastIndex,
                  qPrintable ("the final's lines did not wait behind the reply\n" + show (codec.lines)));
        int soon = 0;
        for (int i = reply.lastIndex + 1; i < codec.lines.size (); ++i)
          soon += outType ("pass_progress") (codec.lines.at (i)) && codec.lines.at (i).ms < reply.last + 1000;
        QVERIFY2 (soon <= 1, qPrintable ("pass_progress queued behind the reply\n" + show (codec.lines)));
        auto const expired = liveness (codec.lines, sent, {"FT8", 15, false}, codec.lines.last ().ms);
        QVERIFY2 (expired.isEmpty (), qPrintable ("expired: " + show (expired) + "\n" + show (codec.lines)));
        QVERIFY2 (count (codec.lines, outType ("busy")) > 0, qPrintable (show (codec.lines)));
      }
  }

  // A client that ignores each new event sees what it saw without busy_events.
  void clientsIgnoringTheEventsStillWork_data ()
  {
    QTest::addColumn<QString> ("event");
    for (auto const name : {"busy", "pass_progress", "planning"}) QTest::newRow (name) << QString {name};
  }

  void clientsIgnoringTheEventsStillWork ()
  {
    QFETCH (QString, event);
    auto const ft8 = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    auto const fst4 = modeOf ("fst4");
    auto const fst4Pcm = period (wavPcm (fst4.wav), fst4.samples);
    auto const jtty = wavPcm (inSamples ("JTTY/260807_134110.wav"));
    QVERIFY2 (!ft8.isEmpty () && !fst4Pcm.isEmpty () && !jtty.isEmpty (), "no recording");
    auto const input = [&] (QJsonObject const& keys) {
      if ("planning" == event) return sessionHeader () + configure (with (JttySettings, keys)) + audio (jtty, FrameSamples)
                                 + halt ();
      auto bytes = sessionHeader ();
      if ("pass_progress" == event) bytes += configure (with (fst4.settings, keys)) + audio (fst4Pcm);
      return bytes + configure (with (modeOf ("ft8").settings, keys)) + audio (ft8) + configure ({{"utc", utcOf (1)}})
        + audio (ft8) + configure ({{"nagain_flag", true}}) + halt ();
    };
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    // JTTY measures its plans only in a fresh -a, so the keyed session goes
    // first there; both then plan alike.
    std::unique_ptr<Codec> without;
    std::unique_ptr<Codec> withKey;
    if ("planning" == event) withKey = runInput (input ({{"busy_events", true}}), shared);
    without = runInput (input ({}), shared);
    if ("planning" != event) withKey = runInput (input ({{"busy_events", true}}), shared);
    QVERIFY2 (without->clean (), qPrintable (without->report ()));
    QVERIFY2 (withKey->clean (), qPrintable (withKey->report ()));
    QVERIFY2 (count (withKey->lines, outType (event.toUtf8 ())) > 0, qPrintable (show (withKey->lines)));
    auto const known = [] (QList<Line> const& lines) {
      QByteArrayList kept;
      for (auto const& line : lines)
        {
          auto const type = typeOf (line.text);
          if (!line.error && "busy" != type && "pass_progress" != type && "planning" != type) kept << line.text;
        }
      return kept;
    };
    QCOMPARE (known (withKey->lines), known (without->lines));
  }

  // A clear_average sent in MSK144 waits for the next period decode:
  // JT65's after the interlude takes it, as when it is sent there.
  void clearAverageSentInMsk144WaitsForJt65 ()
  {
    QList<QByteArray> periods;
    for (int n = 1; n <= 6; ++n)
      {
        periods << period (wavPcm (inSamples (QString {"JT65/JT65B/000000_000%1.wav"}.arg (n))), 60 * SampleRate);
        QVERIFY2 (!periods.last ().isEmpty (), "no recording");
      }
    QJsonObject const keys {{"mode", "JT65"}, {"trperiod", 60}, {"submode", 1}, {"rxfreq", 1700}, {"ntol", 20},
                            {"depth", 19}};
    // clearIn: "msk144", "jt65" or none.
    auto const session = [&] (QString const& clearIn) {
      auto input = sessionHeader ();
      for (int n = 1; n <= 6; ++n)
        {
          if (4 == n)
            {
              QJsonObject interlude {{"mode", "MSK144"}, {"trperiod", 15}, {"utc", "00:03:30"}};
              if ("msk144" == clearIn) interlude.insert ("clear_average", true);
              input += configure (interlude) + frame (0x01u, QByteArray (2 * 15 * SampleRate, '\0'));
            }
          auto frameKeys = 1 == n || 4 == n ? keys : QJsonObject {};
          if (4 == n && "jt65" == clearIn) frameKeys.insert ("clear_average", true);
          frameKeys.insert ("nutc", n);
          input += configure (frameKeys) + audio (periods.at (n - 1));
        }
      auto const run = runInput (input + halt ());
      QList<int> decodes {0};
      for (auto const& line : run->lines)
        {
          if (outType ("decode") (line) && "JT65" == json (line.text).value ("mode").toString ()) ++decodes.last ();
          if (outType ("decode_finished") (line) && "000330" != json (line.text).value ("period_end").toString ())
            decodes << 0;
        }
      decodes.removeLast ();
      return qMakePair (run->clean (), decodes);
    };
    auto const none = session ({});
    auto const there = session ("jt65");
    auto const before = session ("msk144");
    QVERIFY (none.first && there.first && before.first);
    qInfo () << "decodes per JT65 period: none" << none.second << "sent in JT65" << there.second << "sent in MSK144"
             << before.second;
    QVERIFY2 (none.second != there.second, "the clear sent with the JT65 period changes nothing");
    QCOMPARE (before.second, there.second);
  }

  // A snapshot that cannot be allocated (WSJT_CODEC_TEST_ALLOC_LIMIT):
  // one pass_skipped "memory" for a pass the decision would run or defer, a
  // final's decode_finished, held decode-agains run after a final's skip and
  // not after an early pass's, the scheduler told the publication failed or
  // cancelled, and the session goes on.
  void snapshotAllocationFails_data ()
  {
    QTest::addColumn<QString> ("scenario");
    for (auto const name : {"standard", "multithreaded", "defer", "replace", "held again"})
      QTest::newRow (name) << QString {name};
  }

  void snapshotAllocationFails ()
  {
    QFETCH (QString, scenario);
    auto const ft8 = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    QVERIFY2 (!ft8.isEmpty (), "no recording");
    QJsonObject const live {{"mode", "FT8"}, {"depth", 3}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                            {"ft8_cycles", 1}, {"live_audio", true}, {"utc", utcOf (0)}};
    Options options;
    QStringList wantDecisions;
    QStringList wantSkips;
    QStringList wantEnds;
    std::unique_ptr<Codec> run;
    auto const ends = [] (int n) {
      QStringList labels;
      for (int p = 0; p < n; ++p) labels << labelOf (utcOf (p));
      return labels;
    };

    if ("standard" == scenario)
      {
        options.environment << "WSJT_CODEC_TEST_ALLOC_LIMIT=1";
        run = runInput (sessionHeader () + configure ({{"mode", "FT8"}, {"depth", 3}, {"utc", utcOf (0)}}) + audio (ft8)
                        + configure ({{"utc", utcOf (1)}}) + audio (ft8) + marker () + halt (), options);
        for (int p = 0; p < 2; ++p)
          {
            auto const label = labelOf (utcOf (p));
            wantDecisions << "skip 41 early memory" << "skip 47 early memory" << "skip 50 final memory";
            wantSkips << label + " 41 early memory" << label + " 47 early memory" << label + " 50 final memory";
          }
        wantEnds = ends (2);
      }
    else if ("multithreaded" == scenario)
      {
        // Nothing runs, so every trigger finds the decoder free; the GUI's
        // scheduler, told each publication failed, gives the decisions.
        options.environment << "WSJT_CODEC_TEST_ALLOC_LIMIT=1";
        QByteArray input = sessionHeader () + configure (with (live, {{"ft8_decoder_start", 1}}));
        Ft8MtdDecodeScheduler scheduler;
        for (int p = 0; p < 4; ++p)
          {
            input += (p > 0 ? configure ({{"utc", utcOf (p)}}) : QByteArray {}) + audio (ft8);
            auto const label = labelOf (utcOf (p));
            for (auto const& trigger : {qMakePair (Stage::EarlyOne, 41), qMakePair (Stage::EarlyTwo, 46),
                                        qMakePair (Stage::Final, 50)})
              {
                auto const stage = Stage::Final == trigger.first ? "final" : "early";
                auto const decision = scheduler.request (trigger.first, 1000 + p, 2, false, false);
                auto const reason = Action::SkipEarly == decision.action ? "backoff" : "memory";
                if (Action::Publish == decision.action) scheduler.publicationFailed (trigger.first, 1000 + p);
                wantDecisions << QString {"skip %1 %2 %3"}.arg (trigger.second).arg (stage, reason);
                wantSkips << QString {"%1 %2 %3 %4"}.arg (label).arg (trigger.second).arg (stage, reason);
              }
          }
        run = runInput (input + marker () + halt (), options);
        wantEnds = ends (4);
      }
    else if ("defer" == scenario || "replace" == scenario)
      {
        // Start 2: only the final, at 48. A final split at its trigger keeps
        // 331,776 bytes, one split later more. Deferring, period 1's (split
        // later) never fits the limit; replacing, period 1's fits beside
        // period 0's and period 2's (split later) beside period 1's does not.
        auto const replace = "replace" == scenario;
        options.environment << QString {"WSJT_CODEC_TEST_ALLOC_LIMIT=%1"}.arg (replace ? 665000 : 333000);
        auto const behindBy = replace ? 2 : 1;
        run.reset (new Codec {options});
        QVERIFY2 (run->started, qPrintable (run->failure));
        run->write (sessionHeader () + configure (with (live, {{"ft8_decoder_start", 2}})) + frame (0x01u, ft8.left (2)));
        run->waitUntil ((behindBy + 1) * Ft8PeriodSamples / 12 + DueMarginMs);
        run->write (frame (0x01u, samples (ft8, 1, 48 * HsymSamples)));
        QVERIFY2 (run->waitForLine (passLine (48, true), waitMs (), "the anchor"), qPrintable (run->report ()));
        QByteArray burst = frame (0x01u, samples (ft8, 48 * HsymSamples, Ft8PeriodSamples));
        for (int p = 1; p <= behindBy; ++p)
          {
            auto const split = (replace && 1 == p ? 48 : 49) * HsymSamples;
            burst += configure ({{"utc", utcOf (p)}}) + frame (0x01u, ft8.left (2 * split)) + frame (0x01u, ft8.mid (2 * split));
          }
        run->write (burst);
        QVERIFY2 (run->waitForCount (outType ("decode_finished"), behindBy + 1, waitMs (), "the periods"),
                  qPrintable (run->report ()));
        run->write (marker ());
        QVERIFY2 (run->waitForLine (outHas ("configure_parse_error"), waitMs (), "the marker"),
                  qPrintable (run->report ()));
        run->finish ();
        wantEnds = ends (behindBy + 1);
        if (replace)
          {
            wantDecisions << "pass 48 T" << "defer 48" << "skip 48 final memory" << "skip 48 final replaced";
            wantSkips << labelOf (utcOf (1)) + " 48 final replaced" << labelOf (utcOf (2)) + " 48 final memory";
            // The replaced and the new final's skips, in either order.
            auto got = decisions (run->lines);
            std::sort (got.begin () + std::min<int> (2, got.size ()), got.end ());
            QVERIFY2 (got == wantDecisions, qPrintable (got.join (" | ") + "\n" + run->report ()));
            wantDecisions.clear ();
          }
        else
          {
            wantDecisions << "pass 48 T" << "skip 48 final memory";
            wantSkips << labelOf (utcOf (1)) + " 48 final memory";
          }
      }
    else if ("held again" == scenario)
      {
        // Period 0's final (start 2, 331,776 bytes) becomes the decode-again
        // audio; period 1, from a file, then fits its first early pass beside
        // it (283,392) but not the second (324,864) or the final (360,000).
        options.environment << "WSJT_CODEC_TEST_ALLOC_LIMIT=640000";
        run.reset (new Codec {options});
        QVERIFY2 (run->started, qPrintable (run->failure));
        run->write (sessionHeader () + configure (with (live, {{"ft8_decoder_start", 2}}))
                    + frame (0x01u, ft8.left (2 * 48 * HsymSamples)));
        QVERIFY2 (run->waitForLine (outType ("decode_finished"), waitMs (), "period 0"), qPrintable (run->report ()));
        run->write (frame (0x01u, ft8.mid (2 * 48 * HsymSamples))
                    + configure ({{"multithreaded_ft8", false}, {"live_audio", false}, {"utc", utcOf (1)}})
                    + frame (0x01u, ft8.left (2 * 42 * HsymSamples)) + marker ());
        QVERIFY2 (run->waitForLine (outHas ("configure_parse_error"), waitMs (), "period 1's early pass"),
                  qPrintable (run->report ()));
        run->write (configure ({{"nagain_flag", true}}) + frame (0x01u, ft8.mid (2 * 42 * HsymSamples)) + marker ());
        QVERIFY2 (run->waitForCount (outHas ("configure_parse_error"), 2, waitMs (), "the marker"),
                  qPrintable (run->report ()));
        run->finish ();
        wantDecisions << "pass 48 T" << "pass 41 F" << "skip 47 early memory" << "skip 50 final memory"
                      << "pass 50 F again";
        wantSkips << labelOf (utcOf (1)) + " 47 early memory" << labelOf (utcOf (1)) + " 50 final memory";
        wantEnds << labelOf (utcOf (0)) << labelOf (utcOf (1)) << labelOf (utcOf (0));
      }
    QVERIFY2 (run && run->clean (), qPrintable (run ? run->report () : QString {}));
    if (!wantDecisions.isEmpty ()) QCOMPARE (decisions (run->lines), wantDecisions);
    QCOMPARE (skips (run->lines), wantSkips);
    QCOMPARE (periodEnds (run->lines), wantEnds);
    // Each final skipped for memory still ends its period, after its pass_skipped.
    for (int i = 0; i < run->lines.size (); ++i)
      {
        if (!outHas ("\"reason\":\"memory\"") (run->lines.at (i)) || !run->lines.at (i).text.contains ("\"final\"")) continue;
        auto const label = json (run->lines.at (i).text).value ("period_end").toString ();
        auto const next = find (run->lines, outType ("decode_finished"), i);
        QVERIFY2 (next > i && json (run->lines.at (next).text).value ("period_end").toString () == label,
                  qPrintable (show (run->lines)));
      }
    // The session went on: the last marker was answered.
    QVERIFY2 (count (run->lines, outHas ("configure_parse_error")) > 0, qPrintable (show (run->lines)));
  }
};

QTEST_GUILESS_MAIN (TestStreamingDecodeThread)

#include "test_streaming_decode_thread.moc"
