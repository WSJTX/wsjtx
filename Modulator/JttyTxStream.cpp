#include "JttyTxStream.hpp"

#include "Audio/AudioOutputSink.hpp"
#include "Logger.hpp"
#include <QSemaphore>
#include <limits>

#include "moc_JttyTxStream.cpp"

namespace
{
  // Trailing silence (samples) served past the last real sample before we trust
  // the queue has physically played out, when the device buffer size is unknown.
  // Generous by design: it only lengthens the PTT tail after the final message,
  // never the gap between chained messages.
  constexpr qint64 DEFAULT_DRAIN_GUARD = 9600;   // 200 ms at 48 kHz
  constexpr qint64 DRAIN_GUARD_MARGIN  = 4800;   // 100 ms safety margin
}

struct JttyTxStream::PendingEnqueues
{
  QSemaphore samples {int (TxAudioQueue::defaultCapacity ())};
  QSemaphore requests {64};
};

TxEvidence::TxStartSnapshot makeJttyTxStartSnapshot (TxEvidence::TxRequest const& request,
                                                      qint64 committedEndSample)
{
  TxEvidence::TxStartSnapshot snapshot;
  snapshot.session_id = request.session_id;
  snapshot.generation = request.generation;
  snapshot.mode = request.mode;
  snapshot.sample_rate_hz = 48000;
  snapshot.committed_end_sample = committedEndSample;
  snapshot.target_known = false;
  snapshot.diagnostic = "JTTY PCM extent may grow while messages are queued";
  return snapshot;
}

JttyTxStream::JttyTxStream (TxAudioQueue& queue, QObject * parent)
  : AudioDevice {parent}
  , m_pendingEnqueues {std::make_shared<PendingEnqueues> ()}
  , m_queue {queue}
  , m_drainGuard {DEFAULT_DRAIN_GUARD}
  , m_drainTimer {new QTimer {this}}
  , m_active {false}
{
  m_drainTimer->setInterval (25);
  connect (m_drainTimer, &QTimer::timeout, this, &JttyTxStream::pollDrain);
}

void JttyTxStream::start (TxEvidence::TxRequest request, AudioOutputSink * stream)
{
  if (m_active) return;
  auto progress = m_queue.progress ();
  if (progress.epoch != request.queue_epoch)
    {
      LOG_DEBUG ("JTTY transmit stream start rejected before pending reset; "
                 "requested queue epoch=" << request.queue_epoch.value ()
                 << " current epoch=" << progress.epoch.value ());
      return;
    }
  m_queue.applyPendingReset ();
  progress = m_queue.progress ();
  if (progress.epoch != request.queue_epoch)
    {
      LOG_DEBUG ("JTTY transmit stream start rejected after pending reset; "
                 "requested queue epoch=" << request.queue_epoch.value ()
                 << " current epoch=" << progress.epoch.value ());
      return;
    }
  initialize (QIODevice::ReadOnly, request.channel);
  m_lastProgress = {};
  m_active = true;
  m_stream = stream;
  // Restart may pull immediately, before the new backend tail is available.
  m_drainGuard.store (std::numeric_limits<qint64>::max (),
                      std::memory_order_release);
  if (m_stream)
    {
      m_stream->restart (this);
      // Include the sink's unplayed tail estimate and a safety margin.
      qint64 const tailFrames = bytesPerFrame ()
        ? qint64 (m_stream->unplayedBufferBytes ()) / qint64 (bytesPerFrame ())
        : 0;
      m_drainGuard.store (
        qMax (tailFrames, DEFAULT_DRAIN_GUARD) + DRAIN_GUARD_MARGIN,
        std::memory_order_release);
    }
  else
    {
      m_drainGuard.store (DEFAULT_DRAIN_GUARD, std::memory_order_release);
    }
  if (!m_drainTimer->isActive ()) m_drainTimer->start ();
  Q_EMIT txSourceCommitted (makeJttyTxStartSnapshot (
    request, progress.total_samples > 0 ? progress.total_samples - 1 : -1));
}

