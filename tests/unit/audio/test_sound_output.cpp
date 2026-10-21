#include <QtTest>
#include <QBuffer>
#include <QPointer>

#include "Audio/soundout.h"

class TestSoundOutput : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase ()
  {
    qRegisterMetaType<TxEvidence::TxPlaybackContext> ();
    qRegisterMetaType<TxEvidence::TxOutputError> ();
    qRegisterMetaType<TxEvidence::TxRawPlayoutSnapshot> ();
  }

  void failed_reconfiguration_retires_previous_stream ()
  {
    SoundOutput output;
    QAudioFormat format;
    format.setCodec ("audio/pcm");
    format.setSampleRate (48000);
    format.setSampleSize (16);
    format.setSampleType (QAudioFormat::SignedInt);
    format.setChannelCount (1);
    format.setByteOrder (QAudioFormat::LittleEndian);
    QVERIFY (format.isValid ());
    output.m_stream.reset (new QAudioOutput (QAudioDeviceInfo {}, format));
    QPointer<QAudioOutput> previousStream {output.m_stream.data ()};
    output.setFormat (QAudioDeviceInfo {}, 1);

    QSignalSpy active {&output, &AudioOutputSink::audioOutputActive};
    QSignalSpy errors {&output, &AudioOutputSink::error};
    QList<TxEvidence::TxRawPlayoutSnapshot> snapshots;
    bool retiredBeforeNotification {true};
    connect (&output, &AudioOutputSink::rawTxPlayoutSnapshot, this,
             [&] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {
               retiredBeforeNotification &= previousStream.isNull ();
               snapshots.append (snapshot);
             });
    connect (&output, &AudioOutputSink::error, this, [&] (TxEvidence::TxOutputError const&) {
      retiredBeforeNotification &= previousStream.isNull ();
    });
    QBuffer source;
    source.setData (QByteArray (1024, '\0'));
    QVERIFY (source.open (QIODevice::ReadOnly));

    TxEvidence::TxPlaybackContext const contextA {TxEvidence::TxSessionId {101}, TxEvidence::TxGeneration {201}};
    TxEvidence::TxPlaybackContext const contextB {TxEvidence::TxSessionId {102}, TxEvidence::TxGeneration {202}};
    output.restart (&source, contextA);

    QVERIFY (previousStream.isNull ());
    QVERIFY (retiredBeforeNotification);
    QCOMPARE (output.bufferSize (), 0);
    QCOMPARE (active.count (), 0);
    QCOMPARE (errors.count (), 1);
    QCOMPARE (source.pos (), qint64 {0});
    QCOMPARE (snapshots.size (), 1);
    QVERIFY (snapshots.front ().start_event);
    QVERIFY (!snapshots.front ().available);
    QCOMPARE (snapshots.front ().state, TxEvidence::TxBackendState::Unavailable);
    QCOMPARE (snapshots.front ().error, TxEvidence::TxBackendError::Open);
    QCOMPARE (snapshots.front ().backend_start_sequence, qint64 {1});
    QCOMPARE (snapshots.front ().context, contextA);
    auto const firstError = qvariant_cast<TxEvidence::TxOutputError> (errors.front ().front ());
    QCOMPARE (firstError.context, contextA);
    QCOMPARE (firstError.error, TxEvidence::TxBackendError::Open);

    output.restart (&source, contextB);
    QCOMPARE (output.bufferSize (), 0);
    QCOMPARE (active.count (), 0);
    QCOMPARE (errors.count (), 2);
    QCOMPARE (snapshots.size (), 2);
    QVERIFY (!snapshots.back ().available);
    QCOMPARE (snapshots.back ().backend_start_sequence, qint64 {2});
    QCOMPARE (snapshots.back ().context, contextB);
    auto const secondError = qvariant_cast<TxEvidence::TxOutputError> (errors.back ().front ());
    QCOMPARE (secondError.context, contextB);
    QCOMPARE (secondError.error, TxEvidence::TxBackendError::Open);
  }

  void queued_snapshots_and_errors_retain_their_original_context ()
  {
    SoundOutput output;
    output.setFormat (QAudioDeviceInfo {}, 1);
    QList<TxEvidence::TxRawPlayoutSnapshot> snapshots;
    QList<TxEvidence::TxOutputError> errors;
    QObject receiver;
    connect (&output, &AudioOutputSink::rawTxPlayoutSnapshot, &receiver,
             [&] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {
               snapshots.append (snapshot);
             }, Qt::QueuedConnection);
    connect (&output, &AudioOutputSink::error, &receiver,
             [&] (TxEvidence::TxOutputError const& failure) {
               errors.append (failure);
             }, Qt::QueuedConnection);
    TxEvidence::TxPlaybackContext const contextA {TxEvidence::TxSessionId {101}, TxEvidence::TxGeneration {201}};
    TxEvidence::TxPlaybackContext const contextB {TxEvidence::TxSessionId {102}, TxEvidence::TxGeneration {202}};
    output.restart (nullptr, contextA);
    output.stop ();
    output.restart (nullptr, contextB);
    output.reset ();
    QVERIFY (snapshots.isEmpty ());
    QVERIFY (errors.isEmpty ());

    QCoreApplication::sendPostedEvents (&receiver, QEvent::MetaCall);

    QCOMPARE (snapshots.size (), 4);
    QCOMPARE (errors.size (), 2);
    QCOMPARE (snapshots.at (0).context, contextA);
    QCOMPARE (snapshots.at (1).context, contextA);
    QCOMPARE (snapshots.at (2).context, contextB);
    QCOMPARE (snapshots.at (3).context, contextB);
    QVERIFY (snapshots.at (0).start_event);
    QVERIFY (!snapshots.at (1).start_event);
    QCOMPARE (errors.at (0).context, contextA);
    QCOMPARE (errors.at (1).context, contextB);
  }

  void stale_backend_callbacks_cannot_activate_a_replacement ()
  {
    SoundOutput output;
    output.setFormat (QAudioDeviceInfo {}, 1);
    TxEvidence::TxPlaybackContext const context {TxEvidence::TxSessionId {101}, TxEvidence::TxGeneration {201}};
    output.restart (nullptr, context);
    auto const oldSequence = output.m_backendStartSequence;
    QSignalSpy active {&output, &AudioOutputSink::audioOutputActive};
    QSignalSpy snapshots {&output, &AudioOutputSink::rawTxPlayoutSnapshot};
    QVERIFY (QMetaObject::invokeMethod (&output, [&] {
      output.handleStateChanged (QAudio::ActiveState, context, oldSequence);
    }, Qt::QueuedConnection));
    output.restart (nullptr, context);
    QAudioFormat format;
    format.setCodec ("audio/pcm");
    format.setSampleRate (48000);
    format.setSampleSize (16);
    format.setSampleType (QAudioFormat::SignedInt);
    format.setChannelCount (1);
    output.m_stream.reset (new QAudioOutput (QAudioDeviceInfo {}, format));
    snapshots.clear ();

    QCoreApplication::sendPostedEvents (&output, QEvent::MetaCall);

    QCOMPARE (active.count (), 0);
    QCOMPARE (snapshots.count (), 0);
  }
};

QTEST_GUILESS_MAIN (TestSoundOutput)
#include "test_sound_output.moc"
