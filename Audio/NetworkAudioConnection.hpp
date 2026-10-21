// -*- Mode: C++ -*-
#ifndef NETWORK_AUDIO_CONNECTION_HPP__
#define NETWORK_AUDIO_CONNECTION_HPP__

#include <atomic>

#include <QByteArray>
#include <QObject>
#include <QString>

struct na_stream_client;
struct na_client_stats;

//
// One connection to a network audio server (net-audio 0xAF01, via libnaudio),
// shared by everything in this program that talks to that server.
//
// The wire carries both directions on one transport, and the library decides
// at connect time whether the transmit lane exists at all (na_client_set_tx_inject
// must precede na_client_connect). So the connection cannot belong to the
// receive path: an RX source that stops must not take a running transmission's
// link down with it, and a TX sink that starts must find the lane already open.
// This class owns the na_stream_client; NetworkAudioInput borrows it for RX and
// a later sink will borrow it for TX.
//
// Threading. libnaudio delivers RX PCM on its receive worker and every
// lifecycle event on a dispatch thread. Nothing here runs on those threads
// beyond a copy into a QByteArray and a signal emission; every signal below is
// emitted from a libnaudio thread and is meant to be consumed through a QUEUED
// connection on the consumer's own thread. Because audio and lifecycle events
// are posted to the same queue in the order they happened, a consumer sees a
// link event between the last chunk before it and the first chunk after it --
// which is what lets a discontinuity be applied at the right place in the
// audio timeline. The open (), close () and duplex methods belong to the thread
// that owns this object.
//
class NetworkAudioConnection final
  : public QObject
{
  Q_OBJECT

public:
  struct AudioFormat
  {
    int sample_rate_hz {0};
    int sample_size_bits {0};
    int channel_count {0};
    bool isValid () const {return sample_rate_hz > 0 && channel_count > 0;}
  };

  enum class Duplex
  {
    Listen,   // RX flows, nothing this client sends reaches the server (default)
    Talk,     // the TX lane is open; the library mutes local playback, which
              // this program never uses -- RX PCM still arrives (proven by test)
    Full,     // both open
  };
  Q_ENUM (Duplex)

  explicit NetworkAudioConnection (QString host, quint16 port,
                                   QObject * parent = nullptr);
  ~NetworkAudioConnection () override;

  QString host () const {return m_host;}
  quint16 port () const {return m_port;}

  // Whether the transmit lane is opened at the next open (). Read once, at
  // open (): changing it afterwards takes effect at the next open ().
  void setTransmitEnabled (bool enabled) {m_transmit = enabled;}
  bool transmitEnabled () const {return m_transmit;}

  // Connect, requesting `rxSampleRate` (the server may answer with its native
  // rate instead; read audioFormat () rather than assuming). Returns false and
  // sets lastError () on failure; a failed open leaves the connection closed.
  // A second open () on an open connection is a no-op that returns true.
  bool open (int rxSampleRate);
  void close ();

  // True between a successful open () and close (), regardless of whether the
  // link is up at this instant.
  bool isOpen () const {return m_client != nullptr;}
  // True only while the link is actually up (false while libnaudio reconnects).
  bool isConnected () const;

  // The format the audio () signal carries, valid only while open.
  AudioFormat audioFormat () const;

  // Re-read the format from the library; true if it still matches what open ()
  // recorded. A reconnect renews the subscription, and the server it renews
  // against may have been re-provisioned.
  bool formatUnchanged () const;

  bool setDuplex (Duplex);

  // libnaudio's auto-reconnect policy for the next open (): base and maximum
  // delay in milliseconds, and the number of attempts before it gives up.
  // Unset, the library's own defaults apply (1 s, 30 s, 10 attempts).
  void setReconnectPolicy (int baseDelayMs, int maxDelayMs, int maxAttempts);

  // libnaudio's transport counters for the current link. False when closed.
  bool readStats (na_client_stats&) const;

  QString lastError () const {return m_lastError;}

  // Each open () and each close () starts a new generation. Every signal
  // carries the generation of the client that produced it, so a consumer can
  // drop an event queued by a client since destroyed.
  quint64 generation () const {return m_generation.load (std::memory_order_acquire);}

  // --- Emitted from libnaudio's threads; connect them queued. ---
  // One received RX frame's PCM, in audioFormat ().
  Q_SIGNAL void audio (QByteArray pcm, quint64 generation) const;
  // The link went down and the library is trying to bring it back.
  Q_SIGNAL void linkDropped (QString reason, quint64 generation) const;
  Q_SIGNAL void reconnecting (int attempt, int maxAttempts, quint64 generation) const;
  // The link is back; the subscription was renewed.
  Q_SIGNAL void linkRestored (quint64 generation) const;
  // The server stopped the stream without dropping the link.
  Q_SIGNAL void streamStopped (quint64 generation) const;
  // The library has given up: the link is down for good and this handle is
  // spent. close () is the only useful response.
  Q_SIGNAL void linkLost (QString reason, quint64 generation) const;

private:
  static void audioTrampoline (unsigned char const * pcm, size_t n_bytes, void * user);
  static void disconnectedTrampoline (char const *, void * user);
  static void streamStoppedTrampoline (char const *, void * user);
  static void errorTrampoline (char const *, char const * message, void * user);
  static void reconnectingTrampoline (char const *, int attempt, int max_attempts, void * user);
  static void reconnectedTrampoline (char const *, void * user);

  QString m_host;
  quint16 m_port;
  bool m_transmit {false};
  na_stream_client * m_client {nullptr};
  AudioFormat m_format;
  QString m_lastError;
  int m_reconnectBaseMs {-1};
  int m_reconnectMaxMs {-1};
  int m_reconnectAttempts {-1};
  // Read on libnaudio's threads (stamped into each signal), written here.
  std::atomic<quint64> m_generation {0};
  // The most recent on_error text, kept so linkLost () can say why.
  // Written on the dispatch thread, read there too (the trampolines run one
  // at a time, in order).
  QString m_lastLinkError;
};

#endif