void JttyTxStream::stop ()
{
  if (m_stream)
    {
      m_stream->stop ();
  }
  m_active = false;
  m_drainTimer->stop ();
  AudioDevice::close ();
  // Reclaim cancelled PCM on the consumer thread before acknowledging stop.
  // A pending reset preserves samples enqueued after its clear boundary.
  m_queue.applyPendingReset ();
  Q_EMIT stopped ();
}

void JttyTxStream::clearQueue (TxAudioQueueEpoch epoch)
{
  stop ();
  m_queue.clear (epoch);
  m_queue.applyPendingReset ();
}

void JttyTxStream::queuePcm (QByteArray const& samples,
                             TxAudioQueueEpoch epoch, qint64 enqueueId)
{
  int const count = samples.size () / int (sizeof (qint16));
  auto const pending = m_pendingEnqueues;
  if (samples.isEmpty () || samples.size () % int (sizeof (qint16))
      || !pending->requests.tryAcquire ()) {
    Q_EMIT enqueueFailed (epoch, enqueueId, TxAudioQueueEnqueueFailure::Capacity);
    return;
  }
  if (!pending->samples.tryAcquire (count)) {
    pending->requests.release ();
    Q_EMIT enqueueFailed (epoch, enqueueId, TxAudioQueueEnqueueFailure::Capacity);
    return;
  }

  // The reservation follows the event, including across aborts and destruction.
  auto pcm = std::shared_ptr<QByteArray> {
    new QByteArray {samples}, [pending, count] (QByteArray * payload) {
      delete payload;
      pending->samples.release (count);
      pending->requests.release ();
    }};
  QMetaObject::invokeMethod (this, [this, pcm, epoch, enqueueId] {
    enqueuePcm (*pcm, epoch, enqueueId);
  }, Qt::QueuedConnection);
}

void JttyTxStream::enqueuePcm (QByteArray const& samples,
                               TxAudioQueueEpoch epoch,
                               qint64 enqueueId)
{
  if (samples.isEmpty () || samples.size () % int (sizeof (qint16)))
    {
      Q_EMIT enqueueFailed (
        epoch, enqueueId, TxAudioQueueEnqueueFailure::Capacity);
      return;
    }

  qint64 const count = samples.size () / int (sizeof (qint16));
  qint16 const * pcm = reinterpret_cast<qint16 const *> (samples.constData ());
  auto const result = m_queue.enqueue (pcm, count, epoch);
  if (!result.accepted)
    {
      Q_EMIT enqueueFailed (epoch, enqueueId, result.failure);
      return;
    }

  Q_EMIT enqueueAccepted (enqueueId, count, result.progress);
}

qint64 JttyTxStream::readData (char * data, qint64 maxSize)
{
  if (maxSize == 0) return 0;
  Q_ASSERT (!(maxSize % qint64 (bytesPerFrame ()))); // no torn frames

  qint64 const numFrames {maxSize / qint64 (bytesPerFrame ())};
  qint16 * samples {reinterpret_cast<qint16 *> (data)};

  qint64 const drainGuard = m_drainGuard.load (std::memory_order_acquire);
  for (qint64 frame = 0; frame < numFrames; ++frame)
    {
      samples = load (m_queue.pullSample (drainGuard), samples);
    }

  return numFrames * qint64 (bytesPerFrame ());
}

void JttyTxStream::pollDrain ()
{
  if (!m_active) return;
  auto const current = m_queue.progress ();
  if (current.epoch.isValid ()
      && (current.epoch != m_lastProgress.epoch
          || current.served_samples != m_lastProgress.served_samples
          || current.total_samples != m_lastProgress.total_samples))
    {
      m_lastProgress = current;
      Q_EMIT progress (current);
    }
  auto const drain = m_queue.takeDrainReady ();
  if (drain.ready)
    {
      Q_EMIT drained (drain);
    }
}
