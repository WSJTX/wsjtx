#include "JttyPcmFifo.hpp"

#include <algorithm>

namespace
{
  qint64 advanceIndex (qint64 index, qint64 capacity) noexcept
  {
    ++index;
    return index == capacity ? 0 : index;
  }

  qint64 copyIntoRing (QVector<qint16>& buffer, qint64 capacity, qint64 tail,
                       qint16 const * samples, qint64 count)
  {
    qint64 const first = std::min (count, capacity - tail);
    std::copy_n (samples, first, buffer.data () + tail);

    qint64 const remaining = count - first;
    if (remaining > 0)
      {
        std::copy_n (samples + first, remaining, buffer.data ());
        return remaining;
      }

    tail += first;
    return tail == capacity ? 0 : tail;
  }
}

JttyPcmFifo::JttyPcmFifo (qint64 capacitySamples)
  : m_buffer (int (capacitySamples))
  , m_capacity {capacitySamples}
  , m_head {0}
  , m_tail {0}
  , m_used {0}
  , m_totalReal {0}
  , m_servedReal {0}
  , m_streamPos {0}
  , m_realEndStreamPos {0}
  , m_epoch {0}
  , m_resetRequestedGeneration {0}
  , m_resetAppliedGeneration {0}
  , m_resetEpoch {0}
  , m_resetTail {0}
  , m_resetTotalBaseline {0}
  , m_drainSignaled {false}
  , m_drainReady {false}
  , m_drainReadyEpoch {0}
  , m_drainReadyTotal {0}
{
}

bool JttyPcmFifo::enqueue (QVector<qint16> const& samples, qint64 epoch)
{
  if (samples.isEmpty ()) return true;
  return enqueue (samples.constData (), samples.size (), epoch);
}

bool JttyPcmFifo::enqueue (qint16 const * samples, qint64 count, qint64 epoch)
{
  if (!samples || count <= 0) return true;

  qint64 const used = m_used.load (std::memory_order_acquire);
  if (!jttyPcmEnqueueFits (m_capacity, used, count)) return false;

  qint64 const tail = copyIntoRing (m_buffer, m_capacity,
                                    m_tail.load (std::memory_order_relaxed),
                                    samples, count);

  m_epoch.store (epoch, std::memory_order_release);
  m_tail.store (tail, std::memory_order_release);
  m_totalReal.fetch_add (count, std::memory_order_acq_rel);
  m_drainSignaled.store (false, std::memory_order_release);
  m_used.fetch_add (count, std::memory_order_release);
  return true;
}

void JttyPcmFifo::clear (qint64 epoch)
{
  qint64 const tail = m_tail.load (std::memory_order_acquire);
  qint64 const total = m_totalReal.load (std::memory_order_acquire);
  qint64 const generation = m_resetRequestedGeneration.load (std::memory_order_relaxed) + 1;

  m_resetEpoch.store (epoch, std::memory_order_release);
  m_resetTail.store (tail, std::memory_order_release);
  m_resetTotalBaseline.store (total, std::memory_order_release);
  m_resetRequestedGeneration.store (generation, std::memory_order_release);
}

void JttyPcmFifo::applyPendingReset () noexcept
{
  qint64 const requested = m_resetRequestedGeneration.load (std::memory_order_acquire);
  qint64 const applied = m_resetAppliedGeneration.load (std::memory_order_acquire);
  if (requested <= applied) return;

  qint64 const resetTail = m_resetTail.load (std::memory_order_acquire);
  qint64 const resetTotal = m_resetTotalBaseline.load (std::memory_order_acquire);
  qint64 const resetEpoch = m_resetEpoch.load (std::memory_order_acquire);
  qint64 const served = m_servedReal.load (std::memory_order_acquire);
  qint64 const used = m_used.load (std::memory_order_acquire);
  qint64 skipped = resetTotal > served ? resetTotal - served : 0;
  if (skipped > used) skipped = used;

  m_head.store (resetTail, std::memory_order_release);
  if (skipped > 0)
    {
      m_used.fetch_sub (skipped, std::memory_order_acq_rel);
    }
  if (served < resetTotal)
    {
      m_servedReal.store (resetTotal, std::memory_order_release);
    }

  m_streamPos.store (0, std::memory_order_release);
  m_realEndStreamPos.store (0, std::memory_order_release);
  m_epoch.store (resetEpoch, std::memory_order_release);
  m_drainSignaled.store (false, std::memory_order_release);
  m_drainReady.store (false, std::memory_order_release);
  m_drainReadyEpoch.store (resetEpoch, std::memory_order_release);
  m_drainReadyTotal.store (0, std::memory_order_release);
  m_resetAppliedGeneration.store (requested, std::memory_order_release);
}

