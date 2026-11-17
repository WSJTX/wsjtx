// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <thread>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
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

#ifdef Q_OS_UNIX
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char ** environ;
#endif

namespace
{
constexpr int SampleRate = 12000;
constexpr int HsymSamples = 3456;
constexpr int BlockSamples = 3584;      // MSK144's block
constexpr int Ft8PeriodSamples = 15 * SampleRate;
constexpr int Ft4PeriodSamples = 90000;
constexpr int FrameSamples = 1200;      // a paced client's frame, 0.1 s
constexpr int RefPoints = 3456;
constexpr int LineLimit = 64 * 1024;    // RIGGED's longest stdout line

// One wait covers a decode or two; sanitizer builds raise QtTest's limit and this with it.
int waitMs ()
{
  return std::max (60000, qEnvironmentVariableIntValue ("QTEST_FUNCTION_TIMEOUT") / 6);
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

QByteArray flush ()
{
  return control ({{"t", "flush"}});
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

// A mode's recording and settings, as the stream's other rows decode them.
struct Mode
{
  QString wav;
  QJsonObject settings;
  int samples {0};       // its period
  QString name;          // its decodes' mode
  double trperiod {15};
  bool sorted {false};   // its decodes come in thread order
  int stop {0};          // MainWindow::fixStop's half-symbol, which ends diskDat's rows
};

Mode modeOf (QString const& name)
{
  QJsonObject const ft8 {{"mode", "FT8"}, {"utc", "13:34:30"}, {"depth", 3}};
  QJsonObject const mtd {{"multithreaded_ft8", true}, {"ft8_threads", 2}, {"ft8_decoder_start", 0}};
  auto const ft8Sample = inSamples ("FT8/210703_133430.wav");
  QJsonObject const fst4 {{"mode", "FST4"}, {"trperiod", 60}, {"rxfreq", 1331}, {"utc", "00:58:00"}, {"depth", 3}};
  if ("ft8" == name) return {ft8Sample, ft8, Ft8PeriodSamples, "FT8", 15, false, 50};
  if ("ft8-engine" == name) return {fixture ("ft8-engine.wav"), with (ft8, {{"utc", "05:11:15"}}), Ft8PeriodSamples, "FT8", 15, false, 50};
  if ("ft8-mtd" == name) return {ft8Sample, with (ft8, mtd), Ft8PeriodSamples, "FT8", 15, true, 49};
  if ("ft4" == name)
    return {inSamples ("FT4/000000_000002.wav"), {{"mode", "FT4"}, {"utc", "00:00:00"}, {"depth", 3}},
            Ft4PeriodSamples, "FT4", 7.5, false, 21};
  if ("fst4" == name)
    return {fixture ("fst4-engine.wav"), {{"mode", "FST4"}, {"trperiod", 15}, {"depth", 3}, {"nfa", 1300},
                                           {"nfb", 1700}, {"rxfreq", 1500}, {"utc", "00:15:00"}},
            15 * SampleRate, "FST4", 15, false, 39};
  if ("fst4w" == name)
    return {fixture ("fst4w-engine.wav"), {{"mode", "FST4W"}, {"trperiod", 120}, {"depth", 1}, {"nfa", 1300},
                                            {"nfb", 1700}, {"rxfreq", 1500}, {"utc", "00:16:00"}},
            120 * SampleRate, "FST4W", 120, false, 387};
  if ("q65" == name)
    return {fixture ("q65-engine.wav"), {{"mode", "Q65"}, {"trperiod", 15}, {"submode", 0}, {"depth", 3},
                                          {"nfa", 1300}, {"nfb", 1700}, {"rxfreq", 1500}, {"utc", "00:15:00"}},
            15 * SampleRate, "Q65", 15, false, 48};
  if ("jt4" == name)
    return {fixture ("jt4-engine.wav"), {{"mode", "JT4"}, {"trperiod", 60}, {"submode", 0}, {"depth", 3},
                                          {"rxfreq", 1000}, {"ntol", 50}, {"utc", "12:34:00"}},
            60 * SampleRate, "JT4", 60, false, 176};
  if ("jt9" == name)
    return {inSamples ("JT9/130418_1742.wav"), {{"mode", "JT9"}, {"trperiod", 60}, {"nutc", 1742}, {"depth", 3}},
            60 * SampleRate, "JT9", 60, false, 173};
  if ("jt65" == name)
    return {inSamples ("JT65/JT65B/000000_0004.wav"), {{"mode", "JT65"}, {"trperiod", 60}, {"submode", 1},
                                                        {"rxfreq", 1700}, {"nutc", 4}, {"depth", 3}},
            60 * SampleRate, "JT65", 60, false, 174};
  // The GUI has no JT65+JT9 and the codec no stop for it: its rows from a
  // file run to the period's last block, as live.
  if ("jt65jt9" == name)
    return {inSamples ("JT9/130418_1742.wav"), {{"mode", "JT65+JT9"}, {"trperiod", 60}, {"nutc", 1742},
                                                 {"jt65_jt9_split_hz", 1000}, {"tx_mode", "JT9"}, {"depth", 3}},
            60 * SampleRate, "JT65+JT9", 60, false, 60 * SampleRate / HsymSamples};
  if ("fst4-60" == name)
    return {inSamples ("FST4+FST4W/210115_0058.wav"), with (fst4, {{"nfa", 1000}, {"nfb", 1400}}), 60 * SampleRate,
            "FST4", 60, false, 187};
  if ("wspr" == name)
    return {inSamples ("WSPR/150426_0918.wav"), {{"mode", "WSPR"}, {"utc", "09:18:00"}, {"depth", 3}},
            120 * SampleRate, "WSPR", 120, false, 396};
  return {};
}

QJsonObject const Msk144Settings {{"mode", "MSK144"}, {"trperiod", 15}, {"rxfreq", 1500}, {"ntol", 50},
                                  {"depth", 3}, {"utc", "12:08:00"}};
QJsonObject const FastJt9Settings {{"mode", "JT9"}, {"fast", true}, {"submode", "E"}, {"trperiod", 15},
                                   {"rxfreq", 700}, {"ntol", 50}, {"utc", "12:08:00"}};
QJsonObject const JttySettings {{"mode", "JTTY"}, {"rxfreq", 1500}, {"ntol", 50}, {"nfa", 200}, {"nfb", 2800}};

// A configure's next label: utc moved on by whole periods.
QJsonObject labelFor (Mode const& mode, int p)
{
  auto const utc = QTime::fromString (mode.settings.value ("utc").toString (), "HH:mm:ss");
  if (!utc.isValid ()) return {};
  return {{"utc", utc.addSecs (int (p * mode.trperiod)).toString ("HH:mm:ss")}};
}

// --- what the codec writes ----------------------------------------------------

struct Line
{
  qint64 ms;         // when the test read it
  bool error;        // stderr
  QByteArray text;   // without its line end
};

using Match = std::function<bool (Line const&)>;

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

Match passLine (int nzhsym, bool multithreaded = false)
{
  auto const head = QByteArray {"pass nzhsym="} + QByteArray::number (nzhsym) + ' ';
  return [head, multithreaded] (Line const& line) {
    return line.error && line.text.contains (head) && (!multithreaded || line.text.contains ("multithreaded=T"));
  };
}

int find (QList<Line> const& lines, Match const& match, int from = 0)
{
  for (int i = std::max (0, from); i < lines.size (); ++i)
    if (match (lines.at (i))) return i;
  return -1;
}

int count (QList<Line> const& lines, Match const& match)
{
  return int (std::count_if (lines.begin (), lines.end (), match));
}

// Matches each line once however often it is polled.
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
      if (match_ (lines.at (next_))) ++count_;
    return count_;
  }

private:
  Match match_;
  int next_;
  int count_ {0};
};

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

// A session's decoded messages, each once, sorted.
QStringList messageSet (QList<Line> const& lines)
{
  QSet<QString> found;
  for (auto const& line : lines)
    if (outType ("decode") (line)) found.insert (json (line.text).value ("message").toString ().trimmed ());
  auto sorted = found.values ();
  std::sort (sorted.begin (), sorted.end ());
  return sorted;
}

bool isRowType (QByteArray const& type)
{
  return "spectrum" == type || "fast_spectrum" == type || "ref_spectrum" == type;
}

// What a client that ignores the rows and the prepared audio reads: every
// stdout line but theirs.
QByteArrayList known (QList<Line> const& lines)
{
  QByteArrayList kept;
  for (auto const& line : lines)
    {
      auto const type = typeOf (line.text);
      if (!line.error && !isRowType (type) && "rows_dropped" != type && "prepared_audio" != type) kept << line.text;
    }
  return kept;
}

QString show (QList<Line> const& lines, int from = 0)
{
  QStringList text;
  for (int i = from; i < lines.size (); ++i)
    {
      auto const& line = lines.at (i);
      text << QString {"%1 %2 %3"}.arg (line.ms, 7).arg (line.error ? "err" : "out")
        .arg (QString::fromUtf8 (line.text.left (160)));
    }
  return text.join ('\n');
}

// The longest stdout line, line end included.
int longestLine (QList<Line> const& lines)
{
  int longest = 0;
  for (auto const& line : lines)
    if (!line.error) longest = std::max (longest, int (line.text.size ()) + 1);
  return longest;
}

// A prepared_audio line: a period mode's k, or JTTY's reception, begin and
// end; its samples as int16 little-endian bytes.
struct Prepared
{
  bool jtty {false};
  qint64 k {0};
  qint64 reception {0};
  qint64 begin {0};
  qint64 end {0};
  QByteArray pcm;
};

// The prepared_audio lines in order, or the first problem with one: integer
// fields, a period mode's samples or JTTY's end - begin the samples data
// holds, data standard base64 with padding.
QString preparedOf (QList<Line> const& lines, QList<Prepared>& found)
{
  found.clear ();
  for (auto const& line : lines)
    {
      if (line.error || "prepared_audio" != typeOf (line.text)) continue;
      auto const o = json (line.text);
      auto const where = ": " + QString::fromUtf8 (line.text.left (120));
      if (!o.value ("v").isDouble () || o.value ("v").toInt () != 1) return "no \"v\":1" + where;
      auto const integer = [&] (char const * key, qint64& value) {
        auto const v = o.value (key);
        if (!v.isDouble () || v.toDouble () != std::floor (v.toDouble ())) return false;
        value = qint64 (v.toDouble ());
        return true;
      };
      Prepared each;
      qint64 count = 0;
      each.jtty = o.contains ("reception");
      if (each.jtty)
        {
          if (!integer ("reception", each.reception) || !integer ("begin", each.begin) || !integer ("end", each.end))
            return "a JTTY line without integer reception, begin and end" + where;
          if (o.contains ("k") || o.contains ("samples")) return "a JTTY line with k or samples" + where;
          count = each.end - each.begin;
        }
      else if (!integer ("k", each.k) || !integer ("samples", count)) return "a line without integer k and samples" + where;
      auto const encoded = o.value ("data").toString ().toLatin1 ();
      auto const decoded = QByteArray::fromBase64Encoding (encoded, QByteArray::AbortOnBase64DecodingErrors);
      if (QByteArray::Base64DecodingStatus::Ok != decoded.decodingStatus || decoded.decoded.toBase64 () != encoded)
        return "data is not standard base64 with padding" + where;
      if (count <= 0 || decoded.decoded.size () != 2 * count)
        return QString {"data holds %1 bytes for %2 samples"}.arg (decoded.decoded.size ()).arg (count) + where;
      each.pcm = decoded.decoded;
      found << each;
    }
  return {};
}

// A period mode's lines period by period: a line whose samples start at 0
// starts a period, and each other line starts where the one before ended (k
// rising by its samples). The first problem, or empty.
QString preparedPeriods (QList<Prepared> const& lines, QList<QByteArray>& periods, QList<QList<qint64>>* ks = nullptr)
{
  periods.clear ();
  if (ks) ks->clear ();
  qint64 at = -1;
  for (int i = 0; i < lines.size (); ++i)
    {
      auto const& line = lines.at (i);
      auto const from = line.k - line.pcm.size () / 2;
      if (line.jtty) return QString {"line %1 is a JTTY line"}.arg (i);
      if (0 == from)
        {
          periods << QByteArray {};
          if (ks) ks->append (QList<qint64> {});
        }
      else if (from != at) return QString {"line %1 starts at sample %2, not %3"}.arg (i).arg (from).arg (at);
      periods.last () += line.pcm;
      if (ks) ks->last () << line.k;
      at = line.k;
    }
  return {};
}

// JTTY's lines of session reception from stream sample begin on, each from
// the end of the one before; their samples, or the first problem.
QString preparedSession (QList<Prepared> const& lines, qint64 reception, qint64 begin, QByteArray& pcm)
{
  pcm.clear ();
  auto at = begin;
  for (int i = 0; i < lines.size (); ++i)
    {
      auto const& line = lines.at (i);
      if (!line.jtty || line.reception != reception || line.begin != at)
        return QString {"line %1: JTTY %2, reception %3, begin %4; expected reception %5, begin %6"}.arg (i)
          .arg (line.jtty).arg (line.reception).arg (line.begin).arg (reception).arg (at);
      pcm += line.pcm;
      at = line.end;
    }
  return {};
}

// Each spectrum or fast_spectrum row comes after the prepared_audio line of
// the block it ends with (JTTY: the line that ends where the row does), and
// the first ref_spectrum line before the first prepared_audio line; the
// first problem, or empty. Live rows can go out later than the lines that
// follow them, never earlier, so correct code always passes; a row queued
// ahead of its line shows only while the writer has written the row before
// the line overtakes it, which a reader that keeps up gives.
QString preparedOrder (QList<Line> const& lines)
{
  QSet<QString> seen;          // "period k" of each prepared_audio line so far
  int preparedPeriod = 0;
  int rowPeriod = 0;
  qint64 lastRowK = -1;
  int firstPrepared = -1;
  int firstRef = -1;
  int rows = 0;
  for (int i = 0; i < lines.size (); ++i)
    {
      auto const& line = lines.at (i);
      if (line.error) continue;
      auto const type = typeOf (line.text);
      if ("ref_spectrum" == type && firstRef < 0) firstRef = i;
      if ("prepared_audio" != type && "spectrum" != type && "fast_spectrum" != type) continue;
      auto const o = json (line.text);
      auto const jtty = o.contains ("reception");
      auto const k = qint64 (o.value (jtty ? "end" : "k").toDouble ());
      if ("prepared_audio" == type)
        {
          if (firstPrepared < 0) firstPrepared = i;
          if (jtty) preparedPeriod = int (o.value ("reception").toDouble ());
          else if (k - qint64 (o.value ("samples").toDouble ()) == 0) ++preparedPeriod;
          seen.insert (QString {"%1 %2"}.arg (preparedPeriod).arg (k));
          continue;
        }
      ++rows;
      if (jtty) rowPeriod = int (o.value ("reception").toDouble ());
      else if (k <= lastRowK || lastRowK < 0) ++rowPeriod;
      lastRowK = k;
      if (!seen.contains (QString {"%1 %2"}.arg (rowPeriod).arg (k)))
        return QString {"line %1, a %2 row ending at %3, comes before its block's prepared_audio line"}.arg (i)
          .arg (QString::fromUtf8 (type)).arg (k);
    }
  if (0 == rows) return "no rows";
  if (firstRef >= 0 && firstPrepared >= 0 && firstRef > firstPrepared)
    return "the first ref_spectrum line comes after the first prepared_audio line";
  return {};
}

// Where two sample buffers first differ, for a failure message.
QString firstDifference (QByteArray const& got, QByteArray const& expected)
{
  int i = 0;
  while (i < std::min (got.size (), expected.size ()) && got.at (i) == expected.at (i)) ++i;
  return QString {"%1 samples, %2 expected, first difference at sample %3"}.arg (got.size () / 2)
    .arg (expected.size () / 2).arg (i / 2);
}

// --- one codec process ----------------------------------------------------------

struct Options
{
  QStringList environment;   // NAME=value, added to the test's
  QString directory;         // its -a, and its -t unless temporary is set; a temporary one when empty
  QString temporary;         // its -t
};

class Codec
{
public:
  explicit Codec (Options const& options = {})
  {
    directory = options.directory.isEmpty () ? temporary_.path () : options.directory;
    auto environment = QProcessEnvironment::systemEnvironment ();
    for (auto const& setting : options.environment)
      environment.insert (setting.section ('=', 0, 0), setting.section ('=', 1));
    process_.setProcessEnvironment (environment);
    process_.setProcessChannelMode (QProcess::SeparateChannels);
    process_.setWorkingDirectory (options.temporary.isEmpty () ? directory : options.temporary);
    clock_.start ();
    process_.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                    {"-a", directory, "-t", options.temporary.isEmpty () ? directory : options.temporary});
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
    process_.write (bytes);
    while (process_.bytesToWrite () > 0 && QProcess::Running == process_.state ()) pump ();
    return now ();
  }

  void post (QByteArray const& bytes)
  {
    process_.write (bytes);
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

  bool waitForCount (Match const& match, int n, int ms, QString const& what, int from = 0)
  {
    Tally seen {match, from};
    return waitFor ([&] {return seen (lines) >= n;}, ms, what);
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

  QString directory;
  bool started {false};
  QString failure;
  QList<Line> lines;
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
        auto text = buffer.mid (from, end - from);
        if (text.endsWith ('\r')) text.chop (1);
        lines.append ({ms, error, text});
      }
    buffer.remove (0, from);
  }

  QTemporaryDir temporary_;
  QProcess process_;
  QElapsedTimer clock_;
  QByteArray out_;
  QByteArray err_;
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

// --- rows: the codec's and the GUI model's ---------------------------------------

QString bitsOf (float value)
{
  quint32 word;
  std::memcpy (&word, &value, sizeof word);
  return QString {"%1"}.arg (word, 8, 16, QChar {'0'});
}

float floatOfBits (QString const& bits)
{
  auto const word = quint32 (bits.toULong (nullptr, 16));
  float value;
  std::memcpy (&value, &word, sizeof value);
  return value;
}

float floatAt (QByteArray const& bytes, int i)
{
  auto const word = qFromLittleEndian<quint32> (bytes.constData () + 4 * i);
  float value;
  std::memcpy (&value, &word, sizeof value);
  return value;
}

// One row line, from either side: its integer fields and float bits in a
// fixed order per kind, and its arrays' float32 LE bytes.
struct Row
{
  QString kind;            // spectrum, jtty, fast or ref; the model's jttyspectrum
  QList<qint64> ints;
  QStringList floats;      // IEEE bits, hex
  QList<QByteArray> arrays;

