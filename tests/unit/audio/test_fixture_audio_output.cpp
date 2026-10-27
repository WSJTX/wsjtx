#include <QtTest>

#include <QAudioFormat>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>

#include <cstring>
#include <functional>

#include "Audio/BWFFile.hpp"
#include "Audio/FixtureSoundOutput.hpp"

namespace
{
  class PcmSource final : public QIODevice
  {
  public:
    PcmSource () {open (ReadOnly | Unbuffered);}
    qint64 framesRead {0};
    std::function<void ()> onRead;

  protected:
    qint64 readData (char * data, qint64 bytes) override
    {
      qint16 const sample = 10000;
      for (qint64 i = 0; i + 1 < bytes; i += 2)
        std::memcpy (data + i, &sample, sizeof sample);
      framesRead += bytes / 2;
      if (onRead) onRead ();
      return bytes;
    }
    qint64 writeData (char const *, qint64) override {return -1;}
  };

  bool pull (FixtureSoundOutput& output)
  {
    return QMetaObject::invokeMethod (&output, "pullAudio", Qt::DirectConnection);
  }

  QByteArray capturedPcm (QString const& path)
  {
    BWFFile file {QAudioFormat {}, path};
    if (!file.open (QIODevice::ReadOnly)) return {};
    return file.readAll ();
  }
}

