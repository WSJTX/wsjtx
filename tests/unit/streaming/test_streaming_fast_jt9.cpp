// SPDX-License-Identifier: GPL-3.0-or-later
// Fast JT9 in the codec against the GUI's decode: fast_jt9_gui_model calls
// fast_decode_ as MainWindow::decode() does, on the audio the codec decodes.
// The audio is the codec's own fast JT9 render with noise added, and a
// second station outside rxfreq +- ntol that neither may decode; no fast JT9
// recording exists.
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace
{
constexpr int SampleRate = 12000;
// MainWindow::fastSink's block in fast JT9 and the codec's.
constexpr int BlockSamples = 3456;
// A paced client's frame, 0.1 s of audio.
constexpr int FrameSamples = 1200;
constexpr char Message[] = "CQ K1ABC FN42";
// Off the codec's starting rxfreq, 1500 Hz, with ntol 100.
constexpr int AudioFrequency = 1200;
// A station outside rxfreq +- ntol.
constexpr char OutsideMessage[] = "W1AW K1ABC FN31";
constexpr int OutsideFrequency = 1700;

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

// What a client writes, and when: at ms after the codec started (0: at once).
struct Chunk
{
  QByteArray bytes;
  qint64 atMs {0};
  bool trigger {false};   // record when it was written
};

struct Run
{
  bool started {false};
  bool timedOut {false};
  int exitCode {-1};
  QByteArray out;
  QByteArray err;
  QList<QJsonObject> events;
  QList<qint64> arrivals;   // each event's, ms after the codec started
  qint64 triggerMs {-1};

  bool clean () const {return started && !timedOut && 0 == exitCode;}
  QString report () const
  {
    return QString {"started %1, timed out %2, exit %3\nstdout:\n%4\nstderr:\n%5"}
      .arg (started).arg (timedOut).arg (exitCode)
      .arg (QString::fromUtf8 (out.right (4000)), QString::fromUtf8 (err.right (4000)));
  }
};

Run runCodec (QList<Chunk> const& chunks, QStringList const& environment = {})
{
  Run r;
  QTemporaryDir directory;
  if (!directory.isValid ()) return r;
  QProcess process;
  auto env = QProcessEnvironment::systemEnvironment ();
  env.insert ("OMP_STACKSIZE", "10M");
  for (auto const& each : environment) env.insert (each.section ('=', 0, 0), each.section ('=', 1));
  process.setProcessEnvironment (env);
  process.setWorkingDirectory (directory.path ());
  QElapsedTimer clock;
  clock.start ();
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), {"-a", directory.path (), "-t", directory.path ()});
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
        if (line.trimmed ().isEmpty ()) continue;
        r.events << QJsonDocument::fromJson (line).object ();
        r.arrivals << clock.elapsed ();
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
  for (auto const& chunk : chunks)
    {
      pumpUntil ([&] {return clock.elapsed () >= chunk.atMs;});
      process.write (chunk.bytes);
      pumpUntil ([&] {return 0 == process.bytesToWrite ();});
      if (chunk.trigger) r.triggerMs = clock.elapsed ();
    }
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

// The codec's render of text in fast JT9 (slow JT9 at trperiod 0) at freq.
QByteArray render (QString const& submode, int trperiod, QString const& text = Message,
                   int freq = AudioFrequency)
{
  QJsonObject request {{"id", 1}, {"mode", "JT9"}, {"text", text}, {"submode", submode}, {"freq", freq},
                       {"rate", SampleRate}};
  if (trperiod) request = with (request, {{"trperiod", trperiod}, {"fast", true}});
  auto const run = runCodec ({{sessionHeader () + control (request, "render")}});
  QByteArray pcm;
  for (auto const& event : ofType (run.events, "pcm"))
    pcm += QByteArray::fromBase64 (event.value ("data").toString ().toLatin1 ());
  return pcm;
}

// periods periods of trperiod seconds, the render from each one's start
// (WSJT-X's fast JT9 transmits from the period's start) at a tenth of full
// scale, with OutsideMessage's at OutsideFrequency, in Gaussian noise of a
// fixed seed. With more texts, each period's equal slots carry them in turn,
// a render each cut to its slot.
QByteArray fastJt9Audio (QString const& submode, int trperiod, int periods = 1,
                         QStringList const& texts = {Message})
{
  auto const periodSamples = trperiod * SampleRate;
  QByteArray signal (2 * periodSamples, '\0');
  auto const slot = periodSamples / int (texts.size ());
  auto const add = [&] (QByteArray const& one, int at, int count) {
    for (int k = 0; k < count; ++k)
      {
        auto const sum = qFromLittleEndian<qint16> (signal.constData () + 2 * (at + k))
          + qFromLittleEndian<qint16> (one.constData () + 2 * k) / 2;
        qToLittleEndian<qint16> (qint16 (std::max (-32767, std::min (32767, sum))), signal.data () + 2 * (at + k));
      }
  };
  for (int t = 0; t < texts.size (); ++t)
    {
      auto const one = render (submode, trperiod, texts.at (t));
      if (one.isEmpty ()) return {};
      add (one, t * slot, std::min (int (one.size () / 2), texts.size () > 1 ? slot : periodSamples));
    }
  auto const outside = render (submode, trperiod, OutsideMessage, OutsideFrequency);
  if (outside.isEmpty ()) return {};
  add (outside, 0, std::min (int (outside.size () / 2), periodSamples));
  QByteArray pcm (2 * periods * periodSamples, '\0');
  std::mt19937 random {1729u + unsigned (trperiod) + unsigned (submode.at (0).unicode ())};
  std::normal_distribution<double> noise {0.0, 1500.0};
  for (int k = 0; k < periods * periodSamples; ++k)
    {
      auto const within = k % periodSamples;
      double value = noise (random);
      if (2 * within + 1 < signal.size ())
        value += 0.2 * qFromLittleEndian<qint16> (signal.constData () + 2 * within);
      qToLittleEndian<qint16> (qint16 (std::max (-32767.0, std::min (32767.0, std::round (value)))),
                               pcm.data () + 2 * k);
    }
  return pcm;
}

