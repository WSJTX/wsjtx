#include "Audio/NetworkAudioConnection.hpp"

#include "naudio.h"

#include "moc_NetworkAudioConnection.cpp"

namespace
{
  // The minimum libnaudio this code is written against: 0.5.0, where
  // na_client_request_format arrived. This comparison cannot detect a missing
  // symbol -- a build linked against an older library dies at load, before any
  // code here runs; the configure-time check_symbol_exists is what guards
  // presence. What it CAN answer is narrower and still worth asking: 0.5.0 is a
  // RELEASED version, so a library reporting less is one whose negotiation
  // semantics differ, and that becomes a named error instead of a stream
  // silently running at the server's native format. Compare only against
  // something already released; every pre-release up to 1.0.0 reports 1.0.0.
  constexpr int required_naudio_version {NA_VERSION_ENCODE (0, 5, 0)};

  na_duplex toNaudio (NetworkAudioConnection::Duplex mode)
  {
    switch (mode)
      {
      case NetworkAudioConnection::Duplex::Talk: return NA_DUPLEX_TALK;
      case NetworkAudioConnection::Duplex::Full: return NA_DUPLEX_FULL;
      default: return NA_DUPLEX_LISTEN;
      }
  }
}

NetworkAudioConnection::NetworkAudioConnection (QString host, quint16 port,
                                                QObject * parent)
  : QObject {parent}
  , m_host {std::move (host)}
  , m_port {port}
{
}

NetworkAudioConnection::~NetworkAudioConnection ()
{
  // na_client_destroy joins every libnaudio thread, so no trampoline can be
  // running -- or start -- once close () returns. Doing it here, before
  // ~QObject, is what makes the trampolines' use of `this` safe.
  close ();
}

void NetworkAudioConnection::audioTrampoline (unsigned char const * pcm,
                                              size_t n_bytes, void * user)
{
  auto * self = static_cast<NetworkAudioConnection *> (user);
  if (!self || !pcm || !n_bytes)
    {
      return;
    }
  // One copy, one queued emission: this runs on libnaudio's receive worker and
  // the consumer lives on another thread. ~20 ms of PCM, 50 times a second.
  Q_EMIT self->audio (QByteArray {reinterpret_cast<char const *> (pcm),
                                  static_cast<int> (n_bytes)},
                      self->m_generation.load (std::memory_order_acquire));
}

void NetworkAudioConnection::errorTrampoline (char const *, char const * message,
                                              void * user)
{
  // Every failed reconnect attempt arrives here, and so does the receive error
  // that started the outage. None of them is final on its own -- the library
  // says when it has given up through on_disconnected -- so the text is kept
  // for that moment and otherwise only reported as a dropped link.
  if (auto * self = static_cast<NetworkAudioConnection *> (user))
    {
      self->m_lastLinkError = QString::fromUtf8 (message ? message : "");
    }
}

void NetworkAudioConnection::streamStoppedTrampoline (char const *, void * user)
{
  if (auto * self = static_cast<NetworkAudioConnection *> (user))
    {
      Q_EMIT self->streamStopped (self->m_generation.load (std::memory_order_acquire));
    }
}

void NetworkAudioConnection::reconnectingTrampoline (char const *, int attempt,
                                                     int max_attempts, void * user)
{
  if (auto * self = static_cast<NetworkAudioConnection *> (user))
    {
      if (1 == attempt)
        {
          // The first attempt is the moment the outage becomes a fact.
          Q_EMIT self->linkDropped (self->m_lastLinkError, self->m_generation.load (std::memory_order_acquire));
        }
      Q_EMIT self->reconnecting (attempt, max_attempts, self->m_generation.load (std::memory_order_acquire));
    }
}

void NetworkAudioConnection::reconnectedTrampoline (char const *, void * user)
{
  if (auto * self = static_cast<NetworkAudioConnection *> (user))
    {
      Q_EMIT self->linkRestored (self->m_generation.load (std::memory_order_acquire));
    }
}

