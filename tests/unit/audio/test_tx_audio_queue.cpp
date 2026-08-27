#include <QtTest>

#include <type_traits>

#include "Audio/TxAudioQueue.hpp"
#include "Audio/TxAudioQueueEpochSnapshot.hpp"
#include "Modulator/JttyPcmFifo.hpp"

namespace
{
  TxAudioQueueEpoch epoch (qint64 value)
  {
    return TxAudioQueueEpoch {value};
  }
}

class TestTxAudioQueue : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void epoch_snapshot_requires_stable_even_sequence ()
  {
    TxAudioQueueEpoch snapshot;
    QVERIFY (!TxAudioQueueDetail::tryMakeEpochSnapshot (1, 11, 1, snapshot));
    QVERIFY (!TxAudioQueueDetail::tryMakeEpochSnapshot (2, 11, 4, snapshot));
    QVERIFY (TxAudioQueueDetail::tryMakeEpochSnapshot (6, 11, 6, snapshot));
    QCOMPARE (snapshot, epoch (11));
  }

  Q_SLOT void exposes_value_types_and_capacity ()
  {
    static_assert (!std::is_copy_constructible<TxAudioQueue>::value,
                   "the queue must not be copied");
    static_assert (!std::is_copy_assignable<TxAudioQueue>::value,
                   "the queue must not be copy assigned");

    TxAudioQueue queue;
    JttyPcmFifo fifo {TxAudioQueue::defaultCapacity ()};
    QCOMPARE (queue.capacity (), TxAudioQueue::defaultCapacity ());
    QCOMPARE (queue.capacity (), fifo.capacity ());
    QVERIFY (qMetaTypeId<TxAudioQueueEpoch> () != QMetaType::UnknownType);
    QVERIFY (qMetaTypeId<TxAudioQueueProgress> () != QMetaType::UnknownType);
    QVERIFY (qMetaTypeId<TxAudioQueueDrainState> () != QMetaType::UnknownType);
    QVERIFY (qMetaTypeId<TxAudioQueueEnqueueResult> () != QMetaType::UnknownType);
    QVERIFY (epoch (1) == epoch (1));
    QVERIFY (epoch (1) != epoch (2));
  }

  Q_SLOT void enqueues_vectors_and_raw_samples_gaplessly ()
  {
    TxAudioQueue queue {5};
    auto const current = epoch (11);
    queue.clear (current);
    queue.applyPendingReset ();

    QVector<qint16> const first {1, 2, 3};
    auto result = queue.enqueue (first, current);
    QVERIFY (result.accepted);
    QCOMPARE (result.progress.epoch, current);
    QCOMPARE (result.progress.queued_samples, qint64 (3));
    QCOMPARE (result.progress.served_samples, qint64 (0));
    QCOMPARE (result.progress.total_samples, qint64 (3));

    QCOMPARE (queue.pullSample (10), qint16 (1));
    QCOMPARE (queue.pullSample (10), qint16 (2));

    qint16 const second[] {4, 5, 6};
    result = queue.enqueue (second, 3, current);
    QVERIFY (result.accepted);
    QCOMPARE (result.progress.queued_samples, qint64 (4));
    QCOMPARE (result.progress.served_samples, qint64 (2));
    QCOMPARE (result.progress.total_samples, qint64 (6));

    QCOMPARE (queue.pullSample (10), qint16 (3));
    QCOMPARE (queue.pullSample (10), qint16 (4));
    QCOMPARE (queue.pullSample (10), qint16 (5));
    QCOMPARE (queue.pullSample (10), qint16 (6));
    QCOMPARE (queue.pullSample (10), qint16 (0));

    auto const progress = queue.progress ();
    QCOMPARE (progress.queued_samples, qint64 (0));
    QCOMPARE (progress.served_samples, qint64 (6));
    QCOMPARE (progress.total_samples, qint64 (6));
  }

  Q_SLOT void rejects_stale_epoch_with_authoritative_progress ()
  {
    TxAudioQueue queue {4};
    auto const current = epoch (21);
    queue.clear (current);
    queue.applyPendingReset ();
    QVERIFY (queue.enqueue (QVector<qint16> {7, 8}, current).accepted);

    qint16 const stale_sample {9};
    auto const rejected = queue.enqueue (&stale_sample, 1, epoch (22));
    QVERIFY (!rejected.accepted);
    QCOMPARE (rejected.progress.epoch, current);
    QCOMPARE (rejected.progress.queued_samples, qint64 (2));
    QCOMPARE (rejected.progress.served_samples, qint64 (0));
    QCOMPARE (rejected.progress.total_samples, qint64 (2));

    auto const empty = queue.enqueue (nullptr, 0, current);
    QVERIFY (empty.accepted);
    QCOMPARE (empty.progress.total_samples, qint64 (2));
    QVERIFY (!queue.enqueue (nullptr, 0, epoch (99)).accepted);
  }

  Q_SLOT void pending_reset_excludes_aborted_samples ()
  {
    TxAudioQueue queue {8};
    auto const aborted = epoch (31);
    auto const replacement = epoch (32);
    queue.clear (aborted);
    queue.applyPendingReset ();
    QVERIFY (queue.enqueue (QVector<qint16> {1, 2, 3, 4}, aborted).accepted);
    QCOMPARE (queue.pullSample (0), qint16 (1));

    queue.clear (replacement);
    auto progress = queue.progress ();
    QCOMPARE (progress.epoch, replacement);
    QCOMPARE (progress.queued_samples, qint64 (0));
    QCOMPARE (progress.served_samples, qint64 (0));
    QCOMPARE (progress.total_samples, qint64 (0));

    qint16 const samples[] {8, 9};
    auto const result = queue.enqueue (samples, 2, replacement);
    QVERIFY (result.accepted);
    QCOMPARE (result.progress.queued_samples, qint64 (2));
    QCOMPARE (result.progress.total_samples, qint64 (2));

    queue.applyPendingReset ();
    QCOMPARE (queue.pullSample (0), qint16 (8));
    QCOMPARE (queue.pullSample (0), qint16 (9));
    progress = queue.progress ();
    QCOMPARE (progress.served_samples, qint64 (2));
    QCOMPARE (progress.total_samples, qint64 (2));
  }

  Q_SLOT void overflow_leaves_accounting_unchanged ()
  {
    TxAudioQueue queue {3};
    auto const current = epoch (41);
    queue.clear (current);
    queue.applyPendingReset ();
    QVERIFY (queue.enqueue (QVector<qint16> {10, 11}, current).accepted);

    auto const rejected = queue.enqueue (QVector<qint16> {12, 13}, current);
    QVERIFY (!rejected.accepted);
    QCOMPARE (rejected.progress.queued_samples, qint64 (2));
    QCOMPARE (rejected.progress.total_samples, qint64 (2));

    QCOMPARE (queue.pullSample (0), qint16 (10));
    QVERIFY (queue.enqueue (QVector<qint16> {12, 13}, current).accepted);
    QCOMPARE (queue.pullSample (0), qint16 (11));
    QCOMPARE (queue.pullSample (0), qint16 (12));
    QCOMPARE (queue.pullSample (0), qint16 (13));
  }

  Q_SLOT void reports_drain_epoch_total_and_rearms ()
  {
    TxAudioQueue queue {4};
    auto const current = epoch (51);
    queue.clear (current);
    queue.applyPendingReset ();
    QVERIFY (queue.enqueue (QVector<qint16> {20, 21}, current).accepted);

    QCOMPARE (queue.pullSample (2), qint16 (20));
    QCOMPARE (queue.pullSample (2), qint16 (21));
    QCOMPARE (queue.pullSample (2), qint16 (0));
    QVERIFY (!queue.takeDrainReady ().ready);
    QCOMPARE (queue.pullSample (2), qint16 (0));

    auto drain = queue.takeDrainReady ();
    QVERIFY (drain.ready);
    QCOMPARE (drain.epoch, current);
    QCOMPARE (drain.total_at_drain, qint64 (2));
    QVERIFY (!queue.takeDrainReady ().ready);

    qint16 const next {22};
    QVERIFY (queue.enqueue (&next, 1, current).accepted);
    QCOMPARE (queue.pullSample (0), qint16 (22));
    drain = queue.takeDrainReady ();
    QVERIFY (drain.ready);
    QCOMPARE (drain.epoch, current);
    QCOMPARE (drain.total_at_drain, qint64 (3));
  }

  Q_SLOT void reset_suppresses_stale_drain ()
  {
    TxAudioQueue queue {2};
    auto const first = epoch (61);
    auto const second = epoch (62);
    queue.clear (first);
    queue.applyPendingReset ();
    QVERIFY (queue.enqueue (QVector<qint16> {30}, first).accepted);
    QCOMPARE (queue.pullSample (0), qint16 (30));

    queue.clear (second);
    auto const stale = queue.takeDrainReady ();
    QVERIFY (!stale.ready);
    QCOMPARE (stale.epoch, second);
    QCOMPARE (stale.total_at_drain, qint64 (0));
  }
};

QTEST_GUILESS_MAIN (TestTxAudioQueue)

#include "test_tx_audio_queue.moc"
