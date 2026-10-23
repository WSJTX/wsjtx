// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include <QCoreApplication>
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
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace
{
constexpr int SampleRate = 12000;
// MainWindow::fastSink's MSK144 block (on_actionMSK144_triggered: initializeFFT (6, 7 * 512)).
constexpr int BlockSamples = 3584;
// A paced client's frame, 0.1 s of audio.
constexpr int FrameSamples = 1200;
constexpr char MarkerDetail[] = "malformed control frame (missing or invalid t field)";

// One wait covers a session; sanitizer builds raise QtTest's limit and this with it.
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

QByteArray configureFrame (QJsonObject keys)
{
  keys.insert ("t", "configure");
  return frame (0x02u, QJsonDocument {keys}.toJson (QJsonDocument::Compact));
}

// A control frame without "t": its configure_parse_error shows the stream has
// read everything before it.
QByteArray marker ()
{
  return frame (0x02u, "{}");
}

QJsonObject with (QJsonObject keys, QJsonObject const& more)
{
  for (auto it = more.constBegin (); it != more.constEnd (); ++it) keys.insert (it.key (), it.value ());
  return keys;
}

QByteArray wavSamples (QString const& name)
{
  QFile file {name};
  if (!file.open (QIODevice::ReadOnly)) return {};
  auto const bytes = file.readAll ();
  for (int at = 12; at + 8 <= bytes.size ();)
    {
      auto const size = int (qFromLittleEndian<quint32> (bytes.constData () + at + 4));
      if (bytes.mid (at, 4) == "data") return bytes.mid (at + 8, size & ~1);
      at += 8 + size + (size & 1);
    }
  return {};
}

QByteArray recording (char const* name)
{
#ifdef MSK144_SAMPLES
  return wavSamples (QString {MSK144_SAMPLES "/%1.wav"}.arg (name));
#else
  return wavSamples (name);
#endif
}

// msk144sim's output for one of the fixtures CMake prepares.
QByteArray simulated (char const* name)
{
#ifdef MSK144_SIM_DIR
  return wavSamples (QString {MSK144_SIM_DIR "/%1/000000_000001.wav"}.arg (name));
#else
  return wavSamples (name);
#endif
}

QByteArray ft8Recording ()
{
#ifdef FT8_SAMPLE_WAV
  return wavSamples (FT8_SAMPLE_WAV);
#else
  return {};
#endif
}

QByteArray zeroed (QByteArray pcm, int from, int to)
{
  to = std::min (to, int (pcm.size () / 2));
  if (from < to) std::fill (pcm.begin () + 2 * from, pcm.begin () + 2 * to, '\0');
  return pcm;
}

QByteArray samples (QByteArray const& pcm, int count)
{
  return pcm.left (2 * count);
}

struct Period
{
  QString utc;
  QByteArray pcm;      // what the stream gets of the period: all of it, unless the session ends in it
  QJsonObject keys;    // configure keys sent with its utc, before its audio
  QByteArray control;  // control frames sent after that configure
  QJsonObject applied; // what of them the GUI model applies
  QByteArray before;   // control frames sent before the configure
  bool msk144 {true};  // false: another mode's period, which the GUI decodes in its jt9 worker
};

struct Session
{
  QJsonObject settings;  // the first configure's keys
  QList<Period> periods;
  int frameSamples {FrameSamples};
  bool aligned {true};   // frames also end at every block end
  // After this period's decode_finished, answer gives the next period's keys from the events
  // so far and the -a directory.
  int answerAfter {-1};
  std::function<QJsonObject (QList<QJsonObject> const&, QString const&)> answer;
  bool halt {false};     // a halt ends the input, else the input closes
};

struct Expect
{
  QStringList decodes;     // messages the GUI model decodes
  QStringList lacks;       // messages it does not
  int count {-1};          // its decodes, when pinned
  bool marks {false};      // it marks a decode made while training
  int trained {0};         // trainings it completes
  QList<bool> written;     // per training, whether the stream can write its file (empty: each one)
  QStringList errors;      // the codes of the stream's error events
  bool noSessionDefaults {false};  // no event names the session's default calls
};
}

Q_DECLARE_METATYPE (Session)
Q_DECLARE_METATYPE (Expect)

