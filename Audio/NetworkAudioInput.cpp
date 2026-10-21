#include "Audio/NetworkAudioInput.hpp"

#include <algorithm>

#include <QDebug>
#include <QMetaObject>
#include <QSysInfo>
#include <QTimer>

#include "naudio.h"

#include "Audio/NetworkAudioConnection.hpp"

#include "moc_NetworkAudioInput.cpp"

namespace
{
  // How often the transport counters are read. Loss is not an event in
  // libnaudio's callback set -- only connection lifecycle is -- so the counters
  // are the only way to see a stream that is still connected but shedding
  // audio. On the transport this source uses today (TCP) libnaudio measures
  // neither counter and reports -1 for both; the first poll notices that and
  // retires itself, so on TCP discontinuity detection is by link events alone.
  constexpr int stats_poll_interval_ms {1000};

  // Unrecovered sequence slots within one poll interval above which the period
  // buffer is considered unsalvageable rather than merely shifted.
  //
  // The threshold is deliberately high, for two reasons that pull the same way.
  // A gap shifts every later sample in the period EARLIER by the missing
  // duration; the FT8 decoder searches over DT, so a small shift still decodes
  // (with a wrong DT) while a reset throws the whole period away. And
  // sequence_gaps counts every packet type sharing the sequence space -- audio,
  // parity and control -- so it OVERSTATES audio loss and would fire early if
  // read as an audio counter. At one ~20 ms audio packet per slot this is
  // roughly half a second of missing audio, far above the handful of slots a
  // busy roster's control traffic accounts for.
  constexpr long long discontinuity_slot_threshold {25};
}

NetworkAudioInput::NetworkAudioInput (NetworkAudioConnection * connection,
                                      QObject * parent)
  : AudioInputSource {parent}
  , m_connection {connection}
{
  Q_ASSERT (m_connection);
  // Queued, always: the connection emits from libnaudio's threads and these
  // slots write the Detector, which belongs to this object's thread. The
  // audio and the link events share one queue, so an outage is seen between
  // the last chunk before it and the first chunk after it.
  connect (m_connection, &NetworkAudioConnection::audio,
           this, &NetworkAudioInput::deliver, Qt::QueuedConnection);
  connect (m_connection, &NetworkAudioConnection::linkDropped,
           this, &NetworkAudioInput::onLinkDropped, Qt::QueuedConnection);
  connect (m_connection, &NetworkAudioConnection::reconnecting,
           this, &NetworkAudioInput::onReconnecting, Qt::QueuedConnection);
  connect (m_connection, &NetworkAudioConnection::linkRestored,
           this, &NetworkAudioInput::onLinkRestored, Qt::QueuedConnection);
  connect (m_connection, &NetworkAudioConnection::streamStopped,
           this, &NetworkAudioInput::onStreamStopped, Qt::QueuedConnection);
  connect (m_connection, &NetworkAudioConnection::linkLost,
           this, &NetworkAudioInput::onLinkLost, Qt::QueuedConnection);
}

NetworkAudioInput::NetworkAudioInput (QString host, quint16 port, QObject * parent)
  : NetworkAudioInput {new NetworkAudioConnection {std::move (host), port}, parent}
{
  // Built unparented so the delegating constructor could take it; owned from
  // here on, on this thread with the rest of this object.
  m_connection->setParent (this);
}

NetworkAudioInput::~NetworkAudioInput ()
{
  // A connection this object owns is closed here, before ~QObject: close ()
  // joins libnaudio's threads, so no signal can be emitted at an object that
  // is going away. A borrowed one stays open for whoever else is using it.
  teardown ();
}

NetworkAudioConnection * NetworkAudioInput::connection () const {return m_connection.data ();}
QString NetworkAudioInput::host () const {return m_connection ? m_connection->host () : QString {};}
quint16 NetworkAudioInput::port () const {return m_connection ? m_connection->port () : 0;}

bool NetworkAudioInput::isConnected () const
{
  return m_started && m_connection && m_connection->isConnected ();
}

void NetworkAudioInput::setReconnectPolicy (int baseDelayMs, int maxDelayMs, int maxAttempts)
{
  if (m_connection) m_connection->setReconnectPolicy (baseDelayMs, maxDelayMs, maxAttempts);
}

