#include <cmath>
#include <functional>
#include <limits>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>
#include <QtEndian>

namespace
{
constexpr quint32 Jt9AudioCapacityBytes = 30u * 60u * 12000u * 2u;
constexpr quint32 WsprdAudioCapacityBytes = 8u * 114u * 12000u * 2u;
// FT4's 7.5 s period at 12 kHz.
constexpr int Ft4PeriodSamples = 90000;
constexpr int Jt9TenSecondPeriodSamples = 10 * 12000;
// wsjt-codec reads audio 4,096 samples at a time and skips frames 4,096 bytes at a time.
constexpr int Jt9ChunkSamples = 4096;
constexpr int Jt9DrainBytes = 4096;
constexpr int Jt9ControlCapacityBytes = 262144;

constexpr char ReadyLine[] =
  R"({"v":1,"t":"ready","modes":["FT8","FT4","JT9","JT65","JT65+JT9","JT4","FST4","FST4W","Q65","MSK144","JTTY"],"protocol":1})";
constexpr char OddAudioFrameLine[] =
  R"({"v":1,"t":"error","code":"odd_audio_frame","detail":"audio frame length must contain whole int16 samples"})";
constexpr char ControlFrameTooLargeLine[] =
  R"({"v":1,"t":"error","code":"control_frame_too_large","detail":"control frame exceeds the configured buffer capacity"})";
constexpr char FrameTooLargeLine[] =
  R"({"v":1,"t":"error","code":"frame_too_large","detail":"frame length exceeds the streaming sample-buffer capacity"})";
constexpr char InvalidTrperiodLine[] =
  R"({"v":1,"t":"error","code":"invalid_trperiod","detail":"trperiod must be finite and between 0 and 1800 seconds"})";

struct ProcessResult
{
  bool started {};
  bool timedOut {};
  int exitCode {-1};
  QProcess::ExitStatus exitStatus {QProcess::CrashExit};
  QByteArray standardOutput;
  QByteArray standardError;
};

struct ParsedEvents
{
  QList<QJsonObject> values;
  QString error;
};

void appendLittleEndian32 (QByteArray& bytes, quint32 value)
{
  bytes.append (char (value & 0xffu));
  bytes.append (char ((value >> 8) & 0xffu));
  bytes.append (char ((value >> 16) & 0xffu));
  bytes.append (char ((value >> 24) & 0xffu));
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

QByteArray declaredFrame (quint8 type, quint32 length)
{
  QByteArray bytes;
  bytes.append (char (type));
  appendLittleEndian32 (bytes, length);
  return bytes;
}

QByteArray frame (quint8 type, QByteArray const& body)
{
  auto bytes = declaredFrame (type, quint32 (body.size ()));
  bytes.append (body);
  return bytes;
}

QByteArray silentAudioFrame (int samples)
{
  return frame (0x01u, QByteArray (samples * 2, '\0'));
}

QByteArray controlFrame (QByteArray const& json)
{
  return frame (0x02u, json);
}

ProcessResult runProcess (QString const& executable, QStringList const& arguments,
                          QByteArray const& input,
                          QString const& workingDirectory,
                          int timeoutMs = 30000,
                          bool closeInput = true)
{
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.setWorkingDirectory (workingDirectory);
  process.start (executable, arguments);

  ProcessResult result;
  result.started = process.waitForStarted (5000);
  if (!result.started)
    {
      result.standardError = process.errorString ().toUtf8 ();
      return result;
    }

  process.write (input);
  if (closeInput)
    {
      process.closeWriteChannel ();
    }
  if (!process.waitForFinished (timeoutMs))
    {
      result.timedOut = true;
      process.kill ();
      process.waitForFinished (5000);
    }

  result.exitCode = process.exitCode ();
  result.exitStatus = process.exitStatus ();
  result.standardOutput = process.readAllStandardOutput ();
  result.standardError = process.readAllStandardError ();
  return result;
}

ParsedEvents parseEvents (QByteArray const& output)
{
  ParsedEvents parsed;
  auto const lines = output.split ('\n');
  for (auto const& line : lines)
    {
      if (line.trimmed ().isEmpty ())
        {
          continue;
        }
      QJsonParseError jsonError;
      auto const document = QJsonDocument::fromJson (line, &jsonError);
      if (jsonError.error != QJsonParseError::NoError || !document.isObject ())
        {
          parsed.error = QString {"invalid JSON event: %1"}.arg (
            QString::fromUtf8 (line));
          return parsed;
        }
      auto const event = document.object ();
      if (event.value ("v").toInt (-1) != 1 || !event.value ("t").isString ())
        {
          parsed.error = QString {"invalid event envelope: %1"}.arg (
            QString::fromUtf8 (line));
          return parsed;
        }
      auto const type = event.value ("t").toString ();
      if (type == "error" && event.contains ("code"))
        {
          auto const code = event.value ("code");
          if (!code.isString () || code.toString ().isEmpty ())
            {
              parsed.error = QString {"invalid coded error event: %1"}.arg (
                QString::fromUtf8 (line));
              return parsed;
            }
          auto const requiresDetail =
            code.toString () == "frame_too_large" ||
            code.toString () == "unknown_mode" ||
            code.toString () == "invalid_trperiod" ||
            code.toString () == "invalid_wspr_type" ||
            code.toString () == "insufficient_audio";
          if (requiresDetail && !event.value ("detail").isString ())
            {
              parsed.error = QString {"coded error is missing detail: %1"}.arg (
                QString::fromUtf8 (line));
              return parsed;
            }
        }
      parsed.values.append (event);
    }
  return parsed;
}

QList<QJsonObject> eventsMatching (QList<QJsonObject> const& events,
                                   QString const& type,
                                   QString const& code = {})
{
  QList<QJsonObject> matching;
  for (auto const& event : events)
    {
      if (event.value ("t").toString () == type &&
          (code.isEmpty () || event.value ("code").toString () == code))
        {
          matching.append (event);
        }
    }
  return matching;
}

ProcessResult runJt9 (QByteArray const& input, QStringList options = {},
                      int timeoutMs = 30000)
{
  QTemporaryDir directory;
  if (!directory.isValid ())
    {
      return {};
    }
  options << "-a" << directory.path ()
          << "-t" << directory.path ()
          << "--stream";
  return runProcess (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), options, input,
                     directory.path (), timeoutMs);
}

ProcessResult runWsprd (QByteArray const& input, int timeoutMs = 30000,
                        bool closeInput = true)
{
  QTemporaryDir directory;
  if (!directory.isValid ())
    {
      return {};
    }
  return runProcess (QString::fromUtf8 (WSPRD_EXECUTABLE),
                     {"-a", directory.path (), "-H", "-0"}, input,
                     directory.path (), timeoutMs, closeInput);
}

void verifyCompleted (ProcessResult const& result, int expectedExitCode)
{
  QVERIFY2 (result.started, result.standardError.constData ());
  QVERIFY2 (!result.timedOut, result.standardError.constData ());
  QCOMPARE (result.exitStatus, QProcess::NormalExit);
  QCOMPARE (result.exitCode, expectedExitCode);
}

ParsedEvents verifyEvents (ProcessResult const& result)
{
  auto const parsed = parseEvents (result.standardOutput);
  if (!parsed.error.isEmpty ())
    {
      QTest::qFail (qPrintable (parsed.error), __FILE__, __LINE__);
    }
  if (parsed.values.isEmpty ())
    {
      QTest::qFail ("consumer emitted no JSON events", __FILE__, __LINE__);
    }
  return parsed;
}

QByteArray ndjson (QList<QByteArray> const& lines)
{
  QByteArray bytes;
  for (auto const& line : lines)
    {
      bytes += line + '\n';
    }
  return bytes;
}

// Fortran records on Windows may end in "\r\n".
QByteArray streamOutput (ProcessResult const& result)
{
  return QByteArray {result.standardOutput}.replace ("\r\n", "\n");
}

// The events of each decode, split at its decode_finished.
QList<QList<QJsonObject>> decodesByPeriod (QList<QJsonObject> const& events)
{
  QList<QList<QJsonObject>> periods {QList<QJsonObject> {}};
  for (auto const& event : events)
    {
      if (event.value ("t").toString () == "decode")
        {
          periods.last ().append (event);
        }
      else if (event.value ("t").toString () == "decode_finished")
        {
          periods.append (QList<QJsonObject> {});
        }
    }
  periods.removeLast ();
  return periods;
}

QByteArray configureFrame (QJsonObject keys)
{
  keys.insert ("t", "configure");
  return controlFrame (QJsonDocument {keys}.toJson (QJsonDocument::Compact));
}

// jt9's WAV-mode FT8 line carries the 37-column message after " ~  ".
QStringList wavModeFt8Messages (QByteArray const& output)
{
  QStringList messages;
  for (auto const& line : QByteArray {output}.replace ("\r\n", "\n").split ('\n'))
    {
      auto const marker = line.indexOf (" ~ ");
      if (marker >= 0)
        {
          messages << QString::fromLatin1 (line.mid (marker + 4, 37)).trimmed ();
        }
    }
  messages.sort ();
  return messages;
}

QByteArray sessionHeaderWith (int index, char value)
{
  auto bytes = sessionHeader ();
  bytes[index] = value;
  return bytes;
}

// A session whose input stays open until `done` holds for what it has said
// (or waitMs passes): what it reported then, and everything once it closes.
struct OpenSession
{
  QByteArray openOutput;
  QByteArray openError;
  ProcessResult result;
};

using SessionDone = std::function<bool (QByteArray const&, QByteArray const&)>;

OpenSession runOpenSession (QByteArray const& input, int waitMs, SessionDone done,
                            QStringList const& options = {})
{
  OpenSession session;
  QTemporaryDir directory;
  if (!directory.isValid ()) return session;
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.setWorkingDirectory (directory.path ());
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                 options + QStringList {"-a", directory.path (), "-t", directory.path (), "--stream"});
  session.result.started = process.waitForStarted (5000);
  if (!session.result.started) return session;
  process.write (input);
  QElapsedTimer timer;
  timer.start ();
  while (timer.elapsed () < waitMs && process.state () == QProcess::Running)
    {
      if (process.bytesToWrite () > 0) process.waitForBytesWritten (20);
      process.waitForReadyRead (20);
      // Qt 5 on Windows returns on stdout's data with stderr's readyRead still queued.
      QCoreApplication::processEvents ();
      session.openOutput += process.readAllStandardOutput ();
      session.openError += process.readAllStandardError ();
      if (done (session.openOutput, session.openError)) break;
    }
  if (process.state () == QProcess::Running)
    {
      process.closeWriteChannel ();
      if (!process.waitForFinished (120000))
        {
          session.result.timedOut = true;
          process.kill ();
          process.waitForFinished (5000);
        }
    }
  session.result.exitCode = process.exitCode ();
  session.result.exitStatus = process.exitStatus ();
  session.result.standardOutput = session.openOutput + process.readAllStandardOutput ();
  session.result.standardError = session.openError + process.readAllStandardError ();
  return session;
}

// A declined configure: its error shows the frames before it were all read.
QByteArray consumedMarker ()
{
  return controlFrame (R"({"t":"configure","mode":"NOT-A-MODE"})");
}

bool consumed (QByteArray const& output)
{
  return output.contains (R"("code":"unknown_mode")");
}

// The stream's "pass" lines on stderr.
struct Pass
{
  int nzhsym;
  int samples;
  int depth;
  int cycles;
  bool multithreaded;
  bool live;
  bool again;
  double emedelay;
};

QList<Pass> passes (QByteArray const& error)
{
  QRegularExpression const line {
    R"(pass nzhsym=(\d+) samples=(\d+) depth=(\d+) cycles=(\d+) multithreaded=([TF]) live=([TF]) again=([TF]) emedelay=(-?[0-9.]+)\r?$)",
    QRegularExpression::MultilineOption};
  QList<Pass> found;
  auto matches = line.globalMatch (QString::fromUtf8 (error));
  while (matches.hasNext ())
    {
      auto const m = matches.next ();
      found.append ({m.captured (1).toInt (), m.captured (2).toInt (), m.captured (3).toInt (),
                     m.captured (4).toInt (), m.captured (5) == "T", m.captured (6) == "T",
                     m.captured (7) == "T", m.captured (8).toDouble ()});
    }
  return found;
}

QByteArray wavSamples (QString const& path, int samples)
{
  QFile recording {path};
  if (!recording.open (QIODevice::ReadOnly)) return {};
  return recording.readAll ().mid (44, 2 * samples);
}

// The decode-schedule functions run by test_streaming_gui_schedule, apart from
// the rest of this binary, so that their sessions fit the sanitizer budgets.
bool scheduleTests ()
{
  return !qEnvironmentVariableIsEmpty ("WSJT_STREAM_SCHEDULE_TESTS");
}

bool rangeTests ()
{
  return !qEnvironmentVariableIsEmpty ("WSJT_STREAM_RANGE_TESTS");
}

bool readTests ()
{
  return !qEnvironmentVariableIsEmpty ("WSJT_STREAM_READ_TESTS");
}

// The session: frames, then a recording's audio in one-second frames.
ProcessResult runRecording (QString const& path, QByteArray const& frames,
                            QStringList options = {})
{
  QFile wav {path};
  if (!wav.open (QIODevice::ReadOnly))
    {
      return {};
    }
  auto const pcm = wav.readAll ().mid (44);
  auto input = sessionHeader () + frames;
  for (int offset = 0; offset < pcm.size (); offset += 24000)
    {
      input += frame (0x01u, pcm.mid (offset, 24000));
    }
  QTemporaryDir directory;
  if (!directory.isValid ())
    {
      return {};
    }
  options << "-a" << directory.path () << "-t" << directory.path () << "--stream";
  return runProcess (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), options, input,
                     directory.path (), 120000);
}

QJsonObject with (QJsonObject keys, QJsonObject const& more)
{
  for (auto it = more.constBegin (); it != more.constEnd (); ++it) keys.insert (it.key (), it.value ());
  return keys;
}
}

class TestStreamingConsumers final : public QObject
{
  Q_OBJECT

private slots:
  void jt4DecodesWaveform_data ()
  {
    QTest::addColumn<QJsonObject> ("timeFields");
    QTest::addColumn<QString> ("expected");
    QTest::newRow ("afternoon") << QJsonObject {{"utc", "12:34:00"}} << QString {"123400"};
    QTest::newRow ("legacy-numeric") << QJsonObject {{"nutc", 1234}} << QString {"123400"};
    // JT4 decodes with an HHMM00 label, as the GUI gives it.
    QTest::newRow ("early-hour-iso-precedence")
      << QJsonObject {{"nutc", 1234}, {"utc", "00:12:34"}} << QString {"001200"};
  }