qint16 JttyPcmFifo::pullSample (qint64 drainGuard) noexcept
{
  applyPendingReset ();

  qint16 sample {0};
  if (m_used.load (std::memory_order_acquire) > 0)
    {
      qint64 head = m_head.load (std::memory_order_relaxed);
      sample = m_buffer[int (head)];
      head = advanceIndex (head, m_capacity);
      m_head.store (head, std::memory_order_release);
      m_used.fetch_sub (1, std::memory_order_release);
      qint64 const streamPos = m_streamPos.fetch_add (1, std::memory_order_acq_rel) + 1;
      m_servedReal.fetch_add (1, std::memory_order_acq_rel);
      m_realEndStreamPos.store (streamPos, std::memory_order_release);
    }
  else
    {
      m_streamPos.fetch_add (1, std::memory_order_acq_rel);
    }

  updateDrainState (drainGuard);
  return sample;
}

JttyPcmFifo::DrainState JttyPcmFifo::takeDrainReady () noexcept
{
  if (m_resetRequestedGeneration.load (std::memory_order_acquire)
      > m_resetAppliedGeneration.load (std::memory_order_acquire))
    {
      m_drainReady.exchange (false, std::memory_order_acq_rel);
      return DrainState {false, 0, 0};
    }

  if (!m_drainReady.exchange (false, std::memory_order_acq_rel))
    {
      return DrainState {false, 0, 0};
    }

  return DrainState {
    true,
    m_drainReadyEpoch.load (std::memory_order_acquire),
    m_drainReadyTotal.load (std::memory_order_acquire)
  };
}

qint64 JttyPcmFifo::queuedReal () const noexcept
{
  qint64 const used = m_used.load (std::memory_order_acquire);
  qint64 const resetTotal = m_resetTotalBaseline.load (std::memory_order_acquire);
  qint64 const served = m_servedReal.load (std::memory_order_acquire);
  qint64 const abortedQueued = resetTotal > served ? resetTotal - served : 0;
  return used > abortedQueued ? used - abortedQueued : 0;
}

qint64 JttyPcmFifo::servedReal () const noexcept
{
  return logicalServedReal ();
}

qint64 JttyPcmFifo::totalReal () const noexcept
{
  return logicalTotalReal ();
}

void JttyPcmFifo::updateDrainState (qint64 drainGuard) noexcept
{
  if (m_drainSignaled.load (std::memory_order_acquire)) return;

  // Drain is based on consumed real samples plus backend tail coverage. This
  // avoids trusting FIFO-empty alone, which would release PTT before queued
  // audio has actually left the hardware or TCI transmit buffer.
  qint64 const servedReal = logicalServedReal ();
  qint64 const totalReal = logicalTotalReal ();
  qint64 const streamPos = m_streamPos.load (std::memory_order_acquire);
  qint64 const realEndStreamPos = m_realEndStreamPos.load (std::memory_order_acquire);
  if (!jttyTxDrained (servedReal, totalReal, streamPos, realEndStreamPos, drainGuard))
    {
      return;
    }

  bool expected {false};
  if (m_drainSignaled.compare_exchange_strong (expected, true, std::memory_order_acq_rel))
    {
      m_drainReadyEpoch.store (m_epoch.load (std::memory_order_acquire), std::memory_order_release);
      m_drainReadyTotal.store (totalReal, std::memory_order_release);
      m_drainReady.store (true, std::memory_order_release);
    }
}

qint64 JttyPcmFifo::logicalServedReal () const noexcept
{
  qint64 const served = m_servedReal.load (std::memory_order_acquire);
  qint64 const resetTotal = m_resetTotalBaseline.load (std::memory_order_acquire);
  return served > resetTotal ? served - resetTotal : 0;
}

qint64 JttyPcmFifo::logicalTotalReal () const noexcept
{
  qint64 const total = m_totalReal.load (std::memory_order_acquire);
  qint64 const resetTotal = m_resetTotalBaseline.load (std::memory_order_acquire);
  return total > resetTotal ? total - resetTotal : 0;
}

bool jttyTxDrained (qint64 servedReal, qint64 totalReal,
                    qint64 streamPos, qint64 realEndStreamPos,
                    qint64 drainGuard)
{
  if (totalReal <= 0) return false;
  if (servedReal < totalReal) return false;
  return (streamPos - realEndStreamPos) >= drainGuard;
}

bool jttyPcmEnqueueFits (qint64 capacitySamples, qint64 usedSamples,
                         qint64 count) noexcept
{
  if (count <= 0) return true;
  if (capacitySamples <= 0 || usedSamples < 0) return false;
  if (usedSamples > capacitySamples) return false;
  return count <= capacitySamples - usedSamples;
}
