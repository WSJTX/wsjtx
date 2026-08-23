#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QPointer>
#include <QThread>
#include <QVector>

#include <atomic>
#include <thread>

#include "Audio/AudioDevice.hpp"
#include "Audio/soundout.h"
#include "Audio/TxAudioQueue.hpp"
#include "Audio/TxIdentity.hpp"
#include "Audio/TxPlaybackEvidence.hpp"
#include "Audio/TxRequest.hpp"
#include "Modulator/JttyPcmFifo.hpp"
#include "Modulator/JttyTxStream.hpp"

// Unit tests for the JTTY async transmit source. These exercise the FIFO /
// served-sample mechanics, the drain predicate, and the drained() edge signal
// without any audio hardware: readData() is driven through the public
// QIODevice::read() API after opening the device with initialize().

class TestJttyTxStream : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase ();
  void gaplessConcatAndSilencePad ();
  void clearResetsCounters ();
  void clearThenEnqueueSkipsOldSamples ();
  void resetMustBeAppliedBeforeReusingCapacity ();
  void clearDuringPartialPlaybackSkipsRemainder ();
  void pendingResetProgressExcludesAbortedAudio ();
  void fifoOverflowRejectsWithoutTruncating ();
  void fifoWrappedEnqueuePreservesOrder ();
  void concurrentChainedEnqueueAndPullPreservesOrder ();
  void enqueueFitPredicate ();
  void fifoDrainStateCarriesEpochAndTotal ();
  void fifoDrainAfterResetUsesNewEpochAndTotal ();
  void fifoEpochBoundaryAfterNaturalDrainResetsTotal ();
  void unplayedBufferBytesDefaultsToBufferSize ();
  void drainGuardUsesUnplayedBufferBytes ();
  void drainedPredicate ();
  void readDoesNotEmitDrainedDirectly ();
  void timerEmitsDrainedEdge ();
  void progressTracksConsumptionAppendAndReset ();
  void progressPrecedesDrainAfterRefill ();
  void stopAppliesPendingResetAndAcknowledges ();
  void stopPreservesNewEpochSamples ();
  void drainedEmittedFromWorkerThread ();
  void sourceCommitUsesCurrentRealExtent ();
  void staleQueueEpochDoesNotStart ();
  void sourceCommitIsEmittedOncePerStart ();
  void queuedClearAndEnqueueAcknowledgeReplacement ();
  void queuedEnqueueReportsTypedFailures ();
  void pendingPayloadBudgetSurvivesAbort ();
  void pendingRequestBudgetRecoversAfterDelivery ();
};

namespace
{
  class BufferSizeSoundOutput : public SoundOutput
  {
  public:
    explicit BufferSizeSoundOutput (int bufferSize)
      : m_bufferSize {bufferSize}
    {
    }

    int bufferSize () const override { return m_bufferSize; }
    void restart (QIODevice *) override {}
    void stop () override {}

  private:
    int m_bufferSize;
  };

  class EstimatedTailSoundOutput : public BufferSizeSoundOutput
  {
  public:
    EstimatedTailSoundOutput (int bufferSize, int unplayedBufferBytes)
      : BufferSizeSoundOutput {bufferSize}
      , m_unplayedBufferBytes {unplayedBufferBytes}
    {
    }

    int unplayedBufferBytes () const override { return m_unplayedBufferBytes; }

  private:
    int m_unplayedBufferBytes;
  };

  // Default drain guard baked into JttyTxStream when start() has not run (no
  // device buffer to measure). Mirrors DEFAULT_DRAIN_GUARD in JttyTxStream.cpp.
  constexpr int DEFAULT_GUARD = 9600;

  TxEvidence::TxRequest jttyRequest (qint64 sessionId, qint64 generation,
                                     qint64 queueEpoch = -1)
  {
    TxEvidence::TxRequest request;
    request.mode = QStringLiteral ("JTTY");
    request.session_id = TxEvidence::TxSessionId {sessionId};
    request.generation = TxEvidence::TxGeneration {generation};
    request.queue_epoch = TxAudioQueueEpoch {
      queueEpoch >= 0 ? queueEpoch : sessionId};
    return request;
  }

  TxAudioQueueEpoch queueEpoch (qint64 value)
  {
    return TxAudioQueueEpoch {value};
  }

  QVector<qint16> readFrames (JttyTxStream & s, int frames)
  {
    QByteArray buf (frames * 2, '\xff');
    qint64 got = s.read (buf.data (), buf.size ());
    QVector<qint16> out;
    if (got <= 0) return out;
    int n = int (got / 2);
    qint16 const * p = reinterpret_cast<qint16 const *> (buf.constData ());
    for (int i = 0; i < n; ++i) out.append (p[i]);
    return out;
  }

  QByteArray pcmBytes (QVector<qint16> const& samples)
  {
    return QByteArray {reinterpret_cast<char const *> (samples.constData ()),
                       samples.size () * int (sizeof (qint16))};
  }
}

void TestJttyTxStream::initTestCase ()
{
  qRegisterMetaType<TxAudioQueueDrainState> ("TxAudioQueueDrainState");
  qRegisterMetaType<TxAudioQueueEnqueueFailure> (
    "TxAudioQueueEnqueueFailure");
  qRegisterMetaType<TxAudioQueueProgress> ("TxAudioQueueProgress");
}

