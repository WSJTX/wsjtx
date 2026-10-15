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
  void resetCancelsReadsAndCannotBeResumed ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    FixtureSoundOutput output {directory.filePath ("capture.wav")};
    AudioOutputSink * sink = &output;
    PcmSource source;
    QSignalSpy stopped {&output, &FixtureSoundOutput::captureStopped};
    sink->restart (&source);
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
    sink->restart (&source);
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
    QTest::addColumn<bool> ("acceleratedTail");
    QTest::newRow ("ft8") << false;
    QTest::newRow ("jtty-realtime-tail") << true;
  }

  void pauseDoesNotBecomeCatchupTime ()
  {
    QFETCH (bool, acceleratedTail);
    QTemporaryDir directory;
    FixtureSoundOutput output {directory.filePath ("capture.wav"),
      acceleratedTail ? FixtureSoundOutput::Profile::JttyStrict
                      : FixtureSoundOutput::Profile::Ft8Period,
      acceleratedTail ? 4u : 1u};
    if (acceleratedTail) output.configureJttyCapture (257);
    PcmSource source;
    output.restart (&source, 0);
    for (int i = 0; i < 16; ++i) QVERIFY (pull (output));
    auto timer = output.findChild<QTimer *> ();
    QVERIFY (timer);
    auto const remaining = timer->remainingTime ();
    QVERIFY (remaining > 100);
    output.suspend ();
    QTest::qSleep (400);
    output.resume ();
    QVERIFY (timer->remainingTime () > 100);
    QVERIFY (qAbs (timer->remainingTime () - remaining) < 50);
    output.reset ();
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
    output->restart (&source, 0);
    QVERIFY (pull (*output));
    if (action == "stop")
      {
        output->stop ();
        output->stop ();
        QCOMPARE (output->stopCount (), 1);
      }
    else if (action == "reset") output->reset ();
    else if (action == "replace") output->restart (&source, 0);
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
        sink->restart (&source);
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
    output.restart (&source);
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
