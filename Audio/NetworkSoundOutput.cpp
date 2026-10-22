#include "Audio/NetworkSoundOutput.hpp"

#include <algorithm>
#include <cstring>

#include <QAudioDeviceInfo>
#include <QDebug>
#include <QIODevice>
#include <QTimer>
#include <qmath.h>

#include "Audio/AudioDevice.hpp"
#include "Audio/NetworkAudioConnection.hpp"

#include "moc_NetworkSoundOutput.cpp"

namespace
{
  // How long the library may accept nothing before the transmission is
  // declared lost. Inject returns 0 -- not an error -- when the link is down
  // or the lane closed, so a run of zeros is the only signal on this side.
  constexpr int accept_nothing_limit_ms {1000};
  constexpr int owner_poll_ms {100};
  constexpr int report_interval_ms {1000};
}

NetworkSoundOutput::NetworkSoundOutput (QObject * parent)
  : AudioOutputSink {parent}
  , m_timer {new QTimer {this}}
{
  m_timer->setSingleShot (true);
  m_timer->setTimerType (Qt::PreciseTimer);
  connect (m_timer, &QTimer::timeout, this, &NetworkSoundOutput::pump);
}

NetworkSoundOutput::~NetworkSoundOutput ()
{
  // Leave the lane as we found it; the connection is somebody else's.
  closeLane ();
}

void NetworkSoundOutput::setConnection (NetworkAudioConnection * connection)
{
  if (connection == m_connection)
    {
      return;
    }
  if (m_active)
    {
      // A running transmission belongs to the connection it started on.
      fail (TxEvidence::TxBackendError::Io,
            tr ("The network audio connection changed during a transmission."));
    }
  m_connection = connection;
}

int NetworkSoundOutput::bufferSize () const
{
  // What THIS side holds: the lead kept in the library's TX ring, in the
  // bytes of the wire frame. 0 while no transmission runs, as a sound card's
  // sink reports with no stream -- which also stops a stale frame size
  // surviving a failed restart. What may still be unplayed PAST this side is
  // a different question, and it has its own name: unplayedBufferBytes ().
  if (!m_active) return 0;
  return leadBlocks * blockFrames * 2 * m_serverChannels;
}

int NetworkSoundOutput::unplayedBufferBytes () const
{
  // The lead this side holds, plus the depth measured past it at the station
  // (see playoutDepthMs in the header). In the SOURCE's frame size, because
  // that is the clock JttyTxStream counts its trailing silence on.
  if (!m_active) return 0;
  return (leadBlocks * blockFrames + playoutDepthMs * sampleRate / 1000)
    * m_sourceBytesPerFrame;
}

void NetworkSoundOutput::setFormat (QAudioDeviceInfo const&, unsigned, int)
{
  // The wire format is the server's, read from the connection at restart ();
  // the device and channel count chosen for a sound card mean nothing here.
}