void TestJttyTxStream::gaplessConcatAndSilencePad ()
{
  TxAudioQueue queue;
  auto const epoch = queueEpoch (1);
  queue.clear (epoch);
  JttyTxStream s {queue};
  QVERIFY (s.initialize (QIODevice::ReadOnly, AudioDevice::Mono));

  // Two messages queued before any are consumed: they must chain with no gap.
  QVERIFY (queue.enqueue (QVector<qint16> {10, 20, 30}, epoch).accepted);
  QVERIFY (queue.enqueue (QVector<qint16> {40, 50}, epoch).accepted);
  QCOMPARE (queue.progress ().total_samples, qint64 (5));

  // Pull 7 frames: 5 real samples concatenated in order, then silence padding.
  QVector<qint16> got = readFrames (s, 7);
  QCOMPARE (got.size (), 7);
  QCOMPARE (got, (QVector<qint16> {10, 20, 30, 40, 50, 0, 0}));

  QCOMPARE (queue.progress ().served_samples, qint64 (5));   // padding is not counted as real

  // A late message resumes real audio after the silence (late-message path).
  QVERIFY (queue.enqueue (QVector<qint16> {60}, epoch).accepted);
  QCOMPARE (queue.progress ().total_samples, qint64 (6));
  QVector<qint16> more = readFrames (s, 2);
  QCOMPARE (more, (QVector<qint16> {60, 0}));
  QCOMPARE (queue.progress ().served_samples, qint64 (6));
}

void TestJttyTxStream::clearResetsCounters ()
{
  TxAudioQueue queue;
  auto const firstEpoch = queueEpoch (1);
  queue.clear (firstEpoch);
  JttyTxStream s {queue};
  QVERIFY (s.initialize (QIODevice::ReadOnly, AudioDevice::Mono));
  QVERIFY (queue.enqueue (QVector<qint16> {1, 2, 3}, firstEpoch).accepted);
  (void) readFrames (s, 2);
  queue.clear (queueEpoch (2));
  QCOMPARE (queue.progress ().total_samples, qint64 (0));
  QCOMPARE (queue.progress ().served_samples, qint64 (0));
  // After clear the device pads pure silence.
  QCOMPARE (readFrames (s, 3), (QVector<qint16> {0, 0, 0}));
}

void TestJttyTxStream::clearThenEnqueueSkipsOldSamples ()
{
  JttyPcmFifo fifo {16};
  QVERIFY (fifo.enqueue (QVector<qint16> {1, 2, 3}, 1));
  fifo.clear (2);
  QVERIFY (fifo.enqueue (QVector<qint16> {4, 5}, 2));

  QCOMPARE (fifo.totalReal (), qint64 (2));
  QCOMPARE (fifo.servedReal (), qint64 (0));
  QCOMPARE (fifo.queuedReal (), qint64 (2));

  QVector<qint16> got;
  for (int i = 0; i < 4; ++i) got.append (fifo.pullSample (2));
  QCOMPARE (got, (QVector<qint16> {4, 5, 0, 0}));
  QCOMPARE (fifo.totalReal (), qint64 (2));
  QCOMPARE (fifo.servedReal (), qint64 (2));
}

void TestJttyTxStream::resetMustBeAppliedBeforeReusingCapacity ()
{
  JttyPcmFifo fifo {8};
  QVERIFY (fifo.enqueue (QVector<qint16> {1, 2, 3, 4, 5, 6, 7}, 1));
  fifo.clear (2);
  QVERIFY (!fifo.enqueue (QVector<qint16> {8, 9}, 2));

  fifo.applyPendingReset ();
  QVERIFY (fifo.enqueue (QVector<qint16> {8, 9}, 2));

  QCOMPARE (fifo.totalReal (), qint64 (2));
  QCOMPARE (fifo.queuedReal (), qint64 (2));

  QCOMPARE (fifo.pullSample (2), qint16 (8));
  QCOMPARE (fifo.pullSample (2), qint16 (9));
  QCOMPARE (fifo.pullSample (2), qint16 (0));
  QCOMPARE (fifo.servedReal (), qint64 (2));
}

void TestJttyTxStream::clearDuringPartialPlaybackSkipsRemainder ()
{
  JttyPcmFifo fifo {16};
  QVERIFY (fifo.enqueue (QVector<qint16> {1, 2, 3, 4, 5}, 1));
  QCOMPARE (fifo.pullSample (2), qint16 (1));
  QCOMPARE (fifo.pullSample (2), qint16 (2));

  fifo.clear (2);
  QCOMPARE (fifo.totalReal (), qint64 (0));
  QCOMPARE (fifo.servedReal (), qint64 (0));
  QCOMPARE (fifo.queuedReal (), qint64 (0));

  QVERIFY (fifo.enqueue (QVector<qint16> {6}, 2));
  QCOMPARE (fifo.pullSample (2), qint16 (6));
  QCOMPARE (fifo.pullSample (2), qint16 (0));
  QCOMPARE (fifo.totalReal (), qint64 (1));
  QCOMPARE (fifo.servedReal (), qint64 (1));
}