namespace
{
Expect expectDecodes (QStringList const& messages)
{
  Expect expect;
  expect.decodes = messages;
  return expect;
}

Expect expectLacks (QStringList const& messages)
{
  Expect expect;
  expect.lacks = messages;
  return expect;
}

QJsonObject const Defaults {{"mode", "MSK144"}, {"trperiod", 15}, {"rxfreq", 1500}, {"ntol", 50}, {"depth", 3}};

int periodSamples (Session const& session)
{
  return int (session.settings.value ("trperiod").toDouble () * SampleRate);
}

qint64 start (Session const& session, int period)
{
  qint64 first = 0;
  for (int p = 0; p < period; ++p) first += session.periods.at (p).pcm.size () / 2;
  return first;
}

Period period (QString const& utc, QByteArray const& pcm)
{
  Period p;
  p.utc = utc;
  p.pcm = pcm;
  return p;
}

QString label (QString utc)
{
  return utc.remove (':');
}

// Periods cut from one recording.
QList<Period> periodsOf (QByteArray const& pcm, int count, QStringList const& utc)
{
  QList<Period> periods;
  for (int p = 0; p < utc.size (); ++p)
    {
      periods << period (utc.at (p), pcm.mid (2 * p * count, 2 * count));
    }
  return periods;
}

Session session (QJsonObject const& settings, QList<Period> const& periods)
{
  Session s;
  s.settings = with (Defaults, settings);
  s.periods = periods;
  return s;
}

Session one (QJsonObject const& settings, QByteArray const& pcm, QString const& utc)
{
  return session (settings, {period (utc, pcm)});
}

using Frames = QList<QPair<qint64, qint64>>;

QByteArray periodBytes (Session const& session, int p, Frames& frames)
{
  auto const& period = session.periods.at (p);
  auto keys = with (period.keys, {{"utc", period.utc}});
  if (0 == p) keys = with (session.settings, keys);
  auto bytes = period.before + configureFrame (keys) + period.control;
  auto const first = start (session, p);
  auto const n = int (period.pcm.size () / 2);
  for (int k = 0; k < n;)
    {
      auto end = std::min (n, k + session.frameSamples);
      if (session.aligned) end = std::min (end, (k / BlockSamples + 1) * BlockSamples);
      bytes += frame (0x01u, period.pcm.mid (2 * k, 2 * (end - k))) + marker ();
      frames << qMakePair (first + k, first + end);
      k = end;
    }
  return bytes;
}

struct Events
{
  QList<QJsonObject> values;
  QStringList lines;
  QString invalid;
};

Events parse (QByteArray const& out)
{
  Events events;
  for (auto const& line : QByteArray {out}.replace ("\r\n", "\n").split ('\n'))
    {
      if (line.trimmed ().isEmpty ()) continue;
      auto const document = QJsonDocument::fromJson (line);
      auto const event = document.object ();
      if (!document.isObject () || event.value ("v").toInt () != 1 || !event.value ("t").isString ())
        {
          events.invalid = QString::fromUtf8 (line);
          continue;
        }
      events.values << event;
      events.lines << QString::fromUtf8 (line);
    }
  return events;
}

QMap<QString, QByteArray> coefficientFiles (QString const& directory)
{
  QMap<QString, QByteArray> files;
  for (auto const& name : QDir {directory}.entryList ({"*.pcoeff"}, QDir::Files))
    {
      QFile file {QDir {directory}.filePath (name)};
      // An empty one is a name the test took before the stream could.
      if (file.open (QIODevice::ReadOnly) && file.size () > 0) files.insert (name, file.readAll ());
    }
  return files;
}

struct StreamRun
{
  bool started {false};
  bool timedOut {false};
  int exitCode {-1};
  QProcess::ExitStatus exitStatus {QProcess::CrashExit};
  QByteArray out;
  QByteArray err;
  QString dataDir;                    // the session's -a directory
  QMap<QString, QByteArray> pcoeffs;  // its coefficient files
  Frames frames;                      // each audio frame's samples; a marker follows each
  qint64 elapsedMs {0};
};

StreamRun runStream (Session& session)
{
  StreamRun r;
  QTemporaryDir directory;
  if (!directory.isValid ()) return r;
  r.dataDir = directory.path ();
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.setWorkingDirectory (directory.path ());
  QElapsedTimer clock;
  clock.start ();
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                 {"-a", directory.path (), "-t", directory.path (), "--stream"});
  r.started = process.waitForStarted (5000);
  if (!r.started)
    {
      r.err = process.errorString ().toUtf8 ();
      return r;
    }
  auto const read = [&] {
    // Qt 5 on Windows returns on stdout's data with stderr's readyRead still queued.
    QCoreApplication::processEvents ();
    r.out += process.readAllStandardOutput ();
    r.err += process.readAllStandardError ();
  };
  auto const pumpWhile = [&] (std::function<bool ()> const& going) {
    QElapsedTimer timer;
    timer.start ();
    while (going () && QProcess::Running == process.state () && timer.elapsed () < waitMs ())
      {
        if (process.bytesToWrite () > 0) process.waitForBytesWritten (20);
        else process.waitForReadyRead (20);
        read ();
      }
  };
  auto const answered = session.answer && session.answerAfter + 1 < session.periods.size ();
  auto const last = answered ? session.answerAfter : session.periods.size () - 1;
  QByteArray head = sessionHeader ();
  for (int p = 0; p <= last; ++p) head += periodBytes (session, p, r.frames);
  process.write (head);
  if (answered)
    {
      pumpWhile ([&] {return r.out.count (R"("t":"decode_finished")") <= last;});
      auto& next = session.periods[last + 1];
      next.keys = with (next.keys, session.answer (parse (r.out).values, directory.path ()));
      QByteArray tail;
      for (int p = last + 1; p < session.periods.size (); ++p) tail += periodBytes (session, p, r.frames);
      process.write (tail);
    }
  if (session.halt) process.write (frame (0x02u, R"({"t":"halt"})"));
  process.closeWriteChannel ();
  pumpWhile ([] {return true;});
  if (QProcess::Running == process.state ())
    {
      r.timedOut = true;
      process.kill ();
      process.waitForFinished (5000);
    }
  read ();
  r.exitCode = process.exitCode ();
  r.exitStatus = process.exitStatus ();
  r.elapsedMs = clock.elapsed ();
  r.pcoeffs = coefficientFiles (directory.path ());
  return r;
}

// The GUI's settings behind the configure keys (MainWindow::fastSink).
QStringList modelSettings (QJsonObject const& keys)
{
  QStringList lines;
  for (auto it = keys.constBegin (); it != keys.constEnd (); ++it)
    {
      auto const& key = it.key ();
      auto const& value = it.value ();
      auto const flag = [&] (char const* name) {return QString {"%1 %2"}.arg (name).arg (value.toBool () ? 1 : 0);};
      if ("mode" == key || "utc" == key || "live_audio" == key) continue;
      if ("trperiod" == key) lines << QString {"ntr %1"}.arg (int (value.toDouble ()));
      else if ("depth" == key) lines << QString {"depth %1"}.arg (value.toInt ());
      else if ("rxfreq" == key) lines << QString {"rxfreq %1"}.arg (value.toInt ());
      else if ("ntol" == key) lines << QString {"ntol %1"}.arg (value.toInt ());
      else if ("mycall" == key) lines << "mycall " + value.toString ();
      else if ("his_call" == key) lines << "hiscall " + value.toString ();
      else if ("short_messages" == key) lines << flag ("shmsg");
      else if ("swl" == key) lines << flag ("swl");
      else if ("train" == key) lines << flag ("train");
      else if ("phase_eq" == key)
        {
          QStringList terms;
          for (auto const& term : value.toArray ()) terms << QString::number (term.toDouble (), 'g', 17);
          lines << "pcoeffs " + terms.join (' ');
        }
      else lines << "unmodelled " + key;
    }
  return lines;
}

struct ModelLine
{
  QString kind;  // decode, trained or end
  int period;    // the session's
  int block;     // ends at sample BlockSamples * block of the period
  QString text;  // a decode's mskrtd line, or the coefficient file a training wrote
};

struct ModelRun
{
  bool ok {false};
  QByteArray out;
  QByteArray err;
  QList<ModelLine> lines;
  QList<QByteArray> trainings;  // each training's coefficient file
};

ModelRun runModel (Session const& session)
{
  ModelRun m;
  QTemporaryDir directory;
  if (!directory.isValid ()) return m;
  QFile audio {directory.filePath ("audio.raw")};
  if (!audio.open (QIODevice::WriteOnly)) return m;
  for (auto const& period : session.periods) audio.write (period.pcm);
  audio.close ();
  QStringList script {"audio " + audio.fileName ()};
  QList<int> decoded;
  auto keys = session.settings;
  for (int p = 0; p < session.periods.size (); ++p)
    {
      auto const& period = session.periods.at (p);
      if (!period.msk144) continue;
      decoded << p;
      keys = with (with (keys, period.keys), period.applied);
      // A directory per period, with room for a call containing '/': the model writes every
      // training's file, whatever stops the stream writing its own.
      QDir const data {directory.filePath (QString {"p%1"}.arg (p))};
      auto const call = keys.value ("his_call").toString ();
      if (!data.mkpath (call.contains ('/') ? call.section ('/', 0, -2) : QString {"."})) return m;
      script << modelSettings (0 == p ? with (session.settings, period.keys) : period.keys)
             << modelSettings (period.applied) << "datadir " + data.path ()
             << QString {"period %1 %2 %3"}.arg (label (period.utc).toInt ()).arg (start (session, p))
        .arg (period.pcm.size () / 2);
    }
  QFile file {directory.filePath ("script.txt")};
  if (!file.open (QIODevice::WriteOnly)) return m;
  file.write ((script.join ('\n') + '\n').toUtf8 ());
  file.close ();
  QProcess model;
  model.start (QString::fromUtf8 (MSK144_GUI_MODEL), {file.fileName ()});
  if (!model.waitForFinished (waitMs ())) model.kill ();
  m.out = model.readAllStandardOutput ();
  m.err = model.readAllStandardError () + script.join ('\n').toUtf8 ();
  m.ok = QProcess::NormalExit == model.exitStatus () && 0 == model.exitCode ();
  QRegularExpression const line {R"(^(decode|trained|end) (\d+)(?: (\d+))?(?: \|(.*))?$)"};
  for (auto const& text : QString::fromUtf8 (m.out).remove ('\r').split ('\n', Qt::SkipEmptyParts))
    {
      auto const match = line.match (text);
      auto const index = match.captured (2).toInt () - 1;
      if (!match.hasMatch () || index < 0 || index >= decoded.size ())
        {
          m.ok = false;
          continue;
        }
      m.lines << ModelLine {match.captured (1), decoded.at (index), match.captured (3).toInt (), match.captured (4)};
      if ("trained" == m.lines.last ().kind)
        {
          QFile written {m.lines.last ().text};
          m.trainings << (written.open (QIODevice::ReadOnly) ? written.readAll () : QByteArray {});
        }
    }
  return m;
}