struct ModelDecode
{
  QStringList lines;
  double seconds {0};
};

struct ModelRun
{
  bool ok {false};
  QString failure;
  QList<ModelDecode> decodes;
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
  process.start (QString::fromUtf8 (FAST_JT9_GUI_MODEL), {scriptFile.fileName ()});
  if (!process.waitForFinished (waitMs ()) || process.exitCode () != 0)
    {
      r.failure = "model failed: " + QString::fromUtf8 (process.readAllStandardError ());
      return r;
    }
  ModelDecode current;
  for (auto const& line : QString::fromUtf8 (process.readAllStandardOutput ()).split ('\n'))
    {
      if (line.startsWith ("line ")) current.lines << line.mid (5);
      else if (line.startsWith ("seconds ")) current.seconds = line.mid (8).toDouble ();
      else if ("end" == line)
        {
          r.decodes << current;
          current = {};
        }
    }
  r.ok = true;
  return r;
}

// The codec's decodes and decode_finished lines in order, cut at each
// decode_finished: one group a decode.
QList<QList<QJsonObject>> decodeGroups (QList<QJsonObject> const& events)
{
  QList<QList<QJsonObject>> groups;
  QList<QJsonObject> current;
  for (auto const& event : events)
    {
      auto const type = event.value ("t").toString ();
      if ("decode" == type) current << event;
      else if ("decode_finished" == type || "pass_skipped" == type)
        {
          current << event;
          if ("decode_finished" == type)
            {
              groups << current;
              current.clear ();
            }
        }
    }
  return groups;
}

// A group's decodes against the model's lines, (i6.6,i4,f5.1,i5,1x,'@ ',1x,a22)
// (lib/fast9.f90): field for field, in order. Empty when they agree.
QString compareDecodes (QList<QJsonObject> const& group, QStringList const& lines, int trperiod, int pass)
{
  QList<QJsonObject> decodes;
  for (auto const& event : group)
    if ("decode" == event.value ("t").toString ()) decodes << event;
  if (decodes.size () != lines.size ())
    return QString {"%1 decodes against the GUI's %2: %3"}.arg (decodes.size ()).arg (lines.size ())
      .arg (lines.join (" | "));
  for (int i = 0; i < lines.size (); ++i)
    {
      auto const& line = lines.at (i);
      auto const& decode = decodes.at (i);
      auto const differs = [&] (QString const& field) {
        return QString {"decode %1 %2: %3 against the GUI's %4"}.arg (i).arg (field)
          .arg (QString::fromUtf8 (QJsonDocument {decode}.toJson (QJsonDocument::Compact)), line);
      };
      if (line.mid (20, 4) != " @  ") return differs ("marker");
      if (decode.value ("mode").toString () != "JT9") return differs ("mode");
      if (decode.value ("time").toString () != line.left (6)) return differs ("time");
      if (decode.value ("snr").toInt () != line.mid (6, 4).trimmed ().toInt ()) return differs ("snr");
      if (std::abs (decode.value ("dt").toDouble () - double (line.mid (10, 5).trimmed ().toFloat ())) > 1e-6)
        return differs ("dt");
      if (decode.value ("freq").toInt () != line.mid (15, 5).trimmed ().toInt ()) return differs ("freq");
      if (decode.value ("message").toString () != line.mid (24).trimmed ()) return differs ("message");
      if (decode.value ("period").toInt () != trperiod) return differs ("period");
      if (decode.value ("pass").toInt () != pass) return differs ("pass");
      if (decode.value ("message").toString () == OutsideMessage) return differs ("station outside ntol");
    }
  return {};
}

// The codec's stderr line for its decode on the period's first k samples.
bool passLine (Run const& run, int k)
{
  return run.err.contains (QString {"wsjt-codec: pass nzhsym=%1 samples=%2 "}.arg (k / BlockSamples).arg (k).toUtf8 ());
}

QJsonObject settings (QString const& submode, int trperiod, bool live)
{
  return {{"mode", "JT9"}, {"fast", true}, {"submode", submode}, {"trperiod", trperiod},
          {"rxfreq", AudioFrequency}, {"ntol", 100}, {"live_audio", live}};
}

QStringList modelSettings (QString const& submode, int trperiod, int nutc, int minsync = 0)
{
  return {QString {"nutc %1"}.arg (nutc), QString {"ntr %1"}.arg (trperiod),
          QString {"submode %1"}.arg (submode.at (0).unicode () - 'A'), QString {"minsync %1"}.arg (minsync),
          QString {"rxfreq %1"}.arg (AudioFrequency), "ntol 100"};
}

// The GUI's decode points as block ends (MainWindow::fastSink): live, the
// first with less than 0.35 s of the period left, then each later whole
// block; from a file, the first at or past the period's samples less 3584.
int liveTry (int trperiod)
{
  auto const point = int (std::lround ((trperiod - 0.35) * SampleRate));
  return (point + BlockSamples - 1) / BlockSamples * BlockSamples;
}

int lastLiveTry (int trperiod)
{
  return trperiod * SampleRate / BlockSamples * BlockSamples;
}