void TestJttyTxStream::pendingResetProgressExcludesAbortedAudio ()
{
  JttyPcmFifo fifo {16};
  QVERIFY (fifo.enqueue (QVector<qint16> {1, 2, 3, 4, 5}, 1));
  QCOMPARE (fifo.pullSample (2), qint16 (1));
  QCOMPARE (fifo.pullSample (2), qint16 (2));

  fifo.clear (2);
  QVERIFY (fifo.enqueue (QVector<qint16> {6, 7}, 2));

  QCOMPARE (fifo.totalReal (), qint64 (2));
  QCOMPARE (fifo.servedReal (), qint64 (0));
  QCOMPARE (fifo.queuedReal (), qint64 (2));
  QCOMPARE (fifo.pullSample (2), qint16 (6));
  QCOMPARE (fifo.totalReal (), qint64 (2));
  QCOMPARE (fifo.servedReal (), qint64 (1));
  QCOMPARE (fifo.queuedReal (), qint64 (1));
}

void TestJttyTxStream::fifoOverflowRejectsWithoutTruncating ()
{
  JttyPcmFifo fifo {4};
  QVERIFY (fifo.enqueue (QVector<qint16> {1, 2, 3}, 11));
  QVERIFY (!fifo.enqueue (QVector<qint16> {4, 5}, 11));
  QCOMPARE (fifo.totalReal (), qint64 (3));

  QVector<qint16> got;
  for (int i = 0; i < 5; ++i) got.append (fifo.pullSample (2));
  QCOMPARE (got, (QVector<qint16> {1, 2, 3, 0, 0}));
}

void TestJttyTxStream::fifoWrappedEnqueuePreservesOrder ()
{
  JttyPcmFifo fifo {5};
  QVERIFY (fifo.enqueue (QVector<qint16> {1, 2, 3, 4}, 11));
  QCOMPARE (fifo.pullSample (2), qint16 (1));
  QCOMPARE (fifo.pullSample (2), qint16 (2));
  QCOMPARE (fifo.pullSample (2), qint16 (3));

  QVERIFY (fifo.enqueue (QVector<qint16> {5, 6, 7}, 11));

  QVector<qint16> got;
  for (int i = 0; i < 5; ++i) got.append (fifo.pullSample (2));
  QCOMPARE (got, (QVector<qint16> {4, 5, 6, 7, 0}));
  QCOMPARE (fifo.totalReal (), qint64 (7));
  QCOMPARE (fifo.servedReal (), qint64 (7));
}

void TestJttyTxStream::concurrentChainedEnqueueAndPullPreservesOrder ()
{
  constexpr int sampleCount = 5000;
  constexpr int chunkSize = 17;
  JttyPcmFifo fifo {64};
  QVector<qint16> received;
  received.reserve (sampleCount);
  std::atomic<bool> producerDone {false};

  std::thread producer {[&fifo, &producerDone] {
    for (int first = 1; first <= sampleCount; first += chunkSize)
      {
        QVector<qint16> chunk;
        int const last = qMin (first + chunkSize, sampleCount + 1);
        chunk.reserve (last - first);
        for (int sample = first; sample < last; ++sample)
          {
            chunk.append (qint16 (sample));
          }
        while (!fifo.enqueue (chunk, 1))
          {
            std::this_thread::yield ();
          }
      }
    producerDone.store (true, std::memory_order_release);
  }};

  while (!producerDone.load (std::memory_order_acquire)
         || received.size () < sampleCount)
    {
      qint16 const sample = fifo.pullSample (64);
      if (sample != 0) received.append (sample);
    }
  producer.join ();

  QCOMPARE (received.size (), sampleCount);
  for (int i = 0; i < received.size (); ++i)
    {
      QCOMPARE (received.at (i), qint16 (i + 1));
    }
}

void TestJttyTxStream::enqueueFitPredicate ()
{
  QVERIFY (jttyPcmEnqueueFits (4, 0, 4));
  QVERIFY (jttyPcmEnqueueFits (4, 2, 2));
  QVERIFY (jttyPcmEnqueueFits (4, 4, 0));
  QVERIFY (!jttyPcmEnqueueFits (4, 1, 4));
  QVERIFY (!jttyPcmEnqueueFits (4, 5, 1));
  QVERIFY (!jttyPcmEnqueueFits (0, 0, 1));
  QVERIFY (!jttyPcmEnqueueFits (4, -1, 1));
}

void TestJttyTxStream::fifoDrainStateCarriesEpochAndTotal ()
{
  JttyPcmFifo fifo {16};
  QVERIFY (fifo.enqueue (QVector<qint16> {1, 2, 3}, 21));
  for (int i = 0; i < 5; ++i) (void) fifo.pullSample (2);

  auto drain = fifo.takeDrainReady ();
  QVERIFY (drain.ready);
  QCOMPARE (drain.epoch, qint64 (21));
  QCOMPARE (drain.totalAtDrain, qint64 (3));

  QVERIFY (fifo.enqueue (QVector<qint16> {4}, 21));
  for (int i = 0; i < 3; ++i) (void) fifo.pullSample (2);
  drain = fifo.takeDrainReady ();
  QVERIFY (drain.ready);
  QCOMPARE (drain.epoch, qint64 (21));
  QCOMPARE (drain.totalAtDrain, qint64 (4));

  fifo.clear (22);
  QCOMPARE (fifo.totalReal (), qint64 (0));
  QCOMPARE (fifo.servedReal (), qint64 (0));
  QVERIFY (!fifo.takeDrainReady ().ready);
}