QString rightTrimmed (QString text)
{
  while (text.endsWith (' ')) text.chop (1);
  return text;
}

QString decodeToken (QString const& time, int snr, int dt10, int freq, QString const& message,
                     QString const& training)
{
  return QString {"decode %1 snr %2 dt %3 freq %4 %5%6"}.arg (time).arg (snr).arg (dt10 / 10.0, 0, 'f', 1)
    .arg (freq).arg (message, training);
}

// mskrtd's line: i6.6,i4,f5.1,i5,a4,a37 (lib/mskrtd.f90), "^" in the symbol while training.
QString modelDecode (QString const& line)
{
  auto const training = line.mid (20, 4).contains ('^') ? QString {" training"} : QString {};
  return decodeToken (line.mid (0, 6), line.mid (6, 4).trimmed ().toInt (),
                      qRound (line.mid (10, 5).trimmed ().toDouble () * 10), line.mid (15, 5).trimmed ().toInt (),
                      rightTrimmed (line.mid (24, 37)), training);
}

QString modelMessage (QString const& line)
{
  return rightTrimmed (line.mid (24, 37));
}

QString streamDecode (QJsonObject const& event, QString const& line)
{
  QString training;
  if (event.contains ("training"))
    {
      training = event.value ("training") == QJsonValue {true} && line.endsWith (R"(,"training":true})")
        ? QString {" training"} : " training as " + line;
    }
  auto const mode = event.value ("mode").toString ();
  return (mode == "MSK144" ? QString {} : "mode " + mode + " ")
    + decodeToken (event.value ("time").toString (), event.value ("snr").toInt (),
                   qRound (event.value ("dt").toDouble () * 10), event.value ("freq").toInt (),
                   event.value ("message").toString (), training);
}

struct Token
{
  QString text;
  int slot;  // markers before it
};

int slotOf (Frames const& frames, qint64 sample)
{
  int slot = 0;
  while (slot < frames.size () && frames.at (slot).second < sample) ++slot;
  return slot;
}

// The GUI model's decodes, trainings and period ends, each placed after the
// markers of the frames before the one holding its block's last sample.
QList<Token> expected (Session const& session, ModelRun const& model, Frames const& frames, Expect const& expect)
{
  QStringList hisCall;
  auto keys = session.settings;
  for (auto const& period : session.periods)
    {
      keys = with (with (keys, period.keys), period.applied);
      hisCall << keys.value ("his_call").toString ().left (12).trimmed ();
    }
  QList<Token> tokens;
  int trainings = 0;
  for (int p = 0; p < session.periods.size (); ++p)
    {
      auto const& period = session.periods.at (p);
      auto const first = start (session, p);
      auto const count = period.pcm.size () / 2;
      // A period cut short ends when the stream's input does.
      auto const ending = count == periodSamples (session) ? slotOf (frames, first + count) : frames.size ();
      if (!period.msk144) tokens << Token {"finished " + label (period.utc), ending};
      for (auto const& line : model.lines)
        {
          if (line.period != p) continue;
          auto const end = first + qint64 {BlockSamples} * line.block;
          if ("decode" == line.kind)
            {
              tokens << Token {modelDecode (line.text), slotOf (frames, end)};
            }
          else if ("trained" == line.kind)
            {
              auto const written = expect.written.value (trainings++, true);
              tokens << Token {"trained " + hisCall.at (p) + (written ? "" : " without a file"), slotOf (frames, end)};
            }
          else if ("end" == line.kind)
            {
              tokens << Token {"finished " + label (period.utc), ending};
            }
        }
    }
  return tokens;
}

struct Observed
{
  QList<Token> tokens;
  QList<QJsonObject> trained;
  QStringList errors;  // error events other than the markers'
  int markers {0};
  QString invalid;
};

// Another mode's decodes are left out when the session has its periods.
Observed observe (StreamRun const& run, bool otherModes)
{
  Observed o;
  auto const events = parse (run.out);
  o.invalid = events.invalid;
  for (int i = 0; i < events.values.size (); ++i)
    {
      auto const& event = events.values.at (i);
      auto const type = event.value ("t").toString ();
      if ("ready" == type) continue;
      if ("error" == type)
        {
          if ("configure_parse_error" == event.value ("code").toString ()
              && MarkerDetail == event.value ("detail").toString ())
            {
              ++o.markers;
            }
          else
            {
              o.errors << events.lines.at (i);
            }
        }
      else if ("decode" == type)
        {
          if (otherModes && "MSK144" != event.value ("mode").toString ()) continue;
          o.tokens << Token {streamDecode (event, events.lines.at (i)), o.markers};
        }
      else if ("msk144_trained" == type)
        {
          o.trained << event;
          auto const file = event.value ("file");
          auto const how = file.isNull () ? QString {" without a file"}
            : file.isString () ? QString {} : " file as " + events.lines.at (i);
          o.tokens << Token {"trained " + event.value ("call").toString () + how, o.markers};
        }
      else if ("decode_finished" == type)
        {
          o.tokens << Token {"finished " + event.value ("period_end").toString (), o.markers};
        }
      else
        {
          o.tokens << Token {events.lines.at (i), o.markers};
        }
    }
  return o;
}

QStringList texts (QList<Token> const& tokens, bool placed)
{
  QStringList lines;
  for (auto const& token : tokens) lines << (placed ? QString {"@%1 "}.arg (token.slot) : QString {}) + token.text;
  return lines;
}

// A coefficient file's five terms (msk144signalquality's header line, read as
// EqualizationToolsDialog::impl::plot_phase reads it).
QList<double> fileTerms (QByteArray const& contents)
{
  auto const fields = QString::fromLatin1 (contents.left (contents.indexOf ('\n')))
    .split (QRegularExpression {"\\s+"}, Qt::SkipEmptyParts);
  QList<double> terms;
  if (fields.size () < 6) return terms;
  auto const count = fields.at (5).toInt ();
  for (int i = 0; i < count && 6 + i < fields.size (); ++i) terms << fields.at (6 + i).toDouble ();
  return terms;
}

bool same (QList<double> const& a, QList<double> const& b,
           double absoluteTolerance = 1e-15, double relativeTolerance = 1e-12)
{
  if (a.size () != b.size ()) return false;
  for (int i = 0; i < a.size (); ++i)
    {
      if (!std::isfinite (a.at (i)) || !std::isfinite (b.at (i))) return false;
      if (std::abs (a.at (i) - b.at (i))
          > std::max (absoluteTolerance, relativeTolerance * std::abs (b.at (i)))) return false;
    }
  return true;
}

