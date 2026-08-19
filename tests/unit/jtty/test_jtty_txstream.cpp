#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QThread>
#include <QVector>

#include "Audio/AudioDevice.hpp"
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
  void clearDuringPartialPlaybackSkipsRemainder ();
  void pendingResetProgressExcludesAbortedAudio ();
  void fifoOverflowRejectsWithoutTruncating ();
  void fifoWrappedEnqueuePreservesOrder ();
  void enqueueFitPredicate ();
  void fifoDrainStateCarriesEpochAndTotal ();
  void fifoDrainAfterResetUsesNewEpochAndTotal ();
  void fifoEpochBoundaryAfterNaturalDrainResetsTotal ();
  void drainedPredicate ();
  void readDoesNotEmitDrainedDirectly ();
  void timerEmitsDrainedEdge ();
  void drainedEmittedFromWorkerThread ();
  void sourceCommitUsesCurrentRealExtent ();
  void staleQueueEpochDoesNotStart ();
  void sourceCommitIsEmittedOncePerStart ();
};

namespace
{
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
}

void TestJttyTxStream::initTestCase ()
{
  qRegisterMetaType<TxAudioQueueDrainState> ("TxAudioQueueDrainState");
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

  QVERIFY (queue.enqueue (QVector<qint16> {7, 7, 7}, epoch).accepted);
  (void) readFrames (s, 3 + DEFAULT_GUARD);
  QCOMPARE (spy.count (), 0);
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
  JttyTxStream s {queue};
  qint64 drainedSession {-1};
  qint64 drainedTotal {-1};
  int drainedCount {0};
  QObject receiver;
  // Qt 5's QSignalSpy records through a direct connection, so explicitly queue
  // cross-thread test state onto a main-thread receiver.
  connect (&s, &JttyTxStream::drained, &receiver,
           [&drainedSession, &drainedTotal, &drainedCount]
           (TxAudioQueueDrainState drain) {
             drainedSession = drain.epoch.value ();
             drainedTotal = drain.total_at_drain;
             ++drainedCount;
           }, Qt::QueuedConnection);

  QVERIFY (queue.enqueue (QVector<qint16> {7, 7, 7}, epoch).accepted);

  QThread worker;
  s.moveToThread (&worker);

  // Queue before starting the worker so thread creation establishes the
  // cross-thread handoff.
  QMetaObject::invokeMethod (&s, [&s] {
    s.start (jttyRequest (51, 1), nullptr);
    QByteArray buf ((3 + DEFAULT_GUARD) * 2, '\0');
    s.read (buf.data (), buf.size ());
  }, Qt::QueuedConnection);
  worker.start ();

  QTRY_COMPARE_WITH_TIMEOUT (drainedCount, 1, 2000);
  QCOMPARE (drainedSession, qint64 (51));
  QCOMPARE (drainedTotal, qint64 (3));

  // Tear down on the worker thread: stop the timer on its own thread and
  // re-home the object to this thread for safe destruction. moveToThread must
  // run on the object's current (worker) thread, so do it before the worker exits.
  QThread * const home = QThread::currentThread ();
  QMetaObject::invokeMethod (&s, [&s, home] {
    s.stop ();
    s.moveToThread (home);
  }, Qt::BlockingQueuedConnection);
  worker.quit ();
  QVERIFY (worker.wait (2000));
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

QTEST_MAIN (TestJttyTxStream)
#include "test_jtty_txstream.moc"