void TestJttyTxStream::fifoDrainAfterResetUsesNewEpochAndTotal ()
{
  JttyPcmFifo fifo {16};
  QVERIFY (fifo.enqueue (QVector<qint16> {1, 2, 3}, 21));
  fifo.clear (22);
  QVERIFY (!fifo.takeDrainReady ().ready);
  QVERIFY (fifo.enqueue (QVector<qint16> {4, 5}, 22));

  for (int i = 0; i < 4; ++i) (void) fifo.pullSample (2);
  auto drain = fifo.takeDrainReady ();
  QVERIFY (drain.ready);
  QCOMPARE (drain.epoch, qint64 (22));
  QCOMPARE (drain.totalAtDrain, qint64 (2));
}

void TestJttyTxStream::fifoEpochBoundaryAfterNaturalDrainResetsTotal ()
{
  JttyPcmFifo fifo {16};
  QVERIFY (fifo.enqueue (QVector<qint16> {1, 2, 3}, 21));
  for (int i = 0; i < 5; ++i) (void) fifo.pullSample (2);

  auto drain = fifo.takeDrainReady ();
  QVERIFY (drain.ready);
  QCOMPARE (drain.epoch, qint64 (21));
  QCOMPARE (drain.totalAtDrain, qint64 (3));

  fifo.clear (22);
  QVERIFY (fifo.enqueue (QVector<qint16> {4, 5}, 22));
  for (int i = 0; i < 4; ++i) (void) fifo.pullSample (2);

  drain = fifo.takeDrainReady ();
  QVERIFY (drain.ready);
  QCOMPARE (drain.epoch, qint64 (22));
  QCOMPARE (drain.totalAtDrain, qint64 (2));
}

void TestJttyTxStream::unplayedBufferBytesDefaultsToBufferSize ()
{
  BufferSizeSoundOutput sink {123456};
  QCOMPARE (sink.unplayedBufferBytes (), sink.bufferSize ());
}

void TestJttyTxStream::drainGuardUsesUnplayedBufferBytes ()
{
  TxAudioQueue queue;
  auto const epoch = queueEpoch (23);
  queue.clear (epoch);
  QVERIFY (queue.enqueue (QVector<qint16> {7}, epoch).accepted);

  EstimatedTailSoundOutput sink {100000, 0};
  JttyTxStream stream {queue};
  stream.start (jttyRequest (23, 1), &sink);

  (void) readFrames (stream, 1 + DEFAULT_GUARD + 4800);
  QVERIFY (queue.takeDrainReady ().ready);
  stream.stop ();
}

void TestJttyTxStream::drainedPredicate ()
{
  // Nothing queued yet => not drained.
  QVERIFY (!jttyTxDrained (0, 0, 100, 0, 10));

  // Real audio still to be pulled => not drained.
  QVERIFY (!jttyTxDrained (3, 5, 50, 5, 10));

  // All pulled but trailing silence has not yet covered the buffer depth.
  QVERIFY (!jttyTxDrained (5, 5, 12, 5, 10)); // 12-5 = 7 < 10

  // All pulled and trailing silence covers the buffer depth => drained.
  QVERIFY (jttyTxDrained (5, 5, 15, 5, 10));  // 15-5 = 10 >= 10
  QVERIFY (jttyTxDrained (5, 5, 99, 5, 10));
}

void TestJttyTxStream::readDoesNotEmitDrainedDirectly ()
{
  TxAudioQueue queue;
  auto const epoch = queueEpoch (31);
  queue.clear (epoch);
  JttyTxStream s {queue};
  QVERIFY (s.initialize (QIODevice::ReadOnly, AudioDevice::Mono));
  QSignalSpy spy (&s, &JttyTxStream::drained);
  QSignalSpy progressSpy (&s, &JttyTxStream::progress);

  QVERIFY (queue.enqueue (QVector<qint16> {7, 7, 7}, epoch).accepted);
  (void) readFrames (s, 3 + DEFAULT_GUARD);
  QCOMPARE (spy.count (), 0);
  QCOMPARE (progressSpy.count (), 0);
}