QString show (QList<double> const& terms)
{
  QStringList text;
  for (auto const term : terms) text << QString::number (term, 'g', 17);
  return text.join (' ');
}

QList<double> eventTerms (QJsonObject const& event)
{
  QList<double> terms;
  for (auto const& term : event.value ("phase_eq").toArray ())
    {
      terms << (term.isDouble () ? term.toDouble () : std::nan (""));
    }
  return terms;
}

struct Outcome
{
  Session session;  // with the keys answered
  StreamRun stream;
  ModelRun model;
  Observed seen;
  QList<Token> want;
  QString report;
};

Outcome play (Session session, Expect const& expect)
{
  Outcome o;
  o.stream = runStream (session);
  o.session = session;
  o.model = runModel (session);
  auto const otherModes = std::any_of (session.periods.begin (), session.periods.end (),
                                       [] (Period const& period) {return !period.msk144;});
  o.seen = observe (o.stream, otherModes);
  o.want = expected (session, o.model, o.stream.frames, expect);
  o.report = QString {"\nGUI model:\n%1\nstream (markers before each):\n%2\nGUI model, placed:\n%3\nstderr:\n%4"}
    .arg (QString::fromUtf8 (o.model.out), texts (o.seen.tokens, true).join ('\n'), texts (o.want, true).join ('\n'),
          QString::fromUtf8 (o.stream.err.right (2000)));
  return o;
}

// QtTest cuts long messages, so a failed check prints the whole report line by line.
bool shown (bool ok, Outcome const& o)
{
  if (!ok)
    {
      for (auto const& line : o.report.split ('\n')) qInfo ().noquote () << line;
    }
  return ok;
}

QString difference (QStringList const& seen, QStringList const& want)
{
  int i = 0;
  while (i < seen.size () && i < want.size () && seen.at (i) == want.at (i)) ++i;
  return QString {"event %1: GUI model \"%2\", stream \"%3\""}.arg (i)
    .arg (want.value (i, "(none)"), seen.value (i, "(none)"));
}

bool agree (Outcome const& o, bool placed, QString& why)
{
  auto const seen = texts (o.seen.tokens, placed);
  auto const want = texts (o.want, placed);
  why = difference (seen, want);
  return shown (seen == want, o);
}

// What the row says the GUI model shows, before the stream is compared with it.
void checkModel (Outcome const& o, Expect const& expect)
{
  QVERIFY2 (o.model.ok, qPrintable (QString::fromUtf8 (o.model.out + o.model.err)));
  QStringList messages;
  bool marked = false;
  int trained = 0;
  for (auto const& line : o.model.lines)
    {
      if ("decode" == line.kind)
        {
          messages << modelMessage (line.text);
          marked |= line.text.mid (20, 4).contains ('^');
        }
      trained += "trained" == line.kind;
    }
  for (auto const& message : expect.decodes)
    {
      QVERIFY2 (shown (messages.contains (message), o), qPrintable ("the GUI model lacks " + message));
    }
  for (auto const& message : expect.lacks)
    {
      QVERIFY2 (shown (!messages.contains (message), o), qPrintable ("the GUI model decodes " + message));
    }
  if (expect.count >= 0) QVERIFY2 (shown (messages.size () == expect.count, o), "the GUI model's decodes");
  QVERIFY2 (shown (marked == expect.marks, o), "the GUI model's training marks");
  QVERIFY2 (shown (trained == expect.trained, o), "the GUI model's trainings");
  for (auto const& contents : o.model.trainings)
    {
      QVERIFY2 (shown (!contents.isEmpty (), o), "a training the GUI model completed wrote no file");
    }
}

void checkSession (Outcome const& o, Session const& session)
{
  QVERIFY2 (o.stream.started, o.stream.err.constData ());
  QVERIFY2 (shown (!o.stream.timedOut, o), "the stream did not end");
  QCOMPARE (o.stream.exitStatus, QProcess::NormalExit);
  QCOMPARE (o.stream.exitCode, 0);
  QVERIFY2 (o.seen.invalid.isEmpty (), qPrintable (o.seen.invalid));
  QCOMPARE (o.seen.markers, o.stream.frames.size ());
  // One pass line per period, a period cut short included; another mode's have their own.
  if (std::all_of (session.periods.begin (), session.periods.end (), [] (Period const& p) {return p.msk144;}))
    {
      QCOMPARE (int (o.stream.err.count ("wsjt-codec --stream: pass nzhsym=")), session.periods.size ());
    }
}

void checkTrainings (Outcome const& o, Expect const& expect)
{
  QCOMPARE (o.seen.trained.size (), o.model.trainings.size ());
  int files = 0;
  for (int i = 0; i < o.seen.trained.size (); ++i)
    {
      auto const& event = o.seen.trained.at (i);
      auto const terms = eventTerms (event);
      auto const gui = fileTerms (o.model.trainings.at (i));
      // The double-precision fit consumes single-precision FFT phases.
      QVERIFY2 (5 == terms.size () && same (terms, gui, 1e-6, 1e-6),
                qPrintable (QString {"event %1, GUI model %2"}.arg (show (terms), show (gui))));
      if (!expect.written.value (i, true)) continue;
      ++files;
      auto const call = event.value ("call").toString ();
      auto const name = QFileInfo {event.value ("file").toString ()}.fileName ();
      QCOMPARE (event.value ("file").toString (), o.stream.dataDir + '/' + name);
      QVERIFY2 (QRegularExpression {"^" + QRegularExpression::escape (call) + R"(_\d{6}_\d{6}\.pcoeff$)"}
                .match (name).hasMatch (), qPrintable (name));
      QVERIFY2 (o.stream.pcoeffs.contains (name), qPrintable (QStringList {o.stream.pcoeffs.keys ()}.join (' ')));
      auto const written = fileTerms (o.stream.pcoeffs.value (name));
      QVERIFY2 (same (terms, written), qPrintable (QString {"event %1, file %2"}.arg (show (terms), show (written))));
    }
  QVERIFY2 (shown (o.stream.pcoeffs.size () == files, o),
            qPrintable ("the stream's coefficient files: " + QStringList {o.stream.pcoeffs.keys ()}.join (' ')));
}

void compare (Session const& session, Expect const& expect, bool placed, Outcome* outcome = nullptr)
{
  for (auto const& period : session.periods) QVERIFY2 (!period.pcm.isEmpty (), "no recording");
  auto const o = play (session, expect);
  if (outcome) *outcome = o;
  checkModel (o, expect);
  if (QTest::currentTestFailed ()) return;
  checkSession (o, session);
  if (QTest::currentTestFailed ()) return;
  QStringList codes;
  for (auto const& error : o.seen.errors)
    {
      codes << QJsonDocument::fromJson (error.toUtf8 ()).object ().value ("code").toString ();
    }
  QVERIFY2 (codes == expect.errors, qPrintable (o.seen.errors.join ('\n')));
  QString why;
  QVERIFY2 (agree (o, false, why), qPrintable (why));
  if (placed) QVERIFY2 (agree (o, true, why), qPrintable ("placed, " + why));
  checkTrainings (o, expect);
  if (expect.noSessionDefaults)
    {
      // lib/jt9.f90's session defaults are no station's calls.
      QVERIFY2 (!o.stream.out.contains ("K1ABC") && !o.stream.out.contains ("W9XYZ"), o.stream.out.constData ());
    }
  qInfo ().noquote () << QString {"stream %1 ms, %2 events"}.arg (o.stream.elapsedMs).arg (o.seen.tokens.size ());
}