class TestFixtureAudioOutput final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void failureRetainsIdentityDuringCleanup_data ()
  {
    QTest::addColumn<QString> ("callback");
    for (auto name : {"idle-reset", "stopped-restart", "failed-restart"})
      QTest::newRow (name) << QString::fromLatin1 (name);
  }

  void failureRetainsIdentityDuringCleanup ()
  {
    QFETCH (QString, callback);
    QTemporaryDir directory;
    FixtureSoundOutput output {directory.filePath ("capture.wav")};
    PcmSource source;
    TxEvidence::TxPlaybackContext const original {
      TxEvidence::TxSessionId {1}, TxEvidence::TxGeneration {1}};
    TxEvidence::TxPlaybackContext const replacement {
      TxEvidence::TxSessionId {2}, TxEvidence::TxGeneration {2}};
    output.restart (&source, original);
    QVector<TxEvidence::TxOutputError> errors;
    connect (&output, &AudioOutputSink::error, this,
             [&errors] (TxEvidence::TxOutputError error) {errors.append (error);});
    QSignalSpy failed {&output, &FixtureSoundOutput::captureFailed};
    QMetaObject::Connection callbackConnection;
    auto restart = [&] {output.restart (&source, replacement);};
    if (callback == "idle-reset")
      callbackConnection = connect (&output, &AudioOutputSink::audioOutputIdle,
                                    &output, &FixtureSoundOutput::reset);
    else if (callback == "stopped-restart")
      callbackConnection = connect (&output, &FixtureSoundOutput::captureStopped,
                                    &output, restart);
    else
      callbackConnection = connect (&output, &FixtureSoundOutput::captureFailed,
                                    &output, restart);
    output.configureJttyCapture (1);
    disconnect (callbackConnection);
    QCOMPARE (failed.size (), 1);
    QCOMPARE (errors.size (), 1);
    QCOMPARE (errors.front ().context, original);
    QCOMPARE (errors.front ().error, TxEvidence::TxBackendError::Io);
    output.reset ();
  }

  void resetCancelsReadsAndCannotBeResumed ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    FixtureSoundOutput output {directory.filePath ("capture.wav")};
    AudioOutputSink * sink = &output;
    PcmSource source;
    QSignalSpy stopped {&output, &FixtureSoundOutput::captureStopped};
    sink->restart (&source, {TxEvidence::TxSessionId {1}, TxEvidence::TxGeneration {1}});
    QVERIFY (pull (output));
    QCOMPARE (source.framesRead, qint64 {257});
    sink->reset ();
    sink->reset ();
    sink->resume ();
    sink->stop ();
    QVERIFY (pull (output));
    QCOMPARE (source.framesRead, qint64 {257});
    QCOMPARE (stopped.size (), 1);
    QCOMPARE (output.stopCount (), 0);
    QCOMPARE (capturedPcm (output.capturePath ()).size (), 514);
  }

  void suspendPreservesPosition ()
  {
    QTemporaryDir directory;
    FixtureSoundOutput output {directory.filePath ("capture.wav")};
    AudioOutputSink * sink = &output;
    PcmSource source;
    sink->restart (&source, {TxEvidence::TxSessionId {1}, TxEvidence::TxGeneration {1}});
    QVERIFY (pull (output));
    sink->suspend ();
    sink->suspend ();
    QVERIFY (pull (output));
    QCOMPARE (source.framesRead, qint64 {257});
    sink->resume ();
    sink->resume ();
    QVERIFY (pull (output));
    QCOMPARE (source.framesRead, qint64 {257 + 509});
    sink->stop ();
    QCOMPARE (capturedPcm (output.capturePath ()).size (), 2 * (257 + 509));
  }

  void pauseDoesNotBecomeCatchupTime_data ()
  {
    QTest::addColumn<bool> ("ft8");
    QTest::addColumn<qint64> ("audioFrames");
    QTest::newRow ("ft8") << true << qint64 {0};
    QTest::newRow ("jtty-accelerated") << false << qint64 {48000};
    QTest::newRow ("jtty-realtime-tail") << false << qint64 {257};
  }

  void pauseDoesNotBecomeCatchupTime ()
  {
    QFETCH (bool, ft8);
    QFETCH (qint64, audioFrames);
    qint64 now {100000};
    QTemporaryDir directory;
    FixtureSoundOutput output {directory.filePath ("capture.wav"),
      ft8 ? FixtureSoundOutput::Profile::Ft8Period
          : FixtureSoundOutput::Profile::JttyStrict,
      ft8 ? 1u : 4u, nullptr, [&now] {return now;}};
    PcmSource source;
    auto restart = [&] {
      if (!ft8) output.configureJttyCapture (audioFrames);
      output.restart (&source, {TxEvidence::TxSessionId {1}, TxEvidence::TxGeneration {1}}, 0);
    };
    restart ();
    for (int i = 0; i < 3; ++i) QVERIFY (pull (output));
    auto timer = output.findChild<QTimer *> ();
    QVERIFY (timer);
    auto const interval = timer->interval ();
    QVERIFY (interval > 4);
    auto const framesRead = source.framesRead;
    auto const capturedFrames = output.capturedFrames ();

    now += 2;
    output.suspend ();
    QVERIFY (!timer->isActive ());
    QVERIFY (pull (output));
    QCOMPARE (source.framesRead, framesRead);
    QCOMPARE (output.capturedFrames (), capturedFrames);
    now += 400;
    output.suspend ();
    now += 600;
    output.resume ();
    QVERIFY (timer->isActive ());
    QCOMPARE (timer->interval (), interval - 2);
    QCOMPARE (source.framesRead, framesRead);
    QCOMPARE (output.capturedFrames (), capturedFrames);

    now += 2;
    output.suspend ();
    now += 10000;
    output.resume ();
    QCOMPARE (timer->interval (), interval - 4);
    now += 1;
    output.resume ();
    QCOMPARE (timer->interval (), interval - 4);

    output.reset ();
    now += 5000;
    restart ();
    for (int i = 0; i < 3; ++i) QVERIFY (pull (output));
    QCOMPARE (timer->interval (), interval);
    output.reset ();
  }

  void realtimeTailStartsWithFreshPacingOrigin_data ()
  {
    QTest::addColumn<qint64> ("activeBeforeBoundaryMs");
    QTest::addColumn<qint64> ("pauseMs");
    QTest::newRow ("active-7ms") << qint64 {7} << qint64 {0};
    QTest::newRow ("paused-2ms") << qint64 {2} << qint64 {1000};
    QTest::newRow ("paused-7ms") << qint64 {7} << qint64 {10000};
  }

  void realtimeTailStartsWithFreshPacingOrigin ()
  {
    QFETCH (qint64, activeBeforeBoundaryMs);
    QFETCH (qint64, pauseMs);
    qint64 now {100000};
    QTemporaryDir directory;
    FixtureSoundOutput output {directory.filePath ("capture.wav"),
      FixtureSoundOutput::Profile::JttyStrict, 4u, nullptr, [&now] {return now;}};
    PcmSource source;
    auto timer = output.findChild<QTimer *> ();
    QVERIFY (timer);
    int referenceInterval {-1};
    for (int capture = 0; capture < 2; ++capture)
      {
        output.configureJttyCapture (766);
        output.restart (&source, {TxEvidence::TxSessionId {1}, TxEvidence::TxGeneration {1}}, 0);
        QVERIFY (pull (output));
        output.suspend ();
        now += capture == 0 ? 0 : pauseMs;
        output.resume ();
        now += capture == 0 ? 2 : activeBeforeBoundaryMs;
        QVERIFY (pull (output));
        now += 1;
        QVERIFY (pull (output));
        if (capture == 0)
          {
            referenceInterval = timer->interval ();
            QVERIFY (referenceInterval > 1);
          }
        else
          QCOMPARE (timer->interval (), referenceInterval);
        output.reset ();
        now += 5000;
      }
  }

  void onlyNormalStopPadsFt8Period_data ()
  {
    QTest::addColumn<QString> ("action");
    for (auto action : {"stop", "reset", "replace", "failure", "destroy"})
      QTest::newRow (action) << QString::fromLatin1 (action);
  }

  void onlyNormalStopPadsFt8Period ()
  {
    QFETCH (QString, action);
    QTemporaryDir directory;
    auto const path = directory.filePath ("capture.wav");
    auto output = std::make_unique<FixtureSoundOutput> (
      path, FixtureSoundOutput::Profile::Ft8Period);
    PcmSource source;
    QSignalSpy stopped {output.get (), &FixtureSoundOutput::captureStopped};
    output->restart (&source, {TxEvidence::TxSessionId {1}, TxEvidence::TxGeneration {1}}, 0);
    QVERIFY (pull (*output));
    if (action == "stop")
      {
        output->stop ();
        output->stop ();
        QCOMPARE (output->stopCount (), 1);
      }
    else if (action == "reset") output->reset ();
    else if (action == "replace") output->restart (&source, {TxEvidence::TxSessionId {1}, TxEvidence::TxGeneration {1}}, 0);
    else if (action == "failure") output->configureJttyCapture (1);
    else output.reset ();
    QCOMPARE (stopped.size (), 1);
    QCOMPARE (stopped.at (0).at (1).toLongLong (),
              action == "stop" ? qint64 {720000} : qint64 {257});
    if (action != "replace")
      QCOMPARE (capturedPcm (path).size (), action == "stop" ? 1440000 : 514);
  }

  void attenuationPersistsAcrossRestarts ()
  {
    QTemporaryDir directory;
    FixtureSoundOutput output {directory.filePath ("capture.wav")};
    AudioOutputSink * sink = &output;
    PcmSource source;
    sink->setAttenuation (20.);
    for (int capture = 0; capture < 3; ++capture)
      {
        if (capture == 2) sink->resetAttenuation ();
        sink->restart (&source, {TxEvidence::TxSessionId {1}, TxEvidence::TxGeneration {1}});
        QVERIFY (pull (output));
        sink->stop ();
        auto const pcm = capturedPcm (output.capturePath ());
        QCOMPARE (pcm.size (), 514);
        auto const expected = capture == 2 ? 10000 : 1000;
        for (int offset = 0; offset < pcm.size (); offset += 2)
          QCOMPARE (qFromLittleEndian<qint16> (pcm.constData () + offset), expected);
      }
  }

  void callbacksCanCancelCapture_data ()
  {
    QTest::addColumn<QString> ("callback");
    for (auto name : {"started", "active", "read", "non-silent", "checkpoint"})
      QTest::newRow (name) << QString::fromLatin1 (name);
  }

  void callbacksCanCancelCapture ()
  {
    QFETCH (QString, callback);
    QTemporaryDir directory;
    FixtureSoundOutput output {directory.filePath ("capture.wav")};
    PcmSource source;
    if (callback == "started")
      connect (&output, &FixtureSoundOutput::captureStarted,
               &output, &FixtureSoundOutput::reset);
    else if (callback == "active")
      connect (&output, &AudioOutputSink::audioOutputActive,
               &output, &FixtureSoundOutput::reset);
    else if (callback == "read") source.onRead = [&] {output.reset ();};
    else if (callback == "non-silent")
      connect (&output, &FixtureSoundOutput::nonSilentAudioStarted,
               &output, &FixtureSoundOutput::reset);
    else
      connect (&output, &FixtureSoundOutput::playbackCheckpoint,
               &output, &FixtureSoundOutput::reset);
    QSignalSpy stopped {&output, &FixtureSoundOutput::captureStopped};
    output.restart (&source, {TxEvidence::TxSessionId {1}, TxEvidence::TxGeneration {1}});
    for (int i = 0; i < 100 && stopped.empty (); ++i) QVERIFY (pull (output));
    QCOMPARE (stopped.size (), 1);
    auto const frames = source.framesRead;
    QVERIFY (pull (output));
    QCOMPARE (source.framesRead, frames);
    QVERIFY (!output.findChild<QTimer *> ()->isActive ());
  }
};

QTEST_GUILESS_MAIN (TestFixtureAudioOutput)
#include "test_fixture_audio_output.moc"