void NetworkAudioConnection::disconnectedTrampoline (char const *, void * user)
{
  // libnaudio fires this once, when reconnection is exhausted (or was never
  // enabled), and the handle is spent from here on: na_client_connect on it
  // returns NA_ERR_INVALID forever. Measured against v1.0.0rc6 with a
  // three-attempt policy: on_error x3, then "Failed to reconnect after 3
  // attempts", then this. It is NOT the start of an outage.
  if (auto * self = static_cast<NetworkAudioConnection *> (user))
    {
      Q_EMIT self->linkLost (self->m_lastLinkError, self->m_generation.load (std::memory_order_acquire));
    }
}

bool NetworkAudioConnection::open (int rxSampleRate)
{
  if (m_client)
    {
      return true;
    }
  m_lastError.clear ();
  m_lastLinkError.clear ();
  m_generation.fetch_add (1, std::memory_order_acq_rel);

  if (na_version_number () < required_naudio_version)
    {
      m_lastError = tr ("Network audio needs naudio 0.5.0 or later for format negotiation; found %1.")
        .arg (QString::fromUtf8 (na_version_string ()));
      return false;
    }

  m_client = na_client_create (NA_CLIENT_BACKEND_NULL, m_host.toUtf8 ().constData (),
                               m_port, "WSJT-X");
  if (!m_client)
    {
      m_lastError = tr ("Unable to create a network audio client for %1:%2.")
        .arg (m_host).arg (m_port);
      return false;
    }

  auto fail = [this] (QString const& message) {
    m_lastError = message;
    close ();
    return false;
  };

  // Ask for the rate and NOTHING else.
  //
  // A channel layout is deliberately not requested. Under wire spec 1.2 the
  // grant is all-or-nothing and layouts other than NATIVE need a stereo
  // native, so asking for one against a mono server declines the whole request
  // and the rate reduction is lost with it -- measured: (12000, MONO_DOWNMIX)
  // against a mono server is answered 48000, while (12000, NATIVE) is granted.
  // Spec 1.3 answers the two dimensions independently, but 1.2 servers are what
  // exist in the field, and which channel to decode is a fact about the radio
  // that this end knows and the server does not, so the choice stays here
  // whether or not the server could also execute it. The consumer converts:
  // fil4_ for the rate when the sink asks for it, AudioDevice::store () for the
  // channel -- the same code the sound card has always gone through.
  //
  // With the transmit lane the RATE is not requested either. AUDIO_TX is
  // always the server's native format (wire spec 6.4; the library neither
  // resamples nor converts injected audio) and libnaudio 1.0.0rc6 has no call
  // that reports the native format to a client -- na_client_get_audio_format
  // reports the RX grant, which under a reduced subscription says nothing
  // about the native rate. Subscribing natively is the one way to learn it,
  // so a transmitting connection does, and audioFormat () then IS the format
  // the transmit path must produce. The RX consumer's own rate check turns a
  // native rate it cannot use into a named error, for both directions at once.
  auto const requested_rate = m_transmit ? 0 : rxSampleRate;
  if (NA_OK != na_client_request_format (m_client, requested_rate, NA_RX_LAYOUT_NATIVE))
    {
      return fail (requested_rate
                   ? tr ("Unable to request %1 Hz network audio.").arg (requested_rate)
                   : tr ("Unable to request network audio at the server's native rate."));
    }
  // The transmit lane is decided here and only here: connect starts the send
  // worker once, so this cannot be turned on later without reconnecting.
  if (NA_OK != na_client_set_tx_inject (m_client, m_transmit ? 1 : 0))
    {
      return fail (tr ("Unable to %1 network audio transmit.")
                   .arg (m_transmit ? tr ("enable") : tr ("disable")));
    }
  if (NA_OK != na_client_set_audio_cb (m_client, &NetworkAudioConnection::audioTrampoline, this))
    {
      return fail (tr ("Unable to attach the network audio receive callback."));
    }
  if (m_reconnectAttempts >= 0
      && NA_OK != na_client_set_reconnect_policy (m_client, m_reconnectBaseMs,
                                                  m_reconnectMaxMs, m_reconnectAttempts))
    {
      return fail (tr ("Unable to set the network audio reconnect policy."));
    }
  // Lifecycle events are what make an outage observable at all. libnaudio
  // auto-reconnects by default (1 s base, 30 s max, 10 attempts), so without
  // them a dropped link is repaired silently and the audio from either side of
  // the outage is concatenated into one period.
  na_client_callbacks cbs {};
  cbs.struct_size = sizeof cbs;
  cbs.on_disconnected = &NetworkAudioConnection::disconnectedTrampoline;
  cbs.on_stream_stopped = &NetworkAudioConnection::streamStoppedTrampoline;
  cbs.on_error = &NetworkAudioConnection::errorTrampoline;
  cbs.on_reconnecting = &NetworkAudioConnection::reconnectingTrampoline;
  cbs.on_reconnected = &NetworkAudioConnection::reconnectedTrampoline;
  if (NA_OK != na_client_set_callbacks (m_client, &cbs, this))
    {
      return fail (tr ("Unable to attach the network audio lifecycle callbacks."));
    }

  char errbuf[512] {};
  if (NA_OK != na_client_connect (m_client, errbuf, sizeof errbuf))
    {
      return fail (tr ("Unable to connect to network audio server %1:%2: %3")
                   .arg (m_host).arg (m_port).arg (QString::fromUtf8 (errbuf)));
    }

  // Only meaningful after a successful connect: before it, the library reports
  // a local placeholder that says nothing about the server.
  AudioFormat format;
  if (NA_OK != na_client_get_audio_format (m_client, &format.sample_rate_hz,
                                           &format.sample_size_bits,
                                           &format.channel_count))
    {
      return fail (tr ("Unable to read the granted network audio format."));
    }
  m_format = format;
  return true;
}