  QString head () const
  {
    QStringList parts {kind};
    for (auto value : ints) parts << QString::number (value);
    for (auto const& bits : floats) parts << QString {"%1(%2)"}.arg (floatOfBits (bits), 0, 'g', 9).arg (bits);
    return parts.join (' ');
  }
};

// The codec's line as a row, or a problem with it. Its arrays are standard
// base64 with padding of float32 LE values, its floats JSON numbers that read
// back as their single-precision value.
QString rowOf (QByteArray const& text, Row& row)
{
  auto const o = json (text);
  if (o.isEmpty ()) return "not a JSON object";
  if (!o.value ("v").isDouble () || o.value ("v").toInt () != 1) return "no \"v\":1";
  auto const type = o.value ("t").toString ();
  QString problem;
  auto const note = [&] (QString const& what) {if (problem.isEmpty ()) problem = what;};
  auto const integer = [&] (char const * key) -> qint64 {
    auto const value = o.value (key);
    if (!value.isDouble () || value.toDouble () != std::floor (value.toDouble ()))
      {
        note (QString {"\"%1\" is not an integer"}.arg (key));
        return 0;
      }
    return qint64 (value.toDouble ());
  };
  auto const real = [&] (char const * key) -> QString {
    auto const value = o.value (key);
    if (!value.isDouble ())
      {
        note (QString {"\"%1\" is not a number"}.arg (key));
        return {};
      }
    return bitsOf (float (value.toDouble ()));
  };
  auto const array = [&] (char const * key, qint64 floats) -> QByteArray {
    auto const value = o.value (key);
    if (!value.isString ())
      {
        note (QString {"\"%1\" is not a string"}.arg (key));
        return {};
      }
    auto const encoded = value.toString ().toLatin1 ();
    auto const decoded = QByteArray::fromBase64Encoding (encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (QByteArray::Base64DecodingStatus::Ok != decoded.decodingStatus)
      {
        note (QString {"\"%1\" is not base64"}.arg (key));
        return {};
      }
    if (decoded.decoded.toBase64 () != encoded) note (QString {"\"%1\" is not standard base64 with padding"}.arg (key));
    if (decoded.decoded.size () != 4 * floats)
      note (QString {"\"%1\" holds %2 bytes, not %3 floats"}.arg (key).arg (decoded.decoded.size ()).arg (floats));
    return decoded.decoded;
  };
  if ("spectrum" == type && o.contains ("reception"))
    {
      row.kind = "jtty";
      // The codec has no clock for a sample, and the GUI's waterfall alone marks lost audio.
      if (o.contains ("utc_end_ms") || o.contains ("gap")) note ("a JTTY row carries utc_end_ms or gap");
      row.ints = {integer ("reception"), integer ("begin"), integer ("end"), integer ("bins")};
      row.floats = QStringList {real ("power"), real ("peak"), real ("df")};
      row.arrays = {array ("data", row.ints.last ())};
    }
  else if ("spectrum" == type)
    {
      row.kind = "spectrum";
      row.ints = {integer ("k"), integer ("hsym"), integer ("bins")};
      row.floats = QStringList {real ("power"), real ("peak"), real ("df")};
      row.arrays = {array ("data", row.ints.last ())};
    }
  else if ("fast_spectrum" == type)
    {
      row.kind = "fast";
      row.ints = {integer ("k"), integer ("first"), integer ("columns")};
      row.floats = QStringList {real ("power"), real ("peak")};
      auto const columns = row.ints.last ();
      row.arrays = {array ("green", columns), array ("data", 64 * columns)};
    }
  else if ("ref_spectrum" == type)
    {
      row.kind = "ref";
      row.ints = {integer ("bins")};
      row.floats = QStringList {real ("df")};
      row.arrays = {array ("data", row.ints.last ())};
    }
  else return "not a row";
  return problem;
}

struct Rows
{
  QList<Row> rows;
  QString problem;   // the first line that is not a well-formed row, and why
};

Rows rowsOf (QList<Line> const& lines)
{
  Rows result;
  for (auto const& line : lines)
    {
      if (line.error || !isRowType (typeOf (line.text))) continue;
      Row row;
      auto const problem = rowOf (line.text, row);
      if (!problem.isEmpty () && result.problem.isEmpty ())
        result.problem = problem + ": " + QString::fromUtf8 (line.text.left (200));
      result.rows << row;
    }
  return result;
}

// The rows of the given kinds, as a session that asked only for them writes them.
QList<Row> onlyKinds (QList<Row> const& rows, QStringList const& kinds)
{
  QList<Row> kept;
  for (auto const& row : rows)
    if (kinds.contains (row.kind)) kept << row;
  return kept;
}

// The model's rows period by period: a spectrum or fast row whose k is not
// past the one before starts the next period.
QList<QList<Row>> byPeriod (QList<Row> const& rows)
{
  QList<QList<Row>> periods;
  qint64 last = -1;
  for (auto const& row : rows)
    {
      auto const blockRow = "spectrum" == row.kind || "fast" == row.kind;
      if (periods.isEmpty () || (blockRow && row.ints.at (0) <= last)) periods.append (QList<Row> {});
      if (blockRow) last = row.ints.at (0);
      periods.last () << row;
    }
  return periods;
}

// The samples of a session where spectrum turns on, off, on and so on, as
// configure frames placed in its audio.
QByteArray toggled (QByteArray const& pcm, QList<int> const& toggles, QList<QJsonObject> const& on,
                    QMap<int, QJsonObject> const& labels = {}, int frameSamples = 4096)
{
  QMap<int, QJsonObject> events = labels;
  for (int i = 0; i < toggles.size (); ++i)
    events[toggles.at (i)] = with (events.value (toggles.at (i)),
                                   i % 2 ? QJsonObject {{"spectrum", false}} : on.value (i / 2, {{"spectrum", true}}));
  QByteArray bytes;
  int at = 0;
  for (auto it = events.constBegin (); it != events.constEnd (); ++it)
    {
      bytes += audio (samples (pcm, at, it.key ()), frameSamples) + configure (it.value ());
      at = it.key ();
    }
  return bytes + audio (pcm.mid (2 * at), frameSamples);
}

// Whether spectrum is on for sample k of a session that toggles it at toggles.
bool onAt (QList<int> const& toggles, int k)
{
  int flips = 0;
  for (auto t : toggles) flips += t <= k;
  return flips % 2;
}

int countKind (QList<Row> const& rows, QString const& kind)
{
  return int (std::count_if (rows.begin (), rows.end (), [&] (Row const& row) {return kind == row.kind;}));
}

// The first difference between the codec's rows and the model's, or empty.
// tolerance 0 compares the arrays byte for byte; otherwise each value within
// that relative difference, the greatest of which goes to worst.
QString compareRows (QList<Row> const& codec, QList<Row> const& model, double tolerance = 0, double * worst = nullptr)
{
  if (worst) *worst = 0;
  for (int i = 0; i < std::min (codec.size (), model.size ()); ++i)
    {
      auto const& a = codec.at (i);
      auto const& b = model.at (i);
      if (a.kind != b.kind || a.ints != b.ints || a.floats != b.floats || a.arrays.size () != b.arrays.size ())
        return QString {"row %1: the codec's\n  %2\nthe GUI's\n  %3"}.arg (i).arg (a.head (), b.head ());
      for (int n = 0; n < a.arrays.size (); ++n)
        {
          auto const& x = a.arrays.at (n);
          auto const& y = b.arrays.at (n);
          if (x.size () != y.size ())
            return QString {"row %1 (%2) array %3: %4 bytes, the GUI's %5"}.arg (i).arg (a.head ()).arg (n)
              .arg (x.size ()).arg (y.size ());
          if (0 == tolerance)
            {
              if (0 == std::memcmp (x.constData (), y.constData (), size_t (x.size ()))) continue;
              int first = -1, differing = 0;
              for (int v = 0; v < x.size () / 4; ++v)
                if (std::memcmp (x.constData () + 4 * v, y.constData () + 4 * v, 4))
                  {
                    if (first < 0) first = v;
                    ++differing;
                  }
              return QString {"row %1 (%2) array %3: %4 of %5 values differ, first [%6] %7 against the GUI's %8"}
                .arg (i).arg (a.head ()).arg (n).arg (differing).arg (x.size () / 4).arg (first)
                .arg (floatAt (x, first), 0, 'g', 9).arg (floatAt (y, first), 0, 'g', 9);
            }
          for (int v = 0; v < x.size () / 4; ++v)
            {
              auto const p = double (floatAt (x, v));
              auto const q = double (floatAt (y, v));
              auto const scale = std::max (std::abs (p), std::abs (q));
              auto const relative = scale > 0 ? std::abs (p - q) / scale : 0.;
              if (worst) *worst = std::max (*worst, relative);
              if (relative > tolerance)
                return QString {"row %1 (%2) array %3 [%4]: %5 against the GUI's %6, relative %7"}.arg (i)
                  .arg (a.head ()).arg (n).arg (v).arg (p, 0, 'g', 9).arg (q, 0, 'g', 9).arg (relative, 0, 'g', 3);
            }
        }
    }
  if (codec.size () != model.size ())
    {
      auto const& longer = codec.size () > model.size () ? codec : model;
      return QString {"the codec wrote %1 rows, the GUI's model %2; the first unmatched is the %3's %4"}
        .arg (codec.size ()).arg (model.size ()).arg (codec.size () > model.size () ? "codec" : "model")
        .arg (longer.at (std::min (codec.size (), model.size ())).head ());
    }
  return {};
}

// The rows a session wrote against the model's. With live audio the queue
// drops the oldest rows past 20 entries when the reader falls behind (a
// spectrum, fast_spectrum or JTTY row; never a ref_spectrum curve), and a
// rows_dropped line before the next row that can be dropped counts the rows
// dropped since the last. A curve goes out ahead of the rows queued when it
// is written, so it can come before rows the model has ahead of it, but
// never after one the model has after it. So the curves must be the model's,
// every one in order; the rows the model's in order with only counted runs
// missing, the session's last rows and at least min(20, all) of them there;
// and each that came bit for bit the model's. From a file nothing may be
// dropped. Returns the first problem, or empty.
QString compareKeptRows (QList<Line> const& lines, QList<Row> const& model, bool live)
{
  struct Kept
  {
    Row row;
    qint64 dropped;   // rows the rows_dropped lines just before it count
  };
  QList<Kept> written;
  QList<Row> curves;
  QList<qint64> rowsBeforeCurve;   // rows that came or were counted dropped before each curve
  qint64 pending = 0, accounted = 0;
  int countLines = 0;
  for (auto const& line : lines)
    {
      if (line.error) continue;
      auto const type = typeOf (line.text);
      if ("rows_dropped" == type)
        {
          ++countLines;
          auto const dropped = qint64 (json (line.text).value ("lines").toDouble ());
          pending += dropped;
          accounted += dropped;
        }
      else if (isRowType (type))
        {
          Row row;
          auto const problem = rowOf (line.text, row);
          if (!problem.isEmpty ()) return problem + ": " + QString::fromUtf8 (line.text.left (200));
          if ("ref" == row.kind)
            {
              // Rows dropped before a curve are counted before the next row after it.
              if (pending) return QString {"curve %1: a rows_dropped line before a ref_spectrum curve"}.arg (curves.size ());
              curves << row;
              rowsBeforeCurve << accounted;
              continue;
            }
          written << Kept {row, pending};
          pending = 0;
          ++accounted;
        }
    }
  if (!live && countLines) return QString {"%1 rows_dropped lines from a file"}.arg (countLines);
  if (pending) return "a rows_dropped line with no row after it";
  QList<Row> modelRows, modelCurves;
  QList<qint64> modelRowsBeforeCurve;
  for (auto const& row : model)
    if ("ref" == row.kind)
      {
        modelCurves << row;
        modelRowsBeforeCurve << modelRows.size ();
      }
    else modelRows << row;
  auto const curveDifference = compareRows (curves, modelCurves);
  if (!curveDifference.isEmpty ()) return "ref_spectrum curves: " + curveDifference;
  for (int c = 0; c < curves.size (); ++c)
    if (rowsBeforeCurve.at (c) > modelRowsBeforeCurve.at (c))
      return QString {"curve %1 came after %2 rows, the GUI's after %3"}.arg (c).arg (rowsBeforeCurve.at (c))
        .arg (modelRowsBeforeCurve.at (c));
  QList<Row> codecRows, modelKept;
  int j = 0;
  qint64 droppedRows = 0;
  for (int i = 0; i < written.size (); ++i)
    {
      auto const& w = written.at (i);
      if (w.dropped > modelRows.size () - j)
        return QString {"row %1: rows_dropped counts past the session's rows"}.arg (i);
      j += int (w.dropped);
      droppedRows += w.dropped;
      if (j >= modelRows.size ())
        return QString {"row %1 (%2): the GUI's model has no more rows"}.arg (i).arg (w.row.head ());
      if (w.row.kind != modelRows.at (j).kind || w.row.ints != modelRows.at (j).ints)
        return QString {"row %1: the codec's\n  %2\nthe GUI's, after %3 dropped rows,\n  %4"}.arg (i)
          .arg (w.row.head ()).arg (w.dropped).arg (modelRows.at (j).head ());
      codecRows << w.row;
      modelKept << modelRows.at (j);
      ++j;
    }
  if (j < modelRows.size ())
    return QString {"the session's last %1 rows are missing, from %2"}.arg (modelRows.size () - j)
      .arg (modelRows.at (j).head ());
  auto const all = int (modelRows.size ());
  auto const came = int (codecRows.size ());
  if (came < std::min (20, all)) return QString {"%1 of %2 rows came, fewer than the queue keeps"}.arg (came).arg (all);
  if (droppedRows)
    qInfo ().noquote () << QString {"%1 rows_dropped lines, %2 rows dropped; %3 of %4 rows came"}.arg (countLines)
      .arg (droppedRows).arg (came).arg (all);
  return compareRows (codecRows, modelKept);
}

// Spectrum lines written, and the rows the rows_dropped lines among them
// count.
qint64 rowLinesAccounted (QList<Line> const& lines)
{
  qint64 total = 0;
  for (auto const& line : lines)
    {
      if (line.error) continue;
      auto const t = typeOf (line.text);
      if ("rows_dropped" == t) total += qint64 (json (line.text).value ("lines").toDouble ());
      else if ("spectrum" == t) ++total;
    }
  return total;
}

// --- the GUI's model ----------------------------------------------------------------

struct ModelRun
{
  bool ok {false};
  QString failure;
  QList<Row> rows;
  QByteArray err;
};

ModelRun runModel (QString const& directory, QByteArray const& pcm, QStringList script)
{
  ModelRun run;
  QDir {}.mkpath (directory);
  auto const audioPath = directory + "/model-audio.pcm";
  QFile audioFile {audioPath};
  if (!audioFile.open (QIODevice::WriteOnly) || audioFile.write (pcm) != pcm.size ())
    {
      run.failure = "cannot write " + audioPath;
      return run;
    }
  audioFile.close ();
  script.prepend ("audio " + audioPath);
  auto const scriptPath = directory + "/model-script.txt";
  QFile scriptFile {scriptPath};
  if (!scriptFile.open (QIODevice::WriteOnly | QIODevice::Text))
    {
      run.failure = "cannot write " + scriptPath;
      return run;
    }
  scriptFile.write ((script.join ('\n') + '\n').toUtf8 ());
  scriptFile.close ();
  QProcess model;
  model.setWorkingDirectory (directory);
  model.start (QString::fromUtf8 (SPECTRUM_GUI_MODEL), {scriptPath});
  if (!model.waitForStarted (5000) || !model.waitForFinished (waitMs ()))
    {
      run.failure = "spectrum_gui_model did not run: " + model.errorString ();
      model.kill ();
      model.waitForFinished (5000);
      return run;
    }
  run.err = model.readAllStandardError ();
  if (QProcess::NormalExit != model.exitStatus () || 0 != model.exitCode ())
    {
      run.failure = QString {"spectrum_gui_model exited %1: %2"}.arg (model.exitCode ()).arg (QString::fromUtf8 (run.err));
      return run;
    }
  auto const decode = [] (QByteArray const& text) {return QByteArray::fromBase64 (text);};
  for (auto const& line : model.readAllStandardOutput ().split ('\n'))
    {
      auto const words = line.split (' ');
      auto const kind = words.value (0);
      Row row;
      if ("spectrum" == kind && words.size () == 8)
        row = {"spectrum", {words.at (1).toLongLong (), words.at (2).toLongLong (), words.at (6).toLongLong ()},
               {QString::fromLatin1 (words.at (3)), QString::fromLatin1 (words.at (4)), QString::fromLatin1 (words.at (5))},
               {decode (words.at (7))}};
      else if (("jtty" == kind || "jttyspectrum" == kind) && words.size () == 9)
        row = {QString::fromLatin1 (kind), {words.at (1).toLongLong (), words.at (2).toLongLong (),
                                            words.at (3).toLongLong (), words.at (7).toLongLong ()},
               {QString::fromLatin1 (words.at (4)), QString::fromLatin1 (words.at (5)), QString::fromLatin1 (words.at (6))},
               {decode (words.at (8))}};
      else if ("fast" == kind && words.size () == 8)
        row = {"fast", {words.at (1).toLongLong (), words.at (2).toLongLong (), words.at (3).toLongLong ()},
               {QString::fromLatin1 (words.at (4)), QString::fromLatin1 (words.at (5))},
               {decode (words.at (6)), decode (words.at (7))}};
      else if ("ref" == kind && words.size () == 3)
        row = {"ref", {RefPoints}, {bitsOf (12000.0f / 6912.0f)}, {decode (words.at (2))}};
      else continue;
      run.rows << row;
    }
  run.ok = true;
  return run;
}

QStringList modelSettings (bool lowSidelobes, int stop = 50)
{
  return {QString {"low_sidelobes %1"}.arg (lowSidelobes ? 1 : 0), QString {"stop %1"}.arg (stop)};
}

QJsonObject spectrumKeys (bool lowSidelobes)
{
  return {{"spectrum", true}, {"low_sidelobes", lowSidelobes}};
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

// A refspec.dat that the GUI's measurement writes from the FT8 recording, or empty.
QByteArray measuredReference (QString const& directory)
{
  auto const pcm = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
  if (pcm.isEmpty ()) return {};
  auto const run = runModel (directory, pcm, {"datadir " + directory, "reference measure",
                                              QString {"period live 0 %1"}.arg (Ft8PeriodSamples)});
  return run.ok ? readFile (directory + "/refspec.dat") : QByteArray {};
}

// The live rows the GUI draws for a period: Detector hands dataSink each
// whole block from the period's start, and drops the part block at its end.
int liveRows (int periodSamples, int block = HsymSamples)
{
  return periodSamples / block;
}

// diskDat's rows for a file of kin samples.
int fileRows (int kin, int stop)
{
  return std::min (stop, kin / HsymSamples);
}

// A non-empty directory where refspec.dat goes: the codec can neither open
// it as a file nor remove it, as root too (CI's container user), where a
// read-only -a stops only other users.
bool blockRefspec (QString const& directory)
{
  return QDir {directory}.mkdir ("refspec.dat")
    && writeFile (directory + "/refspec.dat/keep", "keeps the directory non-empty\n");
}

#ifdef Q_OS_UNIX
// A codec on pipes the test drives from two threads, so that the reader can
// stop reading, or read slowly, while the writer goes on.
class PipedCodec
{
public:
  using Clock = std::chrono::steady_clock;

  PipedCodec (QString const& directory, QStringList const& environment, QString const& temporary = {})
  {
    int in[2], out[2], err[2];
    if (pipe (in) || pipe (out) || pipe (err)) return;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init (&actions);
    posix_spawn_file_actions_adddup2 (&actions, in[0], 0);
    posix_spawn_file_actions_adddup2 (&actions, out[1], 1);
    posix_spawn_file_actions_adddup2 (&actions, err[1], 2);
    for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) posix_spawn_file_actions_addclose (&actions, fd);
    auto const program = QByteArray {WSJT_CODEC_EXECUTABLE};
    auto const dir = directory.toLocal8Bit ();
    auto const tmp = (temporary.isEmpty () ? directory : temporary).toLocal8Bit ();
    char const * argv[] = {program.constData (), "-a", dir.constData (), "-t", tmp.constData (), nullptr};
    QList<QByteArray> settings;
    for (char ** e = environ; *e; ++e) settings << QByteArray {*e};
    for (auto const& setting : environment) settings << setting.toLocal8Bit ();
    std::vector<char *> envp;
    for (auto& setting : settings) envp.push_back (setting.data ());
    envp.push_back (nullptr);
    started_ = Clock::now ();
    started = 0 == posix_spawn (&pid_, program.constData (), &actions, nullptr, const_cast<char **> (argv), envp.data ());
    posix_spawn_file_actions_destroy (&actions);
    close (in[0]);
    close (out[1]);
    close (err[1]);
    in_ = in[1];
    if (!started)
      {
        close (in_);
        close (out[0]);
        close (err[0]);
        return;
      }
    readers_.emplace_back ([this, fd = out[0]] {readLines (fd, false);});
    readers_.emplace_back ([this, fd = err[0]] {readLines (fd, true);});
  }

  ~PipedCodec ()
  {
    closeInput ();
    if (started) waitpid (pid_, nullptr, 0);
    for (auto& reader : readers_) reader.join ();
  }

  qint64 now () const {return std::chrono::duration_cast<std::chrono::milliseconds> (Clock::now () - started_).count ();}

  // Writes all the bytes; returns when the pipe took the last.
  qint64 write (QByteArray const& bytes)
  {
    for (qint64 at = 0; at < bytes.size ();)
      {
        auto const n = ::write (in_, bytes.constData () + at, size_t (bytes.size () - at));
        if (n <= 0) return -1;
        at += n;
      }
    return now ();
  }

  void closeInput ()
  {
    if (in_ >= 0) close (in_);
    in_ = -1;
  }

  // Stops reading stdout for ms from now.
  void stallReading (int ms)
  {
    std::lock_guard<std::mutex> lock {mutex_};
    stallUntil_ = now () + ms;
  }

  // Reads stdout again at once.
  void resumeReading ()
  {
    std::lock_guard<std::mutex> lock {mutex_};
    stallUntil_ = 0;
  }

  // Reads stdout at no more than bytesPerMs bytes a millisecond (0: as fast
  // as it comes).
  void paceReading (int bytesPerMs)
  {
    std::lock_guard<std::mutex> lock {mutex_};
    pace_ = bytesPerMs;
  }

  // Stops reading stdout for ms once a line of this type has been read.
  void stallAfter (QByteArray const& type, int ms)
  {
    std::lock_guard<std::mutex> lock {mutex_};
    stallType_ = type;
    stallAfterMs_ = ms;
  }

  // Waits for the exit; false after ms.
  bool finish (int ms)
  {
    closeInput ();
    auto const end = now () + ms;
    int status = 0;
    while (now () < end)
      {
        auto const done = waitpid (pid_, &status, WNOHANG);
        if (done == pid_)
          {
            exited = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
            started = false;
            for (auto& reader : readers_) reader.join ();
            readers_.clear ();
            return 0 == exited;
          }
        std::this_thread::sleep_for (std::chrono::milliseconds {20});
      }
    kill (pid_, SIGKILL);
    waitpid (pid_, nullptr, 0);
    started = false;
    for (auto& reader : readers_) reader.join ();
    readers_.clear ();
    return false;
  }

  QList<Line> lines () const
  {
    std::lock_guard<std::mutex> lock {mutex_};
    return lines_;
  }

  bool started {false};
  int exited {-1};

private:
  void readLines (int fd, bool error)
  {
    QByteArray buffer;
    char chunk[65536];
    for (;;)
      {
        if (!error)
          for (;;)
            {
              qint64 until;
              {
                std::lock_guard<std::mutex> lock {mutex_};
                until = stallUntil_;
              }
              if (now () >= until) break;
              std::this_thread::sleep_for (std::chrono::milliseconds {5});
            }
        auto const n = ::read (fd, chunk, error ? sizeof chunk : 4096);
        if (n <= 0) break;
        int pace;
        {
          std::lock_guard<std::mutex> lock {mutex_};
          pace = pace_;
        }
        if (!error && pace > 0) std::this_thread::sleep_for (std::chrono::microseconds {1000 * n / pace});
        auto const ms = now ();
        buffer.append (chunk, int (n));
        int from = 0;
        std::lock_guard<std::mutex> lock {mutex_};
        for (int end; (end = buffer.indexOf ('\n', from)) >= 0; from = end + 1)
          {
            lines_.append ({ms, error, buffer.mid (from, end - from)});
            if (!error && stallAfterMs_ > 0 && typeOf (lines_.last ().text) == stallType_)
              {
                stallUntil_ = now () + stallAfterMs_;
                stallAfterMs_ = 0;
              }
          }
        buffer.remove (0, from);
      }
    close (fd);
  }

  pid_t pid_ {0};
  int in_ {-1};
  Clock::time_point started_;
  mutable std::mutex mutex_;
  QList<Line> lines_;
  qint64 stallUntil_ {0};
  QByteArray stallType_;
  int stallAfterMs_ {0};
  int pace_ {0};
  std::vector<std::thread> readers_;
};

// A process's threads (Linux: /proc; macOS: ps -M), or -1.
int threadCount (qint64 pid)
{
  QDir task {QString {"/proc/%1/task"}.arg (pid)};
  if (task.exists ()) return int (task.entryList (QDir::Dirs | QDir::NoDotAndDotDot).size ());
  QProcess ps;
  ps.start ("ps", {"-M", "-p", QString::number (pid)});
  if (!ps.waitForFinished (10000)) return -1;
  return int (ps.readAllStandardOutput ().trimmed ().split ('\n').size ()) - 1;
}

// A session's spectrum rows around its one rows_dropped line, for periods
// of blocksPerPeriod rows each: those before it must be the session's first
// blocks and those after it its last, and the line must count the rows
// between. Sets first, the session block the rows after the line begin at,
// and after, how many there are; returns the first problem, or empty.
QString droppedRun (QList<Line> const& lines, int blocksPerPeriod, int periods, int& first, int& after)
{
  QList<qint64> before, since;
  int countLines = 0;
  qint64 dropped = 0;
  for (auto const& line : lines)
    {
      if (line.error) continue;
      auto const type = typeOf (line.text);
      if ("rows_dropped" == type)
        {
          ++countLines;
          dropped += qint64 (json (line.text).value ("lines").toDouble ());
        }
      else if ("spectrum" == type) (countLines ? since : before) << qint64 (json (line.text).value ("k").toDouble ());
    }
  if (1 != countLines) return QString {"%1 rows_dropped lines"}.arg (countLines);
  auto const total = periods * blocksPerPeriod;
  auto const kOf = [&] (int block) {return qint64 (block % blocksPerPeriod + 1) * HsymSamples;};
  for (int i = 0; i < before.size (); ++i)
    if (before.at (i) != kOf (i)) return QString {"row %1 before the line has k %2, not %3"}.arg (i).arg (before.at (i)).arg (kOf (i));
  first = total - int (since.size ());
  after = int (since.size ());
  for (int i = 0; i < since.size (); ++i)
    if (since.at (i) != kOf (first + i))
      return QString {"row %1 after the line has k %2, not %3: not the session's last blocks"}.arg (i)
        .arg (since.at (i)).arg (kOf (first + i));
  auto const kept = int (before.size ());
  if (first < kept) return "the rows before and after the line overlap";
  if (dropped != first - kept)
    return QString {"rows_dropped counts %1 rows; blocks %2 to %3 are missing"}.arg (dropped).arg (kept).arg (first - 1);
  return {};
}

// Two periods of a mode's recording, with stdout unread for stallMs from the
// start; the lines once the codec has exited, or the failure.
QString stalledSession (QJsonObject const& settings, QByteArray const& pcm, Mode const& m, int stallMs,
                        QList<Line>& lines)
{
  QTemporaryDir directory;
  PipedCodec codec {directory.path (), {}};
  if (!codec.started) return "the codec did not start";
  codec.stallReading (stallMs);
  codec.write (sessionHeader () + configure (settings) + audio (pcm) + configure (labelFor (m, 1)) + audio (pcm)
               + halt ());
  if (!codec.finish (waitMs ())) return QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (codec.lines ()));
  lines = codec.lines ();
  return {};
}

// The model's rows without the session blocks [from, to).
QList<Row> withoutBlocks (QList<Row> const& rows, int blocksPerPeriod, int from, int to)
{
  QList<Row> kept;
  auto const periods = byPeriod (rows);
  bool keep = true;
  for (int p = 0; p < periods.size (); ++p)
    for (auto const& row : periods.at (p))
      {
        if ("spectrum" == row.kind)
          {
            auto const block = p * blocksPerPeriod + int (row.ints.at (0) / HsymSamples) - 1;
            keep = block < from || block >= to;
          }
        if (keep) kept << row;
      }
  return kept;
}

// Polls what a piped codec has read until done says so; false after waitMs ().
bool waitForLines (PipedCodec const& codec, std::function<bool (QList<Line> const&)> const& done)
{
  QElapsedTimer timer;
  timer.start ();
  while (!done (codec.lines ()))
    {
      if (timer.elapsed () > waitMs ()) return false;
      std::this_thread::sleep_for (std::chrono::milliseconds {5});
    }
  return true;
}

// The decisions on stderr (pass, skip or defer) for half-symbol nzhsym.
int decisionsAt (QList<Line> const& lines, int nzhsym)
{
  int found = 0;
  for (auto const& each : decisions (lines)) found += each.section (' ', 1, 1) == QString::number (nzhsym);
  return found;
}

// The last line that matches, or -1.
int findLast (QList<Line> const& lines, Match const& match)
{
  for (int i = int (lines.size ()) - 1; i >= 0; --i)
    if (match (lines.at (i))) return i;
  return -1;
}

// The lines of the reply to request id.
Match replyLine (int id)
{
  QRegularExpression const pattern {QString {"\"id\":%1[,}]"}.arg (id)};
  return [pattern] (Line const& line) {return !line.error && pattern.match (QString::fromUtf8 (line.text.left (96))).hasMatch ();};
}
#endif

// A JTTY render whose one segment's audio is computed after its segment line.
QJsonObject const JttyLongRender {{"t", "render"}, {"id", 7}, {"freq", 1500}, {"rate", 48000},
                                  {"text", "CQ CQ CQ DE K1ABC K1ABC K1ABC THIS IS A LONG MESSAGE FOR THE TEST OF THE QUEUE"}};
}

