#include "TxAudioQueue.hpp"

#include <atomic>

#include "TxAudioQueueEpochSnapshot.hpp"
#include "Modulator/JttyPcmFifo.hpp"

class TxAudioQueue::Impl
{
public:
  explicit Impl (qint64 capacity_samples)
    : fifo {capacity_samples}
  {
  }

  bool tryEpoch (TxAudioQueueEpoch& result) const noexcept
  {
    auto const before = epoch_sequence.load (std::memory_order_acquire);
    if (before & 1)
      {
        return false;
      }

    auto const current_epoch = epoch.load (std::memory_order_relaxed);
    // Keep payload reads ahead of the sequence validation load.
    std::atomic_thread_fence (std::memory_order_acquire);
    auto const after = epoch_sequence.load (std::memory_order_relaxed);
    return TxAudioQueueDetail::tryMakeEpochSnapshot (
      before, current_epoch, after, result);
  }

  void clear (TxAudioQueueEpoch value)
  {
    epoch_sequence.fetch_add (1, std::memory_order_acq_rel);
    fifo.clear (value.value ());
    epoch.store (value.value (), std::memory_order_relaxed);
    epoch_sequence.fetch_add (1, std::memory_order_release);
  }

  JttyPcmFifo fifo;
  std::atomic<qint64> epoch {0};
  std::atomic<quint64> epoch_sequence {0};
};

TxAudioQueue::TxAudioQueue ()
  : TxAudioQueue {defaultCapacity ()}
{
}

TxAudioQueue::TxAudioQueue (qint64 capacity_samples)
  : impl_ {new Impl {capacity_samples}}
{
}

TxAudioQueue::~TxAudioQueue () = default;

TxAudioQueueEnqueueResult TxAudioQueue::enqueue (
    QVector<qint16> const& samples, TxAudioQueueEpoch epoch)
{
  return enqueue (samples.constData (), samples.size (), epoch);
}

TxAudioQueueEnqueueResult TxAudioQueue::enqueue (
    qint16 const * samples, qint64 count, TxAudioQueueEpoch epoch)
{
  bool accepted {false};
  TxAudioQueueEpoch current_epoch;
  if (impl_->tryEpoch (current_epoch) && epoch == current_epoch)
    {
      accepted = impl_->fifo.enqueue (samples, count, epoch.value ());
    }
  return TxAudioQueueEnqueueResult {accepted, progress ()};
}

void TxAudioQueue::clear (TxAudioQueueEpoch epoch)
{
  impl_->clear (epoch);
}

void TxAudioQueue::applyPendingReset () noexcept
{
  impl_->fifo.applyPendingReset ();
}

qint16 TxAudioQueue::pullSample (qint64 drain_guard) noexcept
{
  return impl_->fifo.pullSample (drain_guard);
}

TxAudioQueueDrainState TxAudioQueue::takeDrainReady () noexcept
{
  TxAudioQueueEpoch epoch_before;
  if (!impl_->tryEpoch (epoch_before))
    {
      return TxAudioQueueDrainState {};
    }

  auto const raw = impl_->fifo.takeDrainReady ();
  TxAudioQueueEpoch epoch;
  if (!impl_->tryEpoch (epoch) || !raw.ready
      || epoch_before != epoch
      || raw.epoch != epoch.value ())
    {
      return TxAudioQueueDrainState {false, epoch, 0};
    }
  return TxAudioQueueDrainState {true, epoch, raw.totalAtDrain};
}

TxAudioQueueProgress TxAudioQueue::progress () const noexcept
{
  TxAudioQueueEpoch epoch;
  impl_->tryEpoch (epoch);
  return TxAudioQueueProgress {
    epoch,
    impl_->fifo.queuedReal (),
    impl_->fifo.servedReal (),
    impl_->fifo.totalReal ()
  };
}

qint64 TxAudioQueue::capacity () const noexcept
{
  return impl_->fifo.capacity ();
}