void TestJttyTxStream::progressTracksConsumptionAppendAndReset ()
{
  TxAudioQueue queue;
  auto const epoch = queueEpoch (35);
  queue.clear (epoch);
  JttyTxStream stream {queue};
  QSignalSpy spy (&stream, &JttyTxStream::progress);
  QVERIFY (queue.enqueue (QVector<qint16> {1, 2, 3}, epoch).accepted);
  stream.start (jttyRequest (35, 1), nullptr);

  QTRY_COMPARE (spy.count (), 1);
  auto current = qvariant_cast<TxAudioQueueProgress> (spy.last ().at (0));
  QCOMPARE (current.epoch, epoch);
  QCOMPARE (current.served_samples, qint64 (0));
  QCOMPARE (current.total_samples, qint64 (3));
  QVERIFY (QMetaObject::invokeMethod (&stream, "pollDrain"));
  QCOMPARE (spy.count (), 1);

  QCOMPARE (readFrames (stream, 2), (QVector<qint16> {1, 2}));
  QCOMPARE (spy.count (), 1);
  QVERIFY (QMetaObject::invokeMethod (&stream, "pollDrain"));
  QCOMPARE (spy.count (), 2);
  current = qvariant_cast<TxAudioQueueProgress> (spy.last ().at (0));
  QCOMPARE (current.served_samples, qint64 (2));
  QCOMPARE (current.queued_samples, qint64 (1));

  QVERIFY (queue.enqueue (QVector<qint16> {4, 5}, epoch).accepted);
  QVERIFY (QMetaObject::invokeMethod (&stream, "pollDrain"));
  QCOMPARE (spy.count (), 3);
  current = qvariant_cast<TxAudioQueueProgress> (spy.last ().at (0));
  QCOMPARE (current.epoch, epoch);
  QCOMPARE (current.served_samples, qint64 (2));
  QCOMPARE (current.total_samples, qint64 (5));

  auto const nextEpoch = queueEpoch (36);
  queue.clear (nextEpoch);
  QVERIFY (queue.enqueue (QVector<qint16> {6}, nextEpoch).accepted);
  QVERIFY (QMetaObject::invokeMethod (&stream, "pollDrain"));
  QCOMPARE (spy.count (), 4);
  current = qvariant_cast<TxAudioQueueProgress> (spy.last ().at (0));
  QCOMPARE (current.epoch, nextEpoch);
  QCOMPARE (current.served_samples, qint64 (0));
  QCOMPARE (current.total_samples, qint64 (1));
  QCOMPARE (current.queued_samples, qint64 (1));

  queue.clear (TxAudioQueueEpoch::invalid ());
  QVERIFY (QMetaObject::invokeMethod (&stream, "pollDrain"));
  QCOMPARE (spy.count (), 4);
  stream.stop ();
  queue.clear (queueEpoch (37));
  QVERIFY (QMetaObject::invokeMethod (&stream, "pollDrain"));
  QCOMPARE (spy.count (), 4);
}

void TestJttyTxStream::progressPrecedesDrainAfterRefill ()
{
  TxAudioQueue queue;
  auto const epoch = queueEpoch (38);
  queue.clear (epoch);
  JttyTxStream stream {queue};
  QSignalSpy drainedSpy (&stream, &JttyTxStream::drained);
  QVERIFY (queue.enqueue (QVector<qint16> {1, 2, 3}, epoch).accepted);
  stream.start (jttyRequest (38, 1), nullptr);
  bool appended {false};
  bool progressPrecededDrain {false};
  connect (&stream, &JttyTxStream::drained, &stream,
           [&appended, &progressPrecededDrain] (TxAudioQueueDrainState) {
             progressPrecededDrain = appended;
           });
  connect (&stream, &JttyTxStream::progress, &stream,
           [&queue, epoch, &appended] (TxAudioQueueProgress current) {
             if (!appended && current.served_samples == current.total_samples)
               {
                 appended = queue.enqueue (QVector<qint16> {4, 5}, epoch).accepted;
               }
           });

  (void) readFrames (stream, 3 + DEFAULT_GUARD);
  QVERIFY (QMetaObject::invokeMethod (&stream, "pollDrain"));
  QVERIFY (appended);
  QVERIFY (progressPrecededDrain);
  QCOMPARE (drainedSpy.count (), 1);
  QCOMPARE (qvariant_cast<TxAudioQueueDrainState> (
              drainedSpy.first ().at (0)).total_at_drain, qint64 (3));
  QCOMPARE (readFrames (stream, 2), (QVector<qint16> {4, 5}));
  (void) readFrames (stream, DEFAULT_GUARD);
  QVERIFY (QMetaObject::invokeMethod (&stream, "pollDrain"));
  QCOMPARE (drainedSpy.count (), 2);
  QCOMPARE (qvariant_cast<TxAudioQueueDrainState> (
              drainedSpy.last ().at (0)).total_at_drain, qint64 (5));
  stream.stop ();
}

void TestJttyTxStream::stopAppliesPendingResetAndAcknowledges ()
{
  TxAudioQueue queue {8};
  auto const epoch = queueEpoch (39);
  auto const nextEpoch = queueEpoch (40);
  queue.clear (epoch);
  JttyTxStream stream {queue};
  QSignalSpy stoppedSpy (&stream, &JttyTxStream::stopped);
  QVERIFY (queue.enqueue (QVector<qint16> {1, 2, 3, 4, 5, 6}, epoch).accepted);

  // Stop during PTT lead: no audio pull has applied either reset yet.
  queue.clear (nextEpoch);
  stream.stop ();
  QCOMPARE (stoppedSpy.count (), 1);
  QVERIFY (queue.enqueue (QVector<qint16> {7, 8, 9, 10, 11, 12}, nextEpoch).accepted);
  stream.start (jttyRequest (40, 1), nullptr);
  QCOMPARE (readFrames (stream, 6), (QVector<qint16> {7, 8, 9, 10, 11, 12}));
  stream.stop ();
  stream.stop ();
  QCOMPARE (stoppedSpy.count (), 3);
}