class TestStreamingSpectrum final : public QObject
{
  Q_OBJECT

private:
  QTemporaryDir modelDirectory_;

  QString modelDir (QString const& name) const {return modelDirectory_.path () + '/' + name;}

private slots:
  // Every period-mode row, JT65+JT9's among them, live through every block of
  // the period and from a file up to the stop, equals the GUI's symspec_ row
  // for the same audio byte for byte on a fresh session, and there are as many
  // as the GUI's dataSink calls. No spectrum_average line comes.
  void periodRowsMatchTheGui_data ()
  {
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<bool> ("live");
    QTest::addColumn<int> ("periods");    // whole periods
    QTest::addColumn<int> ("extra");      // samples of one more period
    QTest::addColumn<bool> ("lowSidelobes");
    QTest::newRow ("ft8 live") << QString {"ft8"} << true << 2 << 0 << false;
    QTest::newRow ("ft8 live low sidelobes") << QString {"ft8"} << true << 1 << 0 << true;
    QTest::newRow ("ft8 live and a half") << QString {"ft8"} << true << 1 << Ft8PeriodSamples / 2 << false;
    QTest::newRow ("ft8 file") << QString {"ft8"} << false << 2 << 0 << false;
    QTest::newRow ("ft8 file short") << QString {"ft8"} << false << 0 << 100000 << false;
    QTest::newRow ("ft8-mtd file") << QString {"ft8-mtd"} << false << 1 << 0 << false;
    QTest::newRow ("ft4 live") << QString {"ft4"} << true << 2 << 0 << false;
    QTest::newRow ("ft4 file low sidelobes") << QString {"ft4"} << false << 1 << 0 << true;
    QTest::newRow ("jt4 file") << QString {"jt4"} << false << 1 << 0 << false;
    QTest::newRow ("jt9 file") << QString {"jt9"} << false << 1 << 0 << false;
    QTest::newRow ("jt65 file") << QString {"jt65"} << false << 1 << 0 << false;
    QTest::newRow ("jt65jt9 file") << QString {"jt65jt9"} << false << 1 << 0 << false;
    QTest::newRow ("q65 file") << QString {"q65"} << false << 1 << 0 << true;
    QTest::newRow ("fst4 file") << QString {"fst4"} << false << 1 << 0 << false;
    QTest::newRow ("fst4w file") << QString {"fst4w"} << false << 1 << 0 << false;
    QTest::newRow ("fst4-60 live low sidelobes") << QString {"fst4-60"} << true << 1 << 0 << true;
    QTest::newRow ("wspr live") << QString {"wspr"} << true << 1 << 0 << false;
    QTest::newRow ("wspr file") << QString {"wspr"} << false << 1 << 0 << false;
  }

