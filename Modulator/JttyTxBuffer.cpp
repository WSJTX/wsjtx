#include "JttyTxBuffer.hpp"

#include <QDebug>

bool JttyTxBuffer::enqueueMessage (QVector<qint16> const& samples, qint64 sessionId)
{
  if (samples.isEmpty ()) return true;
  if (!m_fifo.enqueue (samples, sessionId))
    {
      qWarning () << "JTTY transmit FIFO overflow; rejecting" << samples.size () << "samples";
      return false;
    }
  return true;
}

void JttyTxBuffer::clear (qint64 sessionId)
{
  m_fifo.clear (sessionId);
}

qint64 JttyTxBuffer::queuedReal () const noexcept
{
  return m_fifo.queuedReal ();
}

qint64 JttyTxBuffer::servedReal () const noexcept
{
  return m_fifo.servedReal ();
}

qint64 JttyTxBuffer::totalReal () const noexcept
{
  return m_fifo.totalReal ();
}

void JttyTxBuffer::applyPendingReset () noexcept
{
  m_fifo.applyPendingReset ();
}

qint16 JttyTxBuffer::pullSample (qint64 drainGuard) noexcept
{
  return m_fifo.pullSample (drainGuard);
}

JttyPcmFifo::DrainState JttyTxBuffer::takeDrainReady () noexcept
{
  return m_fifo.takeDrainReady ();
}