void NetworkSoundOutput::restart (QIODevice * source, TxEvidence::TxPlaybackContext context, qint64)
{
  if (m_active)
    {
      m_timer->stop ();
      m_active = false;
      m_source.clear ();
      disconnect (m_linkLost);
    }
  m_context = context;
  ++m_startSequence;
  m_laneOpened = false;
  m_fault = TxEvidence::TxBackendError::None;
  m_faultMessage.clear ();

  if (!source || !source->isReadable ())
    {
      fail (TxEvidence::TxBackendError::Open,
            tr ("Network audio transmit requires a readable audio source."));
      return;
    }
  if (!m_connection)
    {
      fail (TxEvidence::TxBackendError::Open,
            tr ("Network audio transmit has no network audio connection: "
                "enable Network audio with a server first."));
      return;
    }
  auto const where = QString {"%1:%2"}.arg (m_connection->host ()).arg (m_connection->port ());
  if (!m_connection->isOpen ())
    {
      fail (TxEvidence::TxBackendError::Open,
            tr ("Network audio transmit needs the connection to %1 to be open, and it is not.")
            .arg (where));
      return;
    }
  if (!m_connection->isConnected ())
    {
      // Open but not connected: libnaudio is reconnecting, or gave up after
      // linkLost and left the handle open. Nothing injected would arrive.
      fail (TxEvidence::TxBackendError::Open,
            tr ("Network audio transmit needs the link to %1, and it is down.").arg (where));
      return;
    }
  if (!m_connection->transmitEnabled ())
    {
      // Decided at open (): the send worker starts once, at connect.
      fail (TxEvidence::TxBackendError::Open,
            tr ("The network audio connection to %1 was opened without its transmit lane; "
                "reconnect with \"Use for transmit\" enabled.").arg (where));
      return;
    }
  // On a connection opened with the transmit lane this is the server's
  // NATIVE format -- such a connection subscribes natively, because
  // libnaudio offers a client no other way to learn the format AUDIO_TX
  // carries (see NetworkAudioConnection::open).
  auto const format = m_connection->audioFormat ();
  if (format.sample_rate_hz != sampleRate)
    {
      // net-audio does not resample injected audio and AUDIO_TX is always the
      // server's native format; the modulator is fixed at 48 kHz.
      fail (TxEvidence::TxBackendError::Open,
            tr ("Network audio server %1 runs at %2 Hz; transmitting needs a server at %3 Hz.")
            .arg (where).arg (format.sample_rate_hz).arg (sampleRate));
      return;
    }
  if (16 != format.sample_size_bits
      || (1 != format.channel_count && 2 != format.channel_count))
    {
      fail (TxEvidence::TxBackendError::Open,
            tr ("Network audio server %1 supplies %2-bit audio with %3 channel(s); "
                "transmitting needs 16-bit audio with 1 or 2 channels.")
            .arg (where).arg (format.sample_size_bits).arg (format.channel_count));
      return;
    }

  // The modulator is an AudioDevice: its frames are one sample (Mono) or two
  // (Left / Right / Both, with the other side zero or a copy). Anything else
  // readable is taken as 16-bit mono.
  m_sourceBytesPerFrame = 2;
  m_sourceChannel = AudioDevice::Mono;
  if (auto const device = dynamic_cast<AudioDevice *> (source))
    {
      m_sourceBytesPerFrame = static_cast<int> (device->bytesPerFrame ());
      m_sourceChannel = device->channel ();
    }
  if (2 != m_sourceBytesPerFrame && 4 != m_sourceBytesPerFrame)
    {
      fail (TxEvidence::TxBackendError::Open,
            tr ("Network audio transmit received an unsupported source frame size."));
      return;
    }

  if (!m_connection->setDuplex (NetworkAudioConnection::Duplex::Talk))
    {
      fail (TxEvidence::TxBackendError::Open,
            tr ("Unable to open the network audio transmit lane to %1.").arg (where));
      return;
    }

  m_generation = m_connection->generation ();
  m_linkLost = connect (m_connection, &NetworkAudioConnection::linkLost,
                        this, &NetworkSoundOutput::onLinkLost, Qt::QueuedConnection);
  m_source = source;
  m_serverChannels = format.channel_count;
  m_sourceBuffer.resize (blockFrames * m_sourceBytesPerFrame);
  m_wireBuffer.resize (blockFrames * m_serverChannels * 2);
  m_framesSent = 0;
  m_frameOrigin = 0;
  m_framesAccepted = 0;
  m_shortInjects = 0;
  m_zeroRun = 0;
  m_sourceExhausted = false;
  m_suspended = false;
  m_active = true;
  m_laneOpened = true;
  m_lastReportMs = 0;
  m_lastOwnerMs = 0;
  m_elapsed.start ();

  // Dragon 3: ownership arbitration is silent, so say who holds it before
  // our first frame goes in.
  m_owner = QString {"\x01"};   // force the first read to report
  noteOwner ();
  qInfo () << "NetworkSoundOutput: transmit lane open to" << where
           << "at" << sampleRate << "Hz," << m_serverChannels << "channel(s);"
           << "source frame" << m_sourceBytesPerFrame << "bytes, channel"
           << AudioDevice::toString (static_cast<AudioDevice::Channel> (m_sourceChannel));
  Q_EMIT status (tr ("Sending"));
  Q_EMIT audioOutputActive ();
  publishSnapshot (true, TxEvidence::TxBackendState::Active);

  pump ();
}

