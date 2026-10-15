#include <QtTest>
#include <QBuffer>
#include <QPointer>

#include "Audio/soundout.h"

class TestSoundOutput : public QObject
{
  Q_OBJECT

private Q_SLOTS:
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
    connect (&output, &AudioOutputSink::error, this, [&] (QString const&) {
      retiredBeforeNotification &= previousStream.isNull ();
    });
    QBuffer source;
    source.setData (QByteArray (1024, '\0'));
    QVERIFY (source.open (QIODevice::ReadOnly));

    output.restart (&source);

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

    output.restart (&source);
    QCOMPARE (output.bufferSize (), 0);
    QCOMPARE (active.count (), 0);
    QCOMPARE (errors.count (), 1);
    QCOMPARE (snapshots.size (), 2);
    QVERIFY (!snapshots.back ().available);
    QCOMPARE (snapshots.back ().backend_start_sequence, qint64 {2});
  }
};

QTEST_GUILESS_MAIN (TestSoundOutput)
#include "test_sound_output.moc"