  void jt4DecodesWaveform ()
  {
    QFETCH (QJsonObject, timeFields);
    QFETCH (QString, expected);
    QFile recording {QString::fromUtf8 (JT4_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const wav = recording.readAll ();
    QCOMPARE (wav.size (), 44 + 60 * 12000 * 2);
    QCOMPARE (wav.left (4), QByteArray {"RIFF"});
    QCOMPARE (wav.mid (36, 4), QByteArray {"data"});
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QJsonObject configuration {{"t", "configure"}, {"mode", "JT4"}, {"trperiod", 60},
      {"submode", 0}, {"depth_level", 1}, {"rxfreq", 1000}, {"ntol", 50}};
    for (auto it = timeFields.constBegin (); it != timeFields.constEnd (); ++it)
      {
        configuration.insert (it.key (), it.value ());
      }
    auto const input = sessionHeader () + controlFrame (
      QJsonDocument {configuration}.toJson (QJsonDocument::Compact)) +
      frame (0x01u, wav.mid (44)) + controlFrame (R"({"t":"halt"})");
    auto const result = runProcess (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
      {"-a", directory.path (), "-t", directory.path (), "--stream"}, input, directory.path ());
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    auto const decodes = eventsMatching (events.values, "decode");
    QCOMPARE (decodes.size (), 1);
    QCOMPARE (decodes.first ().value ("mode").toString (), QString {"JT4"});
    QCOMPARE (decodes.first ().value ("time").toString (), expected);
    QCOMPARE (decodes.first ().value ("message").toString ().trimmed (), QString {"K1ABC W9XYZ FN42"});
    auto const completions = eventsMatching (events.values, "decode_finished");
    QCOMPARE (completions.size (), 1);
    QCOMPARE (completions.first ().value ("period_end").toString (), expected);
  }

  void fst4DecodesWaveform_data ()
  {
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<QString> ("path");
    QTest::addColumn<int> ("period");
    QTest::addColumn<QString> ("message");
    QTest::newRow ("fst4-15") << QString {"FST4"} << QString::fromUtf8 (FST4_ENGINE_WAV)
      << 15 << QString {"K1ABC W9XYZ FN42"};
    QTest::newRow ("fst4w-120") << QString {"FST4W"} << QString::fromUtf8 (FST4W_ENGINE_WAV)
      << 120 << QString {"K1ABC FN42 37"};
  }

  void fst4DecodesWaveform ()
  {
    QFETCH (QString, mode);
    QFETCH (QString, path);
    QFETCH (int, period);
    QFETCH (QString, message);
    QFile recording {path};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const wav = recording.readAll ();
    QCOMPARE (wav.size (), 44 + period * 12000 * 2);
    QCOMPARE (wav.left (4), QByteArray {"RIFF"});
    QCOMPARE (wav.mid (36, 4), QByteArray {"data"});
    QJsonObject configuration {{"t", "configure"}, {"mode", mode}, {"trperiod", period},
      {"depth_level", 1}, {"nfa", 1300}, {"nfb", 1700}, {"rxfreq", 1500},
      {"noise_blanker_level", 0}, {"utc", "00:15:00"}};
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const input = sessionHeader () + controlFrame (
      QJsonDocument {configuration}.toJson (QJsonDocument::Compact)) + frame (0x01u, wav.mid (44));
    auto const result = runProcess (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
      {"-a", directory.path (), "-t", directory.path (), "--stream"}, input, directory.path (), 360000);
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    bool found = false;
    for (auto const& decode : eventsMatching (events.values, "decode"))
      {
        QCOMPARE (decode.value ("mode").toString (), mode);
        found |= decode.value ("message").toString ().trimmed () == message;
      }
    QVERIFY2 (found, result.standardOutput.constData ());
    QCOMPARE (eventsMatching (events.values, "decode_finished").size (), 1);
  }

  void fst4PartialEofCompletesOnce ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const input = sessionHeader () + controlFrame (
      R"({"t":"configure","mode":"FST4W","trperiod":120,"depth_level":1,"nutc":15})") +
      silentAudioFrame (12001);
    auto const result = runProcess (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
      {"-a", directory.path (), "-t", directory.path (), "--stream"}, input, directory.path ());
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    QCOMPARE (eventsMatching (events.values, "decode").size (), 0);
    auto const completions = eventsMatching (events.values, "decode_finished");
    QCOMPARE (completions.size (), 1);
    QCOMPARE (completions.first ().value ("period_end").toString (), QString {"001500"});
  }

  void q65DecodesWaveform ()
  {
    QFile recording {QString::fromUtf8 (Q65_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const wav = recording.readAll ();
    constexpr quint32 sampleBytes = 15 * 12000 * 2;
    QCOMPARE (wav.size (), int (44 + sampleBytes));
    QCOMPARE (wav.left (4), QByteArray {"RIFF"});
    QCOMPARE (wav.mid (8, 8), QByteArray {"WAVEfmt "});
    QCOMPARE (qFromLittleEndian<quint32> (wav.constData () + 16), quint32 (16));
    QCOMPARE (qFromLittleEndian<quint16> (wav.constData () + 20), quint16 (1));
    QCOMPARE (qFromLittleEndian<quint16> (wav.constData () + 22), quint16 (1));
    QCOMPARE (qFromLittleEndian<quint32> (wav.constData () + 24), quint32 (12000));
    QCOMPARE (qFromLittleEndian<quint16> (wav.constData () + 34), quint16 (16));
    QCOMPARE (wav.mid (36, 4), QByteArray {"data"});
    QCOMPARE (qFromLittleEndian<quint32> (wav.constData () + 40), sampleBytes);

    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const input = sessionHeader () + controlFrame (
      R"({"t":"configure","mode":"Q65","trperiod":15,"submode":0,"depth_level":1,"nfa":1300,"nfb":1700,"rxfreq":1500,"utc":"00:15:00"})") +
      frame (0x01u, wav.mid (44)) + controlFrame (R"({"t":"halt"})");
    auto const result = runProcess (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
      {"-a", directory.path (), "-t", directory.path (), "--stream"}, input, directory.path ());
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    auto const decodes = eventsMatching (events.values, "decode");
    QVERIFY2 (!decodes.isEmpty (), result.standardOutput.constData ());
    bool found = false;
    for (auto const& decode : decodes)
      {
        QCOMPARE (decode.value ("mode").toString (), QString {"Q65"});
        QCOMPARE (decode.value ("time").toString (), QString {"001500"});
        found |= decode.value ("message").toString ().trimmed () == "K1ABC W9XYZ FN42";
      }
    QVERIFY2 (found, result.standardOutput.constData ());
    auto const completions = eventsMatching (events.values, "decode_finished");
    QCOMPARE (completions.size (), 1);
    QCOMPARE (completions.first ().value ("period_end").toString (), QString {"001500"});

    QFile curves {directory.filePath ("red.dat")};
    QVERIFY2 (curves.open (QIODevice::ReadOnly), qPrintable (curves.errorString ()));
    auto const rows = curves.readAll ().trimmed ().split ('\n');
    QVERIFY (rows.size () > 1);
    for (auto const& row : rows)
      {
        auto const values = row.simplified ().split (' ');
        QCOMPARE (values.size (), 3);
        for (auto const& value : values)
          {
            bool ok;
            auto const number = value.toDouble (&ok);
            QVERIFY (ok && std::isfinite (number));
          }
      }
  }

  void wsjtCodecRejectsOversizedFrames_data ()
  {
    QTest::addColumn<quint32> ("declaredLength");
    QTest::newRow ("over capacity") << Jt9AudioCapacityBytes + 2u;
    QTest::newRow ("uint32 max") << std::numeric_limits<quint32>::max ();
  }

  void wsjtCodecRejectsOversizedFrames ()
  {
    QFETCH (quint32, declaredLength);
    auto const result = runJt9 (sessionHeader () +
                                declaredFrame (0x01u, declaredLength));
    verifyCompleted (result, 1);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error", "frame_too_large").size (), 1);
    QCOMPARE (eventsMatching (events.values, "decode").size (), 0);
  }

  void wsjtCodecRejectsUnknownMode_data ()
  {
    QTest::addColumn<QByteArray> ("configure");
    QTest::newRow ("string")
      << QByteArray {R"({"t":"configure","mode":"BOGUS","trperiod":1})"};
    QTest::newRow ("numeric")
      << QByteArray {R"({"t":"configure","mode":999,"trperiod":1})"};
  }

  void wsjtCodecRejectsUnknownMode ()
  {
    QFETCH (QByteArray, configure);
    auto const input = sessionHeader () + controlFrame (configure) +
                       silentAudioFrame (Jt9TenSecondPeriodSamples + 2);
    auto const result = runJt9 (input, {"-9", "-p", "10"});
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error", "unknown_mode").size (), 1);
    // The two samples past the 10 s period open the next period, drained at EOF.
    QCOMPARE (eventsMatching (events.values, "decode_finished").size (), 2);
  }

  void wsjtCodecRejectsOutOfRangeTrperiod_data ()
  {
    QTest::addColumn<QByteArray> ("configure");
    QTest::newRow ("zero")
      << QByteArray {R"({"t":"configure","trperiod":0})"};
    QTest::newRow ("above maximum")
      << QByteArray {R"({"t":"configure","trperiod":1801})"};
  }

  void wsjtCodecRejectsOutOfRangeTrperiod ()
  {
    QFETCH (QByteArray, configure);
    auto const result = runJt9 (sessionHeader () + controlFrame (configure) +
                                controlFrame (R"({"t":"halt"})"));
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error", "invalid_trperiod").size (), 1);
    QCOMPARE (eventsMatching (events.values, "decode").size (), 0);
  }

  void wsjtCodecWritesTheReadyLine ()
  {
    auto const result = runJt9 (sessionHeader ());
    verifyCompleted (result, 0);
    QCOMPARE (streamOutput (result), ndjson ({ReadyLine}));
  }

  // The header is checked in field order, before the ready line.
  void wsjtCodecRejectsBadSessionHeaders_data ()
  {
    QTest::addColumn<QByteArray> ("header");
    QTest::addColumn<QByteArray> ("line");
    auto const shortHeader = QByteArray {R"j({"v":1,"t":"error","msg":"short header (need 8 bytes)"})j"};
    auto const badMagic = QByteArray {R"j({"v":1,"t":"error","msg":"bad magic (expected 'WSJT')"})j"};
    auto const badFormat =
      QByteArray {R"j({"v":1,"t":"error","msg":"unsupported format (only fmt=0 int16 PCM supported)"})j"};
    auto const badChannels =
      QByteArray {R"j({"v":1,"t":"error","msg":"unsupported channel count (only mono supported)"})j"};
    auto const badRate =
      QByteArray {R"j({"v":1,"t":"error","msg":"unsupported sample rate (only 12 kHz supported)"})j"};
    QTest::newRow ("empty") << QByteArray {} << shortHeader;
    QTest::newRow ("seven bytes") << sessionHeader ().left (7) << shortHeader;
    QTest::newRow ("magic") << sessionHeaderWith (3, 'X') << badMagic;
    QTest::newRow ("format 1") << sessionHeaderWith (4, '\x01') << badFormat;
    QTest::newRow ("two channels") << sessionHeaderWith (5, '\x02') << badChannels;
    QTest::newRow ("no channels") << sessionHeaderWith (5, '\x00') << badChannels;
    QTest::newRow ("48 kHz") << sessionHeaderWith (6, '\x30') << badRate;
    QTest::newRow ("rate high byte") << sessionHeaderWith (7, '\x01') << badRate;
    QTest::newRow ("magic first") << QByteArray {"wsjt\x01\x02\x30\x00", 8} << badMagic;
    QTest::newRow ("format before channels") << QByteArray {"WSJT\x01\x02\x30\x00", 8} << badFormat;
    QTest::newRow ("channels before rate") << QByteArray {"WSJT\x00\x02\x30\x00", 8} << badChannels;
  }

  void wsjtCodecRejectsBadSessionHeaders ()
  {
    QFETCH (QByteArray, header);
    QFETCH (QByteArray, line);
    auto const result = runJt9 (header);
    verifyCompleted (result, 1);
    QCOMPARE (streamOutput (result), ndjson ({line}));
  }

  // An odd-length audio frame is reported and skipped whole: the audio
  // around it decodes as if it were absent.
  void wsjtCodecSkipsOddLengthAudioFrames_data ()
  {
    QTest::addColumn<int> ("length");
    QTest::newRow ("one byte") << 1;
    QTest::newRow ("over two drain blocks") << 2 * Jt9DrainBytes + 1;
  }

  void wsjtCodecSkipsOddLengthAudioFrames ()
  {
    QFETCH (int, length);
    QFile recording {QString::fromUtf8 (FT8_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const samples = recording.readAll ().mid (44);
    QCOMPARE (samples.size (), 15 * 12000 * 2);
    auto const configure = controlFrame (R"({"t":"configure","mode":"FT8","utc":"05:11:15"})");
    auto const head = frame (0x01u, samples.left (6 * 12000 * 2));
    auto const tail = frame (0x01u, samples.mid (6 * 12000 * 2));
    auto const plain = runJt9 (sessionHeader () + configure + head + tail);
    verifyCompleted (plain, 0);
    auto const output = streamOutput (plain);
    QVERIFY2 (output.contains (R"("message":"CQ K1JT FN20")"), output.constData ());
    auto const ready = ndjson ({ReadyLine});
    QVERIFY (output.startsWith (ready));
    auto const marked = runJt9 (sessionHeader () + configure + head +
                                frame (0x01u, QByteArray (length, '\x5a')) + tail);
    verifyCompleted (marked, 0);
    QCOMPARE (streamOutput (marked), ready + ndjson ({OddAudioFrameLine}) + output.mid (ready.size ()));
  }

  // A short read at end of input drops the chunk in flight, up to 4,095
  // samples; the samples before it are decoded.
  void wsjtCodecDropsTheChunkInFlightAtEndOfInput_data ()
  {
    QTest::addColumn<int> ("delivered");
    QTest::addColumn<int> ("kept");
    QTest::addColumn<int> ("periods");
    QTest::newRow ("within the first chunk") << Jt9ChunkSamples - 1 << 0 << 0;
    QTest::newRow ("after a chunk") << 2 * Jt9ChunkSamples - 1 << Jt9ChunkSamples << 1;
    QTest::newRow ("after a period")
      << Ft4PeriodSamples + Jt9ChunkSamples - 1 << Ft4PeriodSamples << 1;
  }

  void wsjtCodecDropsTheChunkInFlightAtEndOfInput ()
  {
    QFETCH (int, delivered);
    QFETCH (int, kept);
    QFETCH (int, periods);
    auto const truncated = runJt9 (sessionHeader () + declaredFrame (0x01u, 4 * Ft4PeriodSamples) +
                                   QByteArray (2 * delivered, '\0'), {"--ft4"});
    verifyCompleted (truncated, 0);
    auto const complete = runJt9 (
      sessionHeader () + (kept > 0 ? silentAudioFrame (kept) : QByteArray {}), {"--ft4"});
    verifyCompleted (complete, 0);
    QCOMPARE (streamOutput (truncated), streamOutput (complete));
    auto const events = verifyEvents (truncated);
    QCOMPARE (eventsMatching (events.values, "decode_finished").size (), periods);
  }

  // A control frame over 262,144 bytes is reported and skipped whole.
  void wsjtCodecSkipsOversizedControlFrames_data ()
  {
    QTest::addColumn<int> ("length");
    QTest::addColumn<QByteArray> ("line");
    QTest::newRow ("262144 bytes") << Jt9ControlCapacityBytes << QByteArray {InvalidTrperiodLine};
    QTest::newRow ("262145 bytes") << Jt9ControlCapacityBytes + 1 << QByteArray {ControlFrameTooLargeLine};
    QTest::newRow ("three drain blocks past the limit")
      << Jt9ControlCapacityBytes + 3 * Jt9DrainBytes + 1 << QByteArray {ControlFrameTooLargeLine};
  }

  void wsjtCodecSkipsOversizedControlFrames ()
  {
    QFETCH (int, length);
    QFETCH (QByteArray, line);
    QByteArray configure {R"({"t":"configure","trperiod":0)"};
    configure += QByteArray (length - configure.size () - 1, ' ') + '}';
    QCOMPARE (configure.size (), length);
    auto const result = runJt9 (sessionHeader () + controlFrame (configure) +
                                controlFrame (R"({"t":"configure","trperiod":0})"));
    verifyCompleted (result, 0);
    QCOMPARE (streamOutput (result), ndjson ({ReadyLine, line, InvalidTrperiodLine}));
  }

  // A frame longer than the sample buffer is fatal whatever its type; the
  // capacity itself is accepted.
  void wsjtCodecStopsOnFramesOverTheSampleBuffer_data ()
  {
    QTest::addColumn<quint8> ("type");
    QTest::addColumn<quint32> ("length");
    QTest::addColumn<QByteArray> ("lines");
    QTest::addColumn<int> ("exitCode");
    QTest::newRow ("audio at capacity") << quint8 {0x01u} << Jt9AudioCapacityBytes << QByteArray {} << 0;
    QTest::newRow ("odd audio over capacity")
      << quint8 {0x01u} << Jt9AudioCapacityBytes + 1u << ndjson ({FrameTooLargeLine}) << 1;
    QTest::newRow ("control at capacity")
      << quint8 {0x02u} << Jt9AudioCapacityBytes << ndjson ({ControlFrameTooLargeLine}) << 0;
    QTest::newRow ("control over capacity")
      << quint8 {0x02u} << Jt9AudioCapacityBytes + 1u << ndjson ({FrameTooLargeLine}) << 1;
    QTest::newRow ("unknown type over capacity")
      << quint8 {0x07u} << Jt9AudioCapacityBytes + 1u << ndjson ({FrameTooLargeLine}) << 1;
  }

  void wsjtCodecStopsOnFramesOverTheSampleBuffer ()
  {
    QFETCH (quint8, type);
    QFETCH (quint32, length);
    QFETCH (QByteArray, lines);
    QFETCH (int, exitCode);
    auto const result = runJt9 (sessionHeader () + declaredFrame (type, length));
    verifyCompleted (result, exitCode);
    QCOMPARE (streamOutput (result), ndjson ({ReadyLine}) + lines);
  }

  void wsjtCodecExitsWithItsInputOpen_data ()
  {
    QTest::addColumn<QByteArray> ("input");
    QTest::addColumn<int> ("exitCode");
    QTest::newRow ("halt") << QByteArray {sessionHeader () + controlFrame (R"({"t":"halt"})")} << 0;
    QTest::newRow ("frame too large")
      << QByteArray {sessionHeader () + declaredFrame (0x01u, Jt9AudioCapacityBytes + 2u)} << 1;
  }

  // The stream exits while its reader waits on input the producer holds open.
  void wsjtCodecExitsWithItsInputOpen ()
  {
    QFETCH (QByteArray, input);
    QFETCH (int, exitCode);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const result = runProcess (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
      {"-a", directory.path (), "-t", directory.path (), "--stream"}, input, directory.path (), 10000, false);
    verifyCompleted (result, exitCode);
  }

  // halt decodes the period in flight once and reads nothing after it.
  void wsjtCodecHaltsAfterThePeriodInFlight ()
  {
    auto const third = silentAudioFrame (Ft4PeriodSamples / 3);
    auto const halted = runJt9 (sessionHeader () + third + controlFrame (R"({"t":"halt"})") +
                                silentAudioFrame (Ft4PeriodSamples), {"--ft4"});
    verifyCompleted (halted, 0);
    auto const ended = runJt9 (sessionHeader () + third, {"--ft4"});
    verifyCompleted (ended, 0);
    QCOMPARE (streamOutput (halted), streamOutput (ended));
    QCOMPARE (eventsMatching (verifyEvents (halted).values, "decode_finished").size (), 1);
  }

  // A declined configure is one coded error line: version first, then key types, then the mode.
  void wsjtCodecDeclinesBadConfigureFrames_data ()
  {
    QTest::addColumn<QByteArray> ("configure");
    QTest::addColumn<QByteArray> ("line");
    auto const badVersion = QByteArray {R"({"v":1,"t":"error","code":"unknown_schema_version","got":2})"};
    QTest::newRow ("no t") << QByteArray {R"({"x":1})"} << QByteArray {
      R"j({"v":1,"t":"error","code":"configure_parse_error","detail":"malformed control frame (missing or invalid t field)"})j"};
    QTest::newRow ("version") << QByteArray {R"({"t":"configure","version":2})"} << badVersion;
    QTest::newRow ("type") << QByteArray {R"({"t":"configure","depth":"abc"})"} << QByteArray {
      R"({"v":1,"t":"error","code":"configure_type_error","key":"depth","expected":"int","got":"string"})"};
    QTest::newRow ("mode") << QByteArray {R"({"t":"configure","mode":"BOGUS"})"} << QByteArray {
      R"({"v":1,"t":"error","code":"unknown_mode","detail":"configure frame contains an unsupported mode"})"};
    QTest::newRow ("version first")
      << QByteArray {R"({"t":"configure","mode":"BOGUS","version":2,"depth":"abc"})"} << badVersion;
  }

  void wsjtCodecDeclinesBadConfigureFrames ()
  {
    QFETCH (QByteArray, configure);
    QFETCH (QByteArray, line);
    auto const result = runJt9 (sessionHeader () + controlFrame (configure));
    verifyCompleted (result, 0);
    QCOMPARE (streamOutput (result), ndjson ({ReadyLine, line}));
  }

  // A frame of an unknown type is skipped whole.
  void wsjtCodecSkipsUnknownFrameTypes_data ()
  {
    QTest::addColumn<int> ("length");
    QTest::newRow ("one byte") << 1;
    QTest::newRow ("over three drain blocks") << 3 * Jt9DrainBytes + 1;
  }

  void wsjtCodecSkipsUnknownFrameTypes ()
  {
    QFETCH (int, length);
    auto const result = runJt9 (sessionHeader () + frame (0x07u, QByteArray (length, '\x02')) +
                                controlFrame (R"({"t":"configure","trperiod":0})"));
    verifyCompleted (result, 0);
    QCOMPARE (streamOutput (result), ndjson ({ReadyLine, InvalidTrperiodLine}));
  }

  // A straddling frame carries its remainder into the next period.
  void wsjtCodecCarriesStraddlingFrameIntoNextPeriod ()
  {
    auto const exact = runJt9 (sessionHeader () +
                               silentAudioFrame (Ft4PeriodSamples), {"--ft4"});
    verifyCompleted (exact, 0);
    auto const exactEvents = verifyEvents (exact);
    QCOMPARE (eventsMatching (exactEvents.values, "decode_finished").size (), 1);

    auto const straddle = runJt9 (
      sessionHeader () + silentAudioFrame (Ft4PeriodSamples + 2), {"--ft4"});
    verifyCompleted (straddle, 0);
    auto const straddleEvents = verifyEvents (straddle);
    QCOMPARE (eventsMatching (straddleEvents.values, "decode_finished").size (), 2);

    // The decoder's share alone is not a period; the EOF drain reports it.
    auto const decoderOnly = runJt9 (
      sessionHeader () + silentAudioFrame (21 * 3456), {"--ft4"});
    verifyCompleted (decoderOnly, 0);
    auto const decoderOnlyEvents = verifyEvents (decoderOnly);
    QCOMPARE (eventsMatching (decoderOnlyEvents.values, "decode_finished").size (), 1);
  }

  // A configure that changes the period starts a new one; nothing of the
  // old period drains at the new length.
  void wsjtCodecStartsANewPeriodOnATrperiodChange ()
  {
    // Half a 10 s period, a change to 20 s, then exactly one 20 s period.
    auto const result = runJt9 (
      sessionHeader () +
      controlFrame (R"({"t":"configure","trperiod":10})") +
      silentAudioFrame (Jt9TenSecondPeriodSamples / 2) +
      controlFrame (R"({"t":"configure","trperiod":20})") +
      silentAudioFrame (2 * Jt9TenSecondPeriodSamples), {"-9"});
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "decode_finished").size (), 1);
  }

  // Repeating the current configuration mid-period leaves the period alone.
  void wsjtCodecKeepsThePeriodOnAnUnchangedConfigure ()
  {
    // Half a 10 s period, the same configuration again, then the other half
    // plus two samples: one full period, and the two samples open the next.
    auto const result = runJt9 (
      sessionHeader () +
      controlFrame (R"({"t":"configure","mode":"JT9","trperiod":10})") +
      silentAudioFrame (Jt9TenSecondPeriodSamples / 2) +
      controlFrame (R"({"t":"configure","mode":"JT9","trperiod":10})") +
      silentAudioFrame (Jt9TenSecondPeriodSamples / 2 + 2), {"-9"});
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "decode_finished").size (), 2);
  }

  // A discontinuity ends the period in flight undecoded, and the next sample
  // starts a new one: the GUI restarting its input at a period boundary.
  void discontinuityStartsANewPeriod ()
  {
    auto const recording = frame (0x01u, wavSamples (QString::fromUtf8 (FT8_ENGINE_WAV), 15 * 12000));
    auto const keys = configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"}});
    auto const lead = silentAudioFrame (5 * 12000);
    auto const discontinuity = controlFrame (R"({"t":"discontinuity"})");
    auto const alone = runJt9 (sessionHeader () + keys + recording);
    verifyCompleted (alone, 0);
    auto const events = verifyEvents (alone);
    QCOMPARE (eventsMatching (events.values, "decode_finished").size (), 1);
    QCOMPARE (eventsMatching (events.values, "decode").size (), 1);
    auto const marked = runJt9 (sessionHeader () + keys + lead + discontinuity + recording);
    verifyCompleted (marked, 0);
    QCOMPARE (marked.standardOutput, alone.standardOutput);
    // One at the start of a period changes nothing.
    auto const leading = runJt9 (sessionHeader () + keys + discontinuity + recording + discontinuity);
    verifyCompleted (leading, 0);
    QCOMPARE (leading.standardOutput, alone.standardOutput);
  }

  // flush decodes what the period in flight has, once, as halt does, and the
  // session goes on with a new period at the next sample.
  void flushDecodesThePeriodInFlight ()
  {
    auto const samples = wavSamples (QString::fromUtf8 (FT8_ENGINE_WAV), 15 * 12000);
    auto const keys = configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"}});
    auto const flush = controlFrame (R"({"t":"flush"})");
    // 13 s: past the first early pass, short of the period.
    auto const part = frame (0x01u, samples.left (2 * 13 * 12000));
    auto const halted = runJt9 (sessionHeader () + keys + part + controlFrame (R"({"t":"halt"})"));
    verifyCompleted (halted, 0);
    auto const flushed = runJt9 (sessionHeader () + keys + part + flush + configureFrame ({{"utc", "05:11:30"}})
                                 + frame (0x01u, samples));
    verifyCompleted (flushed, 0);
    QVERIFY2 (flushed.standardOutput.startsWith (halted.standardOutput), flushed.standardOutput.constData ());
    auto const events = verifyEvents (flushed);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    QStringList finished;
    for (auto const& event : eventsMatching (events.values, "decode_finished"))
      finished << event.value ("period_end").toString ();
    QCOMPARE (finished, (QStringList {"051115", "051130"}));
    auto const periods = decodesByPeriod (events.values);
    for (int p = 0; p < periods.size (); ++p)
      {
        QCOMPARE (periods.at (p).size (), 1);
        QCOMPARE (periods.at (p).first ().value ("message").toString (), QString {"CQ K1JT FN20"});
        QCOMPARE (periods.at (p).first ().value ("time").toString (), finished.at (p));
      }
    // With nothing in flight, at the start and after a whole period, it does nothing.
    auto const whole = runJt9 (sessionHeader () + keys + frame (0x01u, samples));
    verifyCompleted (whole, 0);
    auto const idle = runJt9 (sessionHeader () + keys + flush + frame (0x01u, samples) + flush);
    verifyCompleted (idle, 0);
    QCOMPARE (idle.standardOutput, whole.standardOutput);
    // The period after it starts at the sample after it, as the whole one does.
    auto const aligned = eventsMatching (verifyEvents (whole).values, "decode");
    QCOMPARE (aligned.size (), 1);
    QCOMPARE (periods.last ().first ().value ("dt").toDouble (), aligned.first ().value ("dt").toDouble ());
  }

  // Live, a flush is never refused for a busy decoder, as halt's decode is
  // not: here the audio is due already, and the flush comes in while the
  // early pass decodes.
  void flushDecodesEvenWhileBusy ()
  {
    auto const samples = wavSamples (QString::fromUtf8 (FT8_ENGINE_WAV), 13 * 12000);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QProcess process;
    process.setProcessChannelMode (QProcess::SeparateChannels);
    process.setWorkingDirectory (directory.path ());
    process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                   {"-a", directory.path (), "-t", directory.path (), "--stream"});
    QVERIFY (process.waitForStarted (5000));
    process.write (sessionHeader () + configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"}, {"live_audio", true}})
                   + frame (0x01u, samples.left (2)));
    QElapsedTimer clock;
    clock.start ();
    while (clock.elapsed () < 13500 && process.state () == QProcess::Running) process.waitForReadyRead (50);
    process.write (frame (0x01u, samples.mid (2)) + controlFrame (R"({"t":"flush"})"));
    process.closeWriteChannel ();
    QVERIFY (process.waitForFinished (120000));
    auto const output = process.readAllStandardOutput ();
    auto const error = process.readAllStandardError ();
    QVERIFY2 (!output.contains (R"("t":"pass_skipped")"), output.constData ());
    QVERIFY2 (error.contains ("pass nzhsym=50 "), error.constData ());
    QCOMPARE (output.count (R"("t":"decode_finished")"), 1);
  }