void NetworkAudioConnection::close ()
{
  if (m_client)
    {
      // Disconnects, joins every worker, and frees. After it returns no
      // callback can fire.
      na_client_destroy (m_client);
      m_client = nullptr;
    }
  m_format = {};
  // A signal queued before close () and processed after it carries the old
  // generation; bumping here lets a consumer tell the two apart even before
  // the next open ().
  m_generation.fetch_add (1, std::memory_order_acq_rel);
}

bool NetworkAudioConnection::isConnected () const
{
  return m_client && na_client_is_connected (m_client);
}

auto NetworkAudioConnection::audioFormat () const -> AudioFormat
{
  return m_format;
}

bool NetworkAudioConnection::formatUnchanged () const
{
  if (!m_client)
    {
      return false;
    }
  AudioFormat now;
  if (NA_OK != na_client_get_audio_format (m_client, &now.sample_rate_hz,
                                           &now.sample_size_bits, &now.channel_count))
    {
      return false;
    }
  return now.sample_rate_hz == m_format.sample_rate_hz
    && now.sample_size_bits == m_format.sample_size_bits
    && now.channel_count == m_format.channel_count;
}

bool NetworkAudioConnection::setDuplex (Duplex mode)
{
  return m_client && NA_OK == na_client_set_duplex (m_client, toNaudio (mode));
}

int NetworkAudioConnection::injectTransmitAudio (char const * pcm, int n_bytes)
{
  if (!m_client || !pcm || n_bytes <= 0)
    {
      return 0;
    }
  return na_client_inject_tx_audio (m_client, reinterpret_cast<unsigned char const *> (pcm), n_bytes);
}

QString NetworkAudioConnection::transmitOwner () const
{
  if (!m_client)
    {
      return {};
    }
  // Length-return-for-truncation: the call reports the length it needed, so
  // a buffer too small is grown once and the call repeated.
  QByteArray buffer (64, '\0');
  auto needed = na_client_server_tx_owner (m_client, buffer.data (), buffer.size ());
  if (needed >= buffer.size ())
    {
      buffer.resize (needed + 1);
      needed = na_client_server_tx_owner (m_client, buffer.data (), buffer.size ());
    }
  if (needed <= 0)
    {
      return {};
    }
  return QString::fromUtf8 (buffer.constData (), needed);
}

void NetworkAudioConnection::setReconnectPolicy (int baseDelayMs, int maxDelayMs, int maxAttempts)
{
  m_reconnectBaseMs = baseDelayMs;
  m_reconnectMaxMs = maxDelayMs;
  m_reconnectAttempts = maxAttempts;
}

bool NetworkAudioConnection::readStats (na_client_stats& stats) const
{
  return m_client && NA_OK == na_client_get_stats (m_client, &stats, sizeof stats);
}