void NetworkAudioInput::start (QAudioDeviceInfo const&, int,
                               AudioDevice * sink, unsigned downSampleFactor,
                               AudioDevice::Channel channel)
{
  stop ();

  if (!sink)
    {
      fail (tr ("Network audio input has no detector sink."));
      return;
    }
  if (!downSampleFactor)
    {
      fail (tr ("Network audio input requires a non-zero downsample factor."));
      return;
    }
  if (QSysInfo::ByteOrder != QSysInfo::LittleEndian)
    {
      // net-audio carries little-endian PCM; AudioDevice::store () reads it in
      // host order. Equating the two is only safe on a little-endian host.
      fail (tr ("Network audio input does not yet support big-endian hosts."));
      return;
    }
  auto const wanted_rate = detectorSampleRate * static_cast<int> (downSampleFactor);
  if (!m_connection)
    {
      fail (tr ("Network audio input has no connection."));
      return;
    }
  if (!m_connection->open (wanted_rate))
    {
      fail (m_connection->lastError ());
      return;
    }
  m_generation = m_connection->generation ();
  m_started = true;

  auto const format = m_connection->audioFormat ();
  m_grantedRate = format.sample_rate_hz;
  m_grantedChannels = format.channel_count;
  auto const bits = format.sample_size_bits;

  if (m_grantedRate != wanted_rate || 16 != bits
      || (1 != m_grantedChannels && 2 != m_grantedChannels))
    {
      // The rate is the one thing this client cannot fix up itself: the
      // detector's decimator is a fixed 4:1, so an unservable rate really is
      // unusable. Name what was asked for and what arrived.
      fail (tr ("Network audio server %1:%2 supplied %3 Hz / %4-bit / %5 channel(s); "
                "this needs %6 Hz / 16-bit / 1 or 2 channels.")
            .arg (host ()).arg (port ()).arg (m_grantedRate).arg (bits)
            .arg (m_grantedChannels).arg (wanted_rate));
      return;
    }

  // The sink's channel setting is what makes AudioDevice::store () read the
  // right stride and pick the right side, so it follows what actually arrived
  // rather than what was configured:
  //
  //   1 channel  -> Mono. There is nothing to choose, and a non-mono setting
  //                 would make store () read two samples per frame out of a
  //                 one-sample buffer.
  //   2 channels -> the configured channel, because now the choice is real.
  //                 Mono is promoted to Left, since Mono's one-sample stride
  //                 would misread an interleaved stereo buffer as double-rate
  //                 audio -- which is silently wrong rather than noisy.
  auto const sink_channel = 1 == m_grantedChannels
    ? AudioDevice::Mono
    : (AudioDevice::Mono == channel ? AudioDevice::Left : channel);

  if (!sink->initialize (QIODevice::WriteOnly, sink_channel))
    {
      fail (tr ("Unable to initialize the detector for network audio input."));
      return;
    }
  m_sink = sink;

  AudioStreamDescriptor descriptor {};
  descriptor.sample_rate_hz = m_grantedRate;
  descriptor.sample_encoding = AudioStreamDescriptor::SampleEncoding::SignedInteger;
  descriptor.sample_size_bits = bits;
  descriptor.byte_order = AudioStreamDescriptor::ByteOrder::LittleEndian;
  descriptor.channel_count = m_grantedChannels;
  descriptor.channel_layout = 1 == m_grantedChannels
    ? AudioStreamDescriptor::ChannelLayout::Mono
    : AudioStreamDescriptor::ChannelLayout::Stereo;
  // The samples are produced by another host's clock, so this is not our device
  // clock. It is NOT capture-anchored: net-audio's per-frame timestamp is a
  // monotonic counter whose origin is implementation-defined, and the spec is
  // explicit that it must not be read as wall-clock time. AudioStreamClock
  // therefore falls back to arrival time, which is what the sound-card path
  // already uses -- correct, but it does not compensate for network delay.
  //
  // What IS claimed is discontinuity DETECTION, not clock discipline: an
  // outage, or a burst of loss on a transport that measures it, resets the
  // period buffer rather than being carried across into a period of decodes
  // with a DT short by the hole. Steady skew is a wrong-but-consistent DT;
  // a carried-over gap is a wrong-and-unbounded one, and only the second is
  // both fixable from this side and demonstrably real. No measurement has yet
  // shown the skew matters -- the on-air runs over this path decode with an
  // ordinary DT spread -- so an anchor on the wire stays unrequested.
  descriptor.clock_domain = AudioStreamDescriptor::ClockDomain::SystemClock;
  descriptor.timing_evidence = AudioStreamDescriptor::TimingEvidence::Unavailable;
  descriptor.capture_anchor_utc_ms = 0;
  descriptor.can_report_discontinuities = true;
  setStreamDescriptor (descriptor);

  Q_EMIT status (tr ("Network audio connected to %1:%2 (%3 Hz, %4 channel(s), %5)")
                 .arg (host ()).arg (port ()).arg (m_grantedRate)
                 .arg (m_grantedChannels)
                 .arg (QString::fromLatin1 (AudioDevice::toString (sink_channel))));

  m_discontinuities = 0;
  m_lastUnrecoveredGaps = 0;
  m_lastLocalDrops = 0;
  if (!m_statsTimer)
    {
      m_statsTimer = new QTimer {this};
      connect (m_statsTimer, &QTimer::timeout,
               this, &NetworkAudioInput::pollTransportStats);
    }
  m_statsTimer->start (stats_poll_interval_ms);

  // Monitoring decides when audio should flow; a fresh source starts quiet and
  // resume () opens it, exactly as the sound-card path does.
  m_suspended = true;
}