bool haveRecordings ()
{
#ifdef MSK144_SAMPLES
  return true;
#else
  return false;
#endif
}

bool haveFt8Recording ()
{
#ifdef FT8_SAMPLE_WAV
  return true;
#else
  return false;
#endif
}

bool haveSimulator ()
{
#ifdef MSK144_SIM_DIR
  return true;
#else
  return false;
#endif
}

QJsonArray terms (double c3)
{
  return QJsonArray {0, 0, c3, 0, 0};
}

void addRecordingRows (QString const& name, QByteArray const& pcm, QString const& utc, QString const& message)
{
  Expect plain;
  plain.decodes << message;
  QTest::newRow (qPrintable (name + " defaults")) << one ({}, pcm, utc) << plain;
  QTest::newRow (qPrintable (name + " live audio")) << one ({{"live_audio", true}}, pcm, utc) << plain;
  for (auto const depth : {1, 2, 5})
    {
      QTest::newRow (qPrintable (QString {"%1 depth %2"}.arg (name).arg (depth)))
        << one ({{"depth", depth}}, pcm, utc) << Expect {};
    }
  for (auto const ntol : {20, 100})
    {
      QTest::newRow (qPrintable (QString {"%1 ntol %2"}.arg (name).arg (ntol)))
        << one ({{"ntol", ntol}}, pcm, utc) << Expect {};
    }
  QJsonObject const calls {{"mycall", "WA4CQG"}, {"his_call", "K1JT"}};
  QTest::newRow (qPrintable (name + " calls")) << one (calls, pcm, utc) << plain;
  QTest::newRow (qPrintable (name + " short messages"))
    << one (with (calls, {{"short_messages", true}}), pcm, utc) << plain;
  QTest::newRow (qPrintable (name + " swl")) << one (with (calls, {{"swl", true}}), pcm, utc) << plain;
  QTest::newRow (qPrintable (name + " short messages and swl"))
    << one (with (calls, {{"short_messages", true}, {"swl", true}}), pcm, utc) << plain;
}
}

// wsjt-codec --stream decodes MSK144 as the GUI does (widgets/mainwindow.cpp
// fastSink): each row's events equal those of msk144_gui_model, which runs the
// GUI's hspec on the same audio with the same settings.
class TestStreamingMsk144 final : public QObject
{
  Q_OBJECT

private slots:
  void trainingComparisonAllowsSignalRoundoff ()
  {
    QList<double> const stream {-0.013671596863997061, 0.35713625852129233, 0.077501951536153357,
                                -0.31190463596108831, -0.16234377120035451};
    QList<double> const gui {-0.013671626719376179, 0.35713625302992369, 0.077501891874621781,
                             -0.31190457008526978, -0.1623436374074135};
    QVERIFY (same (stream, gui, 1e-6, 1e-6));
    QVERIFY (!same (stream, gui));
    QVERIFY (same (stream, stream));
    auto changed = stream;
    changed[2] += 1e-3;
    QVERIFY (!same (changed, gui, 1e-6, 1e-6));
    for (auto invalid : {std::numeric_limits<double>::quiet_NaN (),
                         std::numeric_limits<double>::infinity ()})
      {
        changed = stream;
        changed[2] = invalid;
        QVERIFY (!same (changed, stream, 1e-6, 1e-6));
        QVERIFY (!same (stream, changed, 1e-6, 1e-6));
        QVERIFY (!same (changed, changed));
      }
  }

  void recording120500MatchesTheGui_data ()
  {
    QTest::addColumn<Session> ("session");
    QTest::addColumn<Expect> ("expect");
    if (!haveRecordings ()) return;
    auto const k1jt = recording ("181211_120500");
    addRecordingRows ("120500", k1jt, "12:05:00", "K1JT WA4CQG EM72");
    QTest::newRow ("120500 phase_eq term 3 = 5") << one ({{"phase_eq", terms (5)}}, k1jt, "12:05:00")
                                                << expectLacks ({"K1JT WA4CQG EM72"});
    Expect marked;
    marked.marks = true;
    QTest::newRow ("120500 train") << one ({{"his_call", "WA4CQG"}, {"train", true}}, k1jt, "12:05:00") << marked;
    // A recording with silence edited in: hspec skips a window with an all-zero half.
    QTest::newRow ("120500 leading 103936 samples zero") << one ({}, zeroed (k1jt, 0, 103936), "12:05:00")
                                                        << expectDecodes ({"K1JT WA4CQG EM72"});
  }

  void recording120500MatchesTheGui ()
  {
    if (!haveRecordings ()) QSKIP ("the repository's MSK144 samples are not in this tree");
    QFETCH (Session, session);
    QFETCH (Expect, expect);
    compare (session, expect, false);
  }

  void recording120800MatchesTheGui_data ()
  {
    QTest::addColumn<Session> ("session");
    QTest::addColumn<Expect> ("expect");
    if (!haveRecordings ()) return;
    auto const w4imd = recording ("181211_120800");
    addRecordingRows ("120800", w4imd, "12:08:00", "CQ W4IMD EM84");
    QTest::newRow ("120800 phase_eq term 3 = 5") << one ({{"phase_eq", terms (5)}}, w4imd, "12:08:00") << Expect {};
    Expect marked;
    marked.marks = true;
    QTest::newRow ("120800 train") << one ({{"train", true}}, w4imd, "12:08:00") << marked;

    // Recordings with silence edited in: hspec skips a window with an all-zero half.
    QTest::newRow ("120800 leading 53760 samples zero") << one ({}, zeroed (w4imd, 0, 53760), "12:08:00")
                                                       << expectDecodes ({"CQ W4IMD EM84"});
    Expect none;
    none.count = 0;
    QTest::newRow ("120800 silent from sample 53760") << one ({}, zeroed (w4imd, 53760, 180000), "12:08:00") << none;
    QTest::newRow ("120800 ends at sample 53760") << one ({}, samples (w4imd, 53760), "12:08:00") << none;
    QTest::newRow ("120800 ends at sample 60000") << one ({}, samples (w4imd, 60000), "12:08:00")
                                                 << expectDecodes ({"CQ W4IMD EM84"});

    QTest::newRow ("120800 TR 30") << session ({{"trperiod", 30}}, {period ("12:08:00", w4imd + w4imd)})
                                   << expectDecodes ({"CQ KD9VV EN71"});
    QTest::newRow ("120800 TR 5") << session ({{"trperiod", 5}},
                                              periodsOf (w4imd, 5 * SampleRate, {"12:08:00", "12:08:05", "12:08:10"}))
                                  << expectDecodes ({"CQ W4IMD EM84"});
    auto change = session ({{"trperiod", 5}, {"mycall", "WA4CQG"}},
                           periodsOf (w4imd, 5 * SampleRate, {"12:08:00", "12:08:05", "12:08:10"}));
    change.periods[1].keys = {{"his_call", "K1JT"}};
    QTest::newRow ("120800 TR 5, his_call in period 2") << change << expectDecodes ({"CQ W4IMD EM84"});
    // hspec decodes nothing past 30 s of a period.
    QTest::newRow ("120800 TR 45") << session ({{"trperiod", 45}},
                                               {period ("12:08:00", w4imd + w4imd + w4imd)})
                                   << expectDecodes ({"CQ KD9VV EN71"});
  }