  // A decode again that waits for the period's last FT8 pass runs at a
  // discontinuity, which ends the period before that pass.
  void discontinuityRunsAWaitingDecodeAgain ()
  {
    auto const samples = wavSamples (QString::fromUtf8 (FT8_ENGINE_WAV), 15 * 12000);
    auto const result = runJt9 (sessionHeader () + configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"}})
      + frame (0x01u, samples) + configureFrame ({{"utc", "05:11:30"}}) + frame (0x01u, samples.left (2 * 42 * 3456))
      + configureFrame ({{"nagain_flag", true}}) + controlFrame (R"({"t":"discontinuity"})"));
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    QStringList finished;
    for (auto const& event : eventsMatching (events.values, "decode_finished"))
      finished << event.value ("period_end").toString ();
    QCOMPARE (finished, (QStringList {"051115", "051115"}));
  }

  // jt9 returns the lines of rx_messages.txt in its working directory as its
  // FT8 decodes; the stream sends them as decode events, else on stderr.
  void rxMessagesComeAsDecodeEvents ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QFile messages {directory.filePath ("rx_messages.txt")};
    QVERIFY (messages.open (QIODevice::WriteOnly));
    messages.write ("051115 -12  0.3 1200 ~  K1JT W9XYZ R-12\nnot a decode line\n");
    messages.close ();
    auto const result = runProcess (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
      {"-a", directory.path (), "-t", directory.path (), "--stream"},
      sessionHeader () + configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"}, {"contest_type", "fox"}})
        + frame (0x01u, wavSamples (QString::fromUtf8 (FT8_ENGINE_WAV), 15 * 12000)),
      directory.path ());
    verifyCompleted (result, 0);
    auto const decodes = eventsMatching (verifyEvents (result).values, "decode");
    QCOMPARE (decodes.size (), 1);
    QCOMPARE (decodes.first ().value ("message").toString (), QString {"K1JT W9XYZ R-12"});
    QCOMPARE (decodes.first ().value ("time").toString (), QString {"051115"});
    QCOMPARE (decodes.first ().value ("snr").toInt (), -12);
    QCOMPARE (decodes.first ().value ("freq").toInt (), 1200);
    QCOMPARE (float (decodes.first ().value ("dt").toDouble ()), 0.3f);
    QCOMPARE (decodes.first ().value ("pass").toInt (), 41);
    // The line jt9 prints is the file's line, trimmed: nothing after the message.
    QCOMPARE (decodes.first ().value ("text_width").toInt (), 15);
    QVERIFY (!decodes.first ().contains ("ap"));
    QVERIFY2 (result.standardError.contains ("not a decode line"), result.standardError.constData ());
  }

  // A period's label is the producer's UTC, whichever key and frame carried it.
  void wsjtCodecLabelsPeriodsWithTheProducersUtc_data ()
  {
    QTest::addColumn<QByteArray> ("configure");
    QTest::addColumn<int> ("samples");
    QTest::addColumn<QString> ("periodEnd");
    QTest::newRow ("FT8 05:11:15")
      << controlFrame (R"({"t":"configure","mode":"FT8","utc":"05:11:15"})") << 15 * 12000 << QString {"051115"};
    QTest::newRow ("FT8 00:45:15")
      << controlFrame (R"({"t":"configure","mode":"FT8","utc":"00:45:15"})") << 15 * 12000 << QString {"004515"};
    QTest::newRow ("JT9, utc, then FT8")
      << controlFrame (R"({"t":"configure","mode":"JT9"})") + controlFrame (R"({"t":"configure","utc":"05:11:15"})")
         + controlFrame (R"({"t":"configure","mode":"FT8"})")
      << 15 * 12000 << QString {"051115"};
    QTest::newRow ("FT8 nutc")
      << controlFrame (R"({"t":"configure","mode":"FT8","nutc":51115})") << 15 * 12000 << QString {"051115"};
    QTest::newRow ("JT9, nutc 1530, then FT8")
      << controlFrame (R"({"t":"configure","mode":"JT9"})") + controlFrame (R"({"t":"configure","nutc":1530})")
         + controlFrame (R"({"t":"configure","mode":"FT8"})")
      << 15 * 12000 << QString {"001530"};
    QTest::newRow ("FT8 nutc 0")
      << controlFrame (R"({"t":"configure","mode":"FT8","nutc":0})") << 15 * 12000 << QString {"000000"};
    QTest::newRow ("FT8 nutc 235959")
      << controlFrame (R"({"t":"configure","mode":"FT8","nutc":235959})") << 15 * 12000 << QString {"235959"};
    QTest::newRow ("FT4 05:11:15")
      << controlFrame (R"({"t":"configure","mode":"FT4","utc":"05:11:15"})") << Ft4PeriodSamples << QString {"051115"};
    QTest::newRow ("MSK144 05:11:15")
      << controlFrame (R"({"t":"configure","mode":"MSK144","trperiod":15,"utc":"05:11:15"})") << 15 * 12000
      << QString {"051115"};
    QTest::newRow ("FST4-15 05:11:15")
      << controlFrame (R"({"t":"configure","mode":"FST4","trperiod":15,"utc":"05:11:15"})") << 15 * 12000
      << QString {"051115"};
    QTest::newRow ("JT9 05:11:00")
      << controlFrame (R"({"t":"configure","mode":"JT9","utc":"05:11:00"})") << 60 * 12000 << QString {"051100"};
    QTest::newRow ("JT9 nutc HHMM")
      << controlFrame (R"({"t":"configure","mode":"JT9","nutc":511})") << 60 * 12000 << QString {"051100"};
    QTest::newRow ("JT9 nutc HHMMSS")
      << controlFrame (R"({"t":"configure","mode":"JT9","nutc":51100})") << 60 * 12000 << QString {"051100"};
    QTest::newRow ("JT9 nutc 2359")
      << controlFrame (R"({"t":"configure","mode":"JT9","nutc":2359})") << 60 * 12000 << QString {"235900"};
    // A minute mode decodes with HHMM, as the GUI gives it.
    QTest::newRow ("JT9 nutc 4515")
      << controlFrame (R"({"t":"configure","mode":"JT9","nutc":4515})") << 60 * 12000 << QString {"004500"};
    QTest::newRow ("JT9 utc and nutc in one frame")
      << controlFrame (R"({"t":"configure","mode":"JT9","utc":"00:15:00","nutc":1530})") << 60 * 12000
      << QString {"001500"};
    QTest::newRow ("utc 00:15:00, then JT9")
      << controlFrame (R"({"t":"configure","utc":"00:15:00"})") + controlFrame (R"({"t":"configure","mode":"JT9"})")
      << 60 * 12000 << QString {"001500"};
    QTest::newRow ("nutc HHMM, then JT9")
      << controlFrame (R"({"t":"configure","nutc":511})") + controlFrame (R"({"t":"configure","mode":"JT9"})")
      << 60 * 12000 << QString {"051100"};
    QTest::newRow ("JT9 utc, then nutc")
      << controlFrame (R"({"t":"configure","mode":"JT9","utc":"05:11:00"})")
         + controlFrame (R"({"t":"configure","nutc":512})")
      << 60 * 12000 << QString {"051200"};
    QTest::newRow ("JT65 05:11:00")
      << controlFrame (R"({"t":"configure","mode":"JT65","utc":"05:11:00"})") << 60 * 12000 << QString {"051100"};
    // The decoder's HHMM drops a utc's seconds.
    QTest::newRow ("JT9 05:11:30")
      << controlFrame (R"({"t":"configure","mode":"JT9","utc":"05:11:30"})") << 60 * 12000 << QString {"051100"};
    QTest::newRow ("JT65 05:11:30")
      << controlFrame (R"({"t":"configure","mode":"JT65","utc":"05:11:30"})") << 60 * 12000 << QString {"051100"};
    QTest::newRow ("JT65+JT9 05:11:30")
      << controlFrame (R"({"t":"configure","mode":"JT65+JT9","trperiod":60,"utc":"05:11:30"})") << 60 * 12000
      << QString {"051100"};
    QTest::newRow ("JT4 05:11:00")
      << controlFrame (R"({"t":"configure","mode":"JT4","utc":"05:11:00"})") << 60 * 12000 << QString {"051100"};
    QTest::newRow ("FST4-60 05:11:00")
      << controlFrame (R"({"t":"configure","mode":"FST4","trperiod":60,"utc":"05:11:00"})") << 60 * 12000
      << QString {"051100"};
    QTest::newRow ("FST4W-120 05:12:00")
      << controlFrame (R"({"t":"configure","mode":"FST4W","trperiod":120,"utc":"05:12:00"})") << 120 * 12000
      << QString {"051200"};
    QTest::newRow ("Q65-60 05:11:00")
      << controlFrame (R"({"t":"configure","mode":"Q65","trperiod":60,"utc":"05:11:00"})") << 60 * 12000
      << QString {"051100"};
  }

  void wsjtCodecLabelsPeriodsWithTheProducersUtc ()
  {
    QFETCH (QByteArray, configure);
    QFETCH (int, samples);
    QFETCH (QString, periodEnd);
    auto const result = runJt9 (sessionHeader () + configure + silentAudioFrame (samples));
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    auto const completions = eventsMatching (events.values, "decode_finished");
    QCOMPARE (completions.size (), 1);
    QCOMPARE (completions.first ().value ("period_end").toString (), periodEnd);
  }

  void ft8DecodeCarriesTheProducersUtc_data ()
  {
    QTest::addColumn<QString> ("utc");
    QTest::addColumn<QString> ("label");
    QTest::newRow ("05:11:15") << QString {"05:11:15"} << QString {"051115"};
    QTest::newRow ("00:45:15") << QString {"00:45:15"} << QString {"004515"};
  }

  void ft8DecodeCarriesTheProducersUtc ()
  {
    QFETCH (QString, utc);
    QFETCH (QString, label);
    QFile recording {QString::fromUtf8 (FT8_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const wav = recording.readAll ();
    QCOMPARE (wav.size (), 44 + 15 * 12000 * 2);
    QJsonObject configuration {{"t", "configure"}, {"mode", "FT8"}, {"utc", utc}};
    auto const result = runJt9 (sessionHeader () + controlFrame (
      QJsonDocument {configuration}.toJson (QJsonDocument::Compact)) + frame (0x01u, wav.mid (44)));
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    bool found = false;
    for (auto const& decode : eventsMatching (events.values, "decode"))
      {
        QCOMPARE (decode.value ("time").toString (), label);
        found |= decode.value ("message").toString ().trimmed () == "CQ K1JT FN20";
      }
    QVERIFY2 (found, result.standardOutput.constData ());
    auto const completions = eventsMatching (events.values, "decode_finished");
    QCOMPARE (completions.size (), 1);
    QCOMPARE (completions.first ().value ("period_end").toString (), label);
  }

  void wsjtCodecDeclinesValuesTheEngineRefuses_data ()
  {
    QTest::addColumn<QString> ("recording");
    QTest::addColumn<QJsonObject> ("accepted");
    QTest::addColumn<QJsonObject> ("refused");
    QTest::addColumn<QString> ("key");
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<QString> ("detail");
    QTest::addColumn<QString> ("message");
    QTest::addColumn<QStringList> ("options");
    QString const ft8 {QString::fromUtf8 (FT8_ENGINE_WAV)};
    QString const jt65 {QString::fromUtf8 (JT65_ENGINE_WAV)};
    QString const fst4 {QString::fromUtf8 (FST4_ENGINE_WAV)};
    QString const jt4 {QString::fromUtf8 (JT4_ENGINE_WAV)};
    QString const q65 {QString::fromUtf8 (Q65_ENGINE_WAV)};
    QJsonObject const q65Keys {{"mode", "Q65"}, {"trperiod", 15}, {"submode", 0}, {"depth_level", 1}, {"nfa", 1300},
                               {"nfb", 1700}, {"rxfreq", 1500}, {"utc", "00:15:00"}};
    QJsonObject const ft8Keys {{"mode", "FT8"}, {"utc", "05:11:15"}};
    QJsonObject const jt65Keys {{"mode", "JT65"}, {"trperiod", 60}, {"depth_level", 1}, {"nfa", 1300},
                                {"nfb", 1700}};
    QJsonObject const fst4Keys {{"mode", "FST4"}, {"trperiod", 15}, {"depth_level", 1}, {"nfa", 1300},
                                {"nfb", 1700}, {"rxfreq", 1500}, {"noise_blanker_level", 0},
                                {"utc", "00:15:00"}};
    QJsonObject const jt4Keys {{"mode", "JT4"}, {"trperiod", 60}, {"submode", 0}, {"depth_level", 1},
                               {"rxfreq", 1000}, {"ntol", 50}, {"utc", "12:34:00"}};
    QString const cq {"CQ K1JT FN20"};
    QString const k1abc {"K1ABC W9XYZ FN42"};
    QTest::newRow ("FT8 nfb 7000") << ft8 << ft8Keys << QJsonObject {{"nfb", 7000}}
      << QString {"nfb"} << QString {"FT8"} << QString {"nfb must be at most 6000 Hz in FT8"} << cq << QStringList {};
    QTest::newRow ("FT8 nfa above nfb") << ft8 << ft8Keys << QJsonObject {{"nfa", 3000}, {"nfb", 2000}}
      << QString {"nfa"} << QString {"FT8"} << QString {"nfa must not be above nfb in FT8"} << cq << QStringList {};
    QTest::newRow ("FT8 ft8_threads 25") << ft8 << ft8Keys << QJsonObject {{"ft8_threads", 25}}
      << QString {"ft8_threads"} << QString {"FT8"} << QString {"ft8_threads must be from 0 to 24 in FT8"} << cq << QStringList {};
    QTest::newRow ("FT8 qso_progress_state 99") << ft8 << ft8Keys << QJsonObject {{"qso_progress_state", 99}}
      << QString {"qso_progress_state"} << QString {"FT8"}
      << QString {"qso_progress_state must be from 0 to 5 in FT8"} << cq << QStringList {};
    QTest::newRow ("FT4 depth_level 4") << ft8 << ft8Keys << QJsonObject {{"mode", "FT4"}, {"depth_level", 4}}
      << QString {"depth_level"} << QString {"FT4"} << QString {"depth_level must be from 1 to 3 in FT4"} << cq << QStringList {};
    QTest::newRow ("JT9 rxfreq 6000") << ft8 << ft8Keys
      << QJsonObject {{"mode", "JT9"}, {"trperiod", 60}, {"rxfreq", 6000}}
      << QString {"rxfreq"} << QString {"JT9"} << QString {"rxfreq must be from 0 to 5000 Hz in JT9"} << cq << QStringList {};
    QTest::newRow ("JT65 n_trials 3000000") << jt65 << jt65Keys << QJsonObject {{"n_trials", 3000000}}
      << QString {"n_trials"} << QString {"JT65"} << QString {"n_trials must be at most 1000000 in JT65"} << k1abc << QStringList {};
    QTest::newRow ("JT65 rxfreq 6000") << jt65 << jt65Keys << QJsonObject {{"rxfreq", 6000}}
      << QString {"rxfreq"} << QString {"JT65"} << QString {"rxfreq must be from 0 to 5000 Hz in JT65"} << k1abc << QStringList {};
    QTest::newRow ("JT65 aggressive 12") << jt65 << jt65Keys << QJsonObject {{"aggressive", 12}}
      << QString {"aggressive"} << QString {"JT65"} << QString {"aggressive must be from 0 to 11 in JT65"} << k1abc
      << QStringList {};
    QTest::newRow ("JT65 after FT8's aggressive") << ft8
      << QJsonObject {{"mode", "FT8"}, {"utc", "05:11:15"}, {"aggressive", -1}} << jt65Keys
      << QString {"aggressive"} << QString {"JT65"}
      << QString {"aggressive must be from 0 to 11 in JT65; it was set before this frame"} << cq << QStringList {};
    QTest::newRow ("JT65 submode 3") << jt65 << jt65Keys << QJsonObject {{"submode", 3}}
      << QString {"submode"} << QString {"JT65"} << QString {"submode must be from 0 to 2 in JT65"} << k1abc << QStringList {};
    // An unpacked depth key alone leaves the depth level at 0.
    QTest::newRow ("JT65 use_averaging alone") << jt65 << jt65Keys << QJsonObject {{"use_averaging", true}}
      << QString {"depth_level"} << QString {"JT65"} << QString {"depth_level must be from 1 to 3 in JT65"}
      << k1abc << QStringList {};
    QTest::newRow ("JT65 after FT8's rxfreq") << ft8
      << QJsonObject {{"mode", "FT8"}, {"utc", "05:11:15"}, {"rxfreq", 5500}} << jt65Keys
      << QString {"rxfreq"} << QString {"JT65"}
      << QString {"rxfreq must be from 0 to 5000 Hz in JT65; it was set before this frame"} << cq << QStringList {};
    // JT65+JT9 runs JT65 when the transmit mode is JT9.
    QTest::newRow ("JT65+JT9 n_trials 3000000") << jt65
      << QJsonObject {{"mode", "JT65+JT9"}, {"tx_mode", "JT9"}, {"trperiod", 60}, {"depth_level", 1},
                      {"nfa", 1300}, {"nfb", 1700}}
      << QJsonObject {{"n_trials", 3000000}} << QString {"n_trials"} << QString {"JT65+JT9"}
      << QString {"n_trials must be at most 1000000 in JT65+JT9"} << k1abc << QStringList {};
    QTest::newRow ("Q65 trperiod 45") << q65 << q65Keys << QJsonObject {{"trperiod", 45}} << QString {"trperiod"} << QString {"Q65"}
      << QString {"trperiod must be one of 15, 30, 60, 120 or 300 s in Q65"} << k1abc << QStringList {};
    QTest::newRow ("FST4 noise_blanker_level 30") << fst4 << fst4Keys << QJsonObject {{"noise_blanker_level", 30}}
      << QString {"noise_blanker_level"} << QString {"FST4"}
      << QString {"noise_blanker_level must be at most 25 in FST4"} << k1abc << QStringList {};
    // With trperiod in the frame, the mode policy resets the delay and eme_delay_seconds wins.
    QTest::newRow ("FST4 eme_delay_seconds 20.5") << fst4 << fst4Keys
      << QJsonObject {{"trperiod", 15}, {"eme_delay_seconds", 20.5}} << QString {"eme_delay_seconds"}
      << QString {"FST4"} << QString {"eme_delay_seconds must be from 0 s to the TR period in FST4"} << k1abc << QStringList {};
    QTest::newRow ("JT4 min_width 1") << jt4 << jt4Keys << QJsonObject {{"min_width", 1}}
      << QString {"min_width"} << QString {"JT4"} << QString {"min_width must be from 0 to the submode in JT4"}
      << k1abc << QStringList {};
    QTest::newRow ("JT4 nutc 2360") << jt4 << jt4Keys << QJsonObject {{"nutc", 2360}}
      << QString {"nutc"} << QString {"JT4"} << QString {"nutc must be a valid time of day in JT4"} << k1abc << QStringList {};
    // Every label in force is a time of day, so no carried one is refused.
    QTest::newRow ("FT8 nutc 2360") << ft8 << ft8Keys << QJsonObject {{"nutc", 2360}}
      << QString {"nutc"} << QString {"FT8"} << QString {"nutc must be a valid time of day in FT8"} << cq << QStringList {};
    QTest::newRow ("JT65 nutc 250000") << jt65 << jt65Keys << QJsonObject {{"nutc", 250000}}
      << QString {"nutc"} << QString {"JT65"} << QString {"nutc must be a valid time of day in JT65"} << k1abc
      << QStringList {};
      // FST4 and Q65 clip nfb to 5000 Hz.
    QTest::newRow ("Q65 nfa above 5000") << q65 << q65Keys << QJsonObject {{"nfa", 5100}, {"nfb", 5500}}
      << QString {"nfa"} << QString {"Q65"} << QString {"nfa must be below nfb and below 5000 Hz in Q65"} << k1abc
      << QStringList {};
    // A change of mode restores nfa and nfb from the command line.
    QTest::newRow ("JT65 after the command line's nfb") << ft8 << ft8Keys
      << QJsonObject {{"mode", "JT65"}, {"trperiod", 60}} << QString {"nfb"} << QString {"JT65"}
      << QString {"nfb must be at most 5000 Hz in JT65; a change of mode or period restores it from the command line"}
      << cq << QStringList {"-H", "5500"};
  }

  // A configure the decoder would refuse is declined with the key that set
  // the value; the configure before it still decodes.
  void wsjtCodecDeclinesValuesTheEngineRefuses ()
  {
    if (!rangeTests ()) QSKIP ("run by test_streaming_configure_ranges");
    QFETCH (QString, recording);
    QFETCH (QJsonObject, accepted);
    QFETCH (QJsonObject, refused);
    QFETCH (QString, key);
    QFETCH (QString, mode);
    QFETCH (QString, detail);
    QFETCH (QString, message);
    QFETCH (QStringList, options);
    accepted.insert ("t", "configure");
    refused.insert ("t", "configure");
    auto const result = runRecording (recording, controlFrame (
      QJsonDocument {accepted}.toJson (QJsonDocument::Compact)) + controlFrame (
      QJsonDocument {refused}.toJson (QJsonDocument::Compact)), options);
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    auto const errors = eventsMatching (events.values, "error");
    QCOMPARE (errors.size (), 1);
    QCOMPARE (errors.first ().value ("code").toString (), QString {"configure_range_error"});
    QCOMPARE (errors.first ().value ("key").toString (), key);
    QCOMPARE (errors.first ().value ("mode").toString (), mode);
    QCOMPARE (errors.first ().value ("detail").toString (), detail);
    bool found = false;
    for (auto const& decode : eventsMatching (events.values, "decode"))
      {
        found |= decode.value ("message").toString ().trimmed () == message;
      }
    QVERIFY2 (found, result.standardOutput.constData ());
  }

  // A label that is not a time of day declines its whole frame, in any mode.
  void wsjtCodecDeclinesBadLabels_data ()
  {
    QTest::addColumn<QByteArray> ("refused");
    QTest::addColumn<QByteArray> ("line");
    auto const range = [] (QString const& mode, QString const& key = "utc") {
      return QString {"{\"v\":1,\"t\":\"error\",\"code\":\"configure_range_error\",\"key\":\"%2\",\"mode\":\"%1\","
                      "\"detail\":\"%2 must be a valid time of day in %1\"}"}.arg (mode, key).toUtf8 ();
    };
    QByteArray const utcType {R"({"v":1,"t":"error","code":"configure_type_error","key":"utc","expected":"HH:MM:SS","got":"string"})"};
    QTest::newRow ("utc 24:00:00, JT9")
      << QByteArray {R"({"t":"configure","mode":"JT9","trperiod":60,"utc":"24:00:00"})"} << range ("JT9");
    QTest::newRow ("utc 12:60:00, Q65")
      << QByteArray {R"({"t":"configure","mode":"Q65","trperiod":60,"utc":"12:60:00"})"} << range ("Q65");
    QTest::newRow ("utc 12:00:60") << QByteArray {R"({"t":"configure","utc":"12:00:60"})"} << range ("FT8");
    QTest::newRow ("utc 13:34:45Z, JT9")
      << QByteArray {R"({"t":"configure","mode":"JT9","trperiod":60,"utc":"13:34:45Z"})"} << utcType;
    QTest::newRow ("utc 13:34") << QByteArray {R"({"t":"configure","utc":"13:34"})"} << utcType;
    QTest::newRow ("utc 1:2:3") << QByteArray {R"({"t":"configure","utc":"1:2:3"})"} << utcType;
    QTest::newRow ("utc 12:00:00, then 125 blanks and junk")
      << QByteArray {"{\"t\":\"configure\",\"utc\":\"12:00:00"} + QByteArray (125, ' ') + QByteArray {"junk\"}"}
      << utcType;
    QTest::newRow ("nutc 133445.0, JT9")
      << QByteArray {R"({"t":"configure","mode":"JT9","trperiod":60,"nutc":133445.0})"}
      << QByteArray {R"({"v":1,"t":"error","code":"configure_type_error","key":"nutc","expected":"int","got":"number"})"};
    QTest::newRow ("nutc -1") << QByteArray {R"({"t":"configure","nutc":-1})"} << range ("FT8", "nutc");
    QTest::newRow ("nutc 240000, JT9")
      << QByteArray {R"({"t":"configure","mode":"JT9","trperiod":60,"nutc":240000})"} << range ("JT9", "nutc");
    QTest::newRow ("nutc 1260, JT9")
      << QByteArray {R"({"t":"configure","mode":"JT9","trperiod":60,"nutc":1260})"} << range ("JT9", "nutc");
    QTest::newRow ("nutc 126000") << QByteArray {R"({"t":"configure","nutc":126000})"} << range ("FT8", "nutc");
    // One error: the engine's own Q65 check never sees the frame.
    QTest::newRow ("nutc 235960, Q65")
      << QByteArray {R"({"t":"configure","mode":"Q65","trperiod":60,"nutc":235960})"} << range ("Q65", "nutc");
    // JTTY's range errors carry no mode.
    QTest::newRow ("nutc -1, JTTY") << QByteArray {R"({"t":"configure","mode":"JTTY","nutc":-1})"}
      << QByteArray {R"({"v":1,"t":"error","code":"configure_range_error","key":"nutc","detail":"nutc must be a valid time of day"})"};
  }

  void wsjtCodecDeclinesBadLabels ()
  {
    if (!rangeTests ()) QSKIP ("run by test_streaming_configure_ranges");
    QFETCH (QByteArray, refused);
    QFETCH (QByteArray, line);
    auto const result = runRecording (QString::fromUtf8 (FT8_ENGINE_WAV),
      configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"}}) + controlFrame (refused));
    verifyCompleted (result, 0);
    QList<QByteArray> errors;
    for (auto const& output : streamOutput (result).split ('\n'))
      {
        if (output.contains (R"("t":"error")")) errors << output;
      }
    QCOMPARE (errors, QList<QByteArray> {line});
    // The period decodes as FT8, under the label before the refused frame.
    auto const decodes = eventsMatching (verifyEvents (result).values, "decode");
    bool found = false;
    for (auto const& decode : decodes)
      {
        QCOMPARE (decode.value ("time").toString (), QString {"051115"});
        found |= decode.value ("message").toString ().trimmed () == "CQ K1JT FN20";
      }
    QVERIFY2 (found, result.standardOutput.constData ());
  }

  void wsjtCodecAcceptsValuesAtTheEngineLimits_data ()
  {
    QTest::addColumn<QString> ("recording");
    QTest::addColumn<QJsonObject> ("keys");
    QTest::addColumn<QString> ("message");
    QString const fst4 {QString::fromUtf8 (FST4_ENGINE_WAV)};
    QJsonObject fst4Keys {{"mode", "FST4"}, {"trperiod", 15}, {"depth_level", 1}, {"nfa", 1300}, {"nfb", 1700},
                          {"rxfreq", 1500}, {"utc", "00:15:00"}};
    QTest::newRow ("FT8") << QString::fromUtf8 (FT8_ENGINE_WAV)
      << QJsonObject {{"mode", "FT8"}, {"utc", "05:11:15"}, {"nfb", 6000}, {"ft8_threads", 24},
                      {"qso_progress_state", 5}}
      << QString {"CQ K1JT FN20"};
    QTest::newRow ("JT65") << QString::fromUtf8 (JT65_ENGINE_WAV)
      << QJsonObject {{"mode", "JT65"}, {"trperiod", 60}, {"depth_level", 1}, {"nfa", 1300}, {"nfb", 1700},
                      {"n_trials", 1000000}, {"aggressive", 11}}
      << QString {"K1ABC W9XYZ FN42"};
    // The rows below need no decode: the recording does not suit the setting.
    auto blanker = fst4Keys;
    blanker.insert ("noise_blanker_level", 25);
    QTest::newRow ("FST4 noise_blanker_level 25") << fst4 << blanker << QString {};
    auto delay = fst4Keys;
    delay.insert ("eme_delay_seconds", 15.0);
    QTest::newRow ("FST4 eme_delay_seconds 15") << fst4 << delay << QString {};
    // JT65+JT9 runs only JT9 unless the transmit mode is JT9, and JT9 takes no trials.
    QTest::newRow ("JT65+JT9 n_trials 3000000") << QString::fromUtf8 (JT65_ENGINE_WAV)
      << QJsonObject {{"mode", "JT65+JT9"}, {"trperiod", 60}, {"depth_level", 1}, {"nfa", 1300}, {"nfb", 1700},
                      {"n_trials", 3000000}}
      << QString {};
    QTest::newRow ("JT9 rxfreq 5000") << QString::fromUtf8 (FT8_ENGINE_WAV)
      << QJsonObject {{"mode", "JT9"}, {"trperiod", 60}, {"rxfreq", 5000}} << QString {};
    QTest::newRow ("Q65 trperiod 60") << QString::fromUtf8 (Q65_ENGINE_WAV)
      << QJsonObject {{"mode", "Q65"}, {"trperiod", 60}, {"submode", 0}, {"depth_level", 1}, {"nfa", 1300},
                      {"nfb", 1700}, {"rxfreq", 1500}, {"utc", "00:15:00"}}
      << QString {};
    QTest::newRow ("Q65 trperiod 300") << QString::fromUtf8 (Q65_ENGINE_WAV)
      << QJsonObject {{"mode", "Q65"}, {"trperiod", 300}, {"submode", 0}, {"depth_level", 1}, {"nfa", 1300},
                      {"nfb", 1700}, {"rxfreq", 1500}, {"utc", "00:15:00"}}
      << QString {};
  }

  void wsjtCodecAcceptsValuesAtTheEngineLimits ()
  {
    if (!rangeTests ()) QSKIP ("run by test_streaming_configure_ranges");
    QFETCH (QString, recording);
    QFETCH (QJsonObject, keys);
    QFETCH (QString, message);
    keys.insert ("t", "configure");
    auto const result = runRecording (recording, controlFrame (
      QJsonDocument {keys}.toJson (QJsonDocument::Compact)));
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    if (message.isEmpty ())
      {
        return;
      }
    bool found = false;
    for (auto const& decode : eventsMatching (events.values, "decode"))
      {
        found |= decode.value ("message").toString ().trimmed () == message;
      }
    QVERIFY2 (found, result.standardOutput.constData ());
  }

  void wsjtCodecReportsRefusedDecodes_data ()
  {
    QTest::addColumn<QString> ("recording");
    QTest::addColumn<QStringList> ("options");
    QTest::addColumn<QString> ("key");
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<QString> ("detail");
    QTest::addColumn<QString> ("field");
    QTest::addColumn<QString> ("rule");
    QTest::newRow ("JT65 at -d 4") << QString::fromUtf8 (JT65_ENGINE_WAV) << QStringList {"-6", "-d", "4"}
      << QString {"depth"} << QString {"JT65"}
      << QString {"depth must hold a decoding depth from 1 to 3 in JT65; the command line set it"}
      << QString {"depth"} << QString {"depth must be from 1 to 3"};
    QTest::newRow ("FT8 at -L 3000 -H 2000") << QString::fromUtf8 (FT8_ENGINE_WAV)
      << QStringList {"-L", "3000", "-H", "2000"} << QString {"nfa"} << QString {"FT8"}
      << QString {"nfa must not be above nfb in FT8; the command line set it"}
      << QString {"search_range"} << QString {"nfa must not be above nfb"};
  }

  // A command-line starting value the decoder refuses is reported at the
  // start, and each decode it refuses is an error event on stdout and a line
  // on stderr.
  void wsjtCodecReportsRefusedDecodes ()
  {
    if (!rangeTests ()) QSKIP ("run by test_streaming_configure_ranges");
    QFETCH (QString, recording);
    QFETCH (QStringList, options);
    QFETCH (QString, key);
    QFETCH (QString, mode);
    QFETCH (QString, detail);
    QFETCH (QString, field);
    QFETCH (QString, rule);
    auto const result = runRecording (recording, {}, options);
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "decode").size (), 0);
    auto const errors = eventsMatching (events.values, "error");
    QVERIFY2 (errors.size () >= 2, result.standardOutput.constData ());
    QCOMPARE (errors.first ().value ("code").toString (), QString {"configure_range_error"});
    QCOMPARE (errors.first ().value ("key").toString (), key);
    QCOMPARE (errors.first ().value ("mode").toString (), mode);
    QCOMPARE (errors.first ().value ("detail").toString (), detail);
    for (int i = 1; i < errors.size (); ++i)
      {
        QCOMPARE (errors[i].value ("code").toString (), QString {"decode_rejected"});
        QCOMPARE (errors[i].value ("operation").toString (), QString {"decode"});
        QCOMPARE (errors[i].value ("status").toString (), QString {"invalid"});
        QCOMPARE (errors[i].value ("mode").toString (), mode);
        QCOMPARE (errors[i].value ("field").toString (), field);
        QCOMPARE (errors[i].value ("detail").toString (), rule);
      }
    QVERIFY2 (result.standardError.contains ("<DecodeRejected> status=invalid"), result.standardError.constData ());
  }

  void wsjtCodecKeepsStdoutJsonWithMultithreadedFt8_data ()
  {
    QTest::addColumn<QJsonObject> ("keys");
    QTest::addColumn<QByteArray> ("warning");
    QTest::addColumn<bool> ("decodes");
    // The working directory has no ALLCALL7.TXT.
    QTest::newRow ("no call database") << QJsonObject {}
      << QByteArray {"ALLCALL7.TXT is too short or broken?"} << true;
    QTest::newRow ("partial period") << QJsonObject {{"delay_units", 2}}
      << QByteArray {"partial loss of data"} << true;
    QTest::newRow ("rx frequency outside the band")
      << QJsonObject {{"nagain_filter", true}, {"rxfreq", 4500}}
      << QByteArray {"nfqso is out of bandwidth"} << false;
  }

  // The multithreaded decoder's warnings go to stderr, not into the event stream.
  void wsjtCodecKeepsStdoutJsonWithMultithreadedFt8 ()
  {
    QFETCH (QJsonObject, keys);
    QFETCH (QByteArray, warning);
    QFETCH (bool, decodes);
    QFile recording {QString::fromUtf8 (FT8_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const wav = recording.readAll ();
    QJsonObject configuration {{"t", "configure"}, {"mode", "FT8"}, {"utc", "05:11:15"},
                               {"multithreaded_ft8", true}, {"ft8_threads", 2}};
    for (auto key = keys.begin (); key != keys.end (); ++key)
      {
        configuration.insert (key.key (), key.value ());
      }
    auto const result = runJt9 (sessionHeader () + controlFrame (
      QJsonDocument {configuration}.toJson (QJsonDocument::Compact)) + frame (0x01u, wav.mid (44)));
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    bool found = false;
    for (auto const& decode : eventsMatching (events.values, "decode"))
      {
        found |= decode.value ("message").toString ().trimmed () == "CQ K1JT FN20";
      }
    QVERIFY2 (found == decodes, result.standardOutput.constData ());
    QVERIFY2 (result.standardError.contains (warning), result.standardError.constData ());
  }

  // SuperFox Hound decodes are decode events, and single-threaded whatever multithreaded_ft8 says.
  void superFoxHoundIgnoresMultithreadedFt8 ()
  {
#ifndef SUPERFOX_WAV
    QSKIP ("sfoxsim is built only with WSJT_BUILD_UTILS");
#else
    QFile recording {QString::fromUtf8 (SUPERFOX_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const audio = frame (0x01u, recording.readAll ().mid (44));
    QJsonObject hound {{"t", "configure"}, {"mode", "FT8"}, {"utc", "00:00:30"},
                       {"contest_type", "hound"}, {"superfox_mode", true},
                       {"even_sequence", true}, {"rxfreq", 750}};
    auto const single = runJt9 (sessionHeader () + controlFrame (
      QJsonDocument {hound}.toJson (QJsonDocument::Compact)) + audio);
    hound.insert ("multithreaded_ft8", true);
    hound.insert ("ft8_threads", 2);
    auto const multi = runJt9 (sessionHeader () + controlFrame (
      QJsonDocument {hound}.toJson (QJsonDocument::Compact)) + audio);
    verifyCompleted (single, 0);
    verifyCompleted (multi, 0);
    QStringList messages;
    for (auto const& decode : eventsMatching (verifyEvents (single).values, "decode"))
      {
        QCOMPARE (decode.value ("time").toString (), QString {"000030"});
        messages << decode.value ("message").toString ().trimmed ();
      }
    messages.sort ();
    QCOMPARE (messages, (QStringList {"$VERIFY$ K1JT 000000", "W0AAA K1JT RR73", "W1BBB K1JT RR73",
                                      "W2CCC K1JT RR73", "W3DDD K1JT RR73", "W4EEE K1JT RR73",
                                      "W5FFF K1JT -18"}));
    QCOMPARE (streamOutput (multi), streamOutput (single));
#endif
  }

  void multithreadedFt8SkipsOnlySuperFoxHound_data ()
  {
    QTest::addColumn<QList<QJsonObject>> ("periods");
    QTest::addColumn<bool> ("multithreaded");
    QJsonObject const hound {{"contest_type", "hound"}, {"superfox_mode", true}, {"even_sequence", true}};
    QTest::newRow ("SuperFox Hound") << QList<QJsonObject> {hound} << false;
    // On its even sequence the GUI's SuperFox Fox decodes nothing before half-symbol 50.
    QTest::newRow ("SuperFox Fox") << QList<QJsonObject> {
      {{"contest_type", "fox"}, {"superfox_mode", true}, {"even_sequence", false}}} << true;
    QTest::newRow ("Hound without SuperFox") << QList<QJsonObject> {{{"contest_type", "hound"}}} << true;
    QTest::newRow ("after SuperFox Hound") << QList<QJsonObject> {
      hound, {{"contest_type", "none"}}} << true;
  }

  // Only SuperFox Hound sets multithreading aside; the multithreaded decoder's
  // missing-ALLCALL7.TXT warning shows whether it ran.
  void multithreadedFt8SkipsOnlySuperFoxHound ()
  {
#ifndef SUPERFOX_WAV
    QSKIP ("sfoxsim is built only with WSJT_BUILD_UTILS");
#else
    QFETCH (QList<QJsonObject>, periods);
    QFETCH (bool, multithreaded);
    QFile recording {QString::fromUtf8 (SUPERFOX_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const audio = frame (0x01u, recording.readAll ().mid (44));
    QByteArray input = sessionHeader () + controlFrame (R"({"t":"configure","mode":"FT8","utc":"00:00:00","rxfreq":750,"multithreaded_ft8":true,"ft8_threads":2})");
    for (auto keys : periods)
      {
        keys.insert ("t", "configure");
        input += controlFrame (QJsonDocument {keys}.toJson (QJsonDocument::Compact)) + audio;
      }
    auto const result = runJt9 (input);
    verifyCompleted (result, 0);
    QCOMPARE (result.standardError.contains ("ALLCALL7.TXT is too short or broken?"), multithreaded);
#endif
  }

  void multithreadedFt8MatchesWavMode_data ()
  {
    QTest::addColumn<int> ("decoderStart");
    QTest::addColumn<int> ("depth");
    QTest::addColumn<int> ("delayMs");
    // Settings 0 and 1 run single-threaded passes first; at depth 3 those decode too.
    QTest::newRow ("start 0, depth 3") << 0 << 3 << 0;
    QTest::newRow ("start 1, depth 3") << 1 << 3 << 0;
    // With a one-second delay an hsym-48 final pass loses a message that passes at 49 and 50 keep.
    QTest::newRow ("start 2, delayed") << 2 << 1 << 1000;
    QTest::newRow ("start 3") << 3 << 1 << 0;
    QTest::newRow ("start 3, delayed") << 3 << 1 << 1000;
    QTest::newRow ("start 4, delayed") << 4 << 1 << 1000;
  }

  // The stream's multithreaded FT8 decodes what jt9 decodes from the same WAV.
  void multithreadedFt8MatchesWavMode ()
  {
    auto const sample = qEnvironmentVariable ("WSJT_FT8_SAMPLE_WAV");
    if (sample.isEmpty ())
      {
        QSKIP ("run by test_streaming_mtd_parity");
      }
    QFETCH (int, decoderStart);
    QFETCH (int, depth);
    QFETCH (int, delayMs);
    QFile recording {sample};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const wav = recording.readAll ();
    auto const pcm = wav.mid (44);
    auto const delayed = (QByteArray (delayMs * 12 * 2, '\0') + pcm).left (pcm.size ());
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QFile input {directory.filePath ("210703_133430.wav")};
    QVERIFY (input.open (QIODevice::WriteOnly));
    input.write (wav.left (44) + delayed);
    input.close ();
    QStringList const paths {"-a", directory.path (), "-t", directory.path (),
                             "-r", qEnvironmentVariable ("WSJT_SHARE_DIR")};
    auto const wavMode = runProcess (QString::fromUtf8 (JT9_EXECUTABLE),
      paths + QStringList {"-8", "-d", QString::number (depth), "-M", "-N", "4",
                           "-D", QString::number (decoderStart), input.fileName ()},
      {}, directory.path (), 120000);
    verifyCompleted (wavMode, 0);
    QJsonObject configuration {{"t", "configure"}, {"mode", "FT8"}, {"utc", "13:34:30"},
                               {"depth", depth}, {"multithreaded_ft8", true},
                               {"ft8_threads", 4}, {"ft8_decoder_start", decoderStart}};
    auto const stream = runProcess (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
      paths + QStringList {"--stream"},
      sessionHeader () + controlFrame (QJsonDocument {configuration}.toJson (QJsonDocument::Compact)) +
        frame (0x01u, delayed),
      directory.path (), 120000);
    verifyCompleted (stream, 0);
    auto const events = verifyEvents (stream);
    QStringList streamed;
    for (auto const& decode : eventsMatching (events.values, "decode"))
      {
        streamed << decode.value ("message").toString ().trimmed ();
      }
    streamed.sort ();
    auto const expected = wavModeFt8Messages (wavMode.standardOutput);
    QVERIFY2 (!expected.isEmpty (), wavMode.standardOutput.constData ());
    QCOMPARE (streamed, expected);
  }

  // Each period is a new decoder input, as in the GUI: a period whose only FT8
  // pass is its final decodes its own audio, not what the last one left, also
  // when the producer sends the time only once.
  void aFinalAloneDecodesItsOwnPeriod ()
  {
    auto const samples = wavSamples (QString::fromUtf8 (FT8_ENGINE_WAV), 15 * 12000);
    QCOMPARE (samples.size (), 2 * 15 * 12000);
    auto const audio = frame (0x01u, samples);
    auto const result = runJt9 (sessionHeader () + configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"},
        {"depth", 3}, {"contest_type", "fox"}, {"superfox_mode", true}, {"even_sequence", false},
        {"live_audio", true}}) + audio
      // Live, SuperFox's even sequence runs only the final.
      + configureFrame ({{"even_sequence", true}}) + audio, {}, 120000);
    verifyCompleted (result, 0);
    QStringList ran;
    for (auto const& pass : passes (result.standardError)) ran << QString::number (pass.nzhsym);
    QCOMPARE (ran.join (' '), QString {"41 47 50 50"});
    auto const periods = decodesByPeriod (verifyEvents (result).values);
    QCOMPARE (periods.size (), 2);
    for (auto const& decodes : periods)
      {
        QCOMPARE (decodes.size (), 1);
        QCOMPARE (decodes.first ().value ("message").toString ().trimmed (), QString {"CQ K1JT FN20"});
        QCOMPARE (decodes.first ().value ("time").toString (), QString {"051115"});
      }
  }

  void decodeAgainDecodesAtTheRxFrequency_data ()
  {
    QTest::addColumn<QJsonObject> ("first");
    QTest::addColumn<QJsonObject> ("again");
    QTest::addColumn<bool> ("found");
    // The single-threaded decode again searches 20 Hz either side; the signal is at 1500 Hz.
    QTest::newRow ("at the signal") << QJsonObject {} << QJsonObject {{"rxfreq", 1500}} << true;
    QTest::newRow ("away from the signal") << QJsonObject {} << QJsonObject {{"rxfreq", 1000}} << false;
    QTest::newRow ("single-threaded after a multithreaded period")
      << QJsonObject {{"multithreaded_ft8", true}, {"ft8_threads", 2}}
      << QJsonObject {{"multithreaded_ft8", false}, {"rxfreq", 1500}} << true;
  }

  // nagain_flag decodes the last period once more, as the GUI's Decode button does.
  void decodeAgainDecodesAtTheRxFrequency ()
  {
    QFETCH (QJsonObject, first);
    QFETCH (QJsonObject, again);
    QFETCH (bool, found);
    QFile recording {QString::fromUtf8 (FT8_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    first.insert ("mode", "FT8");
    first.insert ("utc", "05:11:15");
    again.insert ("nagain_flag", true);
    // The initial decode and retry share this budget, including sanitizer overhead.
    auto const result = runJt9 (sessionHeader () + configureFrame (first) +
      frame (0x01u, recording.readAll ().mid (44)) + configureFrame (again), {}, 180000);
    verifyCompleted (result, 0);
    auto const periods = decodesByPeriod (verifyEvents (result).values);
    QCOMPARE (periods.size (), 2);
    QCOMPARE (periods.first ().size (), 1);
    QCOMPARE (periods.last ().size (), found ? 1 : 0);
    for (auto const& decode : periods.last ())
      {
        QCOMPARE (decode.value ("message").toString ().trimmed (), QString {"CQ K1JT FN20"});
        QCOMPARE (decode.value ("time").toString (), QString {"051115"});
      }
  }

  void decodeAgainNeedsADecodedPeriod_data ()
  {
    QTest::addColumn<QByteArray> ("before");
    QByteArray const period = sessionHeader () + configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"}});
    QTest::newRow ("no period yet") << period;
    QFile recording {QString::fromUtf8 (FT8_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    // A mode change discards the decoded period with the one in flight.
    QTest::newRow ("after a mode change") << period + frame (0x01u, recording.readAll ().mid (44)) +
      configureFrame ({{"mode", "FT4"}}) + configureFrame ({{"mode", "FT8"}});
  }

  // Without a decoded period nagain_flag is refused, and it never carries into the next decode.
  void decodeAgainNeedsADecodedPeriod ()
  {
    QFETCH (QByteArray, before);
    QFile recording {QString::fromUtf8 (FT8_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const audio = frame (0x01u, recording.readAll ().mid (44));
    auto const reference = runJt9 (sessionHeader () +
      configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"}}) + audio);
    verifyCompleted (reference, 0);
    auto const expected = decodesByPeriod (verifyEvents (reference).values);
    QCOMPARE (expected.size (), 1);
    auto const result = runJt9 (before + configureFrame ({{"nagain_flag", true}}) + audio);
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error", "decode_again_unavailable").size (), 1);
    auto const periods = decodesByPeriod (events.values);
    QVERIFY (!periods.isEmpty ());
    QCOMPARE (periods.last (), expected.first ());
  }

  // MSK144 decodes in its own pass and keeps no period to decode again.
  void decodeAgainKeepsNoMsk144Period ()
  {
    auto const result = runJt9 (sessionHeader () + configureFrame ({{"mode", "MSK144"}, {"trperiod", 15}}) +
      silentAudioFrame (15 * 12000) + configureFrame ({{"nagain_flag", true}}));
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result).values;
    QCOMPARE (eventsMatching (events, "decode_finished").size (), 1);
    QCOMPARE (eventsMatching (events, "error", "decode_again_unavailable").size (), 1);
  }

  // The decode again keeps the decoded period's sequence: SuperFox Hound decodes
  // only even sequences, and the request comes in the next, odd, period, which
  // stays odd.
  void decodeAgainKeepsTheSuperFoxSequence ()
  {
#ifndef SUPERFOX_WAV
    QSKIP ("sfoxsim is built only with WSJT_BUILD_UTILS");
#else
    QFile recording {QString::fromUtf8 (SUPERFOX_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const audio = frame (0x01u, recording.readAll ().mid (44));
    auto const result = runJt9 (sessionHeader () +
      configureFrame ({{"mode", "FT8"}, {"utc", "00:00:30"}, {"contest_type", "hound"},
                       {"superfox_mode", true}, {"even_sequence", true}, {"rxfreq", 750}}) + audio +
      configureFrame ({{"utc", "00:00:45"}, {"even_sequence", false}, {"nagain_flag", true}}) + audio);
    verifyCompleted (result, 0);
    auto const periods = decodesByPeriod (verifyEvents (result).values);
    QCOMPARE (periods.size (), 3);
    QCOMPARE (periods.at (0).size (), 7);
    QCOMPARE (periods.at (1).size (), 7);
    for (auto const& decode : periods.at (1))
      {
        QCOMPARE (decode.value ("time").toString (), QString {"000030"});
      }
    QCOMPARE (periods.at (2).size (), 0);
#endif
  }

  void decodeAgainKeepsTheTimeForm_data ()
  {
    QTest::addColumn<QJsonObject> ("kept");
    QTest::addColumn<QJsonObject> ("request");
    QTest::addColumn<QStringList> ("labels");
    // A numeric nutc under 2400 is HHMM in a 60 s mode, an ISO utc HHMMSS.
    QTest::newRow ("numeric kept, ISO request") << QJsonObject {{"nutc", 1234}}
      << QJsonObject {{"utc", "00:12:00"}} << QStringList {"123400", "123400", "001200"};
    QTest::newRow ("ISO kept, numeric request") << QJsonObject {{"utc", "00:12:00"}}
      << QJsonObject {{"nutc", 13}} << QStringList {"001200", "001200", "001300"};
  }

  // The decode again labels with the decoded period's time form, though the
  // request carries the other one; the next period takes the request's form.
  void decodeAgainKeepsTheTimeForm ()
  {
    QFETCH (QJsonObject, kept);
    QFETCH (QJsonObject, request);
    QFETCH (QStringList, labels);
    auto const silence = silentAudioFrame (60 * 12000);
    kept.insert ("mode", "JT65");
    kept.insert ("trperiod", 60);
    request.insert ("nagain_flag", true);
    auto const result = runJt9 (sessionHeader () + configureFrame (kept) + silence +
                                configureFrame (request) + silence);
    verifyCompleted (result, 0);
    QStringList found;
    for (auto const& event : eventsMatching (verifyEvents (result).values, "decode_finished"))
      {
        found << event.value ("period_end").toString ();
      }
    QCOMPARE (found, labels);
  }

  // SuperFox switched on with the request decodes the multithreaded Hound
  // period again single-threaded, on the hsym-50 pass SuperFox needs.
  void decodeAgainTurnsSuperFoxOn ()
  {
#ifndef SUPERFOX_WAV
    QSKIP ("sfoxsim is built only with WSJT_BUILD_UTILS");
#else
    QFile recording {QString::fromUtf8 (SUPERFOX_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const result = runJt9 (sessionHeader () +
      configureFrame ({{"mode", "FT8"}, {"utc", "00:00:30"}, {"contest_type", "hound"},
                       {"even_sequence", true}, {"rxfreq", 750},
                       {"multithreaded_ft8", true}, {"ft8_threads", 2}}) +
      frame (0x01u, recording.readAll ().mid (44)) +
      configureFrame ({{"superfox_mode", true}, {"nagain_flag", true}}));
    verifyCompleted (result, 0);
    auto const periods = decodesByPeriod (verifyEvents (result).values);
    QCOMPARE (periods.size (), 2);
    QCOMPARE (periods.last ().size (), 7);
#endif
  }

  // A SuperFox Hound decode again stays single-threaded, so it searches only at
  // rxfreq, and multithreading comes back after Hound.
  void decodeAgainKeepsSuperFoxHoundSingleThreaded ()
  {
    QFile recording {QString::fromUtf8 (FT8_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const audio = frame (0x01u, recording.readAll ().mid (44));
    auto const result = runJt9 (sessionHeader () +
      configureFrame ({{"mode", "FT8"}, {"utc", "00:00:45"}, {"contest_type", "hound"},
                       {"superfox_mode", true}, {"even_sequence", false},
                       {"multithreaded_ft8", true}, {"ft8_threads", 2}}) + audio +
      configureFrame ({{"rxfreq", 1000}, {"nagain_flag", true}}) +
      configureFrame ({{"contest_type", "none"}}) + audio);
    verifyCompleted (result, 0);
    auto const periods = decodesByPeriod (verifyEvents (result).values);
    QCOMPARE (periods.size (), 3);
    QCOMPARE (periods.at (1).size (), 0);
    // Only the multithreaded decoder warns of the missing ALLCALL7.TXT.
    QVERIFY2 (result.standardError.contains ("ALLCALL7.TXT is too short or broken?"),
              result.standardError.constData ());
  }

  void decodeAgainLeavesThePeriodInFlight_data ()
  {
    QTest::addColumn<bool> ("rest");
    QTest::addColumn<int> ("seconds");     // audio in flight before the request
    QTest::addColumn<bool> ("shifted");
    QTest::addColumn<bool> ("againFirst");
    QTest::addColumn<QJsonObject> ("settings");
    QTest::addColumn<bool> ("next");       // a further period with no configure
    QTest::addColumn<int> ("requests");
    QTest::newRow ("rest of the period") << true << 6 << true << true << QJsonObject {} << false << 1;
    // The input ends after the request: the six seconds in flight are decoded alone.
    QTest::newRow ("end of input") << false << 6 << true << true << QJsonObject {} << false << 1;
    // Between the period's FT8 passes the request waits for the last one, with
    // the period's time sent at its start; the next period decodes as new audio
    // (a decode again left in force would search only around the Rx frequency).
    QTest::newRow ("between FT8 passes") << true << 13 << false << false
      << QJsonObject {{"depth", 3}, {"rxfreq", 400}} << true << 1;
    QTest::newRow ("two requests between FT8 passes") << true << 13 << false << false
      << QJsonObject {{"depth", 3}, {"rxfreq", 400}} << false << 2;
  }

  // A decode again during a period sets that period's audio aside and puts it back.
  void decodeAgainLeavesThePeriodInFlight ()
  {
    QFETCH (bool, rest);
    QFETCH (int, seconds);
    QFETCH (bool, shifted);
    QFETCH (bool, againFirst);
    QFETCH (QJsonObject, settings);
    QFETCH (bool, next);
    QFETCH (int, requests);
    // The depth-3 rows between FT8 passes run under test_streaming_gui_schedule.
    if (!againFirst && !scheduleTests ()) QSKIP ("run by test_streaming_gui_schedule");
    if (againFirst && scheduleTests ()) QSKIP ("run by test_streaming_consumers");
    QFile recording {QString::fromUtf8 (FT8_ENGINE_WAV)};
    QVERIFY2 (recording.open (QIODevice::ReadOnly), qPrintable (recording.errorString ()));
    auto const samples = recording.readAll ().mid (44);
    // The period in flight carries the same signal, half a second later when shifted.
    auto const split = seconds * 12000 * 2;
    auto const later = shifted ? (QByteArray (6000 * 2, '\0') + samples).left (samples.size ()) : samples;
    auto const inFlight = rest ? later : later.left (split);
    auto const reference = runJt9 (sessionHeader () +
      configureFrame (with (settings, {{"mode", "FT8"}, {"utc", "05:11:30"}})) + frame (0x01u, inFlight),
      {}, 120000);
    verifyCompleted (reference, 0);
    auto const expected = decodesByPeriod (verifyEvents (reference).values);
    QCOMPARE (expected.size (), 1);
    auto const timeFirst = !againFirst;
    QByteArray extra;
    for (int i = 1; i < requests; ++i) extra += configureFrame ({{"nagain_flag", true}});
    auto const result = runJt9 (sessionHeader () +
      configureFrame (with (settings, {{"mode", "FT8"}, {"utc", "05:11:15"}})) + frame (0x01u, samples) +
      (timeFirst ? configureFrame ({{"utc", "05:11:30"}}) : QByteArray {}) +
      frame (0x01u, later.left (split)) +
      configureFrame (timeFirst ? QJsonObject {{"nagain_flag", true}}
                                : QJsonObject {{"utc", "05:11:30"}, {"nagain_flag", true}}) + extra +
      (rest ? frame (0x01u, later.mid (split)) : QByteArray {}) +
      (next ? frame (0x01u, samples) : QByteArray {}), {}, 120000);
    verifyCompleted (result, 0);
    auto const periods = decodesByPeriod (verifyEvents (result).values);
    // Each request runs; a second one over the same period finds nothing new.
    QCOMPARE (periods.size (), 2 + requests + (next ? 1 : 0));
    // Only the decode-again passes run as one.
    int againPasses = 0;
    for (auto const& pass : passes (result.standardError)) againPasses += pass.again ? 1 : 0;
    QCOMPARE (againPasses, requests);
    auto const again = periods.at (againFirst ? 1 : 2);
    QCOMPARE (again.size (), 1);
    QCOMPARE (again.first ().value ("time").toString (), QString {"051115"});
    QCOMPARE (periods.at (againFirst ? 2 : 1), expected.first ());
    if (next)
      {
        QStringList first, last;
        for (auto const& decode : periods.at (0)) first << decode.value ("message").toString ();
        for (auto const& decode : periods.last ()) last << decode.value ("message").toString ();
        QVERIFY (!first.isEmpty ());
        for (auto const& message : first) QVERIFY2 (last.contains (message), qPrintable (message));
      }
  }

  void wsprdRejectsOversizedFrames_data ()
  {
    QTest::addColumn<quint32> ("declaredLength");
    QTest::newRow ("over capacity") << WsprdAudioCapacityBytes + 2u;
    QTest::newRow ("uint32 max") << std::numeric_limits<quint32>::max ();
  }

  void wsprdRejectsOversizedFrames ()
  {
    QFETCH (quint32, declaredLength);
    auto const result = runWsprd (sessionHeader () +
                                  declaredFrame (0x01u, declaredLength));
    verifyCompleted (result, 1);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error", "frame_too_large").size (), 1);
    QCOMPARE (eventsMatching (events.values, "decode").size (), 0);
  }

  void wsprdDispatchesConfigureStructurally ()
  {
    auto const configure = QByteArray {
      R"({"t":"configure","date":"260822","time":"1200","dialfreq":14.0956,"wspr_type":2,"mycall":"halt","mygrid":"FN20"})"};
    auto const result = runWsprd (sessionHeader () + controlFrame (configure) +
                                  controlFrame (R"({"t":"halt"})"));
    verifyCompleted (result, 1);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error", "insufficient_audio").size (), 1);
    QCOMPARE (eventsMatching (events.values, "error", "missing_configure").size (), 0);
  }

  // Live, the GUI decodes these modes when its half-symbol count reaches the
  // stop point (MainWindow::fixStop), on the audio up to it; from a file it
  // decodes at the stop on the whole period it read.
  void decodesAtTheGuiStopPoint_data ()
  {
    QTest::addColumn<QJsonObject> ("settings");
    QTest::addColumn<QString> ("path");
    QTest::addColumn<int> ("samples");
    QTest::addColumn<bool> ("decodes");
    QTest::addColumn<int> ("decodedAt");
    QTest::addColumn<int> ("nzhsym");
    QJsonObject const jt4 {{"mode", "JT4"}, {"trperiod", 60}, {"submode", 0}, {"depth_level", 1},
                           {"rxfreq", 1000}, {"ntol", 50}};
    QJsonObject const q65 {{"mode", "Q65"}, {"trperiod", 15}, {"submode", 0}, {"depth_level", 1},
                           {"nfa", 1300}, {"nfb", 1700}, {"rxfreq", 1500}};
    QJsonObject const fst4 {{"mode", "FST4"}, {"trperiod", 15}, {"depth_level", 1}, {"nfa", 1300},
                            {"nfb", 1700}, {"rxfreq", 1500}, {"noise_blanker_level", 0}};
    QJsonObject const live {{"live_audio", true}};
    QJsonObject const eme {{"live_audio", true}, {"decode_after_eme_delay", true}};
    auto const jt4Wav = QString::fromUtf8 (JT4_ENGINE_WAV);
    auto const q65Wav = QString::fromUtf8 (Q65_ENGINE_WAV);
    auto const fst4Wav = QString::fromUtf8 (FST4_ENGINE_WAV);
    QTest::newRow ("JT4 live at 176") << with (jt4, live) << jt4Wav << 176 * 3456 << true << 176 * 3456 << 176;
    QTest::newRow ("JT4 live, one sample short") << with (jt4, live) << jt4Wav << 176 * 3456 - 1
      << false << 0 << 0;
    QTest::newRow ("JT4 live after EME delay, short of 179") << with (jt4, eme) << jt4Wav << 179 * 3456 - 1
      << false << 0 << 0;
    QTest::newRow ("JT4 live after EME delay at 179") << with (jt4, eme) << jt4Wav << 179 * 3456
      << true << 179 * 3456 << 179;
    QTest::newRow ("Q65-15 live at 48") << with (q65, live) << q65Wav << 48 * 3456 << true << 48 * 3456 << 48;
    QTest::newRow ("Q65-15 live, one sample short") << with (q65, live) << q65Wav << 48 * 3456 - 1
      << false << 0 << 0;
    QTest::newRow ("FST4-15 live at 39") << with (fst4, live) << fst4Wav << 39 * 3456 << true << 39 * 3456 << 39;
    QTest::newRow ("FST4-15 live, one sample short") << with (fst4, live) << fst4Wav << 39 * 3456 - 1
      << false << 0 << 0;
    QTest::newRow ("JT4 from a file, past 176") << jt4 << jt4Wav << 177 * 3456 << false << 0 << 0;
    QTest::newRow ("JT4 from a file, the whole period") << jt4 << jt4Wav << 60 * 12000
      << true << 60 * 12000 << 176;
    QTest::newRow ("Q65-15 from a file, the whole period") << q65 << q65Wav << 15 * 12000
      << true << 15 * 12000 << 48;
    QTest::newRow ("FST4-15 from a file, the whole period") << fst4 << fst4Wav << 15 * 12000
      << true << 15 * 12000 << 39;
  }

  void decodesAtTheGuiStopPoint ()
  {
    if (!scheduleTests ()) QSKIP ("run by test_streaming_gui_schedule");
    QFETCH (QJsonObject, settings);
    QFETCH (QString, path);
    QFETCH (int, samples);
    QFETCH (bool, decodes);
    QFETCH (int, decodedAt);
    QFETCH (int, nzhsym);
    auto const audio = wavSamples (path, samples);
    QCOMPARE (audio.size (), 2 * samples);
    auto const session = runOpenSession (sessionHeader () + configureFrame (settings) + frame (0x01u, audio)
      + consumedMarker (), 120000, [decodes] (QByteArray const& output, QByteArray const&) {
        return decodes ? output.contains (R"("t":"decode_finished")") : consumed (output);
      });
    verifyCompleted (session.result, 0);
    auto const open = parseEvents (session.openOutput);
    QVERIFY2 (open.error.isEmpty (), qPrintable (open.error));
    auto const ran = passes (session.openError);
    if (decodes)
      {
        QCOMPARE (eventsMatching (open.values, "decode_finished").size (), 1);
        bool found = false;
        for (auto const& decode : eventsMatching (open.values, "decode"))
          {
            found |= decode.value ("message").toString ().trimmed () == "K1ABC W9XYZ FN42";
          }
        QVERIFY2 (found, session.openOutput.constData ());
        QCOMPARE (ran.size (), 1);
        QCOMPARE (ran.first ().samples, decodedAt);
        QCOMPARE (ran.first ().nzhsym, nzhsym);
      }
    else
      {
        QCOMPARE (eventsMatching (open.values, "decode_finished").size (), 0);
        QVERIFY (ran.isEmpty ());
      }
    // A period cut short still decodes once when the input ends.
    auto const all = verifyEvents (session.result);
    QCOMPARE (eventsMatching (all.values, "decode_finished").size (), 1);
  }

  // From a file a period decodes once the engine's share of it is in: FT4's
  // 72576 samples, multithreaded FT8's stop, the whole period otherwise.
  void fileSharesFollowTheEngine_data ()
  {
    QTest::addColumn<QJsonObject> ("settings");
    QTest::addColumn<int> ("samples");
    QTest::addColumn<int> ("decodedAt");
    QTest::newRow ("FT4") << QJsonObject {{"mode", "FT4"}} << 90000 << 21 * 3456;
    QTest::newRow ("FT8 multithreaded, start 3")
      << QJsonObject {{"mode", "FT8"}, {"multithreaded_ft8", true}, {"ft8_threads", 2}, {"ft8_decoder_start", 3}}
      << 180000 << 49 * 3456;
    QTest::newRow ("FT8") << QJsonObject {{"mode", "FT8"}} << 180000 << 180000;
    QTest::newRow ("JT65") << QJsonObject {{"mode", "JT65"}} << 720000 << 720000;
  }

  void fileSharesFollowTheEngine ()
  {
    if (!scheduleTests ()) QSKIP ("run by test_streaming_gui_schedule");
    QFETCH (QJsonObject, settings);
    QFETCH (int, samples);
    QFETCH (int, decodedAt);
    auto const session = runOpenSession (sessionHeader () + configureFrame (settings) + silentAudioFrame (samples)
      + consumedMarker (), 120000, [] (QByteArray const& output, QByteArray const&) { return consumed (output); });
    verifyCompleted (session.result, 0);
    auto const ran = passes (session.openError);
    QVERIFY (!ran.isEmpty ());
    QCOMPARE (ran.last ().samples, decodedAt);
    QVERIFY (!ran.last ().live);
  }

  // A frame that runs past the stop point decodes there, before the rest of it arrives.
  void decodesInsideALongFrame ()
  {
    if (!scheduleTests ()) QSKIP ("run by test_streaming_gui_schedule");
    QJsonObject const jt4 {{"mode", "JT4"}, {"trperiod", 60}, {"submode", 0}, {"depth_level", 1},
                           {"rxfreq", 1000}, {"ntol", 50}, {"live_audio", true}};
    auto const audio = wavSamples (QString::fromUtf8 (JT4_ENGINE_WAV), 176 * 3456);
    auto const session = runOpenSession (sessionHeader () + configureFrame (jt4)
      + declaredFrame (0x01u, quint32 (2 * 60 * 12000)) + audio, 120000,
      [] (QByteArray const& output, QByteArray const&) {
        return output.contains (R"("t":"decode_finished")");
      });
    auto const open = parseEvents (session.openOutput);
    QVERIFY2 (open.error.isEmpty (), qPrintable (open.error));
    QCOMPARE (eventsMatching (open.values, "decode_finished").size (), 1);
    auto const ran = passes (session.openError);
    QCOMPARE (ran.size (), 1);
    QCOMPARE (ran.first ().samples, 176 * 3456);
  }

  // A configure in mid-period moves the stop point of the period in flight:
  // later, so the period waits; or earlier than the audio already in, so it
  // decodes at once on the samples up to the new stop. The GUI's triggers are
  // equalities, so a stop already passed would never decode there; the stream
  // decodes the period rather than drop it.
  void aConfigureMovesTheStopPoint_data ()
  {
    QTest::addColumn<bool> ("emeFirst");
    QTest::addColumn<int> ("before");     // samples before the second configure
    QTest::addColumn<int> ("after");      // samples in all
    QTest::addColumn<bool> ("decodes");
    QTest::addColumn<int> ("decodedAt");
    QTest::newRow ("EME delay turned on at 175, audio to 177") << false << 175 * 3456 << 177 * 3456
      << false << 0;
    QTest::newRow ("EME delay turned off at 177") << true << 177 * 3456 << 177 * 3456 << true << 176 * 3456;
  }

  void aConfigureMovesTheStopPoint ()
  {
    if (!scheduleTests ()) QSKIP ("run by test_streaming_gui_schedule");
    QFETCH (bool, emeFirst);
    QFETCH (int, before);
    QFETCH (int, after);
    QFETCH (bool, decodes);
    QFETCH (int, decodedAt);
    QJsonObject const jt4 {{"mode", "JT4"}, {"trperiod", 60}, {"submode", 0}, {"depth_level", 1},
                           {"rxfreq", 1000}, {"ntol", 50}, {"live_audio", true},
                           {"decode_after_eme_delay", emeFirst}};
    auto const audio = wavSamples (QString::fromUtf8 (JT4_ENGINE_WAV), after);
    QCOMPARE (audio.size (), 2 * after);
    auto input = sessionHeader () + configureFrame (jt4) + frame (0x01u, audio.left (2 * before))
      + configureFrame ({{"decode_after_eme_delay", !emeFirst}});
    if (after > before) input += frame (0x01u, audio.mid (2 * before));
    auto const session = runOpenSession (input + consumedMarker (), 120000,
      [decodes] (QByteArray const& output, QByteArray const&) {
        return decodes ? output.contains (R"("t":"decode_finished")") : consumed (output);
      });
    verifyCompleted (session.result, 0);
    auto const open = parseEvents (session.openOutput);
    QVERIFY2 (open.error.isEmpty (), qPrintable (open.error));
    auto const ran = passes (session.openError);
    if (decodes)
      {
        QCOMPARE (eventsMatching (open.values, "decode_finished").size (), 1);
        QCOMPARE (ran.size (), 1);
        QCOMPARE (ran.first ().samples, decodedAt);
        QCOMPARE (ran.first ().nzhsym, decodedAt / 3456);
      }
    else
      {
        QCOMPARE (eventsMatching (open.values, "decode_finished").size (), 0);
        QVERIFY (ran.isEmpty ());
      }
  }

  // FT8 passes run as their audio arrives, on the GUI's schedule: live audio
  // takes its live stages, a file jt9a's (widgets/mainwindow.cpp dataSink and
  // decode(), lib/jt9a.f90).
  void ft8PassesFollowTheGui_data ()
  {
    QTest::addColumn<QJsonObject> ("settings");
    QTest::addColumn<int> ("samples");
    QTest::addColumn<QString> ("expected");   // nzhsym:depth:cycles:multithreaded, space separated
    QTest::addColumn<bool> ("live");
    QTest::addColumn<int> ("finalSamples");   // the last pass's samples, when it is the period's whole
    QTest::addColumn<int> ("finished");
    QJsonObject const standard {{"mode", "FT8"}, {"depth", 1}, {"ft8_cycles", 3}};
    auto const mtd = with (standard, {{"multithreaded_ft8", true}, {"ft8_threads", 2}});
    QJsonObject const live {{"live_audio", true}};
    auto const liveMtd = with (mtd, live);
    QJsonObject const hound {{"contest_type", "hound"}, {"superfox_mode", true}, {"even_sequence", false}};
    QTest::newRow ("standard, 41 in") << standard << 41 * 3456 << "41:1:3:F" << false << 0 << 1;
    QTest::newRow ("standard, 47 in") << standard << 47 * 3456 << "41:1:3:F 47:1:3:F" << false << 0 << 1;
    QTest::newRow ("standard from a file") << standard << 15 * 12000
      << "41:1:3:F 47:1:3:F 50:1:3:F" << false << 15 * 12000 << 1;
    QTest::newRow ("standard live") << with (standard, live) << 50 * 3456
      << "41:1:3:F 47:1:3:F 50:1:3:F" << true << 0 << 1;
    QTest::newRow ("standard live, SuperFox Hound's even sequence")
      << with (standard, {{"live_audio", true}, {"contest_type", "hound"}, {"superfox_mode", true},
                          {"even_sequence", true}})
      << 50 * 3456 << "50:1:3:F" << true << 0 << 1;
    QTest::newRow ("multithreaded live, start 0") << with (liveMtd, {{"ft8_decoder_start", 0}}) << 50 * 3456
      << "41:1:2:F 49:1:3:T" << true << 0 << 1;
    QTest::newRow ("multithreaded live, start 1") << with (liveMtd, {{"ft8_decoder_start", 1}}) << 50 * 3456
      << "41:1:2:F 46:2:2:F 50:1:3:T" << true << 0 << 1;
    QTest::newRow ("multithreaded live, start 2") << with (liveMtd, {{"ft8_decoder_start", 2}}) << 50 * 3456
      << "48:1:3:T" << true << 0 << 1;
    QTest::newRow ("multithreaded from a file, start 0") << with (mtd, {{"ft8_decoder_start", 0}}) << 15 * 12000
      << "41:1:3:F 46:1:3:F 49:1:3:T" << false << 0 << 1;
    QTest::newRow ("SuperFox Fox from a file, even sequence")
      << with (standard, {{"contest_type", "fox"}, {"superfox_mode", true}, {"even_sequence", true}})
      << 15 * 12000 << "41:1:3:F 47:1:3:F 50:1:3:F" << false << 15 * 12000 << 1;
    QTest::newRow ("SuperFox Fox multithreaded from a file, even sequence: nothing")
      << with (mtd, {{"contest_type", "fox"}, {"superfox_mode", true}, {"even_sequence", true},
                     {"ft8_decoder_start", 3}})
      << 15 * 12000 << "" << false << 0 << 0;
    QTest::newRow ("SuperFox Hound, multithreaded live, start 0")
      << with (with (liveMtd, hound), {{"ft8_decoder_start", 0}}) << 50 * 3456
      << "41:1:2:F 50:1:3:F" << true << 0 << 1;
    QTest::newRow ("SuperFox Hound, multithreaded live, start 1")
      << with (with (liveMtd, hound), {{"ft8_decoder_start", 1}}) << 50 * 3456
      << "41:1:2:F 46:2:2:F 50:1:3:F" << true << 0 << 1;
    QTest::newRow ("SuperFox Hound, multithreaded live, start 2")
      << with (with (liveMtd, hound), {{"ft8_decoder_start", 2}}) << 50 * 3456
      << "50:1:3:F" << true << 0 << 1;
    QTest::newRow ("SuperFox Fox, multithreaded live, even sequence: nothing")
      << with (liveMtd, {{"contest_type", "fox"}, {"superfox_mode", true}, {"even_sequence", true},
                         {"ft8_decoder_start", 0}})
      << 50 * 3456 << "" << true << 0 << 0;
  }

  void ft8PassesFollowTheGui ()
  {
    if (!scheduleTests ()) QSKIP ("run by test_streaming_gui_schedule");
    QFETCH (QJsonObject, settings);
    QFETCH (int, samples);
    QFETCH (QString, expected);
    QFETCH (bool, live);
    QFETCH (int, finalSamples);
    QFETCH (int, finished);
    auto const audio = wavSamples (QString::fromUtf8 (FT8_ENGINE_WAV), samples);
    QCOMPARE (audio.size (), 2 * samples);
    auto const session = runOpenSession (sessionHeader () + configureFrame (settings) + frame (0x01u, audio)
      + consumedMarker (), 120000, [] (QByteArray const& output, QByteArray const&) {
        return consumed (output);
      });
    verifyCompleted (session.result, 0);
    auto const ran = passes (session.openError);
    QStringList got;
    for (int i = 0; i < ran.size (); ++i)
      {
        auto const& pass = ran.at (i);
        got << QString {"%1:%2:%3:%4"}.arg (pass.nzhsym).arg (pass.depth).arg (pass.cycles)
          .arg (pass.multithreaded ? "T" : "F");
        QCOMPARE (pass.live, live);
        if (finalSamples > 0 && i == ran.size () - 1)
          {
            QCOMPARE (pass.samples, finalSamples);
          }
        else
          {
            // Each pass runs within a read of the audio it covers.
            QVERIFY2 (pass.samples >= pass.nzhsym * 3456 && pass.samples < pass.nzhsym * 3456 + 4096,
                      qPrintable (QString {"pass %1 ran at %2 samples"}.arg (pass.nzhsym).arg (pass.samples)));
          }
      }
    QCOMPARE (got.join (' '), expected);
    // One decode_finished for a decoded period, live or not.
    auto const all = verifyEvents (session.result);
    QCOMPARE (eventsMatching (all.values, "decode_finished").size (), finished);
  }

  // A producer's writes drain while a long decode runs.
  void wsjtCodecReadsWhileItDecodes ()
  {
    if (!readTests ()) QSKIP ("run by test_streaming_reads_while_decoding");
    auto const audio = wavSamples (QString::fromUtf8 (FT8_ENGINE_WAV), 15 * 12000);
    QCOMPARE (audio.size (), 2 * 15 * 12000);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QProcess process;
    process.setProcessChannelMode (QProcess::SeparateChannels);
    process.setWorkingDirectory (directory.path ());
    process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                   {"-a", directory.path (), "-t", directory.path (), "--stream"});
    QVERIFY (process.waitForStarted (5000));
    process.write (sessionHeader () + configureFrame ({{"mode", "FT8"}, {"utc", "05:11:15"}, {"depth", 3},
      {"multithreaded_ft8", true}, {"ft8_threads", 2}, {"ft8_decoder_start", 1}}) + frame (0x01u, audio));
    QByteArray output, error;
    auto const finished = [&output] { return output.contains (R"("t":"decode_finished")"); };
    // Sanitizer builds raise QtTest's limit for their slower decodes.
    auto const limitMs = qEnvironmentVariableIsSet ("QTEST_FUNCTION_TIMEOUT")
      ? qEnvironmentVariableIntValue ("QTEST_FUNCTION_TIMEOUT") : 120000;
    QElapsedTimer timer;
    timer.start ();
    while (!error.contains ("multithreaded=T") && timer.elapsed () < limitMs
           && process.state () == QProcess::Running)
      {
        if (process.bytesToWrite () > 0) process.waitForBytesWritten (20);
        process.waitForReadyRead (20);
        QCoreApplication::processEvents ();
        output += process.readAllStandardOutput ();
        error += process.readAllStandardError ();
      }
    QVERIFY2 (error.contains ("multithreaded=T"), error.constData ());
    QVERIFY2 (!finished (), output.constData ());
    // The multithreaded final is running: 4 MiB in a frame the stream skips.
    process.write (frame (0x7fu, QByteArray (4 << 20, '\0')));
    bool drained = false;
    while (!finished () && timer.elapsed () < limitMs && process.state () == QProcess::Running)
      {
        if (process.bytesToWrite () > 0) process.waitForBytesWritten (20);
        else process.waitForReadyRead (20);
        drained = drained || process.bytesToWrite () == 0;
        QCoreApplication::processEvents ();
        output += process.readAllStandardOutput ();
        error += process.readAllStandardError ();
      }
    QVERIFY2 (finished (), error.constData ());
    QVERIFY2 (drained, qPrintable (QString {"%1 bytes still to write when the decode finished"}
                                   .arg (process.bytesToWrite ())));
    process.closeWriteChannel ();
    QVERIFY (process.waitForFinished (limitMs));
    QCOMPARE (process.exitStatus (), QProcess::NormalExit);
    QCOMPARE (process.exitCode (), 0);
  }

  // The GUI's EME delay is 2.5 s with "Decode after EME delay", in every mode,
  // and 0 otherwise; eme_delay_seconds, when sent, wins.
  void decodeAfterEmeDelaySetsTheDelay_data ()
  {
    QTest::addColumn<QStringList> ("options");
    QTest::addColumn<QJsonObject> ("settings");
    QTest::addColumn<int> ("frames");
    QTest::addColumn<int> ("samples");
    QTest::addColumn<double> ("emedelay");
    QJsonObject const q65_60 {{"mode", "Q65"}, {"trperiod", 60}, {"submode", 0}, {"live_audio", true}};
    QTest::newRow ("Q65-60 by default") << QStringList {} << q65_60 << 1 << 196 * 3456 << 0.0;
    QTest::newRow ("Q65-60 configured twice") << QStringList {} << q65_60 << 2 << 196 * 3456 << 0.0;
    QTest::newRow ("Q65-60 from the command line") << QStringList {"--q65", "-p", "60"}
      << QJsonObject {{"live_audio", true}} << 1 << 196 * 3456 << 0.0;
    QTest::newRow ("Q65-60 after EME delay") << QStringList {}
      << with (q65_60, {{"decode_after_eme_delay", true}}) << 1 << 196 * 3456 << 2.5;
    QTest::newRow ("JT65 after EME delay") << QStringList {}
      << QJsonObject {{"mode", "JT65"}, {"decode_after_eme_delay", true}, {"live_audio", true}}
      << 1 << 179 * 3456 << 2.5;
    QTest::newRow ("explicit eme_delay_seconds") << QStringList {}
      << with (q65_60, {{"decode_after_eme_delay", true}, {"eme_delay_seconds", 12.0}}) << 1 << 196 * 3456 << 12.0;
  }

  void decodeAfterEmeDelaySetsTheDelay ()
  {
    if (!scheduleTests ()) QSKIP ("run by test_streaming_gui_schedule");
    QFETCH (QStringList, options);
    QFETCH (QJsonObject, settings);
    QFETCH (int, frames);
    QFETCH (int, samples);
    QFETCH (double, emedelay);
    QByteArray configures;
    for (int i = 0; i < frames; ++i) configures += configureFrame (settings);
    auto const session = runOpenSession (sessionHeader () + configures + silentAudioFrame (samples)
      + consumedMarker (), 120000, [] (QByteArray const& output, QByteArray const&) {
        return consumed (output);
      }, options);
    verifyCompleted (session.result, 0);
    auto const ran = passes (session.openError);
    QCOMPARE (ran.size (), 1);
    QCOMPARE (ran.first ().samples, samples);
    QCOMPARE (ran.first ().emedelay, emedelay);
  }

  void wsprdRejectsInvalidWsprType ()
  {
    auto const configure = QByteArray {
      R"({"t":"configure","date":"260822","time":"1200","dialfreq":14.0956,"wspr_type":3})"};
    auto const result = runWsprd (sessionHeader () + controlFrame (configure),
                                  3000, false);
    verifyCompleted (result, 1);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error", "invalid_wspr_type").size (), 1);
    QCOMPARE (eventsMatching (events.values, "decode").size (), 0);
    QCOMPARE (eventsMatching (events.values, "decode_finished").size (), 0);
  }
};

QTEST_GUILESS_MAIN (TestStreamingConsumers)

#include "test_streaming_consumers.moc"