void NetworkAudioInput::deliver (QByteArray pcm, quint64 generation)
{
  if (generation != m_generation || !m_started || m_suspended || !m_sink
      || pcm.isEmpty ())
    {
      return;
    }

  // Detector::writeData asserts whole frames. libnaudio delivers whole frames,
  // but a short tail would trip an assert in a debug build and silently skew
  // every later sample in a release one, so drop the remainder explicitly.
  auto const bytes_per_frame = static_cast<int> (m_sink->bytesPerFrame ());
  auto const whole = pcm.size () - (pcm.size () % bytes_per_frame);
  if (whole <= 0)
    {
      return;
    }
  if (whole != pcm.size ())
    {
      qWarning () << "NetworkAudioInput: dropped" << (pcm.size () - whole)
                  << "trailing bytes of a partial frame";
    }
  m_sink->write (pcm.constData (), whole);
}

void NetworkAudioInput::discontinuity (QString const& reason)
{
  // A hole in the timeline is handled at its place in the queue: every chunk
  // received before it has already been written, and the first chunk after it
  // lands in a fresh period. Detector places samples sequentially at
  // dec_data.params.kin and -- with no capture anchor on the wire -- has no
  // way to tell that later samples belong later than the position they land
  // in, so audio from either side of a gap would otherwise be concatenated and
  // every decode in the period would carry a DT short by the missing duration.
  // Resetting discards the partial period instead, which is the honest trade:
  // one period of decodes lost rather than a period of decodes that are
  // silently misaligned.
  ++m_discontinuities;
  if (m_sink)
    {
      m_sink->reset ();
    }
  Q_EMIT status (tr ("Network audio discontinuity (%1); period buffer reset").arg (reason));
}

void NetworkAudioInput::onLinkDropped (QString reason, quint64 generation)
{
  if (generation != m_generation || !m_started) return;
  discontinuity (reason.isEmpty () ? tr ("the connection dropped") : reason);
  Q_EMIT status (tr ("Network audio link to %1:%2 dropped; reconnecting")
                 .arg (host ()).arg (port ()));
}

void NetworkAudioInput::onReconnecting (int attempt, int maxAttempts, quint64 generation)
{
  if (generation != m_generation || !m_started) return;
  Q_EMIT status (tr ("Network audio reconnecting to %1:%2 (attempt %3 of %4)")
                 .arg (host ()).arg (port ()).arg (attempt).arg (maxAttempts));
}

void NetworkAudioInput::onLinkRestored (quint64 generation)
{
  if (generation != m_generation || !m_started) return;
  // Everything between the drop and now is missing, so the period is reset
  // again here -- and not redundantly: a reconnection that happens while the
  // source is suspended still has to leave the buffer clean for resume ().
  discontinuity (tr ("the connection was re-established"));
  // The reconnect renewed the subscription against whatever is serving now.
  // A server re-provisioned to another rate would otherwise feed the detector
  // audio labelled with the old one.
  // The reconnect renewed the subscription against whatever is serving now.
  if (!m_connection)
    {
      fail (tr ("Network audio input lost its connection."));
      return;
    }
  if (!m_connection->formatUnchanged ())
    {
      auto const now = m_connection->audioFormat ();
      fail (tr ("Network audio server %1:%2 changed its format after reconnecting "
                "(now %3 Hz / %4 channel(s)); this source was started for %5 Hz / %6 channel(s).")
            .arg (host ()).arg (port ()).arg (now.sample_rate_hz).arg (now.channel_count)
            .arg (m_grantedRate).arg (m_grantedChannels));
      return;
    }
  Q_EMIT status (tr ("Network audio link to %1:%2 restored").arg (host ()).arg (port ()));
}

void NetworkAudioInput::onStreamStopped (quint64 generation)
{
  if (generation != m_generation || !m_started) return;
  discontinuity (tr ("the server stopped the stream"));
}

void NetworkAudioInput::onLinkLost (QString reason, quint64 generation)
{
  if (generation != m_generation || !m_started) return;
  // libnaudio has given up and the handle is spent; silence here would leave
  // the station deaf with nothing on screen to say so.
  fail (tr ("Network audio connection to %1:%2 was lost and could not be re-established%3")
        .arg (host ()).arg (port ())
        .arg (reason.isEmpty () ? QString {"."} : QString {": %1"}.arg (reason)));
}

