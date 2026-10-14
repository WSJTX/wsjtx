#include <limits>
#include <cmath>

#include <QJsonDocument>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
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
// jt9 reads audio 4,096 samples at a time and skips frames 4,096 bytes at a time.
constexpr int Jt9ChunkSamples = 4096;
constexpr int Jt9DrainBytes = 4096;

constexpr char ReadyLine[] =
  R"({"v":1,"t":"ready","modes":["FT8","FT4","JT9","JT65","JT4","FST4","FST4W","Q65","MSK144"],"protocol":1})";
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

ProcessResult runJt9 (QByteArray const& input, QStringList options = {})
{
  QTemporaryDir directory;
  if (!directory.isValid ())
    {
      return {};
    }
  options << "-a" << directory.path ()
          << "-t" << directory.path ()
          << "--stream";
  return runProcess (QString::fromUtf8 (JT9_EXECUTABLE), options, input,
                     directory.path ());
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

QByteArray sessionHeaderWith (int index, char value)
{
  auto bytes = sessionHeader ();
  bytes[index] = value;
  return bytes;
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
    QTest::newRow ("early-hour-iso-precedence")
      << QJsonObject {{"nutc", 1234}, {"utc", "00:12:34"}} << QString {"001234"};
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
    auto const result = runProcess (QString::fromUtf8 (JT9_EXECUTABLE),
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
    auto const result = runProcess (QString::fromUtf8 (JT9_EXECUTABLE),
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
    auto const result = runProcess (QString::fromUtf8 (JT9_EXECUTABLE),
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
    auto const result = runProcess (QString::fromUtf8 (JT9_EXECUTABLE),
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

  void jt9RejectsOversizedFrames_data ()
  {
    QTest::addColumn<quint32> ("declaredLength");
    QTest::newRow ("over capacity") << Jt9AudioCapacityBytes + 2u;
    QTest::newRow ("uint32 max") << std::numeric_limits<quint32>::max ();
  }

  void jt9RejectsOversizedFrames ()
  {
    QFETCH (quint32, declaredLength);
    auto const result = runJt9 (sessionHeader () +
                                declaredFrame (0x01u, declaredLength));
    verifyCompleted (result, 1);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error", "frame_too_large").size (), 1);
    QCOMPARE (eventsMatching (events.values, "decode").size (), 0);
  }

  void jt9RejectsUnknownMode_data ()
  {
    QTest::addColumn<QByteArray> ("configure");
    QTest::newRow ("string")
      << QByteArray {R"({"t":"configure","mode":"BOGUS","trperiod":1})"};
    QTest::newRow ("numeric")
      << QByteArray {R"({"t":"configure","mode":999,"trperiod":1})"};
  }

  void jt9RejectsUnknownMode ()
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

  void jt9RejectsOutOfRangeTrperiod_data ()
  {
    QTest::addColumn<QByteArray> ("configure");
    QTest::newRow ("zero")
      << QByteArray {R"({"t":"configure","trperiod":0})"};
    QTest::newRow ("above maximum")
      << QByteArray {R"({"t":"configure","trperiod":1801})"};
  }

  void jt9RejectsOutOfRangeTrperiod ()
  {
    QFETCH (QByteArray, configure);
    auto const result = runJt9 (sessionHeader () + controlFrame (configure) +
                                controlFrame (R"({"t":"halt"})"));
    verifyCompleted (result, 0);
    auto const events = verifyEvents (result);
    QCOMPARE (eventsMatching (events.values, "error", "invalid_trperiod").size (), 1);
    QCOMPARE (eventsMatching (events.values, "decode").size (), 0);
  }

  void jt9WritesTheReadyLine ()
  {
    auto const result = runJt9 (sessionHeader ());
    verifyCompleted (result, 0);
    QCOMPARE (streamOutput (result), ndjson ({ReadyLine}));
  }

  // The header is checked in field order, before the ready line.
  void jt9RejectsBadSessionHeaders_data ()
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

  void jt9RejectsBadSessionHeaders ()
  {
    QFETCH (QByteArray, header);
    QFETCH (QByteArray, line);
    auto const result = runJt9 (header);
    verifyCompleted (result, 1);
    QCOMPARE (streamOutput (result), ndjson ({line}));
  }

  // An odd-length audio frame is reported and skipped whole: the audio
  // around it decodes as if it were absent.
  void jt9SkipsOddLengthAudioFrames_data ()
  {
    QTest::addColumn<int> ("length");
    QTest::newRow ("one byte") << 1;
    QTest::newRow ("over two drain blocks") << 2 * Jt9DrainBytes + 1;
  }

  void jt9SkipsOddLengthAudioFrames ()
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
  void jt9DropsTheChunkInFlightAtEndOfInput_data ()
  {
    QTest::addColumn<int> ("delivered");
    QTest::addColumn<int> ("kept");
    QTest::addColumn<int> ("periods");
    QTest::newRow ("within the first chunk") << Jt9ChunkSamples - 1 << 0 << 0;
    QTest::newRow ("after a chunk") << 2 * Jt9ChunkSamples - 1 << Jt9ChunkSamples << 1;
    QTest::newRow ("after a period")
      << Ft4PeriodSamples + Jt9ChunkSamples - 1 << Ft4PeriodSamples << 1;
  }

  void jt9DropsTheChunkInFlightAtEndOfInput ()
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

  // A control frame over 1,024 bytes is reported and skipped whole.
  void jt9SkipsOversizedControlFrames_data ()
  {
    QTest::addColumn<int> ("length");
    QTest::addColumn<QByteArray> ("line");
    QTest::newRow ("1024 bytes") << 1024 << QByteArray {InvalidTrperiodLine};
    QTest::newRow ("1025 bytes") << 1025 << QByteArray {ControlFrameTooLargeLine};
    QTest::newRow ("over three drain blocks") << 3 * Jt9DrainBytes + 1 << QByteArray {ControlFrameTooLargeLine};
  }

  void jt9SkipsOversizedControlFrames ()
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
  void jt9StopsOnFramesOverTheSampleBuffer_data ()
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

  void jt9StopsOnFramesOverTheSampleBuffer ()
  {
    QFETCH (quint8, type);
    QFETCH (quint32, length);
    QFETCH (QByteArray, lines);
    QFETCH (int, exitCode);
    auto const result = runJt9 (sessionHeader () + declaredFrame (type, length));
    verifyCompleted (result, exitCode);
    QCOMPARE (streamOutput (result), ndjson ({ReadyLine}) + lines);
  }

  // halt decodes the period in flight once and reads nothing after it.
  void jt9HaltsAfterThePeriodInFlight ()
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
  void jt9DeclinesBadConfigureFrames_data ()
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

  void jt9DeclinesBadConfigureFrames ()
  {
    QFETCH (QByteArray, configure);
    QFETCH (QByteArray, line);
    auto const result = runJt9 (sessionHeader () + controlFrame (configure));
    verifyCompleted (result, 0);
    QCOMPARE (streamOutput (result), ndjson ({ReadyLine, line}));
  }

  // A frame of an unknown type is skipped whole.
  void jt9SkipsUnknownFrameTypes_data ()
  {
    QTest::addColumn<int> ("length");
    QTest::newRow ("one byte") << 1;
    QTest::newRow ("over three drain blocks") << 3 * Jt9DrainBytes + 1;
  }

  void jt9SkipsUnknownFrameTypes ()
  {
    QFETCH (int, length);
    auto const result = runJt9 (sessionHeader () + frame (0x07u, QByteArray (length, '\x02')) +
                                controlFrame (R"({"t":"configure","trperiod":0})"));
    verifyCompleted (result, 0);
    QCOMPARE (streamOutput (result), ndjson ({ReadyLine, InvalidTrperiodLine}));
  }

  // A straddling frame carries its remainder into the next period.
  void jt9CarriesStraddlingFrameIntoNextPeriod ()
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
  void jt9StartsANewPeriodOnATrperiodChange ()
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
  void jt9KeepsThePeriodOnAnUnchangedConfigure ()
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

  // jt9 does not act on discontinuity: its periods are counted from its
  // first sample whatever controls arrive between the audio frames.
  void jt9IgnoresDiscontinuity ()
  {
    auto const third = silentAudioFrame (Ft4PeriodSamples / 3);
    auto const period = silentAudioFrame (Ft4PeriodSamples);
    auto const discontinuity = controlFrame (R"({"t":"discontinuity"})");
    auto const plain = runJt9 (sessionHeader () + third + third + period, {"--ft4"});
    verifyCompleted (plain, 0);
    auto const marked = runJt9 (sessionHeader () + third + discontinuity + third
                                + discontinuity + period, {"--ft4"});
    verifyCompleted (marked, 0);
    auto const events = verifyEvents (marked);
    QCOMPARE (eventsMatching (events.values, "error").size (), 0);
    QCOMPARE (eventsMatching (events.values, "decode_finished").size (), 2);
    QCOMPARE (marked.standardOutput, plain.standardOutput);
  }

  // A period's label is the producer's UTC, whichever key and frame carried it.
  void jt9LabelsPeriodsWithTheProducersUtc_data ()
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
    QTest::newRow ("FT8 negative nutc")
      << controlFrame (R"({"t":"configure","mode":"FT8","nutc":-1})") << 15 * 12000 << QString {"000000"};
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
    QTest::newRow ("JT9 nutc 4515")
      << controlFrame (R"({"t":"configure","mode":"JT9","nutc":4515})") << 60 * 12000 << QString {"004515"};
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

  void jt9LabelsPeriodsWithTheProducersUtc ()
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