  void recording120800MatchesTheGui ()
  {
    if (!haveRecordings ()) QSKIP ("the repository's MSK144 samples are not in this tree");
    QFETCH (Session, session);
    QFETCH (Expect, expect);
    compare (session, expect, false);
  }

  void samplesDecodeAtTheGuiBlocks_data ()
  {
    QTest::addColumn<Session> ("session");
    QTest::addColumn<Expect> ("expect");
    if (!haveRecordings ()) return;
    auto const k1jt = recording ("181211_120500");
    auto const w4imd = recording ("181211_120800");
    QTest::newRow ("120500") << one ({}, k1jt, "12:05:00") << expectDecodes ({"K1JT WA4CQG EM72"});
    QTest::newRow ("120800") << one ({}, w4imd, "12:08:00") << expectDecodes ({"CQ W4IMD EM84", "CQ KD9VV EN71"});
    auto paced = one ({}, w4imd, "12:08:00");
    paced.aligned = false;
    QTest::newRow ("120800 in 0.1 s frames") << paced << expectDecodes ({"CQ W4IMD EM84", "CQ KD9VV EN71"});
    auto large = paced;
    large.frameSamples = 4096;
    QTest::newRow ("120800 in 4096-sample frames") << large << expectDecodes ({"CQ W4IMD EM84", "CQ KD9VV EN71"});
    QTest::newRow ("120800 live audio") << one ({{"live_audio", true}}, w4imd, "12:08:00")
                                        << expectDecodes ({"CQ W4IMD EM84", "CQ KD9VV EN71"});
    QTest::newRow ("120800 leading 53760 samples zero") << one ({}, zeroed (w4imd, 0, 53760), "12:08:00")
                                                       << expectDecodes ({"CQ W4IMD EM84"});
    QTest::newRow ("120800 TR 5") << session ({{"trperiod", 5}},
                                              periodsOf (w4imd, 5 * SampleRate, {"12:08:00", "12:08:05", "12:08:10"}))
                                  << expectDecodes ({"CQ W4IMD EM84"});
    Expect none;
    none.count = 0;
    QTest::newRow ("120800 ends at sample 53760") << one ({}, samples (w4imd, 53760), "12:08:00") << none;
    QTest::newRow ("120800 ends at sample 60000") << one ({}, samples (w4imd, 60000), "12:08:00")
                                                 << expectDecodes ({"CQ W4IMD EM84"});
    auto halted = one ({}, samples (w4imd, 53760), "12:08:00");
    halted.halt = true;
    QTest::newRow ("120800 halted at sample 53760") << halted << none;
    halted.periods[0].pcm = samples (w4imd, 60000);
    QTest::newRow ("120800 halted at sample 60000") << halted << expectDecodes ({"CQ W4IMD EM84"});
  }

  // Each decode is written before the stream reads past the block the GUI
  // decodes it at: before the marker after that block's last sample, and
  // after the marker before it.
  void samplesDecodeAtTheGuiBlocks ()
  {
    if (!haveRecordings ()) QSKIP ("the repository's MSK144 samples are not in this tree");
    QFETCH (Session, session);
    QFETCH (Expect, expect);
    compare (session, expect, true);
  }

  void simulatedSignalsMatchTheGui_data ()
  {
    QTest::addColumn<Session> ("session");
    QTest::addColumn<Expect> ("expect");
    if (!haveSimulator ()) return;
    auto const shortMessage = simulated ("short");
    QString const sh {"<K1ABC W9XYZ> R+06"};
    QJsonObject const calls {{"mycall", "K1ABC"}, {"his_call", "W9XYZ"}};
    Expect decoded;
    decoded.decodes << sh;
    Expect lacking;
    lacking.lacks << sh;
    QTest::newRow ("short messages") << one (with (calls, {{"short_messages", true}}), shortMessage, "00:00:00")
                                     << decoded;
    QTest::newRow ("short messages off") << one (calls, shortMessage, "00:00:00") << lacking;
    QTest::newRow ("short messages, calls swapped")
      << one ({{"mycall", "W9XYZ"}, {"his_call", "K1ABC"}, {"short_messages", true}}, shortMessage, "00:00:00")
      << lacking;
    QTest::newRow ("short messages and swl")
      << one (with (calls, {{"short_messages", true}, {"swl", true}}), shortMessage, "00:00:00") << decoded;
    // SWL shows a short message whose hash is not the station's after a second sighting.
    QJsonObject const other {{"mycall", "W1AW"}, {"his_call", "W9XYZ"}};
    QTest::newRow ("swl") << one (with (other, {{"swl", true}}), shortMessage, "00:00:00")
                          << expectDecodes ({"<4002> R+06"});
    QTest::newRow ("swl off") << one (other, shortMessage, "00:00:00") << expectLacks ({"<4002> R+06"});
    QTest::newRow ("swl, short messages for other calls")
      << one (with (other, {{"swl", true}, {"short_messages", true}}), shortMessage, "00:00:00")
      << expectDecodes ({"<4002> R+06"});

    // A weak long ping, which averaging decodes; the GUI's depth is depth & 3.
    auto const weak = simulated ("depth");
    for (auto const depth : {1, 2, 3, 5})
      {
        QTest::newRow (qPrintable (QString {"depth %1"}.arg (depth)))
          << one (with (calls, {{"depth", depth}}), weak, "00:00:00") << Expect {};
      }

    // A 37-character message: the hash table learns the DX call in period 1.
    QJsonObject const portable {{"mycall", "PA3XYZ/P"}, {"his_call", "G4ABCD/P"}};
    QString const full {"<PA3XYZ/P> <G4ABCD/P> R 590003 IO91NP"};
    QList<Period> const learned {period ("00:00:00", simulated ("hash")), period ("00:00:15", simulated ("long"))};
    QTest::newRow ("37-character message") << session (portable, learned) << expectDecodes ({full});
    auto unknown = expectDecodes ({"<PA3XYZ/P> <...> R 590003 IO91NP"});
    unknown.lacks << full;
    QTest::newRow ("hashed DX call unknown") << one (portable, simulated ("long"), "00:00:00") << unknown;
  }

  void simulatedSignalsMatchTheGui ()
  {
    if (!haveSimulator ()) QSKIP ("msk144sim is built only with WSJT_BUILD_UTILS");
    QFETCH (Session, session);
    QFETCH (Expect, expect);
    compare (session, expect, true);
  }