void TestJttyTxStream::stopPreservesNewEpochSamples ()
{
  TxAudioQueue queue {8};
  auto const epoch = queueEpoch (42);
  auto const nextEpoch = queueEpoch (43);
  queue.clear (epoch);
  JttyTxStream stream {queue};
  QVERIFY (queue.enqueue (QVector<qint16> {1, 2, 3, 4, 5, 6}, epoch).accepted);
  stream.start (jttyRequest (42, 1), nullptr);
  queue.clear (nextEpoch);
  QVERIFY (queue.enqueue (QVector<qint16> {7, 8}, nextEpoch).accepted);

  stream.stop ();
  QCOMPARE (queue.progress ().queued_samples, qint64 (2));
  stream.start (jttyRequest (43, 2), nullptr);
  QCOMPARE (readFrames (stream, 3), (QVector<qint16> {7, 8, 0}));
  stream.stop ();
}

void TestJttyTxStream::timerEmitsDrainedEdge ()
{
  TxAudioQueue queue;
  auto const epoch = queueEpoch (41);
  queue.clear (epoch);
  JttyTxStream s {queue};
  QSignalSpy spy (&s, &JttyTxStream::drained);

  QVERIFY (queue.enqueue (QVector<qint16> {7, 7, 7}, epoch).accepted);
  s.start (jttyRequest (41, 1), nullptr);

  // Serve the 3 real samples plus exactly the guard worth of trailing silence.
  (void) readFrames (s, 3 + DEFAULT_GUARD);
  QTRY_COMPARE (spy.count (), 1);
  auto drain = qvariant_cast<TxAudioQueueDrainState> (spy.at (0).at (0));
  QCOMPARE (drain.epoch, epoch);
  QCOMPARE (drain.total_at_drain, qint64 (3));

  // Further silence must not re-emit (edge-triggered).
  (void) readFrames (s, 1000);
  QCOMPARE (spy.count (), 1);

  // A new message clears the drain edge; draining again emits once more.
  QVERIFY (queue.enqueue (QVector<qint16> {9}, epoch).accepted);
  (void) readFrames (s, 1 + DEFAULT_GUARD);
  QTRY_COMPARE (spy.count (), 2);
  drain = qvariant_cast<TxAudioQueueDrainState> (spy.at (1).at (0));
  QCOMPARE (drain.epoch, epoch);
  QCOMPARE (drain.total_at_drain, qint64 (4));
  s.stop ();
}

void TestJttyTxStream::drainedEmittedFromWorkerThread ()
{
  // Integration test: drive the stream the way the app does, with the object
  // moved onto a dedicated worker thread (m_audioThread in production). The
  // drain timer is a parented child, so it must ride to the worker thread and
  // tick there; a value-member timer could not be started cross-thread and
  // drained() would never fire (this test would then time out). start() and the
  // audio pull run on the worker thread; drained() must cross back to the
  // main-thread receiver.
  TxAudioQueue queue;
  auto const epoch = queueEpoch (51);
  queue.clear (epoch);
  auto * stream = new JttyTxStream {queue};
  QPointer<JttyTxStream> streamWitness {stream};
  qint64 drainedSession {-1};
  qint64 drainedTotal {-1};
  int drainedCount {0};
  QObject receiver;
  // Qt 5's QSignalSpy records through a direct connection, so explicitly queue
  // cross-thread test state onto a main-thread receiver.
  connect (stream, &JttyTxStream::drained, &receiver,
           [&drainedSession, &drainedTotal, &drainedCount]
           (TxAudioQueueDrainState drain) {
             drainedSession = drain.epoch.value ();
             drainedTotal = drain.total_at_drain;
             ++drainedCount;
           }, Qt::QueuedConnection);

  QVERIFY (queue.enqueue (QVector<qint16> {7, 7, 7}, epoch).accepted);

  QThread worker;
  stream->moveToThread (&worker);
  connect (&worker, &QThread::finished, stream, &QObject::deleteLater);

  // Queue before starting the worker so thread creation establishes the
  // cross-thread handoff.
  QMetaObject::invokeMethod (stream, [stream] {
    stream->start (jttyRequest (51, 1), nullptr);
    QByteArray buf ((3 + DEFAULT_GUARD) * 2, '\0');
    stream->read (buf.data (), buf.size ());
  }, Qt::QueuedConnection);
  worker.start ();

  QTRY_COMPARE_WITH_TIMEOUT (drainedCount, 1, 2000);
  QCOMPARE (drainedSession, qint64 (51));
  QCOMPARE (drainedTotal, qint64 (3));

  // The stream owns timers that must stop and be destroyed on their thread.
  QVERIFY (QMetaObject::invokeMethod (
    stream, "stop", Qt::BlockingQueuedConnection));
  worker.quit ();
  QVERIFY (worker.wait (2000));
  QVERIFY (streamWitness.isNull ());
}

void TestJttyTxStream::sourceCommitUsesCurrentRealExtent ()
{
  auto const snapshot = makeJttyTxStartSnapshot (jttyRequest (61, 7), 3);
  QCOMPARE (snapshot.session_id.value (), qint64 (61));
  QCOMPARE (snapshot.generation.value (), qint64 (7));
  QCOMPARE (snapshot.mode, QString {"JTTY"});
  QCOMPARE (snapshot.sample_rate_hz, 48000);
  QCOMPARE (snapshot.committed_end_sample, qint64 (3));
  QVERIFY (!snapshot.target_known);
  QVERIFY (!snapshot.diagnostic.isEmpty ());

  TxAudioQueue queue;
  auto const epoch = queueEpoch (61);
  queue.clear (epoch);
  JttyTxStream stream {queue};
  QVERIFY (queue.enqueue (QVector<qint16> {1, 2, 3}, epoch).accepted);
  TxEvidence::TxStartSnapshot committed;
  int commitCount {0};
  connect (&stream, &JttyTxStream::txSourceCommitted, &stream,
           [&committed, &commitCount] (TxEvidence::TxStartSnapshot snapshot) {
             committed = snapshot;
             ++commitCount;
           });

  stream.start (jttyRequest (61, 7), nullptr);
  QCOMPARE (commitCount, 1);
  QCOMPARE (committed.committed_end_sample, qint64 (2));
  (void) readFrames (stream, 3 + DEFAULT_GUARD);
  QCOMPARE (queue.progress ().served_samples, qint64 (3));
  stream.stop ();
}

