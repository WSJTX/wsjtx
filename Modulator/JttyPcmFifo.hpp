#ifndef JTTY_PCM_FIFO_HPP__
#define JTTY_PCM_FIFO_HPP__

#include <atomic>

#include <QVector>

class JttyPcmFifo
{
public:
  // Lock-free SPSC PCM queue: one producer appends rendered messages, one
  // backend thread pulls samples. clear() only publishes a reset generation;
  // the consumer applies it so FIFO indices are not concurrently rewritten.
  // The audio backend reports this edge later, outside the pull callback. The
  // epoch/total pair lets MainWindow ignore a tail event that belonged to an
  // older queue state.
  struct DrainState
  {
    bool ready;
    qint64 epoch;
    qint64 totalAtDrain;
  };

  explicit JttyPcmFifo (qint64 capacitySamples);

  bool enqueue (QVector<qint16> const& samples, qint64 epoch);
  bool enqueue (qint16 const * samples, qint64 count, qint64 epoch);
  void clear (qint64 epoch = 0);
  void applyPendingReset () noexcept;

  qint16 pullSample (qint64 drainGuard) noexcept;
  DrainState takeDrainReady () noexcept;

  qint64 capacity () const {return m_capacity;}
  qint64 queuedReal () const noexcept;
  qint64 servedReal () const noexcept;
  qint64 totalReal () const noexcept;

private:
  // Single producer, single consumer: MainWindow appends complete rendered
  // messages, and one audio pull path consumes frames. The pull side is kept to
  // atomics and array access so it can run in a real-time-ish audio callback.
  void updateDrainState (qint64 drainGuard) noexcept;
  qint64 logicalServedReal () const noexcept;
  qint64 logicalTotalReal () const noexcept;

  QVector<qint16> m_buffer;
  qint64 const m_capacity;

  std::atomic<qint64> m_head;
  std::atomic<qint64> m_tail;
  std::atomic<qint64> m_used;

  std::atomic<qint64> m_totalReal;
  std::atomic<qint64> m_servedReal;
  std::atomic<qint64> m_streamPos;
  std::atomic<qint64> m_realEndStreamPos;
  std::atomic<qint64> m_epoch;

  // clear() publishes a reset generation boundary; pullSample() and other
  // consumer-owned entry points apply it. Enqueue, progress getters, and drain
  // signaling must interpret this same generation baseline.
  std::atomic<qint64> m_resetRequestedGeneration;
  std::atomic<qint64> m_resetAppliedGeneration;
  std::atomic<qint64> m_resetEpoch;
  std::atomic<qint64> m_resetTail;
  std::atomic<qint64> m_resetTotalBaseline;

  std::atomic<bool> m_drainSignaled;
  std::atomic<bool> m_drainReady;
  std::atomic<qint64> m_drainReadyEpoch;
  std::atomic<qint64> m_drainReadyTotal;
};

bool jttyTxDrained (qint64 servedReal, qint64 totalReal,
                    qint64 streamPos, qint64 realEndStreamPos,
                    qint64 drainGuard);
bool jttyPcmEnqueueFits (qint64 capacitySamples, qint64 usedSamples,
                         qint64 count) noexcept;

#endif
