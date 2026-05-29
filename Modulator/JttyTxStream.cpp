#include "JttyTxStream.hpp"

#include "Audio/soundout.h"

#include <QDebug>

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

JttyTxStream::JttyTxStream (QObject * parent)
  : AudioDevice {parent}
  , m_drainGuard {DEFAULT_DRAIN_GUARD}
  , m_drainTimer {new QTimer {this}}
  , m_active {false}
{
  m_drainTimer->setInterval (25);
  connect (m_drainTimer, &QTimer::timeout, this, &JttyTxStream::pollDrain);
}

bool JttyTxStream::enqueueMessage (QVector<qint16> const& samples, qint64 sessionId)
{
  if (samples.isEmpty ()) return true;
  if (!m_fifo.enqueue (samples, sessionId))
    {
      qWarning () << "JTTY transmit FIFO overflow; rejecting" << samples.size () << "samples";
      return false;
    }
  return true;
}

void JttyTxStream::clear (qint64 sessionId)
{
  m_fifo.clear (sessionId);
}

qint64 JttyTxStream::servedReal () const
{
  return m_fifo.servedReal ();
}

qint64 JttyTxStream::totalReal () const
{
  return m_fifo.totalReal ();
}

void JttyTxStream::start (SoundOutput * stream, AudioDevice::Channel channel, qint64 sessionId)
{
  if (m_active) return;
  if (!m_fifo.queuedReal () && m_fifo.servedReal () == m_fifo.totalReal ())
    {
      m_fifo.clear (sessionId);
    }
  m_fifo.applyPendingReset ();
  initialize (QIODevice::ReadOnly, channel);
  m_active = true;
  m_stream = stream;
  if (m_stream)
    {
      m_stream->restart (this);
      // Size the drain guard to the actual device buffer (now valid) plus a
      // margin; same audio thread as SoundOutput, so this is not a cross-thread
      // QAudioOutput access.
      qint64 const bufFrames = bytesPerFrame ()
        ? qint64 (m_stream->bufferSize ()) / qint64 (bytesPerFrame ())
        : 0;
      m_drainGuard.store (qMax (bufFrames, DEFAULT_DRAIN_GUARD) + DRAIN_GUARD_MARGIN,
                           std::memory_order_release);
    }
  if (!m_drainTimer->isActive ()) m_drainTimer->start ();
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
  // Do not drop queued PCM here. A GUI stop for the previous session can cross
  // with the next enqueue; clear() is the explicit abort path.
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
      samples = load (m_fifo.pullSample (drainGuard), samples);
    }

  return numFrames * qint64 (bytesPerFrame ());
}

void JttyTxStream::pollDrain ()
{
  auto const drain = m_fifo.takeDrainReady ();
  if (drain.ready)
    {
      Q_EMIT drained (drain.sessionId, drain.totalAtDrain);
    }
}