void TestJttyTxStream::staleQueueEpochDoesNotStart ()
{
  TxAudioQueue queue;
  auto const current = queueEpoch (81);
  queue.clear (current);
  QVERIFY (queue.enqueue (QVector<qint16> {7, 8, 9}, current).accepted);

  JttyTxStream stream {queue};
  TxEvidence::TxStartSnapshot committed;
  int commitCount {0};
  connect (&stream, &JttyTxStream::txSourceCommitted, &stream,
           [&committed, &commitCount] (TxEvidence::TxStartSnapshot snapshot) {
             committed = snapshot;
             ++commitCount;
           });
  stream.start (jttyRequest (80, 1), nullptr);
  QVERIFY (!stream.isActive ());
  QCOMPARE (commitCount, 0);

  stream.start (jttyRequest (81, 2), nullptr);
  QVERIFY (stream.isActive ());
  QCOMPARE (commitCount, 1);
  QCOMPARE (committed.committed_end_sample, qint64 (2));
  stream.stop ();
}

void TestJttyTxStream::sourceCommitIsEmittedOncePerStart ()
{
  TxAudioQueue queue;
  JttyTxStream stream {queue};
  QVector<TxEvidence::TxStartSnapshot> commits;
  connect (&stream, &JttyTxStream::txSourceCommitted, &stream,
           [&commits] (TxEvidence::TxStartSnapshot snapshot) {commits.append (snapshot);});

  queue.clear (queueEpoch (71));
  stream.start (jttyRequest (71, 1), nullptr);
  stream.start (jttyRequest (71, 1), nullptr);
  QCOMPARE (commits.size (), 1);
  QCOMPARE (commits.at (0).committed_end_sample, qint64 (-1));
  stream.stop ();

  queue.clear (queueEpoch (72));
  stream.start (jttyRequest (72, 2), nullptr);
  QCOMPARE (commits.size (), 2);
  QCOMPARE (commits.at (0).session_id.value (), qint64 (71));
  QCOMPARE (commits.at (1).session_id.value (), qint64 (72));
  QCOMPARE (commits.at (1).generation.value (), qint64 (2));
  stream.stop ();
}

void TestJttyTxStream::queuedClearAndEnqueueAcknowledgeReplacement ()
{
  TxAudioQueue queue {8};
  auto const first = queueEpoch (91);
  auto const replacement = queueEpoch (92);
  queue.clear (first);
  queue.applyPendingReset ();
  QVector<qint16> const aborted {1, 2, 3, 4, 5, 6, 7, 8};
  QVector<qint16> const replacementSamples {11, 12, 13, 14, 15, 16, 17, 18};
  QVERIFY (queue.enqueue (aborted, first).accepted);
  QCOMPARE (queue.enqueue (QVector<qint16> {9}, first).failure,
            TxAudioQueueEnqueueFailure::Capacity);

  auto * stream = new JttyTxStream {queue};
  QPointer<JttyTxStream> streamWitness {stream};
  QObject receiver;
  int acceptedCount {0};
  int failedCount {0};
  qint64 acceptedId {-1};
  qint64 acceptedSamples {-1};
  TxAudioQueueProgress signaledProgress;
  TxAudioQueueProgress callbackProgress;
  connect (stream, &JttyTxStream::enqueueAccepted, &receiver,
           [&] (qint64 enqueueId, qint64 sampleCount,
                TxAudioQueueProgress progress) {
             ++acceptedCount;
             acceptedId = enqueueId;
             acceptedSamples = sampleCount;
             signaledProgress = progress;
             callbackProgress = queue.progress ();
           }, Qt::QueuedConnection);
  connect (stream, &JttyTxStream::enqueueFailed, &receiver,
           [&] {++failedCount;}, Qt::QueuedConnection);

  QThread audioThread;
  stream->moveToThread (&audioThread);
  connect (&audioThread, &QThread::finished, stream, &QObject::deleteLater);

  QMetaObject::invokeMethod (stream, [stream, replacement] {
    stream->clearQueue (replacement);
  }, Qt::QueuedConnection);
  QMetaObject::invokeMethod (
    stream, [stream, replacement, replacementSamples] {
      stream->enqueuePcm (pcmBytes (replacementSamples), replacement, 101);
    }, Qt::QueuedConnection);
  audioThread.start ();

  QElapsedTimer callbackWait;
  callbackWait.start ();
  while (!acceptedCount && !failedCount && callbackWait.elapsed () < 2000)
    {
      QCoreApplication::processEvents (QEventLoop::AllEvents, 10);
      QThread::msleep (1);
    }

  bool const stopped = QMetaObject::invokeMethod (
    stream, "stop", Qt::BlockingQueuedConnection);
  audioThread.quit ();
  bool const threadStopped = audioThread.wait (2000);

  QVERIFY (stopped);
  QVERIFY (threadStopped);
  QVERIFY (streamWitness.isNull ());
  QCOMPARE (failedCount, 0);
  QCOMPARE (acceptedCount, 1);
  QCOMPARE (acceptedId, qint64 (101));
  QCOMPARE (acceptedSamples, qint64 (replacementSamples.size ()));
  QCOMPARE (signaledProgress.epoch, replacement);
  QCOMPARE (signaledProgress.queued_samples,
            qint64 (replacementSamples.size ()));
  QCOMPARE (signaledProgress.total_samples,
            qint64 (replacementSamples.size ()));
  QCOMPARE (callbackProgress.epoch, replacement);
  QCOMPARE (callbackProgress.total_samples,
            qint64 (replacementSamples.size ()));
  for (auto sample : replacementSamples)
    {
      QCOMPARE (queue.pullSample (0), sample);
    }
  QCOMPARE (queue.pullSample (0), qint16 (0));
}

