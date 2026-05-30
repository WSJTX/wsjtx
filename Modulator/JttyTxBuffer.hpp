#ifndef JTTY_TX_BUFFER_HPP__
#define JTTY_TX_BUFFER_HPP__

#include <QVector>

#include "Modulator/JttyPcmFifo.hpp"

class JttyTxBuffer
{
public:
  // Thread-safe facade around the JTTY PCM FIFO. Control code may enqueue,
  // clear, and read progress from any thread; only the backend stream should
  // call pullSample(), takeDrainReady(), or applyPendingReset().
  bool enqueueMessage (QVector<qint16> const& samples, qint64 sessionId);
  void clear (qint64 sessionId = 0);

  qint64 queuedReal () const noexcept;
  qint64 servedReal () const noexcept;
  qint64 totalReal () const noexcept;

  void applyPendingReset () noexcept;
  qint16 pullSample (qint64 drainGuard) noexcept;
  JttyPcmFifo::DrainState takeDrainReady () noexcept;

private:
  JttyPcmFifo m_fifo;
};

#endif