int fileTry (int trperiod)
{
  auto const point = trperiod * SampleRate - 3584;
  return (point + BlockSamples - 1) / BlockSamples * BlockSamples;
}

// One period of trperiod seconds (60: slow JT9 A, else fast JT9 E) with
// Message at 700 Hz and OutsideMessage at 1200 Hz, each at a fifth of full
// scale from the period's start (slow JT9: 1 s in), in Gaussian noise of 1500.
QByteArray twoStations (int trperiod)
{
  auto const slow = 60 == trperiod;
  auto const one = render (slow ? "A" : "E", slow ? 0 : trperiod, Message, 700);
  auto const two = render (slow ? "A" : "E", slow ? 0 : trperiod, OutsideMessage, 1200);
  if (one.isEmpty () || two.isEmpty ()) return {};
  auto const periodSamples = trperiod * SampleRate;
  auto const at = slow ? SampleRate : 0;
  QByteArray pcm (2 * periodSamples, '\0');
  std::mt19937 random {7u + unsigned (trperiod)};
  std::normal_distribution<double> noise {0.0, 1500.0};
  for (int k = 0; k < periodSamples; ++k)
    {
      double value = noise (random);
      auto const i = k - at;
      if (i >= 0 && 2 * i + 1 < one.size ()) value += 0.2 * qFromLittleEndian<qint16> (one.constData () + 2 * i);
      if (i >= 0 && 2 * i + 1 < two.size ()) value += 0.2 * qFromLittleEndian<qint16> (two.constData () + 2 * i);
      qToLittleEndian<qint16> (qint16 (std::max (-32767.0, std::min (32767.0, std::round (value)))), pcm.data () + 2 * k);
    }
  return pcm;
}

// The decode lines of a session: control frames (JSON lines), then pcm, then halt.
QStringList sessionDecodes (QByteArray const& frames, QByteArray const& pcm, QString* report)
{
  QByteArray input = sessionHeader ();
  for (auto const& line : frames.split ('\n')) input += frame (0x02u, R"({"t":"configure",)" + line.mid (1));
  auto const codec = runCodec ({{input + audio (pcm, 0, pcm.size () / 2) + halt ()}});
  *report = codec.report ();
  QStringList lines;
  if (!codec.clean () || !ofType (codec.events, "error").isEmpty ()) return {"failed"};
  for (auto const& event : ofType (codec.events, "decode"))
    lines << QString::fromUtf8 (QJsonDocument {event}.toJson (QJsonDocument::Compact));
  return lines;
}
}

