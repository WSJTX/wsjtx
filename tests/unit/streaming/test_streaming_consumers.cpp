#include <limits>

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
constexpr quint32 Jt9AudioCapacityBytes = 30u * 60u * 12000u * 2u;
constexpr quint32 WsprdAudioCapacityBytes = 8u * 114u * 12000u * 2u;
constexpr int Ft4PeriodSamples = 21 * 3456;
constexpr int Jt9TenSecondPeriodSamples = 10 * 12000;

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
      if (type == "warning" &&
          (!event.value ("code").isString () ||
           event.value ("code").toString ().isEmpty () ||
           !event.value ("discarded_samples").isDouble () ||
           event.value ("discarded_samples").toDouble () < 0.0))
        {
          parsed.error = QString {"invalid warning event: %1"}.arg (
            QString::fromUtf8 (line));
          return parsed;
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
}

class TestStreamingConsumers final : public QObject
{
  Q_OBJECT

private slots:
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
    auto const warnings = eventsMatching (
      events.values, "warning", "period_boundary_discard");
    QCOMPARE (warnings.size (), 1);
    QCOMPARE (warnings.front ().value ("discarded_samples").toInt (), 2);
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

  void jt9ReportsBoundaryDiscardOnce ()
  {
    auto const exact = runJt9 (sessionHeader () +
                               silentAudioFrame (Ft4PeriodSamples), {"--ft4"});
    verifyCompleted (exact, 0);
    auto const exactEvents = verifyEvents (exact);
    QCOMPARE (eventsMatching (exactEvents.values, "warning").size (), 0);

    auto const overflow = runJt9 (
      sessionHeader () + silentAudioFrame (Ft4PeriodSamples + 2), {"--ft4"});
    verifyCompleted (overflow, 0);
    auto const overflowEvents = verifyEvents (overflow);
    auto const warnings = eventsMatching (
      overflowEvents.values, "warning", "period_boundary_discard");
    QCOMPARE (warnings.size (), 1);
    QCOMPARE (warnings.front ().value ("discarded_samples").toInt (), 2);
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
