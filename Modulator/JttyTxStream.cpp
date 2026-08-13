#include "JttyTxStream.hpp"

#include "Audio/soundout.h"

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

JttyTxStream::JttyTxStream (JttyTxBuffer& buffer, QObject * parent)
  : AudioDevice {parent}
  , m_buffer {buffer}
  , m_drainGuard {DEFAULT_DRAIN_GUARD}
  , m_drainTimer {new QTimer {this}}
  , m_active {false}
{
  m_drainTimer->setInterval (25);
  connect (m_drainTimer, &QTimer::timeout, this, &JttyTxStream::pollDrain);
}

void JttyTxStream::start (TxEvidence::TxRequest request, SoundOutput * stream)
{
  if (m_active) return;
  if (!m_buffer.queuedReal () && m_buffer.servedReal () == m_buffer.totalReal ())
    {
      m_buffer.clear (request.fifo_session_id);
    }
  m_buffer.applyPendingReset ();
  qint64 const totalReal = m_buffer.totalReal ();
  initialize (QIODevice::ReadOnly, request.channel);
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
  Q_EMIT txSourceCommitted (makeJttyTxStartSnapshot (
    request, totalReal > 0 ? totalReal - 1 : -1));
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
      samples = load (m_buffer.pullSample (drainGuard), samples);
    }

  return numFrames * qint64 (bytesPerFrame ());
}

void JttyTxStream::pollDrain ()
{
  auto const drain = m_buffer.takeDrainReady ();
  if (drain.ready)
    {
      Q_EMIT drained (drain.sessionId, drain.totalAtDrain);
    }
}