  void periodRowsMatchTheGui ()
  {
    QFETCH (QString, mode);
    QFETCH (bool, live);
    QFETCH (int, periods);
    QFETCH (int, extra);
    QFETCH (bool, lowSidelobes);
    auto const m = modeOf (mode);
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const settings = with (with (m.settings, {{"depth", 1}, {"live_audio", live}}), spectrumKeys (lowSidelobes));
    QByteArray input = sessionHeader () + configure (settings);
    QByteArray sent;
    QStringList script = modelSettings (lowSidelobes, m.stop);
    for (int p = 0; p < periods + (extra > 0 ? 1 : 0); ++p)
      {
        auto const part = p < periods ? pcm : pcm.left (2 * extra);
        if (p > 0) input += configure (labelFor (m, p));
        input += audio (part);
        script << QString {"period %1 %2 %3"}.arg (live ? "live" : "file").arg (sent.size () / 2).arg (part.size () / 2);
        sent += part;
      }
    input += (!live && extra > 0 ? flush () : QByteArray {}) + halt ();
    auto const codec = runInput (input);
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    QVERIFY2 (longestLine (codec->lines) < LineLimit + 1, qPrintable (QString::number (longestLine (codec->lines))));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const perPeriod = live ? liveRows (m.samples) : fileRows (m.samples, m.stop);
    auto const lastRows = extra > 0 ? (live ? liveRows (extra) : fileRows (extra, m.stop)) : 0;
    auto const expected = periods * perPeriod + lastRows;
    auto const model = runModel (modelDir (QTest::currentDataTag ()), sent, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    QCOMPARE (countKind (model.rows, "spectrum"), expected);
    auto const difference = compareKeptRows (codec->lines, model.rows, live);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    QCOMPARE (count (codec->lines, outType ("spectrum_average")), 0);
  }

  // With a jt9_wisdom.dat that holds plans a JT65+JT9 session measured, the
  // rows stay within 1e-6 of the GUI's, relative to each value.
  void rowsFromAPopulatedWisdom ()
  {
    auto const jt65jt9 = modeOf ("jt65jt9");
    auto const mixed = period (wavPcm (jt65jt9.wav), jt65jt9.samples);
    auto const ft8 = modeOf ("ft8");
    auto const pcm = period (wavPcm (ft8.wav), ft8.samples);
    QVERIFY2 (!mixed.isEmpty () && !pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    auto const populate = runInput (sessionHeader () + configure (jt65jt9.settings) + audio (mixed) + halt (), shared);
    QVERIFY2 (populate->clean (), qPrintable (populate->report ()));
    auto const wisdom = readFile (directory.path () + "/jt9_wisdom.dat");
    QVERIFY2 (!wisdom.isEmpty (), "the JT65+JT9 session saved no wisdom");
    qInfo ().noquote () << QString {"jt9_wisdom.dat: %1 bytes, %2 lines"}.arg (wisdom.size ()).arg (wisdom.count ('\n'));
    auto const codec = runInput (sessionHeader () + configure (with (with (ft8.settings, {{"depth", 1}}),
                                                                      spectrumKeys (false)))
                                 + audio (pcm) + halt (), shared);
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelDir ("wisdom"), pcm, modelSettings (false)
                                 << QString {"period file 0 %1"}.arg (Ft8PeriodSamples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    double worst = 0;
    auto const difference = compareRows (rows.rows, model.rows, 1e-6, &worst);
    qInfo ().noquote () << QString {"greatest relative difference from the GUI's rows: %1"}.arg (worst, 0, 'g', 3);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // MSK144's and fast JT9's fast_spectrum rows equal hspec_'s outputs per
  // call, as fastSink draws them: per 3584-sample block in MSK144 and per
  // 3456 in fast JT9, which has no spectrum rows. Any audio draws the graph.
  void fastRowsMatchTheGui_data ()
  {
    QTest::addColumn<QString> ("recording");
    QTest::addColumn<QString> ("utc");
    QTest::addColumn<bool> ("live");
    QTest::addColumn<int> ("trperiod");
    QTest::addColumn<int> ("copies");   // of the recording, back to back
    QTest::addColumn<bool> ("fastJt9");
    QTest::newRow ("120500") << QString {"181211_120500"} << QString {"12:05:00"} << false << 15 << 1 << false;
    QTest::newRow ("120800") << QString {"181211_120800"} << QString {"12:08:00"} << false << 15 << 1 << false;
    QTest::newRow ("120800 live") << QString {"181211_120800"} << QString {"12:08:00"} << true << 15 << 1 << false;
    QTest::newRow ("120500 TR 30") << QString {"181211_120500"} << QString {"12:05:00"} << false << 30 << 2 << false;
    QTest::newRow ("120800 TR 5") << QString {"181211_120800"} << QString {"12:08:00"} << true << 5 << 1 << false;
    QTest::newRow ("fast JT9") << QString {"181211_120800"} << QString {"12:08:00"} << false << 15 << 1 << true;
    QTest::newRow ("fast JT9 live") << QString {"181211_120800"} << QString {"12:08:00"} << true << 15 << 1 << true;
    QTest::newRow ("fast JT9 TR 30") << QString {"181211_120500"} << QString {"12:05:00"} << false << 30 << 2 << true;
    QTest::newRow ("fast JT9 TR 10") << QString {"181211_120800"} << QString {"12:08:00"} << true << 10 << 1 << true;
    QTest::newRow ("fast JT9 TR 5") << QString {"181211_120800"} << QString {"12:08:00"} << true << 5 << 1 << true;
  }

  void fastRowsMatchTheGui ()
  {
    QFETCH (QString, recording);
    QFETCH (QString, utc);
    QFETCH (bool, live);
    QFETCH (int, trperiod);
    QFETCH (int, copies);
    QFETCH (bool, fastJt9);
    auto one = period (wavPcm (inSamples ("MSK144/" + recording + ".wav")), Ft8PeriodSamples);
    QVERIFY2 (!one.isEmpty (), "no recording");
    QByteArray pcm;
    for (int i = 0; i < copies; ++i) pcm += one;
    auto const periodSamples = trperiod * SampleRate;
    auto const periods = int (pcm.size () / 2) / periodSamples;
    auto const settings = with (fastJt9 ? FastJt9Settings : Msk144Settings,
                                {{"trperiod", trperiod}, {"utc", utc}, {"live_audio", live}, {"spectrum", true}});
    auto const block = fastJt9 ? HsymSamples : BlockSamples;
    QByteArray input = sessionHeader () + configure (settings);
    QStringList script;
    auto const start = QTime::fromString (utc, "HH:mm:ss");
    for (int p = 0; p < periods; ++p)
      {
        if (p > 0) input += configure ({{"utc", start.addSecs (p * trperiod).toString ("HH:mm:ss")}});
        input += audio (samples (pcm, p * periodSamples, (p + 1) * periodSamples));
        script << QString {"%1 %2 %3 %4"}.arg (fastJt9 ? "fastjt9" : "msk144").arg (p * periodSamples)
          .arg (periodSamples).arg (trperiod);
      }
    auto const codec = runInput (input + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    QVERIFY2 (longestLine (codec->lines) < LineLimit + 1, qPrintable (QString::number (longestLine (codec->lines))));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    QCOMPARE (countKind (rows.rows, "spectrum"), 0);
    auto const model = runModel (modelDir (QTest::currentDataTag ()), pcm.left (2 * periods * periodSamples), script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    // Every block of a period of at most 30 s adds columns.
    QCOMPARE (countKind (model.rows, "fast"), periods * liveRows (periodSamples, block));
    auto const difference = compareKeptRows (codec->lines, model.rows, live);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // JTTY's rows are symspec_'s over the session's samples as one period from
  // its first, bit for bit, numbered by the stream's samples. No
  // spectrum_average line comes.
  void jttyRowsMatchTheGui_data ()
  {
    QTest::addColumn<bool> ("lowSidelobes");
    QTest::addColumn<int> ("before");   // FT8 samples the stream read before the session
    QTest::addColumn<int> ("frame");    // samples an audio frame holds
    QTest::newRow ("plain") << false << 0 << FrameSamples;
    QTest::newRow ("low sidelobes after FT8 audio") << true << 2 * SampleRate << FrameSamples;
    // The first frame ends one sample short of the first block's end.
    QTest::newRow ("frames of 3455 samples") << false << 0 << HsymSamples - 1;
  }

  void jttyRowsMatchTheGui ()
  {
    QFETCH (bool, lowSidelobes);
    QFETCH (int, before);
    QFETCH (int, frame);
    auto const pcm = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
    auto const ft8 = period (wavPcm (modeOf ("ft8").wav), Ft8PeriodSamples);
    QVERIFY2 (!pcm.isEmpty () && !ft8.isEmpty (), "no recording");
    QByteArray input = sessionHeader ();
    if (before > 0) input += configure (modeOf ("ft8").settings) + audio (ft8.left (2 * before));
    input += configure (with (JttySettings, spectrumKeys (lowSidelobes))) + audio (pcm, frame) + halt ();
    auto const codec = runInput (input);
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    QVERIFY2 (longestLine (codec->lines) < LineLimit + 1, qPrintable (QString::number (longestLine (codec->lines))));
    auto const session = find (codec->lines, outType ("session"));
    QVERIFY2 (session >= 0, qPrintable (codec->report ()));
    auto const fields = json (codec->lines.at (session).text);
    auto const reception = fields.value ("session").toInt ();
    auto const origin = qint64 (fields.value ("origin").toDouble ());
    QCOMPARE (origin, qint64 (before));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const frames = int (pcm.size () / 2) / HsymSamples;
    QCOMPARE (countKind (rows.rows, "jtty"), frames);
    QCOMPARE (countKind (rows.rows, "spectrum"), 0);
    auto const model = runModel (modelDir (QTest::currentDataTag ()), pcm,
                                 modelSettings (lowSidelobes)
                                 << QString {"jtty 0 %1 %2 %3"}.arg (pcm.size () / 2).arg (reception).arg (origin));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareRows (rows.rows, model.rows);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    QCOMPARE (count (codec->lines, outType ("spectrum_average")), 0);
  }

  // The same audio as an FT8 file session and as a JTTY session from the
  // same first sample: each row both have, the JTTY row ending where the FT8
  // row's k is, has the same data, power and peak, bit for bit.
  void jttyRowsEqualPeriodRows_data ()
  {
    QTest::addColumn<bool> ("lowSidelobes");
    QTest::newRow ("plain") << false;
    QTest::newRow ("low sidelobes") << true;
  }

  void jttyRowsEqualPeriodRows ()
  {
    QFETCH (bool, lowSidelobes);
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const ft8 = runInput (sessionHeader () + configure (with (with (m.settings, {{"depth", 1}}),
                                                                   spectrumKeys (lowSidelobes)))
                               + audio (pcm) + halt ());
    QVERIFY2 (ft8->clean (), qPrintable (ft8->report ()));
    auto const jtty = runInput (sessionHeader () + configure (with (JttySettings, spectrumKeys (lowSidelobes)))
                                + audio (pcm, FrameSamples) + halt ());
    QVERIFY2 (jtty->clean (), qPrintable (jtty->report ()));
    auto const session = find (jtty->lines, outType ("session"));
    QVERIFY2 (session >= 0, qPrintable (jtty->report ()));
    QCOMPARE (qint64 (json (jtty->lines.at (session).text).value ("origin").toDouble ()), qint64 {0});
    auto const periodRows = rowsOf (ft8->lines);
    auto const jttyRows = rowsOf (jtty->lines);
    QVERIFY2 (periodRows.problem.isEmpty () && jttyRows.problem.isEmpty (),
              qPrintable (periodRows.problem + jttyRows.problem));
    auto const shared = fileRows (m.samples, m.stop);
    QCOMPARE (countKind (periodRows.rows, "spectrum"), shared);
    QCOMPARE (countKind (jttyRows.rows, "jtty"), liveRows (m.samples));
    for (int i = 0; i < shared; ++i)
      {
        auto const& a = periodRows.rows.at (i);   // k, hsym, bins
        auto const& b = jttyRows.rows.at (i);     // reception, begin, end, bins
        auto const what = QString {"row %1: the JTTY row\n  %2\nthe FT8 row\n  %3"}.arg (i).arg (b.head (), a.head ());
        QVERIFY2 (b.ints.at (2) == a.ints.at (0) && b.ints.at (1) == a.ints.at (0) - HsymSamples
                  && b.ints.at (3) == a.ints.at (2) && b.floats == a.floats, qPrintable (what));
        QVERIFY2 (b.arrays == a.arrays, qPrintable (what + "\ntheir data differ"));
      }
  }

  // JTTY's rows agree with the GUI's JttySpectrum on the same samples within
  // what develop accepts between JttySpectrum and symspec_
  // (tests/unit/jtty/test_jtty_spectrum.cpp): every bin within 5e-4 of the
  // greatest of 1 and the two values, power and peak within 0.002 dB, as
  // JttySpectrum sums the power in double and symspec_ in single precision.
  void jttyRowsNearTheGuisJttySpectrum_data ()
  {
    QTest::addColumn<bool> ("lowSidelobes");
    QTest::newRow ("plain") << false;
    QTest::newRow ("low sidelobes") << true;
  }

  void jttyRowsNearTheGuisJttySpectrum ()
  {
    QFETCH (bool, lowSidelobes);
    auto const pcm = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const codec = runInput (sessionHeader () + configure (with (JttySettings, spectrumKeys (lowSidelobes)))
                                 + audio (pcm, FrameSamples) + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const session = find (codec->lines, outType ("session"));
    QVERIFY2 (session >= 0, qPrintable (codec->report ()));
    auto const fields = json (codec->lines.at (session).text);
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelDir (QTest::currentDataTag ()), pcm, modelSettings (lowSidelobes)
                                 << QString {"jttyspectrum 0 %1 %2 %3"}.arg (pcm.size () / 2)
                                    .arg (fields.value ("session").toInt ())
                                    .arg (qint64 (fields.value ("origin").toDouble ())));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const frames = int (pcm.size () / 2) / HsymSamples;
    QCOMPARE (countKind (rows.rows, "jtty"), frames);
    QCOMPARE (countKind (model.rows, "jttyspectrum"), frames);
    auto const tolerated = [] (float actual, float expected) {
      return std::abs (actual - expected) <= 5.e-4f * std::max ({1.f, std::abs (actual), std::abs (expected)});
    };
    float worstDb = 0, worstBin = 0;
    for (int i = 0; i < frames; ++i)
      {
        auto const& a = rows.rows.at (i);
        auto const& b = model.rows.at (i);
        auto const what = QString {"row %1: the codec's\n  %2\nJttySpectrum's\n  %3"}.arg (i).arg (a.head (), b.head ());
        QVERIFY2 (a.ints == b.ints && a.floats.at (2) == b.floats.at (2), qPrintable (what));
        for (int f = 0; f < 2; ++f)
          {
            auto const difference = std::abs (floatOfBits (a.floats.at (f)) - floatOfBits (b.floats.at (f)));
            worstDb = std::max (worstDb, difference);
            QVERIFY2 (difference < 0.002f, qPrintable (what));
          }
        auto const& x = a.arrays.at (0);
        auto const& y = b.arrays.at (0);
        QCOMPARE (x.size (), y.size ());
        for (int v = 0; v < x.size () / 4; ++v)
          {
            auto const p = floatAt (x, v), q = floatAt (y, v);
            worstBin = std::max (worstBin, std::abs (p - q) / std::max ({1.f, std::abs (p), std::abs (q)}));
            QVERIFY2 (tolerated (p, q), qPrintable (QString {"%1\nbin %2: %3 against %4"}.arg (what).arg (v)
                                                    .arg (p, 0, 'g', 9).arg (q, 0, 'g', 9)));
          }
      }
    qInfo ().noquote () << QString {"greatest power or peak difference %1 dB, greatest scaled bin difference %2"}
      .arg (worstDb, 0, 'g', 3).arg (worstBin, 0, 'g', 3);
  }

  // Spectrum turned on within a period: the GUI has no such case, as its
  // displays never turn off; the codec gives that period no rows and the next
  // its whole rows, the GUI's (lib/streaming_emit.f90's schema).
  void periodRowsBeginAtTheNextPeriod ()
  {
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const on = 5 * HsymSamples + 1000;
    auto const codec = runInput (sessionHeader () + configure (with (m.settings, {{"live_audio", true}, {"depth", 1}}))
                                 + toggled (pcm + pcm, {on}, {}, {{m.samples, labelFor (m, 1)}}) + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelDir ("turned on within a period"), pcm + pcm, modelSettings (true)
                                 << QString {"period live 0 %1"}.arg (m.samples)
                                 << QString {"period live %1 %1"}.arg (m.samples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const periods = byPeriod (model.rows);
    QCOMPARE (periods.size (), 2);
    QCOMPARE (countKind (periods.at (1), "spectrum"), liveRows (m.samples));
    auto const difference = compareKeptRows (codec->lines, periods.at (1), true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // MSK144 and fast JT9 with spectrum turned on within a period: the codec
  // runs hspec_ over the period's earlier blocks without rows, so each block
  // whose last sample comes with spectrum on has the GUI's row, its first
  // column past 0 (lib/streaming_emit.f90's schema).
  void fastRowsWhenTurnedOnMidPeriod_data ()
  {
    QTest::addColumn<QList<int>> ("toggles");   // session samples where spectrum turns on, off, on
    QTest::addColumn<bool> ("fastJt9");
    QTest::newRow ("on after block 20") << QList<int> {20 * BlockSamples} << false;
    QTest::newRow ("on in block 13, off after block 30, on after block 40")
      << QList<int> {12 * BlockSamples + 1000, 30 * BlockSamples, 40 * BlockSamples} << false;
    QTest::newRow ("on for period 1, off at period 2, on after its block 20")
      << QList<int> {0, Ft8PeriodSamples, Ft8PeriodSamples + 20 * BlockSamples} << false;
    QTest::newRow ("fast JT9 on in block 13, off after block 30, on after block 40")
      << QList<int> {12 * HsymSamples + 1000, 30 * HsymSamples, 40 * HsymSamples} << true;
  }

  void fastRowsWhenTurnedOnMidPeriod ()
  {
    QFETCH (QList<int>, toggles);
    QFETCH (bool, fastJt9);
    auto const one = period (wavPcm (inSamples ("MSK144/181211_120800.wav")), Ft8PeriodSamples);
    QVERIFY2 (!one.isEmpty (), "no recording");
    auto const two = one + one;
    auto const codec = runInput (sessionHeader () + configure (with (fastJt9 ? FastJt9Settings : Msk144Settings,
                                                                     {{"live_audio", true}}))
                                 + toggled (two, toggles, {}, {{Ft8PeriodSamples, {{"utc", "12:08:15"}}}}) + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const kind = fastJt9 ? "fastjt9" : "msk144";
    auto const model = runModel (modelDir (QTest::currentDataTag ()), two,
                                 {QString {"%1 0 %2 15"}.arg (kind).arg (Ft8PeriodSamples),
                                  QString {"%1 %2 %2 15"}.arg (kind).arg (Ft8PeriodSamples)});
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const periods = byPeriod (model.rows);
    QCOMPARE (periods.size (), 2);
    QList<Row> expected;
    for (int p = 0; p < 2; ++p)
      for (auto const& row : periods.at (p))
        if (onAt (toggles, p * Ft8PeriodSamples + int (row.ints.at (0)) - 1)) expected << row;
    QVERIFY2 (!rows.rows.isEmpty (), "no rows");
    if (toggles.first () % Ft8PeriodSamples)
      QVERIFY2 (rows.rows.first ().ints.at (1) > 0, "the first row after spectrum came on does not start past column 0");
    auto const difference = compareKeptRows (codec->lines, expected, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // JTTY with spectrum turned on within a session: each turn-on's rows are
  // those of a row stream begun at the first sample after it, in the same
  // reception (lib/streaming_emit.f90's schema).
  void jttyRowsWhenTurnedOnMidSession_data ()
  {
    QTest::addColumn<QList<int>> ("toggles");   // session samples where spectrum turns on, off, on
    QTest::newRow ("on mid-session") << QList<int> {12345};
    QTest::newRow ("on, off and on") << QList<int> {12345, 60000, 90001};
  }

  void jttyRowsWhenTurnedOnMidSession ()
  {
    QFETCH (QList<int>, toggles);
    auto const pcm = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    // The second turn-on changes the other keys too.
    QList<QJsonObject> const on {{{"spectrum", true}}, {{"spectrum", true}, {"low_sidelobes", false}}};
    auto const codec = runInput (sessionHeader () + configure (with (JttySettings, {{"live_audio", true}}))
                                 + toggled (pcm, toggles, on, {}, FrameSamples) + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const session = find (codec->lines, outType ("session"));
    QVERIFY2 (session >= 0, qPrintable (codec->report ()));
    auto const reception = json (codec->lines.at (session).text).value ("session").toInt ();
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    QStringList script;
    auto const n = int (pcm.size () / 2);
    for (int i = 0; i < toggles.size (); i += 2)
      {
        auto const from = toggles.at (i);
        auto const to = i + 1 < toggles.size () ? toggles.at (i + 1) : n;
        script << modelSettings (on.value (i / 2).value ("low_sidelobes").toBool (true))
               << QString {"jtty %1 %2 %3 %1"}.arg (from).arg (to - from).arg (reception);
      }
    auto const model = runModel (modelDir (QTest::currentDataTag ()), pcm, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    QVERIFY2 (countKind (model.rows, "jtty") > 0, "the GUI's model drew no rows");
    auto const difference = compareKeptRows (codec->lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // Spectrum turned on within a period's first block: the block in progress
  // has its rows, so the period has rows from block 1, the GUI's.
  void periodRowsOnInTheFirstBlock ()
  {
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const codec = runInput (sessionHeader () + configure (with (m.settings, {{"live_audio", true}, {"depth", 1}}))
                                 + toggled (pcm + pcm, {1000}, {}, {{m.samples, labelFor (m, 1)}}) + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelDir ("on in the first block"), pcm + pcm, modelSettings (true)
                                 << QString {"period live 0 %1"}.arg (m.samples)
                                 << QString {"period live %1 %1"}.arg (m.samples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    QCOMPARE (countKind (model.rows, "spectrum"), 2 * liveRows (m.samples));
    auto const difference = compareKeptRows (codec->lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // A period that drew one row, then a whole one, the codec's rule where the
  // GUI's symspec_ and hspec_ would carry the first on (their k did not go
  // down): the second period's rows are a fresh period's, hsym and the first
  // column from the start, as the model's are in a process of its own.
  void rowsAfterAOneBlockPeriod_data ()
  {
    QTest::addColumn<bool> ("msk144");
    QTest::newRow ("ft8") << false;
    QTest::newRow ("msk144") << true;
  }

  void rowsAfterAOneBlockPeriod ()
  {
    QFETCH (bool, msk144);
    auto const m = modeOf ("ft8");
    auto const pcm = msk144 ? period (wavPcm (inSamples ("MSK144/181211_120800.wav")), Ft8PeriodSamples)
                            : period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const block = msk144 ? BlockSamples : HsymSamples;
    auto const settings = msk144 ? with (Msk144Settings, {{"live_audio", true}, {"spectrum", true}})
                                 : with (m.settings, {{"live_audio", true}, {"depth", 1}, {"spectrum", true}});
    auto const codec = runInput (sessionHeader () + configure (settings) + audio (samples (pcm, 0, block))
                                 + control ({{"t", "discontinuity"}}) + audio (pcm) + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const command = [&] (int count) {
      return msk144 ? QString {"msk144 0 %1 15"}.arg (count) : QString {"period live 0 %1"}.arg (count);
    };
    auto const first = runModel (modelDir (QString {"%1 one block"}.arg (QTest::currentDataTag ())),
                                 pcm.left (2 * block), modelSettings (true) << command (block));
    auto const second = runModel (modelDir (QString {"%1 then a period"}.arg (QTest::currentDataTag ())), pcm,
                                  modelSettings (true) << command (Ft8PeriodSamples));
    QVERIFY2 (first.ok && second.ok, qPrintable (first.failure + second.failure));
    QCOMPARE (first.rows.size (), 1);
    auto const difference = compareKeptRows (codec->lines, first.rows + second.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // Spectrum on at a period's start, off after its first block and on again
  // within it: that period keeps its one row, and the next has the GUI's rows.
  void periodRowsAfterSpectrumOffAtBlock1 ()
  {
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const again = 2 * HsymSamples + 1000;
    auto const codec = runInput (sessionHeader ()
                                 + configure (with (m.settings, {{"live_audio", true}, {"depth", 1}, {"spectrum", true}}))
                                 + audio (samples (pcm, 0, HsymSamples)) + configure ({{"spectrum", false}})
                                 + audio (samples (pcm, HsymSamples, again)) + configure ({{"spectrum", true}})
                                 + audio (samples (pcm, again, m.samples)) + configure (labelFor (m, 1)) + audio (pcm)
                                 + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelDir ("off after block 1"), pcm + pcm, modelSettings (true)
                                 << QString {"period live 0 %1"}.arg (m.samples)
                                 << QString {"period live %1 %1"}.arg (m.samples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const periods = byPeriod (model.rows);
    QCOMPARE (periods.size (), 2);
    auto const difference = compareKeptRows (codec->lines, QList<Row> {periods.at (0).first ()} + periods.at (1), true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // JT65+JT9 entered from a period mode that drew one row, with
  // low_sidelobes on: its rows are a period mode's, one per block from its
  // first, windowed, those of the model's period alone, where the GUI's
  // symspec_ would carry the one-row period on (its k did not go down).
  void jt65jt9RowsAfterAPeriodModeRow ()
  {
    auto const ft8 = modeOf ("ft8");
    auto const mixed = modeOf ("jt65jt9");
    auto const ft8Pcm = period (wavPcm (ft8.wav), ft8.samples);
    auto const pcm = period (wavPcm (mixed.wav), mixed.samples);
    QVERIFY2 (!ft8Pcm.isEmpty () && !pcm.isEmpty (), "no recording");
    auto const live = QJsonObject {{"live_audio", true}};
    auto const codec = runInput (sessionHeader () + configure (with (with (ft8.settings, {{"depth", 1}}),
                                                                     with (live, spectrumKeys (true))))
                                 + audio (samples (ft8Pcm, 0, HsymSamples))
                                 + configure (with (mixed.settings, live)) + audio (pcm) + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const first = runModel (modelDir ("jt65jt9 after one block"), ft8Pcm.left (2 * HsymSamples),
                                 modelSettings (true) << QString {"period live 0 %1"}.arg (HsymSamples));
    auto const second = runModel (modelDir ("jt65jt9 alone"), pcm,
                                  modelSettings (true) << QString {"period live 0 %1"}.arg (mixed.samples));
    QVERIFY2 (first.ok && second.ok, qPrintable (first.failure + second.failure));
    QCOMPARE (first.rows.size (), 1);
    QCOMPARE (countKind (second.rows, "spectrum"), liveRows (mixed.samples));
    auto const difference = compareKeptRows (codec->lines, first.rows + second.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // The reverse: a JT65+JT9 period with rows, then an FT8 period in the same
  // fresh -a. Each period's rows are the model's, FT8's bit for bit the GUI's
  // although JT65+JT9's decoder planned its own symspec_ before them.
  void periodRowsAfterAJt65jt9Period ()
  {
    auto const ft8 = modeOf ("ft8");
    auto const mixed = modeOf ("jt65jt9");
    auto const ft8Pcm = period (wavPcm (ft8.wav), ft8.samples);
    auto const pcm = period (wavPcm (mixed.wav), mixed.samples);
    QVERIFY2 (!ft8Pcm.isEmpty () && !pcm.isEmpty (), "no recording");
    auto const codec = runInput (sessionHeader () + configure (with (mixed.settings, {{"live_audio", true},
                                                                                      {"spectrum", true}}))
                                 + audio (pcm)
                                 + configure (with (ft8.settings, {{"live_audio", true}, {"depth", 1}}))
                                 + audio (ft8Pcm) + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelDir ("after a JT65+JT9 period"), pcm + ft8Pcm, modelSettings (true)
                                 << QString {"period live 0 %1"}.arg (mixed.samples)
                                 << QString {"period live %1 %2"}.arg (mixed.samples).arg (ft8.samples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const periods = byPeriod (model.rows);
    QCOMPARE (periods.size (), 2);
    QCOMPARE (countKind (periods.at (0), "spectrum"), liveRows (mixed.samples));
    QCOMPARE (countKind (periods.at (1), "spectrum"), liveRows (ft8.samples));
    auto const difference = compareKeptRows (codec->lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // JTTY while equalizing (ref_spectrum "apply", live) holds its samples to the
  // end of each 3456-sample block from the session's first sample, so spectrum
  // turned on within a block gives rows from that block's start, on the
  // equalized audio.
  void jttyRowsOnMidBlockWhileEqualizing ()
  {
    auto const pcm = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const reference = measuredReference (modelDir ("held block measure"));
    QVERIFY2 (!reference.isEmpty (), "no reference spectrum");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QVERIFY (writeFile (directory.path () + "/refspec.dat", reference));
    auto const modelPath = modelDir ("held block");
    QDir {}.mkpath (modelPath);
    QVERIFY (writeFile (modelPath + "/refspec.dat", reference));
    auto const on = 3 * HsymSamples + 1977;
    Options options;
    options.directory = directory.path ();
    auto const codec = runInput (sessionHeader () + configure (with (JttySettings, {{"live_audio", true},
                                                                                    {"ref_spectrum", "apply"}}))
                                 + toggled (pcm, {on}, {}, {}, FrameSamples) + halt (), options);
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelPath, pcm, modelSettings (true) << "datadir " + modelPath << "reference apply"
                                 << QString {"jtty 0 %1 1 0 %2"}.arg (pcm.size () / 2).arg (3 * HsymSamples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    QVERIFY2 (countKind (model.rows, "jtty") > 0 && 3 * HsymSamples == model.rows.at (1).ints.at (1),
              "the GUI's model drew no rows from the held block");
    auto const difference = compareKeptRows (codec->lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // With ref_spectrum "apply" the rows are the GUI's rows of its equalized
  // audio, in a period mode, MSK144 and JTTY, and each turn to apply sends
  // the curve it loads.
  void equalizedRowsMatchTheGui_data ()
  {
    QTest::addColumn<QString> ("kind");
    QTest::addColumn<QStringList> ("references");   // per period
    QTest::newRow ("ft4") << QString {"ft4"} << QStringList {"apply", "apply"};
    QTest::newRow ("ft4 apply off apply") << QString {"ft4"} << QStringList {"apply", "off", "apply"};
    QTest::newRow ("msk144") << QString {"msk144"} << QStringList {"apply"};
    QTest::newRow ("fast JT9") << QString {"fastjt9"} << QStringList {"apply"};
    QTest::newRow ("jtty") << QString {"jtty"} << QStringList {"apply"};
    QTest::newRow ("wspr") << QString {"wspr"} << QStringList {"apply"};
  }

  void equalizedRowsMatchTheGui ()
  {
    QFETCH (QString, kind);
    QFETCH (QStringList, references);
    auto const reference = measuredReference (modelDir (QString {"%1 measure"}.arg (QTest::currentDataTag ())));
    QVERIFY2 (!reference.isEmpty (), "no reference spectrum");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QVERIFY (writeFile (directory.path () + "/refspec.dat", reference));
    auto const applyDir = modelDir (QTest::currentDataTag ());
    QDir {}.mkpath (applyDir);
    QVERIFY (writeFile (applyDir + "/refspec.dat", reference));
    QByteArray input = sessionHeader ();
    QByteArray sent;
    QStringList script = modelSettings (true) << "datadir " + applyDir;
    if ("jtty" == kind)
      {
        auto const pcm = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        input += configure (with (JttySettings, {{"live_audio", true}, {"spectrum", true},
                                                 {"ref_spectrum", references.first ()}}))
          + audio (pcm, FrameSamples);
        script << "reference " + references.first () << QString {"jtty 0 %1 1 0"}.arg (pcm.size () / 2);
        sent = pcm;
      }
    else if ("msk144" == kind || "fastjt9" == kind)
      {
        auto const pcm = period (wavPcm (inSamples ("MSK144/181211_120800.wav")), Ft8PeriodSamples);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        input += configure (with ("msk144" == kind ? Msk144Settings : FastJt9Settings,
                                  {{"live_audio", true}, {"spectrum", true}, {"ref_spectrum", references.first ()}}))
          + audio (pcm);
        script << "reference " + references.first () << QString {"%1 0 %2 15"}.arg (kind).arg (Ft8PeriodSamples);
        sent = pcm;
      }
    else
      {
        auto const m = modeOf (kind);
        auto const pcm = period (wavPcm (m.wav), m.samples);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        input += configure (with (m.settings, {{"live_audio", true}, {"depth", 1}, {"spectrum", true}}));
        for (int p = 0; p < references.size (); ++p)
          {
            input += configure (with (p > 0 ? labelFor (m, p) : QJsonObject {}, {{"ref_spectrum", references.at (p)}}))
              + audio (pcm);
            script << "reference " + references.at (p) << QString {"period live %1 %2"}.arg (p * m.samples).arg (m.samples);
            sent += pcm;
          }
      }
    Options options;
    options.directory = directory.path ();
    auto const codec = runInput (input + halt (), options);
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    QVERIFY2 (longestLine (codec->lines) < LineLimit + 1, qPrintable (QString::number (longestLine (codec->lines))));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    // Each turn to apply loads the curve once.
    int loads = 0;
    QString previous {"off"};
    for (auto const& each : references)
      {
        loads += "apply" == each && "apply" != previous;
        previous = each;
      }
    QCOMPARE (countKind (rows.rows, "ref"), loads);
    auto const model = runModel (applyDir, sent, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    QCOMPARE (countKind (model.rows, "ref"), countKind (rows.rows, "ref"));
    auto const difference = compareKeptRows (codec->lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    // The filter changed what the rows show.
    QStringList plainScript = script;
    plainScript.replaceInStrings (QRegularExpression {"^reference .*"}, "reference off");
    auto const plain = runModel (modelDir (QString {"%1 plain"}.arg (QTest::currentDataTag ())), sent, plainScript);
    QVERIFY2 (plain.ok, qPrintable (plain.failure));
    QVERIFY2 (!compareRows (rows.rows, plain.rows).isEmpty (), "the equalized rows equal the plain ones");
  }

  // Measuring writes the GUI's refspec.dat for the same audio, and a
  // ref_spectrum line with its curve on each write.
  void measureWritesTheGuisFile_data ()
  {
    QTest::addColumn<QString> ("kind");
    QTest::newRow ("ft8") << QString {"ft8"};
    QTest::newRow ("msk144") << QString {"msk144"};
    QTest::newRow ("jtty") << QString {"jtty"};
  }

  void measureWritesTheGuisFile ()
  {
    QFETCH (QString, kind);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const modelPath = modelDir (QTest::currentDataTag ());
    QByteArray input = sessionHeader ();
    QByteArray sent;
    QStringList script {"datadir " + modelPath, "reference measure"};
    if ("jtty" == kind)
      {
        sent = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
        input += configure (with (JttySettings, {{"live_audio", true}, {"ref_spectrum", "measure"}}))
          + audio (sent, FrameSamples);
        script << QString {"jtty 0 %1 1 0"}.arg (sent.size () / 2);
      }
    else if ("msk144" == kind)
      {
        sent = period (wavPcm (inSamples ("MSK144/181211_120500.wav")), Ft8PeriodSamples);
        input += configure (with (Msk144Settings, {{"live_audio", true}, {"ref_spectrum", "measure"}})) + audio (sent);
        script << QString {"msk144 0 %1 15"}.arg (Ft8PeriodSamples);
      }
    else
      {
        auto const m = modeOf ("ft8");
        auto const pcm = period (wavPcm (m.wav), m.samples);
        input += configure (with (m.settings, {{"live_audio", true}, {"depth", 1}, {"ref_spectrum", "measure"}}))
          + audio (pcm) + configure (labelFor (m, 1)) + audio (pcm);
        script << QString {"period live 0 %1"}.arg (m.samples) << QString {"period live %1 %1"}.arg (m.samples);
        sent = pcm + pcm;
      }
    QVERIFY2 (!sent.isEmpty (), "no recording");
    Options options;
    options.directory = directory.path ();
    auto const codec = runInput (input + halt (), options);
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const rows = rowsOf (codec->lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelPath, sent, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    QVERIFY2 (countKind (model.rows, "ref") > 0, "the GUI's model wrote no refspec.dat");
    auto const difference = compareRows (rows.rows, onlyKinds (model.rows, {"ref"}));
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    auto const file = readFile (directory.path () + "/refspec.dat");
    QVERIFY2 (!file.isEmpty (), "the codec wrote no refspec.dat");
    QVERIFY2 (file == readFile (modelPath + "/refspec.dat"), "refspec.dat differs from the GUI's");
  }

  // ref_spectrum_clear deletes refspec.dat, as the GUI's Erase does, and
  // leaves ref_spectrum off: a later measurement starts again, and audio after
  // a clear while applying is not equalized.
  void clearDeletesTheFile_data ()
  {
    QTest::addColumn<QString> ("before");
    QTest::newRow ("measuring") << QString {"measure"};
    QTest::newRow ("applying") << QString {"apply"};
  }

  void clearDeletesTheFile ()
  {
    QFETCH (QString, before);
    auto const m = modeOf ("ft4");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const measuring = "measure" == before;
    auto const modelPath = modelDir (QTest::currentDataTag ());
    QDir {}.mkpath (modelPath);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const file = directory.path () + "/refspec.dat";
    if (!measuring)
      {
        auto const reference = measuredReference (modelPath + " measure");
        QVERIFY2 (!reference.isEmpty (), "no reference spectrum");
        QVERIFY (writeFile (file, reference));
        QVERIFY (writeFile (modelPath + "/refspec.dat", reference));
      }
    Options options;
    options.directory = directory.path ();
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (with (m.settings, {{"live_audio", true}, {"depth", 1},
                                                                    {"spectrum", !measuring},
                                                                    {"ref_spectrum", before}}))
                 + audio (pcm) + marker ());
    QVERIFY2 (codec.waitForCount (outHas ("configure_parse_error"), 1, waitMs (), "the first marker"),
              qPrintable (codec.report ()));
    QVERIFY2 (QFile::exists (file), "no refspec.dat before the clear");
    codec.write (configure ({{"ref_spectrum_clear", true}}) + marker ());
    QVERIFY2 (codec.waitForCount (outHas ("configure_parse_error"), 2, waitMs (), "the second marker"),
              qPrintable (codec.report ()));
    QVERIFY2 (!QFile::exists (file), "refspec.dat is still there after the clear");
    QByteArray second = configure (labelFor (m, 1));
    if (measuring) second += configure ({{"ref_spectrum", "measure"}});
    codec.write (second + audio (pcm) + halt ());
    QVERIFY2 (codec.finish (), qPrintable (codec.report ()));
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const rows = rowsOf (codec.lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    QStringList script = modelSettings (true) << "datadir " + modelPath << "reference " + before
      << QString {"period live 0 %1"}.arg (m.samples) << "erase";
    if (measuring) script << "reference measure";
    script << QString {"period live %1 %1"}.arg (m.samples);
    auto const model = runModel (modelPath, pcm + pcm, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    QVERIFY2 (countKind (rows.rows, "ref") > 0, "no ref_spectrum line");
    auto const difference = measuring ? compareRows (rows.rows, onlyKinds (model.rows, {"ref"}))
                                      : compareKeptRows (codec.lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    if (measuring)
      QVERIFY2 (readFile (file) == readFile (modelPath + "/refspec.dat"), "refspec.dat differs from the GUI's");
    else QVERIFY2 (!QFile::exists (file), "applying again wrote refspec.dat");
  }

  // Decodes of live audio with ref_spectrum "apply" equal the decodes of the
  // GUI's equalized audio sent without it.
  void equalizedDecodesMatchTheFilteredInput ()
  {
    auto const m = modeOf ("ft4");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const modelPath = modelDir ("equalized decodes");
    auto const reference = measuredReference (modelPath + " measure");
    QVERIFY2 (!reference.isEmpty (), "no reference spectrum");
    QDir {}.mkpath (modelPath);
    QVERIFY (writeFile (modelPath + "/refspec.dat", reference));
    auto const equalizedPath = modelPath + "/equalized.pcm";
    auto const model = runModel (modelPath, pcm + pcm, {"datadir " + modelPath, "reference apply",
                                                        "equalized " + equalizedPath,
                                                        QString {"period live 0 %1"}.arg (m.samples),
                                                        QString {"period live %1 %1"}.arg (m.samples)});
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const equalized = readFile (equalizedPath);
    QCOMPARE (equalized.size (), 2 * pcm.size ());
    QVERIFY2 (equalized != pcm + pcm, "the filter changed nothing");
    auto const settings = with (m.settings, {{"live_audio", true}});
    auto const input = [&] (QByteArray const& first, QByteArray const& second, QJsonObject const& keys) {
      return sessionHeader () + configure (with (settings, keys)) + audio (first) + configure (labelFor (m, 1))
        + audio (second) + halt ();
    };
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QVERIFY (writeFile (directory.path () + "/refspec.dat", reference));
    Options options;
    options.directory = directory.path ();
    auto const applied = runInput (input (pcm, pcm, {{"ref_spectrum", "apply"}}), options);
    QVERIFY2 (applied->clean (), qPrintable (applied->report ()));
    auto const filtered = runInput (input (equalized.left (pcm.size ()), equalized.mid (pcm.size ()), {}));
    QVERIFY2 (filtered->clean (), qPrintable (filtered->report ()));
    auto const expected = decodeLines (filtered->lines);
    QVERIFY2 (!messageSet (filtered->lines).isEmpty (), qPrintable (filtered->report ()));
    QCOMPARE (decodeLines (applied->lines), expected);
  }

  // Only live audio is equalized, as in the GUI. From a file, in a period
  // mode and in JTTY (whose configure leaves live_audio out), ref_spectrum
  // "apply" and then "measure" neither load nor write refspec.dat, send no
  // ref_spectrum line, and leave the session's decodes and rows as with "off".
  void fileInputIsNeverEqualized ()
  {
    auto const reference = measuredReference (modelDir ("file input reference"));
    QVERIFY2 (!reference.isEmpty (), "no reference spectrum");
    auto const m = modeOf ("ft4");
    auto const ft4 = period (wavPcm (m.wav), m.samples);
    auto const jtty = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
    QVERIFY2 (!ft4.isEmpty () && !jtty.isEmpty (), "no recording");
    using Input = std::function<QByteArray (QString const&, QString const&)>;
    QMap<QString, Input> const cases {
      {"ft4 file", [&] (QString const& first, QString const& second) {
         return sessionHeader () + configure (with (m.settings, {{"live_audio", false}, {"depth", 1}, {"spectrum", true},
                                                                 {"ref_spectrum", first}}))
           + audio (ft4) + configure (with (labelFor (m, 1), {{"ref_spectrum", second}})) + audio (ft4) + halt ();
       }},
      {"jtty", [&] (QString const& first, QString const& second) {
         return sessionHeader () + configure (with (JttySettings, {{"spectrum", true}, {"ref_spectrum", first}}))
           + audio (jtty, FrameSamples) + configure ({{"ref_spectrum", second}}) + audio (jtty, FrameSamples) + halt ();
       }}};
    for (auto it = cases.constBegin (); it != cases.constEnd (); ++it)
      {
        auto const& name = it.key ();
        auto const& input = it.value ();
        // Both sessions plan from one session's wisdom: JTTY measures its plans in a fresh -a.
        QTemporaryDir warm, equalizing, plain;
        QVERIFY (warm.isValid () && equalizing.isValid () && plain.isValid ());
        Options options;
        options.directory = warm.path ();
        auto const first = runInput (input ("off", "off"), options);
        QVERIFY2 (first->clean (), qPrintable (first->report ()));
        auto const wisdom = readFile (warm.path () + "/jt9_wisdom.dat");
        for (auto const& directory : {equalizing.path (), plain.path ()})
          {
            QVERIFY (writeFile (directory + "/refspec.dat", reference));
            if (!wisdom.isEmpty ()) QVERIFY (writeFile (directory + "/jt9_wisdom.dat", wisdom));
          }
        options.directory = equalizing.path ();
        auto const keyed = runInput (input ("apply", "measure"), options);
        QVERIFY2 (keyed->clean (), qPrintable (keyed->report ()));
        options.directory = plain.path ();
        auto const off = runInput (input ("off", "off"), options);
        QVERIFY2 (off->clean (), qPrintable (off->report ()));
        QVERIFY2 (0 == count (keyed->lines, outType ("ref_spectrum")), qPrintable (name + "\n" + show (keyed->lines)));
        QVERIFY2 (0 == count (keyed->lines, outHas ("\"ref_spectrum_file\"")), qPrintable (name + "\n" + show (keyed->lines)));
        QVERIFY2 (readFile (equalizing.path () + "/refspec.dat") == reference, qPrintable (name + ": refspec.dat was written"));
        QVERIFY2 ("jtty" == name ? count (off->lines, outType ("jtty_update")) > 0 : !messageSet (off->lines).isEmpty (),
                  qPrintable (name + ": nothing decoded\n" + off->report ()));
        QCOMPARE (decodeLines (keyed->lines), decodeLines (off->lines));
        QCOMPARE (known (keyed->lines), known (off->lines));
        auto const keyedRows = rowsOf (keyed->lines);
        auto const offRows = rowsOf (off->lines);
        QVERIFY2 (keyedRows.problem.isEmpty (), qPrintable (keyedRows.problem));
        QVERIFY2 (offRows.problem.isEmpty (), qPrintable (offRows.problem));
        auto const difference = compareRows (keyedRows.rows, offRows.rows);
        QVERIFY2 (difference.isEmpty (), qPrintable (name + ": " + difference));
        qInfo ().noquote () << QString {"%1: %2 decode lines, %3 other lines, %4 rows, each side"}.arg (name)
          .arg (decodeLines (off->lines).size ()).arg (known (off->lines).size ()).arg (offRows.rows.size ());
      }
  }

  // A refspec.dat that cannot be written, read or deleted gives one
  // ref_spectrum_file error per change of ref_spectrum, naming the operation
  // and the path, and the session goes on. While measuring, the curve still
  // comes at every write, the GUI's (its Reference curve reads ref whether or
  // not the file was written), the first before the error. blockRefspec
  // makes writing and deleting fail; "apply" without a file makes reading fail.
  void refSpectrumFileErrors_data ()
  {
    QTest::addColumn<QString> ("operation");
    QTest::newRow ("write") << QString {"write"};
    QTest::newRow ("read") << QString {"read"};
    QTest::newRow ("delete") << QString {"delete"};
  }

  void refSpectrumFileErrors ()
  {
    QFETCH (QString, operation);
#ifndef Q_OS_UNIX
    if ("read" != operation) QSKIP ("not yet run on Windows");
#endif
    auto const m = modeOf ("ft4");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir data, temporary;
    QVERIFY (data.isValid () && temporary.isValid ());
    auto const file = data.path () + "/refspec.dat";
    if ("read" != operation) QVERIFY (blockRefspec (data.path ()));
    QJsonObject keys {{"live_audio", true}, {"depth", 1}};
    if ("write" == operation) keys.insert ("ref_spectrum", "measure");
    if ("read" == operation) keys.insert ("ref_spectrum", "apply");
    Options options;
    options.directory = data.path ();
    options.temporary = temporary.path ();
    Codec codec {options};
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader ());
    QVERIFY2 (codec.waitForCount (outType ("ready"), 1, waitMs (), "ready"), qPrintable (codec.report ()));
    QByteArray input = configure (with (m.settings, keys));
    if ("delete" == operation) input += configure ({{"ref_spectrum_clear", true}});
    codec.write (input + audio (pcm) + configure (labelFor (m, 1)) + audio (pcm) + halt ());
    auto const finished = codec.finish ();
    QVERIFY2 (finished && codec.clean (), qPrintable (codec.report ()));
    QList<QJsonObject> errors;
    for (auto const& line : codec.lines)
      if (outHas ("\"ref_spectrum_file\"") (line)) errors << json (line.text);
    QVERIFY2 (1 == errors.size (), qPrintable (codec.report ()));
    auto const detail = errors.first ().value ("detail").toString ();
    QCOMPARE (errors.first ().value ("t").toString (), QString {"error"});
    QVERIFY2 (detail.contains (operation) && detail.contains ("refspec.dat") && detail.contains (data.path ()),
              qPrintable (detail));
    QCOMPARE (count (codec.lines, outType ("decode_finished")), 2);
    if ("delete" == operation) QVERIFY (QFileInfo {file}.isDir ());
    if ("write" != operation)
      {
        QCOMPARE (count (codec.lines, outType ("ref_spectrum")), 0);
        return;
      }
    auto const firstCurve = find (codec.lines, outType ("ref_spectrum"));
    QVERIFY2 (firstCurve >= 0 && firstCurve < find (codec.lines, outHas ("\"ref_spectrum_file\"")),
              qPrintable (codec.report ()));
    auto const rows = rowsOf (codec.lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const modelPath = modelDir ("unwritable measure");
    auto const model = runModel (modelPath, pcm + pcm, {"datadir " + modelPath, "reference measure",
                                                        QString {"period live 0 %1"}.arg (m.samples),
                                                        QString {"period live %1 %1"}.arg (m.samples)});
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareRows (rows.rows, onlyKinds (model.rows, {"ref"}));
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // prepared_audio carries the audio after the receive chain, live: with
  // ref_spectrum "apply" the GUI's equalized audio for each block, otherwise
  // the audio as sent. A period mode's lines run a block each from the
  // period's first sample, past the stop, through its last whole block
  // (JT65+JT9, which has no stop, through its last sample), each period's
  // from k 0 again; JTTY's from the session's first sample to its last,
  // without a gap, a block each while equalizing and a frame each otherwise.
  // With ref_spectrum off this key is the only one on; with rows on too,
  // each block's line comes before its row.
  void preparedAudioMatchesTheGui_data ()
  {
    QTest::addColumn<QString> ("kind");
    QTest::addColumn<QString> ("reference");
    QTest::addColumn<bool> ("rows");
    QTest::newRow ("ft8 apply") << QString {"ft8"} << QString {"apply"} << true;
    QTest::newRow ("ft8 apply without rows") << QString {"ft8"} << QString {"apply"} << false;
    QTest::newRow ("ft8 off") << QString {"ft8"} << QString {"off"} << false;
    QTest::newRow ("ft8 measure") << QString {"ft8"} << QString {"measure"} << false;
    QTest::newRow ("msk144 apply") << QString {"msk144"} << QString {"apply"} << true;
    QTest::newRow ("msk144 off") << QString {"msk144"} << QString {"off"} << false;
    QTest::newRow ("jt65jt9 off") << QString {"jt65jt9"} << QString {"off"} << false;
    QTest::newRow ("jtty apply") << QString {"jtty"} << QString {"apply"} << true;
    QTest::newRow ("jtty off") << QString {"jtty"} << QString {"off"} << false;
    QTest::newRow ("wspr apply") << QString {"wspr"} << QString {"apply"} << true;
  }

  void preparedAudioMatchesTheGui ()
  {
    QFETCH (QString, kind);
    QFETCH (QString, reference);
    QFETCH (bool, rows);
    auto const applying = "apply" == reference;
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const modelPath = modelDir (QTest::currentDataTag ());
    QDir {}.mkpath (modelPath);
    if (applying)
      {
        auto const curve = measuredReference (modelPath + " measure");
        QVERIFY2 (!curve.isEmpty (), "no reference spectrum");
        QVERIFY (writeFile (directory.path () + "/refspec.dat", curve));
        QVERIFY (writeFile (modelPath + "/refspec.dat", curve));
      }
    QJsonObject keys {{"live_audio", true}, {"prepared_audio", true}};
    if ("off" != reference) keys.insert ("ref_spectrum", reference);
    if (rows) keys.insert ("spectrum", true);
    auto const equalizedPath = modelPath + "/equalized.pcm";
    QStringList script {"datadir " + modelPath, "reference " + reference, "equalized " + equalizedPath};
    QByteArray input = sessionHeader ();
    QByteArray sent;
    int periodSamples = 0;
    int block = HsymSamples;
    int periods = 1;
    if ("jtty" == kind)
      {
        sent = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
        input += configure (with (JttySettings, keys)) + audio (sent, FrameSamples);
        script << QString {"jtty 0 %1 1 0"}.arg (sent.size () / 2);
      }
    else if ("msk144" == kind)
      {
        sent = period (wavPcm (inSamples ("MSK144/181211_120800.wav")), Ft8PeriodSamples);
        input += configure (with (Msk144Settings, keys)) + audio (sent);
        script << QString {"msk144 0 %1 15"}.arg (Ft8PeriodSamples);
        periodSamples = Ft8PeriodSamples;
        block = BlockSamples;
      }
    else
      {
        auto const m = modeOf (kind);
        auto const pcm = period (wavPcm (m.wav), m.samples);
        input += configure (with (with (m.settings, {{"depth", 1}}), keys)) + audio (pcm) + configure (labelFor (m, 1))
          + audio (pcm);
        script << QString {"period live 0 %1"}.arg (m.samples) << QString {"period live %1 %1"}.arg (m.samples);
        sent = pcm + pcm;
        periodSamples = m.samples;
        periods = 2;
      }
    QVERIFY2 (!sent.isEmpty (), "no recording");
    Options options;
    options.directory = directory.path ();
    auto const codec = runInput (input + halt (), options);
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    QVERIFY2 (longestLine (codec->lines) < LineLimit + 1, qPrintable (QString::number (longestLine (codec->lines))));
    QList<Prepared> lines;
    auto problem = preparedOf (codec->lines, lines);
    QVERIFY2 (problem.isEmpty (), qPrintable (problem));
    QVERIFY2 (!lines.isEmpty (), qPrintable ("no prepared_audio line\n" + codec->report ()));
    // What the decoders got: the GUI's equalized audio, or the audio sent.
    auto expected = sent;
    if (applying)
      {
        auto const model = runModel (modelPath, sent, script);
        QVERIFY2 (model.ok, qPrintable (model.failure));
        expected = readFile (equalizedPath);
        QCOMPARE (expected.size (), sent.size ());
        QVERIFY2 (expected != sent, "the filter changed nothing");
      }
    if (rows)
      {
        auto const order = preparedOrder (codec->lines);
        QVERIFY2 (order.isEmpty (), qPrintable (order));
      }
    else QCOMPARE (rowsOf (codec->lines).rows.size (), countKind (rowsOf (codec->lines).rows, "ref"));
    if ("jtty" == kind)
      {
        QByteArray got;
        problem = preparedSession (lines, 1, 0, got);
        QVERIFY2 (problem.isEmpty (), qPrintable (problem));
        QVERIFY2 (got == expected, qPrintable (firstDifference (got, expected)));
        for (int i = 0; i < lines.size (); ++i)
          {
            auto const n = int (lines.at (i).pcm.size () / 2);
            auto const want = applying ? HsymSamples : FrameSamples;
            QVERIFY2 (n == want || (applying && i == lines.size () - 1 && n < want),
                      qPrintable (QString {"line %1 carries %2 samples"}.arg (i).arg (n)));
          }
        return;
      }
    QList<QByteArray> got;
    QList<QList<qint64>> ks;
    problem = preparedPeriods (lines, got, &ks);
    QVERIFY2 (problem.isEmpty (), qPrintable (problem));
    QCOMPARE (got.size (), periods);
    auto const end = "jt65jt9" == kind ? periodSamples : periodSamples / block * block;
    QList<qint64> wantKs;
    for (int k = block; k <= end; k += block) wantKs << k;
    if (end % block) wantKs << end;
    for (int p = 0; p < periods; ++p)
      {
        QCOMPARE (ks.at (p), wantKs);
        auto const want = expected.mid (2 * p * periodSamples, 2 * end);
        QVERIFY2 (got.at (p) == want, qPrintable (QString {"period %1: %2"}.arg (p).arg (firstDifference (got.at (p), want))));
      }
  }

  // When a period's lines start and stop: a period has them only if
  // prepared_audio is on when its first block completes (turned on later, they
  // start at the next period), also while the rows already run in blocks;
  // turned off, the next block sends none. A flush before the stop sends the
  // block it cuts short; a discontinuity or a mode change drops it; after
  // each, k starts again with the next period. A configure that moves the
  // stop to audio already in sends the part block, and the period goes on
  // with the rest of the block at its end. In JTTY the lines start with the
  // first audio after the key comes on and stop with the first after it goes
  // off, and a discontinuity starts a new session's.
  void preparedAudioStartsAndStops_data ()
  {
    QTest::addColumn<QString> ("scenario");
    QTest::newRow ("on within a period") << QString {"on within a period"};
    QTest::newRow ("on within a period with rows") << QString {"on within a period with rows"};
    QTest::newRow ("stop moved within a block") << QString {"stop moved within a block"};
    QTest::newRow ("off within a period") << QString {"off within a period"};
    QTest::newRow ("flush within a period") << QString {"flush within a period"};
    QTest::newRow ("discontinuity within a period") << QString {"discontinuity within a period"};
    QTest::newRow ("mode change within a period") << QString {"mode change within a period"};
    QTest::newRow ("jtty on within a session") << QString {"jtty on within a session"};
    QTest::newRow ("jtty off within a session") << QString {"jtty off within a session"};
    QTest::newRow ("jtty after a discontinuity") << QString {"jtty after a discontinuity"};
  }

  void preparedAudioStartsAndStops ()
  {
    QFETCH (QString, scenario);
    QJsonObject const on {{"prepared_audio", true}};
    QJsonObject const off {{"prepared_audio", false}};
    constexpr int Cut = 100000;          // inside block 29, before FT8's stop
    constexpr int Toggle = 50000;        // inside block 15
    if (scenario.startsWith ("jtty"))
      {
        auto const pcm = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        auto const turnsOn = scenario.contains (" on ");
        auto const split = scenario.contains ("discontinuity");
        auto const middle = split ? control ({{"t", "discontinuity"}}) : configure (turnsOn ? on : off);
        auto const codec = runInput (sessionHeader () + configure (with (JttySettings, {{"live_audio", true}, {"prepared_audio", !turnsOn}}))
                                     + audio (samples (pcm, 0, Toggle), FrameSamples) + middle
                                     + audio (pcm.mid (2 * Toggle), FrameSamples) + halt ());
        QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
        QList<Prepared> lines;
        auto problem = preparedOf (codec->lines, lines);
        QVERIFY2 (problem.isEmpty (), qPrintable (problem));
        QByteArray got;
        if (split)
          {
            // Session 1 to the discontinuity, session 2 from it.
            QList<Prepared> first, second;
            for (auto const& line : lines) (1 == line.reception ? first : second) << line;
            problem = preparedSession (first, 1, 0, got);
            QVERIFY2 (problem.isEmpty (), qPrintable (problem));
            QVERIFY2 (got == samples (pcm, 0, Toggle), qPrintable (firstDifference (got, samples (pcm, 0, Toggle))));
            problem = preparedSession (second, 2, Toggle, got);
            QVERIFY2 (problem.isEmpty (), qPrintable (problem));
            QVERIFY2 (got == pcm.mid (2 * Toggle), qPrintable (firstDifference (got, pcm.mid (2 * Toggle))));
            return;
          }
        problem = preparedSession (lines, 1, turnsOn ? Toggle : 0, got);
        QVERIFY2 (problem.isEmpty (), qPrintable (problem));
        auto const want = turnsOn ? pcm.mid (2 * Toggle) : samples (pcm, 0, Toggle);
        QVERIFY2 (got == want, qPrintable (firstDifference (got, want)));
        return;
      }
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    auto const ft4 = modeOf ("ft4");
    auto const ft4Pcm = period (wavPcm (ft4.wav), ft4.samples);
    QVERIFY2 (!pcm.isEmpty () && !ft4Pcm.isEmpty (), "no recording");
    auto const base = with (m.settings, {{"live_audio", true}, {"depth", 1}});
    auto const whole = liveRows (m.samples) * HsymSamples;
    auto const before = Cut / HsymSamples * HsymSamples;
    QByteArray input = sessionHeader ();
    QList<QByteArray> expected;      // each period's samples
    QList<QList<qint64>> expectedKs;
    auto const blocks = [] (int from, int to) {
      QList<qint64> ks;
      for (int k = from + HsymSamples; k <= to; k += HsymSamples) ks << k;
      return ks;
    };
    if (scenario.startsWith ("on within a period"))
      {
        auto const first = scenario.endsWith ("with rows") ? with (base, {{"spectrum", true}}) : base;
        input += configure (first) + audio (samples (pcm, 0, Toggle)) + configure (on) + audio (pcm.mid (2 * Toggle))
          + configure (labelFor (m, 1)) + audio (pcm);
        expected << samples (pcm, 0, whole);
        expectedKs << blocks (0, whole);
      }
    else if ("stop moved within a block" == scenario)
      {
        // Multithreaded FT8 from the first pass stops at half-symbol 49, not 50.
        constexpr int Moved = 170000;
        input += configure (with (base, on)) + audio (samples (pcm, 0, Moved))
          + configure ({{"multithreaded_ft8", true}, {"ft8_threads", 2}, {"ft8_decoder_start", 0}})
          + audio (pcm.mid (2 * Moved));
        expected << samples (pcm, 0, whole);
        auto ks = blocks (0, 49 * HsymSamples);
        ks << Moved;
        for (int k = 50 * HsymSamples; k <= whole; k += HsymSamples) ks << k;
        expectedKs << ks;
      }
    else if ("off within a period" == scenario)
      {
        input += configure (with (base, on)) + audio (samples (pcm, 0, Toggle)) + configure (off)
          + audio (pcm.mid (2 * Toggle)) + configure (labelFor (m, 1)) + audio (pcm);
        auto const last = Toggle / HsymSamples * HsymSamples;
        expected << samples (pcm, 0, last);
        expectedKs << blocks (0, last);
      }
    else if ("flush within a period" == scenario)
      {
        input += configure (with (base, on)) + audio (samples (pcm, 0, Cut)) + flush () + configure (labelFor (m, 1))
          + audio (pcm);
        expected << samples (pcm, 0, Cut) << samples (pcm, 0, whole);
        expectedKs << (blocks (0, before) << Cut) << blocks (0, whole);
      }
    else if ("discontinuity within a period" == scenario)
      {
        input += configure (with (base, on)) + audio (samples (pcm, 0, Cut)) + control ({{"t", "discontinuity"}})
          + configure (labelFor (m, 1)) + audio (pcm);
        expected << samples (pcm, 0, before) << samples (pcm, 0, whole);
        expectedKs << blocks (0, before) << blocks (0, whole);
      }
    else if ("mode change within a period" == scenario)
      {
        input += configure (with (base, on)) + audio (samples (pcm, 0, Cut))
          + configure (with (ft4.settings, {{"depth", 1}})) + audio (ft4Pcm);
        auto const ft4Whole = liveRows (ft4.samples) * HsymSamples;
        expected << samples (pcm, 0, before) << samples (ft4Pcm, 0, ft4Whole);
        expectedKs << blocks (0, before) << blocks (0, ft4Whole);
      }
    else QFAIL (qPrintable ("no scenario " + scenario));
    auto const codec = runInput (input + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    QList<Prepared> lines;
    auto problem = preparedOf (codec->lines, lines);
    QVERIFY2 (problem.isEmpty (), qPrintable (problem));
    QList<QByteArray> got;
    QList<QList<qint64>> ks;
    problem = preparedPeriods (lines, got, &ks);
    QVERIFY2 (problem.isEmpty (), qPrintable (problem));
    QCOMPARE (ks, expectedKs);
    for (int p = 0; p < expected.size (); ++p)
      QVERIFY2 (got.at (p) == expected.at (p), qPrintable (QString {"period %1: %2"}.arg (p).arg (firstDifference (got.at (p), expected.at (p)))));
  }

  // From a file nothing is sent, as the GUI saves only live audio, in a
  // period mode and in JTTY.
  void noPreparedAudioFromAFile ()
  {
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    auto const jtty = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
    QVERIFY2 (!pcm.isEmpty () && !jtty.isEmpty (), "no recording");
    auto const file = runInput (sessionHeader () + configure (with (m.settings, {{"depth", 1}, {"prepared_audio", true}}))
                                + audio (pcm) + configure (labelFor (m, 1)) + audio (pcm) + halt ());
    QVERIFY2 (file->clean (), qPrintable (file->report ()));
    QVERIFY2 (!messageSet (file->lines).isEmpty (), qPrintable (file->report ()));
    QCOMPARE (count (file->lines, outType ("prepared_audio")), 0);
    auto const session = runInput (sessionHeader () + configure (with (JttySettings, {{"prepared_audio", true}}))
                                   + audio (jtty, FrameSamples) + halt ());
    QVERIFY2 (session->clean (), qPrintable (session->report ()));
    QVERIFY2 (count (session->lines, outType ("session")) > 0, qPrintable (session->report ()));
    QCOMPARE (count (session->lines, outType ("prepared_audio")), 0);
  }

  // The new keys' values: a wrong JSON type is configure_type_error, a value
  // out of range configure_range_error, and the frame is declined whole.
  void newKeysDeclineBadValues_data ()
  {
    QTest::addColumn<QJsonObject> ("keys");
    QTest::addColumn<QString> ("code");
    QTest::addColumn<QString> ("key");
    QTest::newRow ("spectrum 1") << QJsonObject {{"spectrum", 1}} << QString {"configure_type_error"} << QString {"spectrum"};
    QTest::newRow ("low_sidelobes string") << QJsonObject {{"low_sidelobes", "yes"}} << QString {"configure_type_error"}
                                          << QString {"low_sidelobes"};
    QTest::newRow ("ref_spectrum on") << QJsonObject {{"spectrum", true}, {"ref_spectrum", "on"}}
                                     << QString {"configure_range_error"} << QString {"ref_spectrum"};
    QTest::newRow ("ref_spectrum 2") << QJsonObject {{"ref_spectrum", 2}} << QString {"configure_type_error"} << QString {"ref_spectrum"};
    QTest::newRow ("ref_spectrum_clear string") << QJsonObject {{"ref_spectrum_clear", "yes"}}
                                               << QString {"configure_type_error"} << QString {"ref_spectrum_clear"};
    QTest::newRow ("prepared_audio 1") << QJsonObject {{"prepared_audio", 1}} << QString {"configure_type_error"}
                                      << QString {"prepared_audio"};
  }

  void newKeysDeclineBadValues ()
  {
    QFETCH (QJsonObject, keys);
    QFETCH (QString, code);
    QFETCH (QString, key);
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const codec = runInput (sessionHeader () + configure (with (m.settings, {{"depth", 1}, {"live_audio", true}}))
                                 + configure (keys) + audio (pcm) + halt ());
    QVERIFY2 (codec->clean (), qPrintable (codec->report ()));
    auto const at = find (codec->lines, outHas ("\"" + code.toUtf8 () + "\""));
    QVERIFY2 (at >= 0, qPrintable (codec->report ()));
    QCOMPARE (json (codec->lines.at (at).text).value ("key").toString (), key);
    // The declined frame's keys are not applied.
    QCOMPARE (count (codec->lines, outType ("spectrum")), 0);
    QCOMPARE (count (codec->lines, outType ("prepared_audio")), 0);
  }

  // Every mode's decodes, JT65+JT9's, MSK144's and JTTY's among them, are the
  // same with the rows and low_sidelobes on as with the rows off and
  // low_sidelobes off; JT9's and JT65+JT9's have low_sidelobes on in both,
  // since it windows the spectra they decode. From a file FT8's early passes
  // run without the clock.
  // The same holds with only prepared_audio on, against all the keys at
  // their defaults ("prepared_audio" rows; JTTY's live there): a live
  // session sends lines, one from a file none.
  void decodesUnchangedByRows_data ()
  {
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<bool> ("live");
    QTest::addColumn<int> ("periods");
    QTest::addColumn<bool> ("prepared");
    struct Each
    {
      char const * name;
      char const * mode;
      bool live;
      int periods;
    };
    for (auto const& each : {Each {"ft8 file", "ft8", false, 2}, Each {"ft8-mtd file", "ft8-mtd", false, 2},
                             Each {"ft4 live", "ft4", true, 2}, Each {"jt4 file", "jt4", false, 1},
                             Each {"jt9 file", "jt9", false, 1}, Each {"jt65 file", "jt65", false, 1},
                             Each {"jt65jt9 file", "jt65jt9", false, 1}, Each {"q65 live", "q65", true, 1},
                             Each {"fst4 file", "fst4", false, 1}, Each {"fst4w file", "fst4w", false, 1},
                             Each {"msk144 live", "msk144", true, 1}, Each {"jtty", "jtty", true, 1},
                             Each {"wspr live", "wspr", true, 1}})
      for (auto prepared : {false, true})
        QTest::newRow (prepared ? qPrintable (QString {each.name} + " prepared_audio") : each.name)
          << QString {each.mode} << each.live << each.periods << prepared;
  }

  void decodesUnchangedByRows ()
  {
    QFETCH (QString, mode);
    QFETCH (bool, live);
    QFETCH (int, periods);
    QFETCH (bool, prepared);
    auto const keys = prepared ? QJsonObject {{"prepared_audio", true}} : spectrumKeys (true);
    std::function<QByteArray (QJsonObject const&)> input;
    bool sorted = false;
    if ("jtty" == mode)
      {
        auto const pcm = wavPcm (inSamples ("JTTY/261105_101915_567.wav"));
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        auto const settings = prepared ? with (JttySettings, {{"live_audio", true}}) : JttySettings;
        input = [=] (QJsonObject const& on) {
          return sessionHeader () + configure (with (settings, on)) + audio (pcm, FrameSamples) + halt ();
        };
      }
    else if ("msk144" == mode)
      {
        auto const pcm = period (wavPcm (inSamples ("MSK144/181211_120800.wav")), Ft8PeriodSamples);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        input = [=] (QJsonObject const& on) {
          return sessionHeader () + configure (with (with (Msk144Settings, {{"live_audio", live}}), on)) + audio (pcm)
            + halt ();
        };
      }
    else
      {
        auto const m = modeOf (mode);
        auto const pcm = period (wavPcm (m.wav), m.samples);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        sorted = m.sorted;
        input = [=] (QJsonObject const& on) {
          QByteArray bytes = sessionHeader () + configure (with (with (m.settings, {{"live_audio", live}}), on));
          for (int p = 0; p < periods; ++p) bytes += (p > 0 ? configure (labelFor (m, p)) : QByteArray {}) + audio (pcm);
          return bytes + halt ();
        };
      }
    // JTTY measures its plans only in a fresh -a, so the session with the
    // rows goes first and the other plans from its wisdom.
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    auto const on = runInput (input (keys), shared);
    QVERIFY2 (on->clean (), qPrintable (on->report ()));
    bool const windowed = "jt9" == mode || "jt65jt9" == mode;
    auto const off = runInput (input (prepared ? QJsonObject {} : QJsonObject {{"low_sidelobes", windowed}}),
                               shared);
    QVERIFY2 (off->clean (), qPrintable (off->report ()));
    QVERIFY2 (!messageSet (off->lines).isEmpty () || "jtty" == mode, qPrintable (off->report ()));
    if (prepared)
      {
        QList<Prepared> lines;
        auto const problem = preparedOf (on->lines, lines);
        QVERIFY2 (problem.isEmpty (), qPrintable (problem));
        QVERIFY2 (live == !lines.isEmpty (), qPrintable (QString {"%1 prepared_audio lines"}.arg (lines.size ())));
        QCOMPARE (rowsOf (on->lines).rows.size (), 0);
      }
    else
      {
        auto const rows = rowsOf (on->lines);
        QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
        QVERIFY2 (!rows.rows.isEmpty (), "the session with the key wrote no rows");
      }
    QVERIFY2 (longestLine (on->lines) < LineLimit + 1, qPrintable (QString::number (longestLine (on->lines))));
    QCOMPARE (decodeLines (on->lines, sorted), decodeLines (off->lines, sorted));
    QCOMPARE (decisions (on->lines), decisions (off->lines));
    if ("jtty" == mode) QCOMPARE (known (on->lines), known (off->lines));
  }

  // low_sidelobes windows the symbol spectra JT65+JT9's JT9 decoder reads,
  // as WSJT-X's Low sidelobes windowed symspec_'s, so it moves those decodes:
  // on this recording one decode's DT and SNR, from the same messages.
  void lowSidelobesReachesTheJt65Jt9Decoder ()
  {
    auto const m = modeOf ("jt65jt9");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const input = [&] (bool lowSidelobes) {
      return sessionHeader () + configure (with (m.settings, {{"low_sidelobes", lowSidelobes}})) + audio (pcm) + halt ();
    };
    auto const off = runInput (input (false));
    QVERIFY2 (off->clean (), qPrintable (off->report ()));
    auto const on = runInput (input (true));
    QVERIFY2 (on->clean (), qPrintable (on->report ()));
    QVERIFY2 (!messageSet (off->lines).isEmpty (), qPrintable (off->report ()));
    QCOMPARE (messageSet (on->lines), messageSet (off->lines));
    QVERIFY2 (decodeLines (on->lines, m.sorted) != decodeLines (off->lines, m.sorted),
              "low_sidelobes did not reach JT65+JT9's JT9 decoder");
  }

  // low_sidelobes windows the spectra JT9 alone decodes from, and starts on
  // as in WSJT-X. The depth a client sends does not set the window, and a
  // configure that sends only depth keeps it. Weak recordings with the
  // receive frequency off the signal, where the window changes the decode
  // line (its SNR) on some of them.
  void lowSidelobesReachesTheJt9Decoder ()
  {
    QJsonObject const settings {{"mode", "JT9"}, {"trperiod", 60}, {"nutc", 0}, {"depth", 1},
                                {"rxfreq", 1500}, {"ntol", 20}};
    bool moved = false;
    for (auto const amplitude : {"4", "4.2", "4.9", "5"})
      {
        auto const wav = fixture (QString {"jt9-weak-%1.wav"}.arg (amplitude));
        auto const pcm = period (wavPcm (wav), 60 * SampleRate);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        auto const run = [&] (QJsonObject const& keys) {
          return runInput (sessionHeader () + configure (with (settings, keys)) + audio (pcm) + halt ());
        };
        auto const off = run ({{"low_sidelobes", false}});
        QVERIFY2 (off->clean (), qPrintable (off->report ()));
        auto const on = run ({{"low_sidelobes", true}});
        QVERIFY2 (on->clean (), qPrintable (on->report ()));
        auto const start = run ({});
        QVERIFY2 (start->clean (), qPrintable (start->report ()));
        auto const bit = run ({{"low_sidelobes", false}, {"depth", 1 | 256}});
        QVERIFY2 (bit->clean (), qPrintable (bit->report ()));
        auto const depthOnly = runInput (sessionHeader () + configure (with (settings, {{"low_sidelobes", true}}))
                                         + configure ({{"depth", 1}}) + audio (pcm) + halt ());
        QVERIFY2 (depthOnly->clean (), qPrintable (depthOnly->report ()));
        QVERIFY2 (!messageSet (off->lines).isEmpty (), qPrintable (off->report ()));
        QCOMPARE (messageSet (on->lines), messageSet (off->lines));
        QCOMPARE (decodeLines (start->lines, false), decodeLines (on->lines, false));
        QCOMPARE (decodeLines (depthOnly->lines, false), decodeLines (on->lines, false));
        QCOMPARE (decodeLines (bit->lines, false), decodeLines (off->lines, false));
        moved = moved || decodeLines (on->lines, false) != decodeLines (off->lines, false);
      }
    QVERIFY2 (moved, "low_sidelobes did not reach the JT9 decoder");
  }

  // A multithreaded final deferred while the decoder is held decodes the same
  // set with the rows on as off.
  void deferredFinalUnchangedByRows ()
  {
    auto const pcm = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QJsonObject const settings {{"mode", "FT8"}, {"depth", 3}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                                {"ft8_decoder_start", 1}, {"ft8_cycles", 1}, {"live_audio", true},
                                {"utc", "13:34:30"}};
    int rows = 0;
    auto const session = [&] (QJsonObject const& keys, QStringList& found) -> QString {
      Options options;
      options.environment << "WSJT_CODEC_TEST_HOLD_PASS=41:1500";
      Codec codec {options};
      if (!codec.started) return codec.failure;
      codec.write (sessionHeader () + configure (with (settings, keys)) + frame (0x01u, pcm.left (2)));
      auto const t0 = codec.now ();
      // The period whole is due before it is sent, so that its triggers fire
      // on arrival: the held early pass runs, the second is busy and the final defers.
      if (!codec.waitUntil (t0 + 15500)) return codec.report ();
      codec.write (frame (0x01u, samples (pcm, 1, 41 * HsymSamples))
                   + frame (0x01u, samples (pcm, 41 * HsymSamples, Ft8PeriodSamples)));
      if (!codec.waitForCount (outType ("decode_finished"), 1, waitMs (), "the deferred final"))
        return codec.report ();
      codec.write (halt ());
      if (!codec.finish () || !codec.clean ()) return codec.report ();
      found = decisions (codec.lines);
      found << messageSet (codec.lines);
      rows += count (codec.lines, outType ("spectrum"));
      return {};
    };
    QStringList off, on;
    auto failure = session ({}, off);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    failure = session (spectrumKeys (false), on);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    QCOMPARE (off.mid (0, 4), (QStringList {"pass 41 F", "skip 46 early busy", "defer 50", "pass 50 T"}));
    QCOMPARE (on, off);
    QVERIFY2 (rows > 0, "the session with the key wrote no rows");
  }

  // Decode again of a live period whose rows went past the stop decodes what
  // it decodes with the rows off.
  void decodeAgainAfterTheStopUnchangedByRows ()
  {
    auto const m = modeOf ("ft8-engine");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const split = 6 * SampleRate;
    auto const input = [&] (QJsonObject const& keys) {
      return sessionHeader () + configure (with (with (m.settings, {{"live_audio", true}}), keys)) + audio (pcm)
        + configure ({{"utc", "05:11:30"}}) + audio (pcm.left (2 * split)) + configure ({{"nagain_flag", true}})
        + audio (pcm.mid (2 * split)) + halt ();
    };
    auto const off = runInput (input ({}));
    QVERIFY2 (off->clean (), qPrintable (off->report ()));
    auto const on = runInput (input (spectrumKeys (false)));
    QVERIFY2 (on->clean (), qPrintable (on->report ()));
    QVERIFY2 (decisions (off->lines).filter (" again").size () == 1, qPrintable (off->report ()));
    QCOMPARE (decodeLines (on->lines), decodeLines (off->lines));
    QCOMPARE (decisions (on->lines), decisions (off->lines));
    // Every row line of both periods came or was counted dropped (live audio).
    QCOMPARE (rowLinesAccounted (on->lines), qint64 (2 * liveRows (m.samples)));
    QVERIFY2 (count (on->lines, outType ("spectrum")) >= 20, "fewer rows came than the queue keeps");
  }

  // A client that ignores each new event reads, with the event on, what it
  // reads with it off.
  void clientsIgnoringTheRowsStillWork_data ()
  {
    QTest::addColumn<QString> ("event");
    QTest::addColumn<QString> ("mode");
    QTest::newRow ("spectrum") << QString {"spectrum"} << QString {"ft8"};
    QTest::newRow ("fast_spectrum") << QString {"fast_spectrum"} << QString {"msk144"};
    QTest::newRow ("ref_spectrum") << QString {"ref_spectrum"} << QString {"ft4"};
    QTest::newRow ("spectrum jtty") << QString {"spectrum"} << QString {"jtty"};
    QTest::newRow ("prepared_audio") << QString {"prepared_audio"} << QString {"ft4"};
    QTest::newRow ("prepared_audio jtty") << QString {"prepared_audio"} << QString {"jtty"};
  }

  void clientsIgnoringTheRowsStillWork ()
  {
    QFETCH (QString, event);
    QFETCH (QString, mode);
    QJsonObject const keys = "ref_spectrum" == event ? QJsonObject {{"ref_spectrum", "measure"}}
      : "prepared_audio" == event ? QJsonObject {{"prepared_audio", true}} : QJsonObject {{"spectrum", true}};
    std::function<QByteArray (QJsonObject const&)> input;
    if ("jtty" == mode)
      {
        auto const pcm = wavPcm (inSamples ("JTTY/261105_101915_567.wav")).left (2 * 15 * SampleRate);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        // prepared_audio comes only from live audio.
        auto const settings = "prepared_audio" == event ? with (JttySettings, {{"live_audio", true}}) : JttySettings;
        input = [=] (QJsonObject const& on) {
          return sessionHeader () + configure (with (settings, on)) + audio (pcm, FrameSamples) + halt ();
        };
      }
    else if ("msk144" == mode)
      {
        auto const pcm = period (wavPcm (inSamples ("MSK144/181211_120500.wav")), Ft8PeriodSamples);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        input = [=] (QJsonObject const& on) {
          return sessionHeader () + configure (with (with (Msk144Settings, {{"utc", "12:05:00"}}), on)) + audio (pcm)
            + halt ();
        };
      }
    else
      {
        auto const m = modeOf (mode);
        auto const pcm = period (wavPcm (m.wav), m.samples);
        QVERIFY2 (!pcm.isEmpty (), "no recording");
        input = [=] (QJsonObject const& on) {
          return sessionHeader () + configure (with (with (m.settings, {{"live_audio", "ft4" == mode}}), on))
            + audio (pcm) + configure (labelFor (m, 1)) + audio (pcm) + halt ();
        };
      }
    // JTTY measures its plans only in a fresh -a, so the keyed session goes
    // first and the other plans from its wisdom.
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    Options shared;
    shared.directory = directory.path ();
    auto const withKey = runInput (input (keys), shared);
    QVERIFY2 (withKey->clean (), qPrintable (withKey->report ()));
    auto const without = runInput (input ({}), shared);
    QVERIFY2 (without->clean (), qPrintable (without->report ()));
    QVERIFY2 (count (withKey->lines, outType (event.toUtf8 ())) > 0, qPrintable (show (withKey->lines)));
    QVERIFY2 (longestLine (withKey->lines) < LineLimit + 1, qPrintable (QString::number (longestLine (withKey->lines))));
    QCOMPARE (count (without->lines, outType (event.toUtf8 ())), 0);
    QCOMPARE (known (withKey->lines), known (without->lines));
  }

  // Rows keep coming while a decode is held, and with busy_events the
  // decode's lines are those it writes without them.
  void rowsKeepComingWhileADecodeIsHeld ()
  {
    auto const m = modeOf ("ft4");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    constexpr int HoldMs = 4000;
    auto const session = [&] (QJsonObject const& keys, QList<Line>& lines) -> QString {
      Options options;
      options.environment << QString {"WSJT_CODEC_TEST_HOLD_PASS=21:%1"}.arg (HoldMs);
      Codec codec {options};
      if (!codec.started) return codec.failure;
      codec.write (sessionHeader () + configure (with (with (m.settings, {{"live_audio", true}, {"depth", 1},
                                                                          {"busy_events", true}}), keys)));
      // Real time: each frame is sent when it falls due.
      auto const t0 = codec.now ();
      int sent = 0;
      for (int p = 0; p < 2; ++p)
        {
          if (p > 0) codec.write (configure (labelFor (m, p)));
          for (int at = 0; at < m.samples; at += FrameSamples)
            {
              if (!codec.waitUntil (t0 + qint64 (sent + FrameSamples) * 1000 / SampleRate)) return codec.report ();
              codec.write (frame (0x01u, samples (pcm, at, std::min (at + FrameSamples, m.samples))));
              sent += FrameSamples;
            }
        }
      codec.write (halt ());
      if (!codec.finish () || !codec.clean ()) return codec.report ();
      lines = codec.lines;
      return {};
    };
    QList<Line> off, on;
    auto failure = session ({}, off);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    failure = session (spectrumKeys (false), on);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    auto const start = find (on, passLine (21));
    auto const end = find (on, outType ("decode_finished"));
    QVERIFY2 (start >= 0 && end > start, qPrintable (show (on)));
    // Rows the reader fell behind on were made too: dropped ones count.
    auto const during = rowLinesAccounted (on.mid (start, end - start));
    qInfo ().noquote () << QString {"%1 rows during the %2 ms the first pass was held (%3 ms)"}.arg (during)
      .arg (HoldMs).arg (on.at (end).ms - on.at (start).ms);
    QVERIFY2 (during >= HoldMs * SampleRate / 1000 / HsymSamples - 2, qPrintable (show (on)));
    QCOMPARE (decodeLines (on), decodeLines (off));
    auto const busy = [] (QList<Line> const& lines) {
      QByteArrayList found;
      for (auto const& line : lines)
        if (outType ("busy") (line)) found << line.text;
      return found;
    };
    QCOMPARE (busy (on), busy (off));
  }

  // The row queue keeps a few seconds of rows and drops the oldest: with live
  // audio, a reader stalled past the cap of 20 entries loses the oldest
  // rows, and one rows_dropped line, before the first row
  // written after, counts their lines; the rows after it are the session's
  // newest and run to its end, the GUI's. The decodes are those of the session
  // without rows, which writes no row and no rows_dropped line.
  void rowsPastTheCapDropTheOldest ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    auto const m = modeOf ("ft4");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const blocks = liveRows (m.samples);
    QList<Line> on, off;
    auto failure = stalledSession (with (m.settings, {{"depth", 1}, {"live_audio", true}, {"spectrum", true}}),
                                   pcm, m, 4000, on);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    failure = stalledSession (with (m.settings, {{"depth", 1}, {"live_audio", true}}), pcm, m, 4000, off);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    QCOMPARE (count (off, outType ("rows_dropped")) + count (off, outType ("spectrum")), 0);
    int first = 0, after = 0;
    auto const problem = droppedRun (on, blocks, 2, first, after);
    QVERIFY2 (problem.isEmpty (), qPrintable (problem + "\n" + show (on)));
    QCOMPARE (after, 20);
    // The configure that sets live_audio makes the session's first period droppable.
    QVERIFY2 (countKind (rowsOf (on).rows, "spectrum") - after < blocks, "nothing was dropped in the first period");
    QCOMPARE (decodeLines (on), decodeLines (off));
    auto const rows = rowsOf (on);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelDir ("past the cap"), pcm + pcm, modelSettings (true)
                                 << QString {"period live 0 %1"}.arg (m.samples)
                                 << QString {"period live %1 %1"}.arg (m.samples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const kept = int (countKind (rows.rows, "spectrum")) - after;
    auto const difference = compareRows (rows.rows, withoutBlocks (model.rows, blocks, kept, first));
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
#endif
  }

  // From a file nothing is dropped, however far the reader falls behind: every
  // row comes, the GUI's, and no rows_dropped line.
  void fileRowsPastTheCapAreAllKept ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QList<Line> lines;
    auto const failure = stalledSession (with (m.settings, {{"depth", 1}, {"spectrum", true}}), pcm, m, 4000, lines);
    QVERIFY2 (failure.isEmpty (), qPrintable (failure));
    QCOMPARE (count (lines, outType ("rows_dropped")), 0);
    auto const rows = rowsOf (lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelDir ("file past the cap"), pcm + pcm, modelSettings (true, m.stop)
                                 << QString {"period file 0 %1"}.arg (m.samples)
                                 << QString {"period file %1 %1"}.arg (m.samples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareRows (rows.rows, model.rows);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
#endif
  }

  // MSK144's Fast Graph and JTTY's rows from a file are never dropped either,
  // however far the reader falls behind: every row comes, the GUI's.
  void fileFastAndJttyRowsPastTheCapAreAllKept_data ()
  {
    QTest::addColumn<QString> ("mode");
    QTest::newRow ("msk144") << QString {"msk144"};
    QTest::newRow ("jtty") << QString {"jtty"};
  }

  void fileFastAndJttyRowsPastTheCapAreAllKept ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    QFETCH (QString, mode);
    auto const jtty = "jtty" == mode;
    auto const pcm = jtty ? wavPcm (inSamples ("JTTY/261105_101915_567.wav"))
                          : period (wavPcm (inSamples ("MSK144/181211_120800.wav")), Ft8PeriodSamples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    PipedCodec codec {directory.path (), {}};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.stallReading (4000);
    QByteArray input = sessionHeader ();
    QStringList script;
    QByteArray sent;
    if (jtty)
      {
        input += configure (with (JttySettings, {{"live_audio", false}, {"spectrum", true}})) + audio (pcm, FrameSamples);
        script << QString {"jtty 0 %1 1 0"}.arg (pcm.size () / 2);
        sent = pcm;
      }
    else
      {
        input += configure (with (Msk144Settings, {{"live_audio", false}, {"spectrum", true}})) + audio (pcm)
          + configure ({{"utc", "12:08:15"}}) + audio (pcm);
        script << QString {"msk144 0 %1 15"}.arg (Ft8PeriodSamples) << QString {"msk144 %1 %1 15"}.arg (Ft8PeriodSamples);
        sent = pcm + pcm;
      }
    codec.write (input + halt ());
    auto const finished = codec.finish (waitMs ());
    auto const lines = codec.lines ();
    QVERIFY2 (finished, qPrintable (QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (lines))));
    QCOMPARE (count (lines, outType ("rows_dropped")), 0);
    auto const rows = rowsOf (lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    QVERIFY2 (rows.rows.size () > 20, "fewer rows than the cap");
    auto const model = runModel (modelDir (QString {"%1 file past the cap"}.arg (mode)), sent, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareRows (rows.rows, model.rows);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
#endif
  }

  // Past 16 MiB of lines that are never dropped (rows from a file, here), the
  // codec waits for the reader: with stdout unread for 15 s, no more final
  // passes start than the periods whose rows fit in 16 MiB, and one more (the
  // decoder stops at its next line, or, with no decode thread, the frame loop
  // that decodes stops at its next row); then every row and every decode
  // comes, as with rows off.
  void decodesWaitPastSixteenMebibytes ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    constexpr int Periods = 16;
    constexpr int StallMs = 15000;
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const input = [&] (bool rows) {
      QByteArray bytes = sessionHeader () + configure (with (m.settings, {{"depth", 1}, {"spectrum", rows}}));
      for (int p = 0; p < Periods; ++p) bytes += (p > 0 ? configure (labelFor (m, p)) : QByteArray {}) + audio (pcm);
      return bytes + halt ();
    };
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    PipedCodec codec {directory.path (), {}};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.stallReading (StallMs);
    auto const resumed = codec.now () + StallMs;
    codec.write (input (true));
    auto const finished = codec.finish (5 * waitMs ());
    auto const lines = codec.lines ();
    QVERIFY2 (finished, qPrintable (QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (lines))));
    int finals = 0;
    qint64 rowBytes = 0;
    for (auto const& line : lines)
      {
        finals += passLine (50) (line) && line.ms < resumed;
        if (outType ("spectrum") (line)) rowBytes += line.text.size () + 1;
      }
    auto const fit = int ((qint64 {16} << 20) / std::max<qint64> (1, rowBytes / Periods));
    qInfo ().noquote () << QString {"%1 of %2 final passes started while stdout was unread; the rows of %3 periods "
                                    "fit in 16 MiB"}.arg (finals).arg (Periods).arg (fit);
    QVERIFY2 (fit + 1 < Periods, "every period's rows fit in 16 MiB: nothing to wait for");
    QVERIFY2 (finals <= fit + 1,
              qPrintable (QString {"%1 final passes started while stdout was unread, more than the %2 periods whose "
                                   "rows fit in 16 MiB and one more"}.arg (finals).arg (fit)));
    QCOMPARE (count (lines, outType ("rows_dropped")), 0);
    QCOMPARE (count (lines, outType ("spectrum")), Periods * m.stop);
    auto const off = runInput (input (false));
    QVERIFY2 (off->clean (), qPrintable (off->report ()));
    QCOMPARE (count (lines, outType ("decode_finished")), Periods);
    QCOMPARE (decodeLines (lines), decodeLines (off->lines));
#endif
  }

  // "Apply" under a stall past the cap: a ref_spectrum curve is never dropped,
  // so every curve comes, the measured ones and the one apply loads, the GUI's.
  void curvesPastTheCapAreAllKept ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    PipedCodec codec {directory.path (), {}};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.stallReading (6000);
    codec.write (sessionHeader () + configure (with (m.settings, {{"depth", 1}, {"live_audio", true}, {"spectrum", true},
                                                                  {"ref_spectrum", "measure"}}))
                 + audio (pcm) + configure (with (labelFor (m, 1), {{"ref_spectrum", "apply"}})) + audio (pcm) + halt ());
    auto const finished = codec.finish (waitMs ());
    auto const lines = codec.lines ();
    QVERIFY2 (finished, qPrintable (QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (lines))));
    QVERIFY2 (count (lines, outType ("rows_dropped")) > 0, "nothing was dropped: the stall did not pass the cap");
    auto const rows = rowsOf (lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const modelPath = modelDir ("curves past the cap");
    auto const model = runModel (modelPath, pcm + pcm, {"datadir " + modelPath, "reference measure",
                                                        QString {"period live 0 %1"}.arg (m.samples), "reference apply",
                                                        QString {"period live %1 %1"}.arg (m.samples)});
    QVERIFY2 (model.ok, qPrintable (model.failure));
    QCOMPARE (countKind (model.rows, "ref"), 14);
    auto const difference = compareRows (onlyKinds (rows.rows, {"ref"}), onlyKinds (model.rows, {"ref"}));
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
#endif
  }

  // A reply stays whole while rows are dropped around it: a render after the
  // first period of a live session, the reader stalled 4 s after its encoded
  // line, has every line from encoded to rendered consecutive, with no
  // rows_dropped or other line among them.
  void repliesStayWholePastTheCap ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    PipedCodec codec {directory.path (), {}};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.stallAfter ("encoded", 4000);
    QByteArray input = sessionHeader () + configure (with (m.settings, {{"depth", 1}, {"live_audio", true},
                                                                         {"spectrum", true}}))
      + audio (pcm) + control ({{"t", "render"}, {"id", 7}, {"mode", "FT8"}, {"text", "CQ K1ABC FN42"},
                                {"freq", 1500}, {"rate", 48000}});
    for (int p = 1; p < 5; ++p) input += configure (labelFor (m, p)) + audio (pcm);
    codec.write (input + halt ());
    auto const finished = codec.finish (waitMs ());
    auto const lines = codec.lines ();
    QVERIFY2 (finished, qPrintable (QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (lines))));
    QVERIFY2 (count (lines, outType ("rows_dropped")) > 0, "nothing was dropped: the stall did not pass the cap");
    auto const ofReply = replyLine (7);
    auto const first = find (lines, outType ("encoded"));
    auto const last = findLast (lines, ofReply);
    QVERIFY2 (first >= 0 && last > first && outType ("rendered") (lines.at (last)), qPrintable (show (lines)));
    for (int i = first; i <= last; ++i)
      if (!lines.at (i).error)
        QVERIFY2 (ofReply (lines.at (i)), qPrintable (QString {"line %1 inside the reply: %2"}.arg (i)
                                                     .arg (QString::fromUtf8 (lines.at (i).text.left (120)))));
    QVERIFY2 (count (lines.mid (first, last - first + 1), outType ("pcm")) > 1, "a render of one pcm line");
#endif
  }

  // A decode line goes out ahead of the rows queued when it is written.
  // Live FT4 with spectrum on: the reader stops once it has read the
  // period's 10th row, then the rest of the period comes, so the rows from
  // the 11th to the stop wait while the period decodes. The reader resumes
  // once the next period's decision is on stderr, when every line of this
  // period's decode is queued; that period has spectrum off, so it adds no
  // row. The period's first decode line comes before at least one of its rows
  // whose k is at or before the decode's stop sample, where a
  // first-in-first-out queue writes them all first, and the rows are the
  // model's, any dropped counted.
  void decodeLinesGoAheadOfQueuedRows ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    constexpr int ReadRows = 10;
    auto const m = modeOf ("ft4");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    PipedCodec codec {directory.path (), {}};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.write (sessionHeader () + configure (with (m.settings, {{"depth", 1}, {"live_audio", true}, {"spectrum", true}}))
                 + audio (samples (pcm, 0, ReadRows * HsymSamples)));
    QVERIFY2 (waitForLines (codec, [&] (QList<Line> const& lines) {return count (lines, outType ("spectrum")) >= ReadRows;}),
              qPrintable (show (codec.lines ())));
    codec.stallReading (5 * waitMs ());
    codec.write (audio (samples (pcm, ReadRows * HsymSamples, m.samples))
                 + configure (with (labelFor (m, 1), {{"spectrum", false}})) + audio (pcm));
    auto const decided = waitForLines (codec, [&] (QList<Line> const& lines) {return decisionsAt (lines, m.stop) >= 2;});
    codec.resumeReading ();
    QVERIFY2 (decided, qPrintable ("no decision for the second period\n" + show (codec.lines ())));
    codec.write (halt ());
    auto const finished = codec.finish (waitMs ());
    auto const lines = codec.lines ();
    QVERIFY2 (finished, qPrintable (QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (lines))));
    static QRegularExpression const pass {"wsjt-codec: pass nzhsym=\\d+ samples=(\\d+) "};
    qint64 stop = -1;
    for (auto const& line : lines)
      {
        auto const match = pass.match (QString::fromUtf8 (line.text));
        if (line.error && match.hasMatch ())
          {
            stop = match.captured (1).toLongLong ();
            break;
          }
      }
    QVERIFY2 (stop > ReadRows * HsymSamples, qPrintable (show (lines)));
    auto const firstDecode = find (lines, outType ("decode"));
    QVERIFY2 (firstDecode >= 0, qPrintable (show (lines)));
    QCOMPARE (json (lines.at (firstDecode).text).value ("time").toString (),
              QTime::fromString (m.settings.value ("utc").toString (), "HH:mm:ss").toString ("HHmmss"));
    int behind = 0;
    for (int i = firstDecode + 1; i < lines.size (); ++i)
      behind += outType ("spectrum") (lines.at (i)) && qint64 (json (lines.at (i).text).value ("k").toDouble ()) <= stop;
    qInfo ().noquote () << QString {"%1 rows up to the stop at sample %2 came after the period's first decode line"}
      .arg (behind).arg (stop);
    QVERIFY2 (behind > 0, qPrintable ("every row up to the stop came before the period's first decode line\n"
                                      + show (lines)));
    auto const model = runModel (modelDir ("decodes ahead of rows"), pcm, modelSettings (true)
                                 << QString {"period live 0 %1"}.arg (m.samples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareKeptRows (lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
#endif
  }

  // No row goes out inside a reply, even when the writer has caught up with
  // it: a reader slower than the rows come keeps live rows queued, and a
  // JTTY render's segment line goes out while its audio is still being
  // computed. Every line from the reply's first to rendered is the reply's,
  // the rows queued before it come after it, and every row is the GUI's.
  // With busy_events a pass_progress line, written under a lock that is only
  // tried, comes before the reply.
  void repliesStayWholeWithRowsQueued_data ()
  {
    QTest::addColumn<bool> ("progress");
    QTest::newRow ("render") << false;
    QTest::newRow ("render after pass_progress") << true;
  }

  void repliesStayWholeWithRowsQueued ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the paced reader runs on Unix pipes");
#else
    QFETCH (bool, progress);
    constexpr int Blocks = 30;
    auto const m = modeOf (progress ? "fst4" : "ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    PipedCodec codec {directory.path (), {}};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.paceReading (8192);
    auto const settings = with (m.settings, {{"live_audio", true}, {"spectrum", true}, {"busy_events", progress}});
    QByteArray sent;
    QStringList script = modelSettings (true);
    if (progress)
      {
        codec.write (sessionHeader () + configure (settings) + audio (pcm));
        script << QString {"period live 0 %1"}.arg (m.samples);
        sent = pcm;
        QVERIFY2 (waitForLines (codec, [] (QList<Line> const& lines) {return count (lines, outType ("pass_progress")) > 0;}),
                  qPrintable ("no pass_progress\n" + show (codec.lines ())));
        codec.write (configure (labelFor (m, 1)));
      }
    else codec.write (sessionHeader () + configure (with (settings, {{"depth", 1}})));
    codec.write (audio (samples (pcm, 0, Blocks * HsymSamples)) + control (JttyLongRender) + halt ());
    script << QString {"period live %1 %2"}.arg (sent.size () / 2).arg (Blocks * HsymSamples);
    sent += pcm.left (2 * Blocks * HsymSamples);
    auto const finished = codec.finish (waitMs ());
    auto const lines = codec.lines ();
    QVERIFY2 (finished, qPrintable (QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (lines))));
    auto const ofReply = replyLine (7);
    auto const first = find (lines, ofReply);
    auto const last = findLast (lines, ofReply);
    QVERIFY2 (first >= 0 && last > first && outType ("rendered") (lines.at (last))
              && count (lines.mid (first, last - first), outType ("pcm")) > 1, qPrintable (show (lines)));
    for (int i = first; i <= last; ++i)
      if (!lines.at (i).error)
        QVERIFY2 (ofReply (lines.at (i)), qPrintable (QString {"line %1 inside the reply: %2"}.arg (i)
                                                     .arg (QString::fromUtf8 (lines.at (i).text.left (120)))));
    QVERIFY2 (count (lines.mid (last), outType ("spectrum")) > 0, "no row queued before the reply came after it");
    auto const rows = rowsOf (lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    auto const model = runModel (modelDir (QTest::currentDataTag ()), sent, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareKeptRows (lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
#endif
  }

  // A decode line made after a reply still goes out ahead of the rows queued
  // before the reply. Live FT4 with spectrum on: the reader stops once it
  // has read the period's 5th row; then come the rows to the 15th, a pack
  // request, and the rest of the period, whose decode comes after the reply.
  // The reader resumes once the next period's decision is on stderr, as in
  // decodeLinesGoAheadOfQueuedRows. The period's first decode line comes
  // before at least one row queued before the reply, as does the reply.
  void decodeLinesGoAheadAfterAReply ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    constexpr int ReadRows = 5;
    constexpr int ReplyAfter = 15;   // blocks in before the request
    auto const m = modeOf ("ft4");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    PipedCodec codec {directory.path (), {}};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.write (sessionHeader () + configure (with (m.settings, {{"depth", 1}, {"live_audio", true}, {"spectrum", true}}))
                 + audio (samples (pcm, 0, ReadRows * HsymSamples)));
    QVERIFY2 (waitForLines (codec, [&] (QList<Line> const& lines) {return count (lines, outType ("spectrum")) >= ReadRows;}),
              qPrintable (show (codec.lines ())));
    codec.stallReading (5 * waitMs ());
    codec.write (audio (samples (pcm, ReadRows * HsymSamples, ReplyAfter * HsymSamples))
                 + control ({{"t", "pack"}, {"id", 7}, {"mode", "FT4"}, {"text", "CQ K1ABC FN42"}})
                 + audio (samples (pcm, ReplyAfter * HsymSamples, m.samples))
                 + configure (with (labelFor (m, 1), {{"spectrum", false}})) + audio (pcm));
    auto const decided = waitForLines (codec, [&] (QList<Line> const& lines) {return decisionsAt (lines, m.stop) >= 2;});
    codec.resumeReading ();
    QVERIFY2 (decided, qPrintable ("no decision for the second period\n" + show (codec.lines ())));
    codec.write (halt ());
    auto const finished = codec.finish (waitMs ());
    auto const lines = codec.lines ();
    QVERIFY2 (finished, qPrintable (QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (lines))));
    auto const ofReply = replyLine (7);
    auto const first = find (lines, ofReply);
    auto const last = find (lines, outType ("packed"));
    QVERIFY2 (first >= 0 && last == first + 1 && ofReply (lines.at (last)), qPrintable (show (lines)));
    auto const firstDecode = find (lines, outType ("decode"));
    QVERIFY2 (firstDecode > last, qPrintable (show (lines)));
    auto const queuedBefore = [&] (int from) {
      int found = 0;
      for (int i = from; i < lines.size (); ++i)
        found += outType ("spectrum") (lines.at (i))
          && qint64 (json (lines.at (i).text).value ("k").toDouble ()) <= ReplyAfter * HsymSamples;
      return found;
    };
    auto const behindTheDecode = queuedBefore (firstDecode);
    auto const behindTheReply = queuedBefore (last);
    qInfo ().noquote () << QString {"of the rows queued before the reply, %1 came after it and %2 after the first decode "
                                    "line"}.arg (behindTheReply).arg (behindTheDecode);
    QVERIFY2 (behindTheDecode > 0, "every row queued before the reply came before the first decode line after it");
    QVERIFY2 (behindTheReply > 0, "every row queued before the reply came before it");
    auto const model = runModel (modelDir ("decodes ahead after a reply"), pcm, modelSettings (true)
                                 << QString {"period live 0 %1"}.arg (m.samples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareKeptRows (lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
#endif
  }

  // A configure from live audio to a file waits until the live rows queued
  // are written, so the rows keep the stream's order across the switch: a
  // live FT4 period with the reader stopped, then a file period. The reader
  // resumes once the live period's decode has started and the frame loop has
  // had a second to reach the configure, or at once if the file period's
  // decode starts first. Every live row comes before the file period's
  // first, the file period's rows all come, none dropped, and every row is
  // the GUI's.
  void liveRowsGoOutBeforeAFilePeriod ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    auto const m = modeOf ("ft4");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    PipedCodec codec {directory.path (), {}};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.stallReading (5 * waitMs ());
    codec.write (sessionHeader () + configure (with (m.settings, {{"depth", 1}, {"live_audio", true}, {"spectrum", true}}))
                 + audio (pcm) + configure (with (labelFor (m, 1), {{"live_audio", false}})) + audio (pcm));
    auto const livePass = waitForLines (codec, [&] (QList<Line> const& lines) {return decisionsAt (lines, m.stop) >= 1;});
    if (livePass)
      {
        QElapsedTimer timer;
        timer.start ();
        while (timer.elapsed () < 1000 && decisionsAt (codec.lines (), m.stop) < 2)
          std::this_thread::sleep_for (std::chrono::milliseconds {5});
      }
    codec.resumeReading ();
    QVERIFY2 (livePass, qPrintable ("no decision for the live period\n" + show (codec.lines ())));
    codec.write (halt ());
    auto const finished = codec.finish (waitMs ());
    auto const lines = codec.lines ();
    QVERIFY2 (finished, qPrintable (QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (lines))));
    QVERIFY2 (count (lines, outType ("rows_dropped")) > 0, "nothing was dropped: the live rows were not held up");
    int boundary = -1;
    qint64 previous = 0;
    for (int i = 0; i < lines.size () && boundary < 0; ++i)
      if (outType ("spectrum") (lines.at (i)))
        {
          auto const k = qint64 (json (lines.at (i).text).value ("k").toDouble ());
          if (k <= previous) boundary = i;
          previous = k;
        }
    QVERIFY2 (boundary >= 0, qPrintable (show (lines)));
    auto const fileLines = lines.mid (boundary);
    QCOMPARE (count (fileLines, outType ("spectrum")), fileRows (m.samples, m.stop));
    QCOMPARE (count (fileLines, outType ("rows_dropped")), 0);
    auto const model = runModel (modelDir ("live then file"), pcm + pcm, modelSettings (true, m.stop)
                                 << QString {"period live 0 %1"}.arg (m.samples)
                                 << QString {"period file %1 %1"}.arg (m.samples));
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareKeptRows (lines, model.rows, true);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
#endif
  }

  // A fatal error with rows queued and the reader paused: everything queued
  // goes out first, none dropped, the error is the last line, and the exit
  // status is 1.
  void frameTooLargeEndsTheQueuedLines ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the paused reader runs on Unix pipes");
#else
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    PipedCodec codec {directory.path (), {}};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.stallReading (3000);
    QByteArray tooLong;
    tooLong.append (char (0x01));
    for (auto const byte : {0x01, 0x30, 0x93, 0x02}) tooLong.append (char (byte));   // 43,200,513 bytes
    codec.write (sessionHeader () + configure (with (m.settings, {{"depth", 1}, {"live_audio", true}, {"spectrum", true}}))
                 + audio (samples (pcm, 0, 17 * HsymSamples)) + tooLong);
    codec.finish (waitMs ());
    auto const lines = codec.lines ();
    QCOMPARE (codec.exited, 1);
    QList<Line> out;
    for (auto const& line : lines)
      if (!line.error) out << line;
    QVERIFY2 (!out.isEmpty () && out.last ().text.contains ("\"frame_too_large\""), qPrintable (show (lines)));
    QCOMPARE (count (out, outHas ("\"frame_too_large\"")), 1);
    QCOMPARE (count (out, outType ("spectrum")), 17);
    QCOMPARE (count (out, outType ("rows_dropped")), 0);
#endif
  }

  // A ref_spectrum_file error is never dropped: queued at the first failed
  // write, with the reader stalled from the start past the cap, it still
  // comes, once.
  void errorsAreNeverDropped ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader is a Unix test");
#else
    auto const m = modeOf ("ft4");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    QTemporaryDir data, temporary;
    QVERIFY (data.isValid () && temporary.isValid ());
    QVERIFY (blockRefspec (data.path ()));
    PipedCodec codec {data.path (), {}, temporary.path ()};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.write (sessionHeader ());
    QElapsedTimer timer;
    timer.start ();
    while (0 == count (codec.lines (), outType ("ready")) && timer.elapsed () < waitMs ())
      std::this_thread::sleep_for (std::chrono::milliseconds {10});
    codec.stallReading (4000);
    codec.write (configure (with (m.settings, {{"live_audio", true}, {"depth", 1}, {"spectrum", true},
                                               {"ref_spectrum", "measure"}}))
                 + audio (pcm) + configure (labelFor (m, 1)) + audio (pcm) + halt ());
    auto const finished = codec.finish (waitMs ());
    auto const lines = codec.lines ();
    QVERIFY2 (finished, qPrintable (QString {"the codec exited %1\n%2"}.arg (codec.exited).arg (show (lines))));
    QCOMPARE (count (lines, outHas ("\"ref_spectrum_file\"")), 1);
    QVERIFY2 (count (lines, outType ("rows_dropped")) > 0, "nothing was dropped: the stall did not pass the cap");
    QCOMPARE (count (lines, outType ("decode_finished")), 2);
#endif
  }

  // The queue's writer thread starts with the configure that first turns a
  // receive key that sends lines on: none while they are off.
  void writerThreadStartsWithTheFirstKey_data ()
  {
    QTest::addColumn<QString> ("key");
    QTest::newRow ("spectrum") << QString {"spectrum"};
    QTest::newRow ("prepared_audio") << QString {"prepared_audio"};
  }

  void writerThreadStartsWithTheFirstKey ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("threads are counted on Unix");
#else
    QFETCH (QString, key);
    auto const m = modeOf ("ft8");
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    Codec codec;
    QVERIFY2 (codec.started, qPrintable (codec.failure));
    codec.write (sessionHeader () + configure (with (m.settings, {{"depth", 1}})) + audio (samples (pcm, 0, 5 * HsymSamples))
                 + marker ());
    QVERIFY2 (codec.waitForCount (outHas ("configure_parse_error"), 1, waitMs (), "the first marker"),
              qPrintable (codec.report ()));
    auto const before = threadCount (codec.processId ());
    codec.write (configure ({{key, true}}) + audio (samples (pcm, 5 * HsymSamples, 6 * HsymSamples)) + marker ());
    QVERIFY2 (codec.waitForCount (outHas ("configure_parse_error"), 2, waitMs (), "the second marker"),
              qPrintable (codec.report ()));
    auto const after = threadCount (codec.processId ());
    codec.write (halt ());
    QVERIFY2 (codec.finish () && codec.clean (), qPrintable (codec.report ()));
    QVERIFY2 (before > 0, "threads not counted");
    QCOMPARE (after, before + 1);
#endif
  }

  // TIMING: live FT8 at depth 3 in real time, the reader stalled 10 s from 11 s
  // into the first of three periods: the decode sets and decisions are those
  // with rows off, pass starts within WSJT_SPECTRUM_PASS_TOLERANCE_MS
  // (default 100) of them, and one rows_dropped line, after which the rows run
  // from no more than 23 blocks before the reader resumed to the session's end.
  // With prepared_audio on too, every one of its lines arrives (they are never
  // discarded) and rows_dropped counts the rows alone.
  void stallPastTheCapLeavesLiveDecodes_data ()
  {
    QTest::addColumn<bool> ("prepared");
    QTest::newRow ("rows") << false;
    QTest::newRow ("rows and prepared audio") << true;
  }

  void stallPastTheCapLeavesLiveDecodes ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes");
#else
    QFETCH (bool, prepared);
    constexpr int Periods = 3;
    constexpr int StallAt = 11000;
    constexpr int StallMs = 10000;
    auto const tolerance = qEnvironmentVariableIsSet ("WSJT_SPECTRUM_PASS_TOLERANCE_MS")
      ? qEnvironmentVariableIntValue ("WSJT_SPECTRUM_PASS_TOLERANCE_MS") : 100;
    auto const pcm = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    struct Outcome
    {
      QList<QStringList> sets;
      QStringList decisions;
      QList<QList<qint64>> starts;
      QList<Line> lines;
      int resumed {0};   // blocks whose last sample was written before the reader resumed
      QString failure;
    };
    auto const session = [&] (bool rows) {
      Outcome outcome;
      QTemporaryDir directory;
      PipedCodec codec {directory.path (), {}};
      if (!codec.started)
        {
          outcome.failure = "the codec did not start";
          return outcome;
        }
      auto const utcMs = QDateTime::currentMSecsSinceEpoch ();
      auto const next = (utcMs / 15000 + 2) * 15000;
      std::this_thread::sleep_for (std::chrono::milliseconds {next - utcMs});
      auto const base = codec.now ();
      QJsonObject settings {{"mode", "FT8"}, {"depth", 3}, {"live_audio", true},
                            {"utc", QDateTime::fromMSecsSinceEpoch (next, Qt::UTC).toString ("HH:mm:ss")}};
      if (rows) settings = with (settings, spectrumKeys (false));
      if (rows && prepared) settings.insert ("prepared_audio", true);
      codec.write (sessionHeader () + configure (settings));
      qint64 resume = -1;
      for (int p = 0; p < Periods; ++p)
        {
          if (p > 0)
            codec.write (configure ({{"utc", QDateTime::fromMSecsSinceEpoch (next + 15000 * p, Qt::UTC).toString ("HH:mm:ss")}}));
          for (int at = 0; at < Ft8PeriodSamples; at += FrameSamples)
            {
              auto const due = base + 15000 * p + qint64 (at + FrameSamples) * 1000 / SampleRate;
              while (codec.now () < due) std::this_thread::sleep_for (std::chrono::milliseconds {1});
              auto const written = codec.write (frame (0x01u, samples (pcm, at, at + FrameSamples)));
              if (resume < 0 && 0 == p && written >= base + StallAt)
                {
                  codec.stallReading (StallMs);
                  resume = codec.now () + StallMs;
                }
              for (int k = (at / HsymSamples + 1) * HsymSamples; k <= at + FrameSamples; k += HsymSamples)
                if (written < resume || resume < 0) ++outcome.resumed;
            }
        }
      codec.write (halt ());
      if (!codec.finish (waitMs ()))
        {
          outcome.failure = QString {"the codec exited %1"}.arg (codec.exited);
          return outcome;
        }
      outcome.lines = codec.lines ();
      outcome.decisions = decisions (outcome.lines);
      static QRegularExpression const pass {"wsjt-codec: pass nzhsym=(\\d+) "};
      for (int p = 0; p < Periods; ++p)
        {
          outcome.sets << QStringList {};
          outcome.starts << QList<qint64> {};
        }
      for (auto const& line : outcome.lines)
        {
          if (line.error && pass.match (QString::fromUtf8 (line.text)).hasMatch ())
            {
              auto const p = std::min (Periods - 1, int ((line.ms - base) / 15000));
              outcome.starts[p] << line.ms - base - 15000 * p;
            }
          if (outType ("decode") (line))
            {
              auto const time = json (line.text).value ("time").toString ();
              for (int p = 0; p < Periods; ++p)
                if (QDateTime::fromMSecsSinceEpoch (next + 15000 * p, Qt::UTC).toString ("HHmmss") == time)
                  outcome.sets[p] << json (line.text).value ("message").toString ().trimmed ();
            }
        }
      for (auto& set : outcome.sets)
        {
          set.removeDuplicates ();
          std::sort (set.begin (), set.end ());
        }
      return outcome;
    };
    auto const off = session (false);
    QVERIFY2 (off.failure.isEmpty (), qPrintable (off.failure));
    auto const on = session (true);
    QVERIFY2 (on.failure.isEmpty (), qPrintable (on.failure));
    for (int p = 0; p < Periods; ++p)
      {
        QStringList offTimes, onTimes;
        for (auto t : off.starts.at (p)) offTimes << QString::number (t);
        for (auto t : on.starts.at (p)) onTimes << QString::number (t);
        qInfo ().noquote () << QString {"period %1: %2 and %3 decodes; pass starts off [%4] on [%5] ms"}.arg (p)
          .arg (off.sets.at (p).size ()).arg (on.sets.at (p).size ()).arg (offTimes.join (' '), onTimes.join (' '));
      }
    QCOMPARE (count (off.lines, outType ("rows_dropped")) + count (off.lines, outType ("spectrum")), 0);
    QCOMPARE (on.decisions, off.decisions);
    QCOMPARE (on.sets, off.sets);
    for (int p = 0; p < Periods; ++p)
      for (int i = 0; i < std::min (on.starts.at (p).size (), off.starts.at (p).size ()); ++i)
        QVERIFY2 (std::abs (on.starts.at (p).at (i) - off.starts.at (p).at (i)) <= tolerance,
                  qPrintable (QString {"period %1 pass %2 starts at %3 ms, %4 ms without rows"}.arg (p).arg (i)
                              .arg (on.starts.at (p).at (i)).arg (off.starts.at (p).at (i))));
    int first = 0, after = 0;
    auto const problem = droppedRun (on.lines, liveRows (Ft8PeriodSamples), Periods, first, after);
    QVERIFY2 (problem.isEmpty (), qPrintable (problem));
    qInfo ().noquote () << QString {"rows after rows_dropped from block %1; %2 blocks were in before the reader resumed"}
      .arg (first).arg (on.resumed);
    QVERIFY2 (first <= on.resumed && first >= on.resumed - 23, "the rows after rows_dropped are not the newest");
    QList<Prepared> lines;
    auto const problemLines = preparedOf (on.lines, lines);
    QVERIFY2 (problemLines.isEmpty (), qPrintable (problemLines));
    if (!prepared)
      {
        QCOMPARE (lines.size (), 0);
        return;
      }
    QList<QByteArray> got;
    auto const problemPeriods = preparedPeriods (lines, got);
    QVERIFY2 (problemPeriods.isEmpty (), qPrintable (problemPeriods));
    QCOMPARE (got.size (), Periods);
    auto const want = samples (pcm, 0, liveRows (Ft8PeriodSamples) * HsymSamples);
    for (int p = 0; p < Periods; ++p)
      QVERIFY2 (got.at (p) == want, qPrintable (QString {"period %1: %2"}.arg (p).arg (firstDifference (got.at (p), want))));
#endif
  }

  // A reader that keeps up on average but stops once a period for longer
  // than a full pipe takes to fill leaves live FT8's decode sets and its pass
  // decisions as with the rows off; pass start times are reported.
  // WSJT_SPECTRUM_STALL_MS sets the stall (default 3000).
  void backPressureLeavesLiveDecodes_data ()
  {
    QTest::addColumn<int> ("stallAt");   // ms into each period
    QTest::newRow ("stall before pass 41") << 11000;
    QTest::newRow ("stall at the stop") << 14000;
  }

  void backPressureLeavesLiveDecodes ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("the stalled reader runs on Unix pipes; Windows reads through QProcess");
#else
    QFETCH (int, stallAt);
    auto const stallMs = qEnvironmentVariableIsSet ("WSJT_SPECTRUM_STALL_MS")
      ? qEnvironmentVariableIntValue ("WSJT_SPECTRUM_STALL_MS") : 3000;
    constexpr int Periods = 4;
    auto const pcm = period (wavPcm (inSamples ("FT8/210703_133430.wav")), Ft8PeriodSamples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    struct Outcome
    {
      QList<QStringList> sets;     // per period
      QStringList decisions;
      QList<QList<qint64>> starts; // per period, each pass's start after the period's first sample
      int rows {0};
      QString failure;
    };
    auto const session = [&] (bool rows) {
      Outcome outcome;
      QTemporaryDir directory;
      PipedCodec codec {directory.path (), {}};
      if (!codec.started)
        {
          outcome.failure = "the codec did not start";
          return outcome;
        }
      // Each period starts on its UTC 15 s boundary, as a live client's does.
      auto const utcMs = QDateTime::currentMSecsSinceEpoch ();
      auto const next = (utcMs / 15000 + 2) * 15000;
      std::this_thread::sleep_for (std::chrono::milliseconds {next - utcMs});
      auto const base = codec.now ();
      QList<qint64> periodStarts;
      QJsonObject settings {{"mode", "FT8"}, {"depth", 3}, {"live_audio", true},
                            {"utc", QDateTime::fromMSecsSinceEpoch (next, Qt::UTC).toString ("HH:mm:ss")}};
      if (rows) settings = with (settings, spectrumKeys (false));
      codec.write (sessionHeader () + configure (settings));
      for (int p = 0; p < Periods; ++p)
        {
          auto const label = QDateTime::fromMSecsSinceEpoch (next + 15000 * p, Qt::UTC).toString ("HH:mm:ss");
          if (p > 0) codec.write (configure ({{"utc", label}}));
          periodStarts << base + 15000 * p;
          bool stalled = false;
          for (int at = 0; at < Ft8PeriodSamples; at += FrameSamples)
            {
              auto const due = base + 15000 * p + qint64 (at + FrameSamples) * 1000 / SampleRate;
              while (codec.now () < due) std::this_thread::sleep_for (std::chrono::milliseconds {1});
              codec.write (frame (0x01u, samples (pcm, at, at + FrameSamples)));
              if (!stalled && codec.now () >= base + 15000 * p + stallAt)
                {
                  codec.stallReading (stallMs);
                  stalled = true;
                }
            }
        }
      codec.write (halt ());
      if (!codec.finish (waitMs ()))
        {
          outcome.failure = QString {"the codec exited %1"}.arg (codec.exited);
          return outcome;
        }
      auto const lines = codec.lines ();
      outcome.decisions = decisions (lines);
      outcome.rows = count (lines, outType ("spectrum"));
      static QRegularExpression const pass {"wsjt-codec: pass nzhsym=(\\d+) "};
      for (int p = 0; p < Periods; ++p)
        {
          outcome.sets << QStringList {};
          outcome.starts << QList<qint64> {};
        }
      for (auto const& line : lines)
        {
          if (line.error && pass.match (QString::fromUtf8 (line.text)).hasMatch ())
            {
              auto const p = std::min (Periods - 1, int ((line.ms - base) / 15000));
              outcome.starts[p] << line.ms - periodStarts.at (p);
            }
          if (outType ("decode") (line))
            {
              auto const time = json (line.text).value ("time").toString ();
              for (int p = 0; p < Periods; ++p)
                if (QDateTime::fromMSecsSinceEpoch (next + 15000 * p, Qt::UTC).toString ("HHmmss") == time)
                  outcome.sets[p] << json (line.text).value ("message").toString ().trimmed ();
            }
        }
      for (auto& set : outcome.sets)
        {
          set.removeDuplicates ();
          std::sort (set.begin (), set.end ());
        }
      return outcome;
    };
    auto const off = session (false);
    QVERIFY2 (off.failure.isEmpty (), qPrintable (off.failure));
    auto const on = session (true);
    QVERIFY2 (on.failure.isEmpty (), qPrintable (on.failure));
    for (int p = 0; p < Periods; ++p)
      {
        QStringList offTimes, onTimes;
        for (auto t : off.starts.at (p)) offTimes << QString::number (t);
        for (auto t : on.starts.at (p)) onTimes << QString::number (t);
        qInfo ().noquote () << QString {"period %1: %2 and %3 decodes; pass starts off [%4] on [%5] ms; stall %6 ms at %7 ms"}
          .arg (p).arg (off.sets.at (p).size ()).arg (on.sets.at (p).size ()).arg (offTimes.join (' '), onTimes.join (' '))
          .arg (stallMs).arg (stallAt);
      }
    QVERIFY2 (on.rows > 0, "the session with the key wrote no rows");
    QCOMPARE (on.decisions, off.decisions);
    QCOMPARE (on.sets, off.sets);
#endif
  }

  // Paced, during the slowest decodes, each block's row comes within a block
  // interval plus the row's own time (the greatest latency before the
  // session's first pass) plus WSJT_SPECTRUM_LATENCY_MARGIN_MS (default 20;
  // 100 on the capped image) of the block's last sample, and the rows equal
  // the GUI's. WSJT_CODEC_TEST_DECODE_NICE passes through to the codec.
  void rowLatencyDuringTheSlowestDecodes_data ()
  {
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<bool> ("varied");   // frames of 20 ms to 500 ms, else 100 ms
    QTest::newRow ("ft8-mtd depth 3") << QString {"ft8-mtd"} << false;
    QTest::newRow ("ft8-mtd depth 3 varied") << QString {"ft8-mtd"} << true;
    QTest::newRow ("fst4-60 depth 3") << QString {"fst4-60-wide"} << false;
    QTest::newRow ("fst4-60 depth 3 varied") << QString {"fst4-60-wide"} << true;
  }

  void rowLatencyDuringTheSlowestDecodes ()
  {
#ifndef Q_OS_UNIX
    QSKIP ("measured on Unix pipes");
#else
    QFETCH (QString, mode);
    QFETCH (bool, varied);
    auto const wide = "fst4-60-wide" == mode;
    auto m = modeOf (wide ? "fst4-60" : mode);
    if (wide) m.settings = with (m.settings, {{"nfa", 200}, {"nfb", 4000}});
    auto const pcm = period (wavPcm (m.wav), m.samples);
    QVERIFY2 (!pcm.isEmpty (), "no recording");
    auto const margin = qEnvironmentVariableIsSet ("WSJT_SPECTRUM_LATENCY_MARGIN_MS")
      ? qEnvironmentVariableIntValue ("WSJT_SPECTRUM_LATENCY_MARGIN_MS") : 20;
    auto const periods = wide ? 2 : 4;
    QTemporaryDir directory;
    QStringList environment {"OMP_STACKSIZE=16M"};
    PipedCodec codec {directory.path (), environment};
    QVERIFY2 (codec.started, "the codec did not start");
    codec.write (sessionHeader () + configure (with (with (m.settings, {{"live_audio", true}, {"depth", 3},
                                                                        {"busy_events", true}}),
                                                     spectrumKeys (false))));
    std::mt19937 random {1};
    std::uniform_int_distribution<int> sizes {240, 6000};
    QList<qint64> arrivals;   // when each block's last sample was written
    QByteArray sent;
    auto const base = codec.now () + 50;
    qint64 total = 0;
    for (int p = 0; p < periods; ++p)
      {
        if (p > 0) codec.write (configure (labelFor (m, p)));
        for (int at = 0; at < m.samples;)
          {
            auto const size = std::min (varied ? sizes (random) : FrameSamples, m.samples - at);
            auto const due = base + (total + size) * 1000 / SampleRate;
            while (codec.now () < due) std::this_thread::sleep_for (std::chrono::milliseconds {1});
            auto const written = codec.write (frame (0x01u, samples (pcm, at, at + size)));
            for (int k = (at / HsymSamples + 1) * HsymSamples; k <= at + size; k += HsymSamples) arrivals << written;
            at += size;
            total += size;
          }
        sent += pcm;
      }
    codec.write (halt ());
    QVERIFY2 (codec.finish (5 * waitMs ()), qPrintable (show (codec.lines ())));
    auto const lines = codec.lines ();
    QList<qint64> latencies;
    int firstPass = -1;
    for (int i = 0; i < lines.size (); ++i)
      {
        if (firstPass < 0 && outType ("busy") (lines.at (i))) firstPass = latencies.size ();
        if (outType ("spectrum") (lines.at (i))) latencies << lines.at (i).ms;
      }
    QCOMPARE (latencies.size (), arrivals.size ());
    for (int i = 0; i < latencies.size (); ++i) latencies[i] -= arrivals.at (i);
    QVERIFY2 (firstPass > 0, "no row before the first pass");
    auto const own = *std::max_element (latencies.begin (), latencies.begin () + firstPass);
    auto sorted = latencies;
    std::sort (sorted.begin (), sorted.end ());
    auto const blockMs = qint64 (1000 * HsymSamples / SampleRate);
    auto const bound = blockMs + own + margin;
    qInfo ().noquote () << QString {"%1 rows: median %2 ms, 95th %3 ms, greatest %4 ms; before the first pass at most "
                                    "%5 ms; bound %6 ms"}
      .arg (sorted.size ()).arg (sorted.at (sorted.size () / 2)).arg (sorted.at (sorted.size () * 95 / 100))
      .arg (sorted.last ()).arg (own).arg (bound);
    QVERIFY2 (sorted.last () <= bound, "a row came later than the bound");
    auto const rows = rowsOf (lines);
    QVERIFY2 (rows.problem.isEmpty (), qPrintable (rows.problem));
    QStringList script = modelSettings (false, m.stop);
    for (int p = 0; p < periods; ++p) script << QString {"period live %1 %2"}.arg (p * m.samples).arg (m.samples);
    auto const model = runModel (modelDir (QTest::currentDataTag ()), sent, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareRows (rows.rows, model.rows);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
#endif
  }
};

QTEST_GUILESS_MAIN (TestStreamingSpectrum)

#include "test_streaming_spectrum.moc"