void TestJttyTxStream::queuedEnqueueReportsTypedFailures ()
{
  TxAudioQueue queue {2};
  auto const current = queueEpoch (101);
  queue.clear (current);
  queue.applyPendingReset ();
  JttyTxStream stream {queue};
  QSignalSpy accepted (&stream, &JttyTxStream::enqueueAccepted);
  QSignalSpy failed (&stream, &JttyTxStream::enqueueFailed);

  stream.enqueuePcm (pcmBytes (QVector<qint16> {1, 2}), current, 201);
  stream.enqueuePcm (pcmBytes (QVector<qint16> {3}), current, 202);
  stream.enqueuePcm (pcmBytes (QVector<qint16> {4}), queueEpoch (102), 203);

  QCOMPARE (accepted.count (), 1);
  QCOMPARE (failed.count (), 2);
  QCOMPARE (failed.at (0).at (0).value<TxAudioQueueEpoch> (), current);
  QCOMPARE (failed.at (0).at (1).toLongLong (), qint64 (202));
  QCOMPARE (failed.at (0).at (2).value<TxAudioQueueEnqueueFailure> (),
            TxAudioQueueEnqueueFailure::Capacity);
  QCOMPARE (failed.at (1).at (1).toLongLong (), qint64 (203));
  QCOMPARE (failed.at (1).at (2).value<TxAudioQueueEnqueueFailure> (),
            TxAudioQueueEnqueueFailure::StaleEpoch);
}

void TestJttyTxStream::pendingPayloadBudgetSurvivesAbort ()
{
  TxAudioQueue queue;
  JttyTxStream stream {queue};
  auto const first = queueEpoch (1);
  auto const replacement = queueEpoch (2);
  stream.clearQueue (first);
  QSignalSpy accepted (&stream, &JttyTxStream::enqueueAccepted);
  QSignalSpy failed (&stream, &JttyTxStream::enqueueFailed);
  QByteArray const full (int (TxAudioQueue::defaultCapacity () * sizeof (qint16)), '\0');

  stream.queuePcm (full, first, 1);
  stream.clearQueue (replacement);
  stream.queuePcm (pcmBytes ({7}), replacement, 2);
  QCOMPARE (failed.count (), 1);
  QCOMPARE (failed.at (0).at (2).value<TxAudioQueueEnqueueFailure> (),
            TxAudioQueueEnqueueFailure::Capacity);

  QCoreApplication::sendPostedEvents (&stream, QEvent::MetaCall);
  QCOMPARE (accepted.count (), 0);
  QCOMPARE (failed.count (), 2);
  QCOMPARE (failed.at (1).at (2).value<TxAudioQueueEnqueueFailure> (),
            TxAudioQueueEnqueueFailure::StaleEpoch);

  stream.queuePcm (full, replacement, 3);
  QCoreApplication::sendPostedEvents (&stream, QEvent::MetaCall);
  QCOMPARE (accepted.count (), 1);
}

void TestJttyTxStream::pendingRequestBudgetRecoversAfterDelivery ()
{
  TxAudioQueue queue;
  JttyTxStream stream {queue};
  auto const epoch = queueEpoch (1);
  stream.clearQueue (epoch);
  QSignalSpy accepted (&stream, &JttyTxStream::enqueueAccepted);
  QSignalSpy failed (&stream, &JttyTxStream::enqueueFailed);
  for (int id = 1; id <= 65; ++id) stream.queuePcm (pcmBytes ({7}), epoch, id);
  QCOMPARE (failed.count (), 1);
  QCOMPARE (failed.at (0).at (1).toLongLong (), qint64 (65));

  QCoreApplication::sendPostedEvents (&stream, QEvent::MetaCall);
  QCOMPARE (accepted.count (), 64);
  stream.queuePcm (pcmBytes ({8}), epoch, 66);
  QCoreApplication::sendPostedEvents (&stream, QEvent::MetaCall);
  QCOMPARE (accepted.count (), 65);
  QCOMPARE (failed.count (), 1);
}

QTEST_MAIN (TestJttyTxStream)
#include "test_jtty_txstream.moc"