void NetworkSoundOutput::pump ()
{
  if (!m_active || m_suspended)
    {
      return;
    }
  if (!m_connection)
    {
      fail (TxEvidence::TxBackendError::Io,
            tr ("The network audio connection disappeared during a transmission."));
      return;
    }

  auto const elapsed_ms = m_elapsed.elapsed ();
  auto const target = m_frameOrigin + elapsed_ms * sampleRate / 1000
    + qint64 {leadBlocks} * blockFrames;
  while (m_active && !m_sourceExhausted && m_framesSent < target)
    {
      if (!pushBlock ())
        {
          break;
        }
    }
  if (!m_active)
    {
      return;
    }

  if (elapsed_ms - m_lastOwnerMs >= owner_poll_ms)
    {
      m_lastOwnerMs = elapsed_ms;
      noteOwner ();
    }
  if (elapsed_ms - m_lastReportMs >= report_interval_ms)
    {
      m_lastReportMs = elapsed_ms;
      publishSnapshot (false, m_sourceExhausted ? TxEvidence::TxBackendState::Idle
                                                : TxEvidence::TxBackendState::Active);
    }

  if (m_sourceExhausted)
    {
      // The source has nothing more, as a sound card's source going quiet
      // leaves QAudioOutput idle: the lane stays open until stop (), and the
      // server releases TX ownership on its own after 500 ms of silence.
      return;
    }
  scheduleNextPump ();
}

bool NetworkSoundOutput::pushBlock ()
{
  auto const want = m_sourceBuffer.size ();
  auto const got = m_source ? m_source->read (m_sourceBuffer.data (), want) : -1;
  if (got < 0)
    {
      fail (TxEvidence::TxBackendError::Io,
            tr ("Network audio transmit could not read from the audio source."));
      return false;
    }
  if (0 == got)
    {
      m_sourceExhausted = true;
      Q_EMIT status (tr ("Idle"));
      Q_EMIT audioOutputIdle ();
      publishSnapshot (false, TxEvidence::TxBackendState::Idle);
      return false;
    }
  if (got % m_sourceBytesPerFrame)
    {
      fail (TxEvidence::TxBackendError::Io,
            tr ("Network audio transmit received a partial frame from the audio source."));
      return false;
    }
  auto const frames = static_cast<int> (got / m_sourceBytesPerFrame);

  // Channel mapping is the sink's job: the modulator expanded its mono signal
  // to the configured channel (AudioDevice::load), and what the server wants
  // is its own channel count.
  auto const * src = reinterpret_cast<qint16 const *> (m_sourceBuffer.constData ());
  auto * dst = reinterpret_cast<qint16 *> (m_wireBuffer.data ());
  auto const scale = [this] (qint16 sample) -> qint16 {
    if (1. == m_gain) return sample;
    return static_cast<qint16> (qBound (-32768., qRound (sample * m_gain) + 0., 32767.));
  };
  for (int frame = 0; frame < frames; ++frame)
    {
      if (4 == m_sourceBytesPerFrame)
        {
          qint16 const left {src[2 * frame]};
          qint16 const right {src[2 * frame + 1]};
          if (2 == m_serverChannels)
            {
              *dst++ = scale (left);
              *dst++ = scale (right);
            }
          else
            {
              // One channel on the wire: take the side the source was told
              // to use, as AudioDevice::store () does on the way in.
              *dst++ = scale (AudioDevice::Right == m_sourceChannel ? right : left);
            }
        }
      else
        {
          qint16 const sample {scale (src[frame])};
          *dst++ = sample;
          if (2 == m_serverChannels)
            {
              *dst++ = sample;
            }
        }
    }

  auto const bytes = frames * m_serverChannels * 2;
  auto const accepted = m_connection->injectTransmitAudio (m_wireBuffer.constData (), bytes);
  if (accepted < 0)
    {
      fail (TxEvidence::TxBackendError::Fatal,
            tr ("The network audio library refused transmit audio (error %1).").arg (accepted));
      return false;
    }
  m_framesAccepted += accepted / (2 * m_serverChannels);
  if (accepted < bytes)
    {
      if (!m_shortInjects)
        {
          qWarning () << "NetworkSoundOutput: the library accepted" << accepted << "of"
                      << bytes << "bytes" << (accepted ? "(ring full)" : "(nothing: link down or lane closed)");
        }
      ++m_shortInjects;
      if (0 == accepted)
        {
          ++m_zeroRun;
          if (m_zeroRun * (1000 * blockFrames / sampleRate) >= accept_nothing_limit_ms)
            {
              fail (TxEvidence::TxBackendError::Io,
                    tr ("The network audio link to %1:%2 is not accepting transmit audio.")
                    .arg (m_connection->host ()).arg (m_connection->port ()));
              return false;
            }
        }
      else
        {
          m_zeroRun = 0;
        }
    }
  else
    {
      m_zeroRun = 0;
    }
  m_framesSent += frames;
  return true;
}