  void stationIdentityComesFromConfigures_data ()
  {
    QTest::addColumn<Session> ("session");
    QTest::addColumn<Expect> ("expect");
    if (!haveSimulator ()) return;
    auto const shortMessage = simulated ("short");
    QTest::newRow ("calls configured")
      << one ({{"mycall", "K1ABC"}, {"his_call", "W9XYZ"}, {"short_messages", true}}, shortMessage, "00:00:00")
      << expectDecodes ({"<K1ABC W9XYZ> R+06"});
    Expect blank;
    blank.count = 0;
    blank.noSessionDefaults = true;
    QTest::newRow ("no calls") << one ({{"short_messages", true}}, shortMessage, "00:00:00") << blank;
    Expect swl;
    swl.decodes << "<4002> R+06";
    swl.noSessionDefaults = true;
    QTest::newRow ("no calls, swl") << one ({{"short_messages", true}, {"swl", true}}, shortMessage, "00:00:00")
                                    << swl;
    QTest::newRow ("mycall only") << one ({{"mycall", "K1ABC"}, {"short_messages", true}}, shortMessage, "00:00:00")
                                  << expectLacks ({"<K1ABC W9XYZ> R+06"});
    // A declined configure sets none of its keys.
    auto declined = one ({}, shortMessage, "00:00:00");
    declined.periods[0].control = frame (0x02u, R"({"t":"configure","mycall":"K1ABC","his_call":"W9XYZ",)"
                                                R"("short_messages":true,"phase_eq":[0,0,0,0]})");
    auto refused = blank;
    refused.errors << "configure_range_error";
    QTest::newRow ("calls in a declined configure") << declined << refused;
    auto accepted = declined;
    accepted.periods[0].control = frame (0x02u, R"({"t":"configure","mycall":"K1ABC","his_call":"W9XYZ",)"
                                                R"("short_messages":true,"phase_eq":[0,0,0,0,0]})");
    accepted.periods[0].applied = {{"mycall", "K1ABC"}, {"his_call", "W9XYZ"}, {"short_messages", true},
                                   {"phase_eq", terms (0)}};
    QTest::newRow ("calls in an accepted configure") << accepted << expectDecodes ({"<K1ABC W9XYZ> R+06"});
  }

  void stationIdentityComesFromConfigures ()
  {
    if (!haveSimulator ()) QSKIP ("msk144sim is built only with WSJT_BUILD_UTILS");
    QFETCH (Session, session);
    QFETCH (Expect, expect);
    compare (session, expect, true);
  }

  void trainingMatchesTheGui_data ()
  {
    QTest::addColumn<Session> ("session");
    QTest::addColumn<Expect> ("expect");
    if (!haveSimulator ()) return;
    auto const ping = simulated ("train");
    QJsonObject const calls {{"mycall", "K1ABC"}, {"his_call", "W9XYZ"}};
    QJsonObject const train {{"train", true}};
    Expect trains;
    trains.marks = true;
    trains.trained = 1;
    QTest::newRow ("one period") << one (with (calls, train), ping, "00:00:00") << trains;
    // Period 1's audio stops before training completes; it completes in period 2.
    QList<Period> const two {period ("00:00:00", zeroed (ping, 6 * BlockSamples, 180000)),
                             period ("00:00:15", ping)};
    QTest::newRow ("two periods") << session (with (calls, train), two) << trains;
    QTest::newRow ("off") << session (calls, two) << Expect {};
    // msk144signalquality goes on training after the toggle is cleared.
    auto cleared = session (with (calls, train), two);
    cleared.periods[1].keys = {{"train", false}};
    Expect continues;
    continues.marks = true;
    continues.trained = 1;
    QTest::newRow ("cleared while training") << cleared << continues;

    // A file the stream cannot create leaves the training complete, reported without a file.
    auto const rover = simulated ("rover");
    auto slash = trains;
    slash.written << false;
    QTest::newRow ("his_call with a slash") << one ({{"mycall", "K1ABC"}, {"his_call", "W9XYZ/R"}, {"train", true}},
                                                     rover, "00:00:00") << slash;
    QTest::newRow ("his_call without the slash") << one (with (calls, train), rover, "00:00:00") << trains;
    // Re-armed after its first training, the second finds its file's name taken, as one
    // completing within the same second does.
    auto again = session (with (calls, train), {period ("00:00:00", ping), period ("00:00:15", ping)});
    again.answerAfter = 0;
    again.answer = [] (QList<QJsonObject> const& events, QString const& directory) {
      // The names follow the codec's clock: the window starts at the first file's stamp.
      QRegularExpression const named {R"(^(.+)_(\d{6}_\d{6})\.pcoeff$)"};
      QRegularExpressionMatch match;
      for (auto const& event : events)
        {
          if ("msk144_trained" == event.value ("t").toString () && !match.hasMatch ())
            {
              match = named.match (QFileInfo {event.value ("file").toString ()}.fileName ());
            }
        }
      auto first = QDateTime::fromString (match.captured (2), "yyMMdd_HHmmss").addYears (100);
      first.setTimeSpec (Qt::UTC);
      for (int second = 0; first.isValid () && second <= 600; ++second)
        {
          auto const stamp = first.addSecs (second).toString ("_yyMMdd_HHmmss");
          QFile taken {QDir {directory}.filePath (match.captured (1) + stamp + ".pcoeff")};
          if (!taken.exists ()) taken.open (QIODevice::WriteOnly);
        }
      return QJsonObject {{"train", true}};
    };
    auto twice = trains;
    twice.trained = 2;
    twice.written << true << false;
    QTest::newRow ("re-armed, name taken") << again << twice;
  }

  void trainingMatchesTheGui ()
  {
    if (!haveSimulator ()) QSKIP ("msk144sim is built only with WSJT_BUILD_UTILS");
    QFETCH (Session, session);
    QFETCH (Expect, expect);
    compare (session, expect, true);
  }

  // The coefficients msk144_trained reports, sent back as phase_eq, equalize
  // the next period as the GUI's applied coefficients do.
  void trainedCoefficientsGoBack ()
  {
    if (!haveSimulator ()) QSKIP ("msk144sim is built only with WSJT_BUILD_UTILS");
    if (!haveRecordings ()) QSKIP ("the repository's MSK144 samples are not in this tree");
    auto const ping = simulated ("train");
    auto s = session ({{"mycall", "K1ABC"}, {"his_call", "W9XYZ"}, {"train", true}},
                      {period ("00:00:00", zeroed (ping, 6 * BlockSamples, 180000)),
                       period ("00:00:15", ping),
                       period ("00:00:30", recording ("181211_120800"))});
    s.answerAfter = 1;
    s.answer = [] (QList<QJsonObject> const& events, QString const&) {
      for (auto const& event : events)
        {
          if ("msk144_trained" == event.value ("t").toString ())
            {
              return QJsonObject {{"phase_eq", event.value ("phase_eq")}};
            }
        }
      return QJsonObject {};
    };
    Expect expect;
    expect.marks = true;
    expect.trained = 1;
    Outcome o;
    compare (s, expect, true, &o);
    if (QTest::currentTestFailed ()) return;
    QVERIFY2 (shown (o.session.periods.at (2).keys.contains ("phase_eq"), o), "no coefficients to send back");
    // Without them the GUI decodes period 3 otherwise.
    auto bare = o.session;
    bare.periods[2].keys = {};
    auto const without = runModel (bare);
    QVERIFY2 (shown (without.ok, o), without.out.constData ());
    auto const decodes = [] (ModelRun const& model) {
      QStringList result;
      for (auto const& line : model.lines)
        if (line.period == 2 && line.kind == "decode") result << modelDecode (line.text);
      return result;
    };
    QVERIFY2 (shown (decodes (without) != decodes (o.model), o), without.out.constData ());
  }