void NetworkAudioInput::pollTransportStats ()
{
  if (!m_started)
    {
      return;
    }
  na_client_stats stats;
  if (!m_connection || !m_connection->readStats (stats))
    {
      return;
    }
  if (!stats.connected)
    {
      // Every field is a default rather than a reading, so a zero here says
      // nothing. The lifecycle events own this case.
      return;
    }

  // UNAVAILABLE IS NOT ZERO. Each counter reads -1 where libnaudio is not
  // measuring it, and -1 never means "nothing was lost": sequence_gaps is
  // measured only where a reorder buffer is engaged, and socket_rx_drops only
  // where the platform exposes the kernel's drop count. On TCP -- the only
  // transport this source selects -- both are -1, so there is nothing to poll
  // and the timer is stopped rather than left reading fill every second.
  if (stats.sequence_gaps < 0 && stats.socket_rx_drops < 0)
    {
      m_statsTimer->stop ();
      Q_EMIT status (tr ("Network audio transport reports no loss counters; "
                         "discontinuities are detected from link events only"));
      return;
    }

  // The two are complementary, not redundant: sequence_gaps sees holes that
  // arrived with a hole in them, and socket_rx_drops sees audio the kernel
  // discarded before libnaudio ever saw it -- which sequence_gaps structurally
  // CANNOT see, because a full receive buffer tail-drops and leaves an unbroken
  // prefix with no hole to count.
  long long unrecovered {0};
  if (stats.sequence_gaps >= 0)
    {
      // Gaps are counted before the FEC decoder runs, so a slot counted there
      // may still have been refilled by parity; the unrecovered remainder is
      // what actually left a hole in the audio.
      auto const recovered = stats.packets_recovered_by_fec > 0
        ? stats.packets_recovered_by_fec : 0;
      unrecovered = std::max (0LL, stats.sequence_gaps - recovered);
    }
  long long const local_drops = stats.socket_rx_drops >= 0
    ? stats.socket_rx_drops : 0;

  // Counters are cumulative; a reconnect can reset them, so a negative delta is
  // treated as a fresh baseline rather than as negative loss.
  auto const gap_delta = std::max (0LL, unrecovered - m_lastUnrecoveredGaps);
  auto const drop_delta = std::max (0LL, local_drops - m_lastLocalDrops);
  m_lastUnrecoveredGaps = unrecovered;
  m_lastLocalDrops = local_drops;

  auto const lost = gap_delta + drop_delta;
  if (lost >= discontinuity_slot_threshold)
    {
      discontinuity (tr ("audio was lost in transit"));
    }
  else if (lost > 0)
    {
      // Below the threshold the period is kept: the decoder searches over DT, so
      // a small shift still decodes, and discarding the period would cost more
      // than the shift does. Reported rather than acted on.
      Q_EMIT status (tr ("Network audio lost %1 packet(s) in the last second "
                         "(%2 in transit, %3 locally); period kept")
                     .arg (lost).arg (gap_delta).arg (drop_delta));
    }
}

void NetworkAudioInput::suspend ()
{
  // Dropped on the worker thread rather than disconnected: monitoring toggles
  // often, and the stream costs nothing to hold open.
  m_suspended = true;
  Q_EMIT status (tr ("Network audio input suspended"));
}

void NetworkAudioInput::resume ()
{
  if (!m_started)
    {
      return;
    }
  if (m_sink)
    {
      m_sink->reset ();
    }
  m_suspended = false;
  Q_EMIT status (tr ("Network audio input receiving"));
}

void NetworkAudioInput::stop ()
{
  teardown ();
  clearStreamDescriptor ();
}

void NetworkAudioInput::reset (bool)
{
  // Nothing to reset: libnaudio owns the transport counters, and this class
  // holds no dropped-frame accounting of its own.
}

void NetworkAudioInput::teardown ()
{
  m_suspended = true;
  m_started = false;
  if (m_statsTimer)
    {
      m_statsTimer->stop ();
    }
  // Close only a connection this source owns. A borrowed one belongs to the
  // router and to whatever else is using it; this source simply stops
  // listening (m_started gates every slot above).
  if (m_connection && m_connection->parent () == this)
    {
      m_connection->close ();
    }
  m_sink.clear ();
  m_grantedRate = 0;
  m_grantedChannels = 0;
  m_lastUnrecoveredGaps = 0;
  m_lastLocalDrops = 0;
}

void NetworkAudioInput::fail (QString const& message)
{
  teardown ();
  clearStreamDescriptor ();
  Q_EMIT error (message);
}