void NetworkSoundOutput::scheduleNextPump ()
{
  // The next block is due when the clock catches up to the lead.
  auto const due_ms = (m_framesSent - m_frameOrigin - qint64 {leadBlocks} * blockFrames) * 1000
    / sampleRate;
  auto const delay = qBound<qint64> (0, due_ms - m_elapsed.elapsed (), 1000 * blockFrames / sampleRate);
  m_timer->start (static_cast<int> (delay));
}

void NetworkSoundOutput::noteOwner ()
{
  if (!m_connection)
    {
      return;
    }
  auto const owner = m_connection->transmitOwner ();
  if (owner == m_owner)
    {
      return;
    }
  m_owner = owner;
  qInfo () << "NetworkSoundOutput: server TX owner is now"
           << (owner.isEmpty () ? QString {"(none)"} : owner)
           << "after" << m_framesSent << "frames";
  Q_EMIT transmitOwnerChanged (owner);
}

void NetworkSoundOutput::suspend ()
{
  if (!m_active || m_suspended)
    {
      return;
    }
  m_suspended = true;
  m_timer->stop ();
  Q_EMIT status (tr ("Suspended"));
  publishSnapshot (false, TxEvidence::TxBackendState::Suspended);
}

void NetworkSoundOutput::resume ()
{
  if (!m_active || !m_suspended)
    {
      return;
    }
  m_suspended = false;
  // Re-anchor the clock so the pause is not made up in a burst.
  m_frameOrigin = m_framesSent;
  m_elapsed.restart ();
  m_lastReportMs = 0;
  m_lastOwnerMs = 0;
  Q_EMIT status (tr ("Sending"));
  publishSnapshot (false, TxEvidence::TxBackendState::Active);
  pump ();
}

void NetworkSoundOutput::closeLane ()
{
  m_timer->stop ();
  disconnect (m_linkLost);
  m_source.clear ();
  if (m_connection && m_connection->isOpen ())
    {
      m_connection->setDuplex (NetworkAudioConnection::Duplex::Listen);
    }
}

void NetworkSoundOutput::reset ()
{
  // A sound card drops what it had buffered; here there is nothing buffered on
  // this side, so reset and stop are the same act.
  stop ();
}

void NetworkSoundOutput::stop ()
{
  if (!m_active)
    {
      publishSnapshot (false, TxEvidence::TxBackendState::Stopped);
      return;
    }
  m_active = false;
  m_suspended = false;
  closeLane ();
  noteOwner ();
  qInfo () << "NetworkSoundOutput: transmit lane closed after" << m_framesSent << "frames,"
           << m_shortInjects << "short inject(s), server TX owner"
           << (m_owner.isEmpty () ? QString {"(none)"} : m_owner);
  Q_EMIT status (tr ("Stopped"));
  Q_EMIT audioOutputIdle ();
  publishSnapshot (false, TxEvidence::TxBackendState::Stopped);
}