  // MSK144's decoder state survives a period of another mode: SWL shows a short message on its
  // second sighting, the first in period 1 and the second after an FT8 period.
  void modeChangeKeepsTheDecoderState ()
  {
    if (!haveSimulator ()) QSKIP ("msk144sim is built only with WSJT_BUILD_UTILS");
    if (!haveFt8Recording ()) QSKIP ("the repository's FT8 sample is not in this tree");
    auto const shortMessage = simulated ("short");
    QJsonObject const swl {{"swl", true}};
    auto s = session (with ({{"mycall", "W1AW"}, {"his_call", "W9XYZ"}}, swl),
                      {period ("00:00:00", zeroed (shortMessage, 4 * BlockSamples, 180000)),
                       period ("13:34:30", ft8Recording ()), period ("00:00:30", shortMessage)});
    s.periods[1].keys = {{"mode", "FT8"}, {"trperiod", 15}};
    s.periods[1].msk144 = false;
    s.periods[2].keys = with (Defaults, swl);
    // The control: a his_call change resets mskrtd, which forgets the first sighting.
    auto reset = s;
    reset.periods[2].keys.insert ("his_call", "K9AN");
    auto const expect = expectDecodes ({"<4002> R+06"});
    Outcome kept;
    compare (s, expect, false, &kept);
    if (QTest::currentTestFailed ()) return;
    Outcome forgot;
    compare (reset, expect, false, &forgot);
    if (QTest::currentTestFailed ()) return;
    QVERIFY2 (kept.model.out != forgot.model.out, kept.model.out.constData ());
  }

  void phaseEqErrorsKeepTheValueInForce_data ()
  {
    QTest::addColumn<QByteArray> ("value");
    QTest::addColumn<QString> ("code");        // empty: accepted
    QTest::addColumn<QString> ("got");         // a type error's category, when pinned
    QTest::addColumn<QByteArray> ("members");  // the refused frame's other keys
    QTest::addColumn<double> ("inForce");      // term 3 of the coefficients in force
    QString const parse {"configure_parse_error"};
    QString const type {"configure_type_error"};
    QString const range {"configure_range_error"};
    auto const row = [] (char const* name, QByteArray const& value, QString const& code, QString const& got = {},
                         QByteArray const& members = {}, double inForce = 5) {
      QTest::newRow (name) << value << code << got << members << inForce;
    };
    row ("five numbers", "[0,0,0,0,0]", QString {});
    row ("five numbers, exponents", "[0e0,-0.0,0E+1,0.0e-1,0]", QString {});
    row ("four", "[0,0,0,0]", range);
    row ("six", "[0,0,0,0,0,0]", range);
    row ("empty", "[]", range);
    row ("overflow", "[0,0,1e999,0,0]", range);
    row ("a string", R"([0,0,"0",0,0])", type, "string");
    row ("true", "[0,0,true,0,0]", type, "bool");
    row ("null", "[0,0,null,0,0]", type, "null");
    // The parser types a value by its first character.
    row ("nan", "[0,0,nan,0,0]", type, "null");
    row ("nested", "[0,0,[0],0,0]", type);
    row ("an object", R"([0,0,{"a":0},0,0])", type);
    row ("a number", "0", type, "number");
    row ("a word", "abc", type, "number");
    row ("a boolean", "true", type, "bool");
    row ("a bare null", "null", type, "null");
    row ("a whole object", R"({"a":0})", type);
    row ("a string of numbers", R"("0,0,0,0,0")", type, "string");
    row ("NaN", "[0,0,NaN,0,0]", parse);
    row ("Infinity", "[0,0,Infinity,0,0]", parse);
    row ("a slash", "[0,0,/,0,0]", parse);
    row ("two numbers in one", "[0,0,0 0,0]", parse);
    row ("a trailing point", "[0,0,0.,0,0]", parse);
    row ("a leading point", "[0,0,.5,0,0]", parse);
    row ("an exponent without digits", "[0,0,5e,0,0]", parse);
    row ("a plus sign", "[0,0,+0,0,0]", parse);
    row ("a leading zero", "[0,0,00,0,0]", parse);
    row ("a missing element", "[0,0,,0,0]", parse);
    row ("a trailing comma", "[0,0,0,0,0,]", parse);
    row ("unclosed", "[0,0,0,0,0", parse);
    // The stream stays in MSK144, and the error names the mode the frame would have left it in.
    row ("four, with a mode change", "[0,0,0,0]", range, {}, R"("mode":"FT8",)", 0);
  }

  // A phase_eq that is not five finite JSON numbers is refused, and the
  // coefficients in force stay: term 3 = 5 loses K1JT on 181211_120500.
  void phaseEqErrorsKeepTheValueInForce ()
  {
    if (!haveRecordings ()) QSKIP ("the repository's MSK144 samples are not in this tree");
    QFETCH (QByteArray, value);
    QFETCH (QString, code);
    QFETCH (QString, got);
    QFETCH (QByteArray, members);
    QFETCH (double, inForce);
    auto s = one ({{"trperiod", 10}, {"phase_eq", terms (inForce)}},
                  samples (recording ("181211_120500"), 10 * SampleRate), "12:05:00");
    s.periods[0].control = frame (0x02u, R"({"t":"configure",)" + members + R"("phase_eq":)" + value + "}");
    if (code.isEmpty ()) s.periods[0].applied = {{"phase_eq", terms (0)}};
    Expect expect;
    (code.isEmpty () || 0 == inForce ? expect.decodes : expect.lacks) << "K1JT WA4CQG EM72";
    auto const o = play (s, expect);
    checkModel (o, expect);
    if (QTest::currentTestFailed ()) return;
    checkSession (o, s);
    if (QTest::currentTestFailed ()) return;
    QString why;
    QVERIFY2 (agree (o, false, why), qPrintable (why));
    QVERIFY2 (agree (o, true, why), qPrintable ("placed, " + why));
    if (code.isEmpty ())
      {
        QVERIFY2 (o.seen.errors.isEmpty (), qPrintable (o.seen.errors.join ('\n')));
        return;
      }
    QVERIFY2 (shown (1 == o.seen.errors.size (), o), qPrintable (o.seen.errors.join ('\n')));
    auto const error = QJsonDocument::fromJson (o.seen.errors.first ().toUtf8 ()).object ();
    QCOMPARE (error.value ("code").toString (), code);
    if (code != "configure_parse_error") QCOMPARE (error.value ("key").toString (), QString {"phase_eq"});
    if (code == "configure_range_error")
      {
        QCOMPARE (error.value ("mode").toString (), QString {members.contains ("FT8") ? "FT8" : "MSK144"});
      }
    if (code == "configure_type_error")
      {
        QCOMPARE (error.value ("expected").toString (), QString {"array of numbers"});
        if (!got.isEmpty ()) QCOMPARE (error.value ("got").toString (), got);
      }
  }
};

QTEST_GUILESS_MAIN (TestStreamingMsk144)

#include "test_streaming_msk144.moc"