class TestStreamingFastJt9 : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  // From a file the codec decodes each period once, at the GUI's file
  // decode point, with the GUI's lines; a decode again gives the GUI's
  // Decode button's lines (newdat 0). Every decode carries the period and
  // the block it was decoded at.
  void decodesMatchTheGui_data ()
  {
    QTest::addColumn<QString> ("submode");
    QTest::addColumn<int> ("trperiod");
    QTest::addColumn<int> ("minsync");
    for (int trperiod : {5, 10, 15, 30})
      for (QString submode : {"E", "F", "G", "H"})
        QTest::newRow (QString {"%1 s %2"}.arg (trperiod).arg (submode).toUtf8 ().constData ())
          << submode << trperiod << 0;
    // A sync limit below 0 takes up to 50 lines, where 0 takes 2: three
    // messages in turn give three lines and two.
    QTest::newRow ("15 s E three messages") << QString {"E"} << 15 << 0;
    QTest::newRow ("15 s E three messages, min_sync -1") << QString {"E"} << 15 << -1;
  }

  void decodesMatchTheGui ()
  {
    QFETCH (QString, submode);
    QFETCH (int, trperiod);
    QFETCH (int, minsync);
    auto const three = QString {QTest::currentDataTag ()}.contains ("three");
    auto const pcm = three ? fastJt9Audio (submode, trperiod, 1, {Message, "K1ABC W9XYZ EN37", "W9XYZ K1ABC RRR"})
                           : fastJt9Audio (submode, trperiod);
    QVERIFY2 (!pcm.isEmpty (), "no render");
    auto const periodSamples = trperiod * SampleRate;
    auto keys = with (settings (submode, trperiod, false), {{"utc", "12:00:15"}, {"busy_events", true}});
    if (minsync) keys.insert ("min_sync", minsync);
    auto const codec = runCodec ({{sessionHeader () + control (keys) + audio (pcm, 0, periodSamples)
                                   + control ({{"nagain_flag", true}}) + halt ()}});
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (ofType (codec.events, "error").size (), 0);
    QCOMPARE (ofType (codec.events, "pass_skipped").size (), 0);
    auto const groups = decodeGroups (codec.events);
    QCOMPARE (groups.size (), 2);
    auto const k = fileTry (trperiod);
    for (auto const& group : groups)
      {
        QCOMPARE (group.last ().value ("period_end").toString (), QString {"120015"});
        for (auto const count : {"synced", "decoded", "average"}) QCOMPARE (group.last ().value (count).toInt (-1), 0);
      }
    QVERIFY2 (passLine (codec, k), codec.err.constData ());
    // Each decode's busy line first: the mode and the block, the second a decode again.
    QList<QJsonObject> busy;
    for (auto const& event : ofType (codec.events, "busy"))
      if (event.value ("busy").toBool ()) busy << event;
    QCOMPARE (busy.size (), 2);
    for (int b = 0; b < 2; ++b)
      {
        QCOMPARE (busy.at (b).value ("mode").toString (), QString {"JT9"});
        QCOMPARE (busy.at (b).value ("nzhsym").toInt (), k / BlockSamples);
        QCOMPARE (busy.at (b).value ("period_end").toString (), QString {"120015"});
        QCOMPARE (busy.at (b).value ("again").toBool (), b == 1);
      }
    // In stream order: busy, the decodes, decode_finished, then the same for the decode again.
    QStringList order;
    for (auto const& event : codec.events)
      {
        auto const type = event.value ("t").toString ();
        if ("busy" == type && event.value ("busy").toBool ()) order << "busy";
        else if ("decode_finished" == type) order << "finished";
        else if ("decode" == type && (order.isEmpty () || order.last () != "decode")) order << "decode";
      }
    QCOMPARE (order, (QStringList {"busy", "decode", "finished", "busy", "decode", "finished"}));
    auto script = modelSettings (submode, trperiod, 120015, minsync);
    // Third, the outside station is decodable where the GUI looks for it.
    script << QString {"decode 0 %1 1"}.arg (k) << QString {"decode 0 %1 0"}.arg (k)
           << QString {"rxfreq %1"}.arg (OutsideFrequency) << QString {"decode 0 %1 1"}.arg (k);
    auto const model = runModel (pcm, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    QCOMPARE (model.decodes.size (), 3);
    // (In H its tones, 100 Hz apart from 1700 Hz, run into fast9's 2500 Hz limit.)
    if ("H" != submode)
      QVERIFY2 (model.decodes.at (2).lines.join ('\n').contains (OutsideMessage),
                qPrintable ("the GUI decodes no outside station: " + model.decodes.at (2).lines.join (" | ")));
    if (three)
      {
        // At most 2 lines, or 50 below sync 0: the three messages, or two of them.
        QCOMPARE (model.decodes.first ().lines.size (), minsync < 0 ? 3 : 2);
        QCOMPARE (QSet<QString> (model.decodes.first ().lines.begin (), model.decodes.first ().lines.end ()).size (),
                  model.decodes.first ().lines.size ());
      }
    else
      {
        QVERIFY2 (model.decodes.first ().lines.join ('\n').contains (Message),
                  qPrintable ("the GUI decodes no " + QString {Message} + ": " + model.decodes.first ().lines.join (" | ")));
      }
    for (int d = 0; d < 2; ++d)
      {
        auto const difference = compareDecodes (groups.at (d), model.decodes.at (d).lines, trperiod, k / BlockSamples);
        QVERIFY2 (difference.isEmpty (), qPrintable (QString {"decode %1: %2"}.arg (d).arg (difference)));
      }
  }

  // Live, a decoder still busy at the period's last try skips the period
  // (pass_skipped "busy", then its decode_finished), as the GUI's busy
  // decoder runs no fast decode. At 5 s the GUI's first try is its last.
  void busyDecoderSkipsThePeriod ()
  {
    auto const trperiod = 5;
    auto const pcm = fastJt9Audio ("E", trperiod, 2);
    QVERIFY2 (!pcm.isEmpty (), "no render");
    auto const periodSamples = trperiod * SampleRate;
    auto const k = liveTry (trperiod);
    QCOMPARE (lastLiveTry (trperiod), k);
    auto const codec = runCodec ({{sessionHeader () + control (with (settings ("E", trperiod, true), {{"utc", "12:00:00"}}))
                                   + audio (pcm, 0, periodSamples) + control ({{"utc", "12:00:05"}})
                                   + audio (pcm, periodSamples, 2 * periodSamples) + halt ()}},
                                 {QString {"WSJT_CODEC_TEST_HOLD_PASS=%1:12000"}.arg (k / BlockSamples)});
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const groups = decodeGroups (codec.events);
    QCOMPARE (groups.size (), 2);
    QVERIFY2 (ofType (groups.first (), "decode").size () > 0, qPrintable (codec.report ()));
    QCOMPARE (groups.first ().last ().value ("period_end").toString (), QString {"120000"});
    QCOMPARE (groups.at (1).size (), 2);
    auto const skipped = groups.at (1).first ();
    QCOMPARE (skipped.value ("t").toString (), QString {"pass_skipped"});
    QCOMPARE (skipped.value ("mode").toString (), QString {"JT9"});
    QCOMPARE (skipped.value ("period_end").toString (), QString {"120005"});
    QCOMPARE (skipped.value ("nzhsym").toInt (), k / BlockSamples);
    QCOMPARE (skipped.value ("stage").toString (), QString {"final"});
    QCOMPARE (skipped.value ("reason").toString (), QString {"busy"});
    QCOMPARE (groups.at (1).last ().value ("period_end").toString (), QString {"120005"});
  }

  // Live, a decoder busy at the period's first try decodes at a later one
  // that finds it free, as the GUI retries on each block while less than
  // 0.35 s remains. The second period's tries arrive late, at 31 s and 37 s
  // from the test's start, so they fire on arrival. The first period's decode
  // is held 33 s from the start of its pass, which comes after the codec's
  // startup and the first period's read, and its decode follows the hold:
  // the first try finds it busy with at least 2 s to spare, and the last finds
  // it free with 4 s less that startup, read and decode.
  void busyDecoderRetries ()
  {
    auto const trperiod = 15;
    auto const pcm = fastJt9Audio ("E", trperiod, 2);
    QVERIFY2 (!pcm.isEmpty (), "no render");
    auto const periodSamples = trperiod * SampleRate;
    auto const first = liveTry (trperiod);
    auto const last = lastLiveTry (trperiod);
    QCOMPARE (last, first + BlockSamples);
    auto const codec = runCodec ({{sessionHeader () + control (with (settings ("E", trperiod, true), {{"utc", "12:00:00"}}))
                                   + audio (pcm, 0, periodSamples) + control ({{"utc", "12:00:15"}})},
                                  {audio (pcm, periodSamples, periodSamples + first), 31000},
                                  {audio (pcm, periodSamples + first, 2 * periodSamples) + halt (), 37000}},
                                 {QString {"WSJT_CODEC_TEST_HOLD_PASS=%1:33000"}.arg (first / BlockSamples)});
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (ofType (codec.events, "pass_skipped").size (), 0);
    auto const groups = decodeGroups (codec.events);
    QCOMPARE (groups.size (), 2);
    // The first period decoded at its first try, the one held.
    QVERIFY2 (codec.err.contains (QString {"hold done nzhsym=%1"}.arg (first / BlockSamples).toUtf8 ()),
              codec.err.constData ());
    QVERIFY2 (ofType (groups.first (), "decode").size () > 0, qPrintable (codec.report ()));
    for (auto const& decode : ofType (groups.first (), "decode")) QCOMPARE (decode.value ("pass").toInt (), first / BlockSamples);
    QVERIFY2 (passLine (codec, first) && passLine (codec, last), codec.err.constData ());
    QCOMPARE (groups.at (1).last ().value ("period_end").toString (), QString {"120015"});
    auto script = modelSettings ("E", trperiod, 120015);
    script << QString {"decode %1 %2 1"}.arg (periodSamples).arg (last);
    auto const model = runModel (pcm, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const difference = compareDecodes (groups.at (1), model.decodes.first ().lines, trperiod, last / BlockSamples);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // Paced at real time, the decode arrives no later than the GUI's trigger,
  // the block that takes the period past 0.35 s before its end, plus the
  // decode's compute time, with 300 ms for the pipes and the thread. At 15 s
  // the first try is a block before the last.
  void decodesArriveOnTime ()
  {
    auto const trperiod = 15;
    auto const pcm = fastJt9Audio ("E", trperiod);
    QVERIFY2 (!pcm.isEmpty (), "no render");
    auto const periodSamples = trperiod * SampleRate;
    auto const k = liveTry (trperiod);
    QList<Chunk> chunks {{sessionHeader () + control (with (settings ("E", trperiod, true), {{"utc", "12:00:00"}}))}};
    for (int from = 0; from < periodSamples; from += FrameSamples)
      chunks << Chunk {audio (pcm, from, from + FrameSamples, FrameSamples), 200 + from / 12,
                       from < k && k <= from + FrameSamples};
    chunks.last ().bytes += halt ();
    auto const codec = runCodec (chunks);
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QVERIFY (codec.triggerMs > 0);
    QVERIFY2 (passLine (codec, k), codec.err.constData ());
    auto script = modelSettings ("E", trperiod, 120000);
    script << QString {"decode 0 %1 1"}.arg (k);
    auto const model = runModel (pcm, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const groups = decodeGroups (codec.events);
    QCOMPARE (groups.size (), 1);
    auto const difference = compareDecodes (groups.first (), model.decodes.first ().lines, trperiod, k / BlockSamples);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
    for (int i = 0; i < codec.events.size (); ++i)
      {
        auto const type = codec.events.at (i).value ("t").toString ();
        if ("decode" != type && "decode_finished" != type) continue;
        auto const late = codec.arrivals.at (i) - codec.triggerMs;
        auto const allowed = qint64 (1000.0 * model.decodes.first ().seconds) + 300;
        qInfo ().noquote () << QString {"%1 %2 ms after the trigger, %3 ms allowed"}.arg (type).arg (late).arg (allowed);
        QVERIFY2 (late <= allowed, qPrintable (QString {"%1 came %2 ms after the trigger"}.arg (type).arg (late)));
      }
  }

  // A period cut short (a flush, halt or the end of input) before its last
  // try decodes as the GUI decodes a file of the samples it has: at the first
  // block end at or past them less 3584, or on all of them when they are
  // fewer than one block.
  void cutShortPeriodDecodesAsAShortFile_data ()
  {
    QTest::addColumn<int> ("samples");
    QTest::addColumn<bool> ("live");
    QTest::addColumn<QString> ("end");
    QTest::newRow ("10 s of 15, flush") << 10 * SampleRate << false << "flush";
    QTest::newRow ("10 s of 15, end of input") << 10 * SampleRate << false << "";
    QTest::newRow ("10 s of 15, live, halt") << 10 * SampleRate << true << "halt";
    QTest::newRow ("0.25 s of 15, flush") << 3000 << false << "flush";
  }

  void cutShortPeriodDecodesAsAShortFile ()
  {
    QFETCH (int, samples);
    QFETCH (bool, live);
    QFETCH (QString, end);
    auto const trperiod = 15;
    auto const pcm = fastJt9Audio ("E", trperiod);
    QVERIFY2 (!pcm.isEmpty (), "no render");
    auto bytes = sessionHeader () + control (with (settings ("E", trperiod, live), {{"utc", "12:00:15"}}))
      + audio (pcm, 0, samples);
    if ("flush" == end) bytes += frame (0x02u, R"({"t":"flush"})");
    if (!end.isEmpty ()) bytes += halt ();
    auto const codec = runCodec ({{bytes}});
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    auto const k = samples < BlockSamples ? samples
      : std::min (samples, (std::max (samples - 3584, 1) + BlockSamples - 1) / BlockSamples * BlockSamples);
    QVERIFY2 (passLine (codec, k), codec.err.constData ());
    auto const groups = decodeGroups (codec.events);
    QCOMPARE (groups.size (), 1);
    QCOMPARE (groups.first ().last ().value ("period_end").toString (), QString {"120015"});
    auto script = modelSettings ("E", trperiod, 120015);
    script << QString {"decode 0 %1 1"}.arg (k);
    auto const model = runModel (pcm, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    if (samples > 5 * SampleRate) QVERIFY2 (model.decodes.first ().lines.join ('\n').contains (Message),
                                            qPrintable (model.decodes.first ().lines.join (" | ")));
    auto const difference = compareDecodes (groups.first (), model.decodes.first ().lines, trperiod, k / BlockSamples);
    QVERIFY2 (difference.isEmpty (), qPrintable (difference));
  }

  // JT9 at 60 s in submode E, without fast, is accepted and decodes with
  // JT9's slow decoder at its stop, 173 half-symbols, on the whole period,
  // with no period on its decodes. The slow wide decoder finds this render
  // with a narrow F Tol, so the row uses ntol 20.
  void slowJt9AtSixtySecondsSubmodeE ()
  {
    auto const signal = render ("E", 0);
    QVERIFY2 (!signal.isEmpty (), "no render");
    auto const periodSamples = 60 * SampleRate;
    QByteArray pcm (2 * periodSamples, '\0');
    std::mt19937 random {60u};
    std::normal_distribution<double> noise {0.0, 1500.0};
    for (int k = 0; k < periodSamples; ++k)
      {
        auto const at = k - SampleRate;
        double value = noise (random);
        if (at >= 0 && 2 * at + 1 < signal.size ()) value += 0.2 * qFromLittleEndian<qint16> (signal.constData () + 2 * at);
        qToLittleEndian<qint16> (qint16 (std::max (-32767.0, std::min (32767.0, std::round (value)))), pcm.data () + 2 * k);
      }
    auto const codec = runCodec ({{sessionHeader ()
                                   + control ({{"mode", "JT9"}, {"submode", "E"}, {"trperiod", 60}, {"rxfreq", AudioFrequency},
                                               {"ntol", 20}, {"utc", "12:01:00"}})
                                   + audio (pcm, 0, periodSamples) + halt ()}});
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (ofType (codec.events, "error").size (), 0);
    QVERIFY2 (codec.err.contains ("wsjt-codec: pass nzhsym=173 samples=720000 "), codec.err.constData ());
    auto const decodes = ofType (codec.events, "decode");
    QCOMPARE (decodes.size (), 1);
    QCOMPARE (decodes.first ().value ("mode").toString (), QString {"JT9"});
    QCOMPARE (decodes.first ().value ("message").toString (), QString {Message});
    QCOMPARE (decodes.first ().value ("freq").toInt (), AudioFrequency);
    QCOMPARE (decodes.first ().value ("time").toString (), QString {"120100"});
    QCOMPARE (decodes.first ().value ("pass").toInt (), 173);
    QVERIFY (!decodes.first ().contains ("period"));
    QCOMPARE (ofType (codec.events, "decode_finished").size (), 1);
  }

  // Entering fast JT9 (a configure that carries mode JT9 with fast true, or
  // turns fast on) sets the receive frequency to 700 unless the frame carries
  // rxfreq, as choosing JT9 with Fast on and WSJT-X's Fast check box set Rx
  // Freq. In fast JT9 ntol's default is 50, JT9's F Tol, reset as in every
  // mode by a configure that carries mode or trperiod. A frame that carries
  // fast true in fast JT9 without mode is no entry and resets neither, as
  // WSJT-X keeps Rx Freq and F Tol on a submode change; one that carries only
  // trperiod keeps rxfreq. Message at 700 Hz and OutsideMessage at 1200 tell
  // the windows apart; from a file, against the GUI's fast_decode_.
  void fastEntryDefaults_data ()
  {
    QTest::addColumn<QByteArray> ("frames");   // control JSON lines
    QTest::addColumn<int> ("trperiod");         // fast JT9's period after them
    QTest::addColumn<int> ("rxfreq");           // the window the GUI's decoder gets
    QTest::addColumn<int> ("ntol");
    QTest::addColumn<QString> ("expected");     // the message decoded there
    auto const fast15 = QByteArray {R"({"mode":"JT9","fast":true,"submode":"E","trperiod":15})"};
    QTest::newRow ("from FT8 with rxfreq 1200") << QByteArray {R"({"mode":"FT8","rxfreq":1200})"} + "\n" + fast15
                                                 << 15 << 700 << 50 << QString {Message};
    QTest::newRow ("fast frame carries rxfreq and ntol")
      << QByteArray {R"({"mode":"FT8"})"} + "\n" + R"({"mode":"JT9","fast":true,"submode":"E","trperiod":15,"rxfreq":1200,"ntol":600})"
      << 15 << 1200 << 600 << QString {Message};
    QTest::newRow ("trperiod alone keeps rxfreq")
      << QByteArray {R"({"mode":"JT9","fast":true,"submode":"E","trperiod":15,"rxfreq":1200,"ntol":100})"} + "\n"
           + R"({"trperiod":10})" << 10 << 1200 << 50 << QString {OutsideMessage};
    QTest::newRow ("redundant fast keeps rxfreq and ntol")
      << QByteArray {R"({"mode":"JT9","fast":true,"submode":"E","trperiod":15,"rxfreq":1500,"ntol":400})"} + "\n"
           + R"({"fast":true,"submode":"E"})" << 15 << 1500 << 400 << QString {OutsideMessage};
    QTest::newRow ("fast turning on from slow JT9")
      << QByteArray {R"({"mode":"JT9","submode":"E","trperiod":60,"rxfreq":1200,"ntol":100})"} + "\n"
           + R"({"fast":true,"trperiod":15})" << 15 << 700 << 50 << QString {Message};
    QTest::newRow ("mode frame in fast JT9 enters again")
      << QByteArray {R"({"mode":"JT9","fast":true,"submode":"E","trperiod":15,"rxfreq":1200,"ntol":100})"} + "\n" + fast15
      << 15 << 700 << 50 << QString {Message};
  }

  void fastEntryDefaults ()
  {
    QFETCH (QByteArray, frames);
    QFETCH (int, trperiod);
    QFETCH (int, rxfreq);
    QFETCH (int, ntol);
    QFETCH (QString, expected);
    auto const pcm = twoStations (trperiod);
    QVERIFY2 (!pcm.isEmpty (), "no render");
    QString report;
    auto const decodes = sessionDecodes (frames, pcm, &report);
    QVERIFY2 (decodes != QStringList {"failed"}, qPrintable (report));
    auto script = modelSettings ("E", trperiod, 0);
    script.replaceInStrings (QRegularExpression {"^rxfreq .*"}, QString {"rxfreq %1"}.arg (rxfreq));
    script.replaceInStrings (QRegularExpression {"^ntol .*"}, QString {"ntol %1"}.arg (ntol));
    auto const k = fileTry (trperiod);
    script << QString {"decode 0 %1 1"}.arg (k);
    auto const model = runModel (pcm, script);
    QVERIFY2 (model.ok, qPrintable (model.failure));
    auto const lines = model.decodes.first ().lines;
    QVERIFY2 (lines.join ('\n').contains (expected), qPrintable ("the GUI decodes no " + expected + ": " + lines.join (" | ")));
    QCOMPARE (decodes.size (), lines.size ());
    for (int i = 0; i < lines.size (); ++i)
      {
        auto const decode = QJsonDocument::fromJson (decodes.at (i).toUtf8 ()).object ();
        auto const& line = lines.at (i);
        QCOMPARE (decode.value ("snr").toInt (), line.mid (6, 4).trimmed ().toInt ());
        QVERIFY (std::abs (decode.value ("dt").toDouble () - double (line.mid (10, 5).trimmed ().toFloat ())) <= 1e-6);
        QCOMPARE (decode.value ("freq").toInt (), line.mid (15, 5).trimmed ().toInt ());
        QCOMPARE (decode.value ("message").toString (), line.mid (24).trimmed ());
        QCOMPARE (decode.value ("pass").toInt (), k / BlockSamples);
      }
  }

  // Slow JT9's defaults are as before fast JT9's: leaving fast JT9 for slow
  // JT9 resets ntol to 1000 and carries rxfreq, as from any other mode. This
  // pins today's behaviour; WSJT-X restores F Tol (Ftol_JT9, 50 by default)
  // when JT9 is chosen with VHF features on, a departure recorded for slow
  // JT9 and not changed here. Each
  // session decodes as one that sends those values; the order of its two
  // decodes shows the window, since the decoder takes rxfreq +- ntol first.
  // (Submode carries over, so leaving fast JT9 sends it.)
  void slowJt9DefaultsAfterFast_data ()
  {
    QTest::addColumn<QByteArray> ("frames");
    QTest::addColumn<QByteArray> ("sent");   // the same values, sent
    QTest::newRow ("back to slow JT9 resets ntol")
      << QByteArray {R"({"mode":"FT8","rxfreq":1200})"} + "\n" + R"({"mode":"JT9","fast":true,"submode":"E","trperiod":15})"
           + "\n" + R"({"mode":"JT9","submode":"A","trperiod":60,"rxfreq":1200})"
      << QByteArray {R"({"mode":"JT9","submode":"A","trperiod":60,"rxfreq":1200,"ntol":1000})"};
    QTest::newRow ("slow JT9 from FT8 keeps rxfreq")
      << QByteArray {R"({"mode":"FT8","rxfreq":1200})"} + "\n" + R"({"mode":"JT9","trperiod":60,"ntol":50})"
      << QByteArray {R"({"mode":"JT9","trperiod":60,"rxfreq":1200,"ntol":50})"};
  }

  void slowJt9DefaultsAfterFast ()
  {
    QFETCH (QByteArray, frames);
    QFETCH (QByteArray, sent);
    auto const pcm = twoStations (60);
    QVERIFY2 (!pcm.isEmpty (), "no render");
    QString report;
    auto const decodes = sessionDecodes (frames, pcm, &report);
    QVERIFY2 (decodes != QStringList {"failed"}, qPrintable (report));
    auto const expected = sessionDecodes (sent, pcm, &report);
    QVERIFY2 (expected != QStringList {"failed"}, qPrintable (report));
    QCOMPARE (expected.size (), 2);
    QCOMPARE (decodes, expected);
  }

  // Entering fast JT9 also sets the transmit frequency to 700, as WSJT-X's
  // Fast entry sets Tx Freq, unless the frame carries rxfreq or
  // tx_audio_offset_hz; FreqCal's default moves only the receive frequency.
  // FT8 shows the transmit frequency: its AP passes near it (ft8b, iaptype 3
  // and up, within ap_width_hz of the receive or transmit frequency) decode a
  // weak "K1ABC W9XYZ -10" at 700 Hz that no other pass decodes. Each session
  // goes FT8 (rxfreq 2000), fast JT9, FreqCal (receive 1500), then FT8, and
  // must decode as one that sends receive 1500 and the transmit frequency;
  // at one level at least the transmit frequency must change the decodes.
  void txFollowsFastEntry_data ()
  {
    QTest::addColumn<QByteArray> ("fastFrame");
    QTest::addColumn<int> ("tx");   // the transmit frequency the session should have
    QTest::newRow ("entry sets it to 700") << QByteArray {R"({"mode":"JT9","fast":true,"submode":"E","trperiod":15})"} << 700;
    QTest::newRow ("entry keeps a sent tx_audio_offset_hz")
      << QByteArray {R"({"mode":"JT9","fast":true,"submode":"E","trperiod":15,"tx_audio_offset_hz":2000})"} << 2000;
  }

  void txFollowsFastEntry ()
  {
    QFETCH (QByteArray, fastFrame);
    QFETCH (int, tx);
    QJsonObject const request {{"id", 1}, {"mode", "FT8"}, {"text", "K1ABC W9XYZ -10"}, {"freq", 700}, {"rate", SampleRate}};
    auto const run = runCodec ({{sessionHeader () + control (request, "render")}});
    QByteArray signal;
    for (auto const& event : ofType (run.events, "pcm")) signal += QByteArray::fromBase64 (event.value ("data").toString ().toLatin1 ());
    QVERIFY2 (!signal.isEmpty (), "no render");
    auto const ft8 = QByteArray {R"({"mode":"FT8","trperiod":15,"depth":3,"mycall":"K1ABC","his_call":"W9XYZ","qso_progress_state":2,"utc":"12:00:00")"};
    bool sensitive = false;
    for (double scale : {0.0033, 0.0036, 0.0039, 0.0042, 0.0045})
      {
        auto const periodSamples = 15 * SampleRate;
        QByteArray pcm (2 * periodSamples, '\0');
        std::mt19937 random {11u};
        std::normal_distribution<double> noise {0.0, 1500.0};
        for (int k = 0; k < periodSamples; ++k)
          {
            double value = noise (random);
            auto const i = k - SampleRate / 2;
            if (i >= 0 && 2 * i + 1 < signal.size ()) value += scale * qFromLittleEndian<qint16> (signal.constData () + 2 * i);
            qToLittleEndian<qint16> (qint16 (std::max (-32767.0, std::min (32767.0, std::round (value)))), pcm.data () + 2 * k);
          }
        auto const sent = [&] (int frequency) {
          return ft8 + QString {R"(,"rxfreq":1500,"tx_audio_offset_hz":%1})"}.arg (frequency).toUtf8 ();
        };
        QString report;
        auto const session = sessionDecodes (QByteArray {R"({"mode":"FT8","rxfreq":2000})"} + "\n" + fastFrame + "\n"
                                             + R"({"mode":"FREQCAL","trperiod":15})" + "\n" + ft8 + "}", pcm, &report);
        QVERIFY2 (session != QStringList {"failed"}, qPrintable (report));
        auto const near = sessionDecodes (sent (700), pcm, &report);
        auto const far = sessionDecodes (sent (2000), pcm, &report);
        QVERIFY2 (near != QStringList {"failed"} && far != QStringList {"failed"}, qPrintable (report));
        sensitive = sensitive || near != far;
        QVERIFY2 (session == (700 == tx ? near : far),
                  qPrintable (QString {"at %1: %2 against %3"}.arg (scale).arg (session.join (" | "), (700 == tx ? near : far).join (" | "))));
      }
    QVERIFY2 (sensitive, "at no level did the transmit frequency change the decodes");
  }

  // Live, a try that gets no copy of its audio waits for the next, as a busy
  // one does, and the last such try skips the period (pass_skipped "memory",
  // stage final, at the last try's block) before its decode_finished. Here
  // every copy fails (WSJT_CODEC_TEST_ALLOC_LIMIT), so no pass runs.
  void memoryRetriesThenSkips ()
  {
    auto const trperiod = 15;
    auto const pcm = fastJt9Audio ("E", trperiod);
    QVERIFY2 (!pcm.isEmpty (), "no render");
    auto const first = liveTry (trperiod);
    auto const last = lastLiveTry (trperiod);
    QCOMPARE (last, first + BlockSamples);
    auto const codec = runCodec ({{sessionHeader () + control (with (settings ("E", trperiod, true), {{"utc", "12:00:15"}}))
                                   + audio (pcm, 0, trperiod * SampleRate) + halt ()}},
                                 {"WSJT_CODEC_TEST_ALLOC_LIMIT=1"});
    QVERIFY2 (codec.clean (), qPrintable (codec.report ()));
    QCOMPARE (ofType (codec.events, "decode").size (), 0);
    QVERIFY2 (!passLine (codec, first) && !passLine (codec, last), codec.err.constData ());
    QStringList order;
    for (auto const& event : codec.events)
      {
        auto const type = event.value ("t").toString ();
        if ("pass_skipped" == type || "decode_finished" == type) order << type;
      }
    QCOMPARE (order, (QStringList {"pass_skipped", "decode_finished"}));
    auto const skipped = ofType (codec.events, "pass_skipped").first ();
    QCOMPARE (skipped.value ("mode").toString (), QString {"JT9"});
    QCOMPARE (skipped.value ("period_end").toString (), QString {"120015"});
    QCOMPARE (skipped.value ("nzhsym").toInt (), last / BlockSamples);
    QCOMPARE (skipped.value ("stage").toString (), QString {"final"});
    QCOMPARE (skipped.value ("reason").toString (), QString {"memory"});
    QCOMPARE (ofType (codec.events, "decode_finished").first ().value ("period_end").toString (), QString {"120015"});
  }
};

QTEST_GUILESS_MAIN (TestStreamingFastJt9)

#include "test_streaming_fast_jt9.moc"