void NetworkSoundOutput::onLinkLost (QString reason, quint64 generation)
{
  if (!m_active || generation != m_generation)
    {
      return;
    }
  if (!m_connection || !m_connection->isOpen ())
    {
      // Closed on this side, not lost on the wire: a link libnaudio gives up
      // on stays open, and the receive path closes its connection only to
      // replace it (a settings change) or at shutdown.
      fail (TxEvidence::TxBackendError::Io,
            tr ("The network audio transmit route changed during a transmission."));
      return;
    }
  fail (TxEvidence::TxBackendError::Io,
        reason.isEmpty ()
        ? tr ("The network audio link was lost during a transmission.")
        : tr ("The network audio link was lost during a transmission: %1").arg (reason));
}

void NetworkSoundOutput::fail (TxEvidence::TxBackendError kind, QString const& message)
{
  auto const was_active = m_active;
  m_active = false;
  m_suspended = false;
  m_fault = kind;
  m_faultMessage = message;
  closeLane ();
  if (!was_active)
    {
      // A restart () that never opened the lane: its start snapshot comes
      // first, as the sound card's sink publishes one for a stream it could
      // not open.
      publishUnavailableSnapshot (true, kind, message);
    }
  Q_EMIT error ({m_context, kind, message});
  Q_EMIT status (tr ("Error"));
  if (was_active)
    {
      Q_EMIT audioOutputIdle ();
      publishSnapshot (false, TxEvidence::TxBackendState::Stopped);
    }
}

void NetworkSoundOutput::setAttenuation (qreal a)
{
  Q_ASSERT (0. <= a && a <= 999.);
  m_attenuation = a;
  m_gain = qPow (10., -a / 20.);
}

void NetworkSoundOutput::resetAttenuation ()
{
  m_attenuation = 0.;
  m_gain = 1.;
}

TxEvidence::TxRawPlayoutSnapshot NetworkSoundOutput::makeSnapshot (bool startEvent,
                                                                   TxEvidence::TxBackendState state) const
{
  // Dead reckoning: frames are counted as the library accepts them, not as
  // the server's DAC plays them.
  TxEvidence::TxRawPlayoutSnapshot snapshot;
  snapshot.context = m_context;
  snapshot.tier = TxEvidence::TxPlayoutTier::DeadReckoning;
  snapshot.backend_start_sequence = m_startSequence;
  snapshot.start_event = startEvent;
  snapshot.available = true;
  snapshot.state = state;
  snapshot.error = m_fault;
  // What this side holds, in bytes of the wire frame the bytes_per_frame
  // field below names -- not the unplayed depth, which counts the path past us.
  snapshot.capacity_bytes = qint64 {leadBlocks} * blockFrames * 2 * m_serverChannels;
  snapshot.processed_usecs = m_framesAccepted * 1000000 / sampleRate;
  snapshot.elapsed_usecs = m_elapsed.isValid () ? m_elapsed.nsecsElapsed () / 1000 : -1;
  snapshot.successfully_written_frames = m_framesAccepted;
  snapshot.channel_count = m_serverChannels;
  snapshot.bytes_per_frame = 2 * m_serverChannels;
  snapshot.sample_rate_hz = sampleRate;
  snapshot.report_interval_ms = report_interval_ms;
  snapshot.diagnostic = TxEvidence::TxBackendError::None == m_fault
    ? QStringLiteral ("network audio: frames counted at injection") : m_faultMessage;
  return snapshot;
}

void NetworkSoundOutput::publishSnapshot (bool startEvent, TxEvidence::TxBackendState state) const
{
  if (!m_laneOpened)
    {
      publishUnavailableSnapshot (startEvent, TxEvidence::TxBackendError::Open,
                                  tr ("No network audio transmission is active."));
      return;
    }
  Q_EMIT rawTxPlayoutSnapshot (makeSnapshot (startEvent, state));
}

void NetworkSoundOutput::publishUnavailableSnapshot (bool startEvent, TxEvidence::TxBackendError kind,
                                                     QString const& diagnostic) const
{
  TxEvidence::TxRawPlayoutSnapshot snapshot;
  snapshot.context = m_context;
  snapshot.backend_start_sequence = m_startSequence;
  snapshot.start_event = startEvent;
  snapshot.state = TxEvidence::TxBackendState::Unavailable;
  snapshot.error = kind;
  snapshot.diagnostic = diagnostic;
  Q_EMIT rawTxPlayoutSnapshot (snapshot);
}
