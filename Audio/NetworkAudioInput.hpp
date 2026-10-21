// -*- Mode: C++ -*-
#ifndef NETWORK_AUDIO_INPUT_HPP__
#define NETWORK_AUDIO_INPUT_HPP__

#include "Audio/AudioInputSource.hpp"

#include <QByteArray>
#include <QPointer>
#include <QString>

class QTimer;
class NetworkAudioConnection;

//
// An AudioInputSource that receives RX audio from a network audio server
// (net-audio 0xAF01) instead of from a local sound card.
//
// It is deliberately thin. The wire protocol, its framing, CRC, reconnection
// and loss recovery all live in libnaudio, and the connection itself belongs
// to NetworkAudioConnection so that a transmit path can share it. This class:
//
//   1. opens the connection asking for the exact PCM rate the decoders want --
//      12 kHz * downSampleFactor -- and checks what was actually granted;
//   2. takes the received PCM on this object's thread, which is the thread
//      that owns the Detector, and writes it there;
//   3. publishes an AudioStreamDescriptor describing what actually arrived;
//   4. turns an outage into a period-buffer reset, so audio from either side
//      of a hole is never spliced into one period, and a link that cannot be
//      restored into an error rather than silence.
//
// Everything downstream -- downsampling (when the sink asks for it), period
// alignment, the waterfall, the decoders -- is the existing Detector path,
// unchanged. This class adds a producer; it does not add a second way to fill
// dec_data.
//
class NetworkAudioInput final
  : public AudioInputSource
{
  Q_OBJECT

public:
  // The decoders' sample rate. The requested rate is this times the sink's
  // downSampleFactor, so a sink that wants to run fil4_ itself asks for 48 kHz
  // and one that wants ready-made 12 kHz asks for 12 kHz.
  static constexpr int detectorSampleRate = 12000;

  // Borrow a connection owned by someone else (the router, which will also
  // lend it to the transmit path). It must live on this object's thread and
  // outlive this object; stop () leaves it open.
  explicit NetworkAudioInput (NetworkAudioConnection *, QObject * parent = nullptr);
  // Own a private connection to host:port -- for a caller with no transmit
  // path to share with, and for tests. stop () closes it.
  NetworkAudioInput (QString host, quint16 port, QObject * parent = nullptr);
  ~NetworkAudioInput () override;

  NetworkAudioConnection * connection () const;
  QString host () const;
  quint16 port () const;

  // True between a successful start () and the next stop () while the link
  // is actually up. False while libnaudio is reconnecting, and after it has
  // given up.
  bool isConnected () const;

  // How many discontinuities have been raised since the last start (). A
  // discontinuity is a hole in the audio timeline big enough that carrying the
  // period buffer across it would misalign every later decode in that period.
  qint64 discontinuityCount () const {return m_discontinuities;}

  // Forwarded to the connection; see NetworkAudioConnection::setReconnectPolicy.
  void setReconnectPolicy (int baseDelayMs, int maxDelayMs, int maxAttempts);

  Q_SLOT void start (QAudioDeviceInfo const&, int framesPerBuffer,
                     AudioDevice * sink, unsigned downSampleFactor,
                     AudioDevice::Channel channel = AudioDevice::Mono) override;
  Q_SLOT void suspend () override;
  Q_SLOT void resume () override;
  Q_SLOT void stop () override;
  Q_SLOT void reset (bool reportDroppedFrames) override;

private:
  // Everything the connection reports arrives through a queued connection
  // from libnaudio's threads and so runs on THIS object's thread, in the one
  // event queue, in the order it happened. So a link event is handled between
  // the last chunk received before it and the first chunk after it, which is
  // what lets the period buffer be reset at the right place in the audio
  // timeline. Each signal carries the generation of the client that produced
  // it, so an event queued by a client since closed is recognised and dropped.
  void deliver (QByteArray pcm, quint64 generation);
  void onLinkDropped (QString reason, quint64 generation);
  void onReconnecting (int attempt, int maxAttempts, quint64 generation);
  void onLinkRestored (quint64 generation);
  void onStreamStopped (quint64 generation);
  void onLinkLost (QString reason, quint64 generation);

  // Poll libnaudio's transport counters for loss the lifecycle events cannot
  // see. Runs on this object's thread.
  Q_SLOT void pollTransportStats ();

  void discontinuity (QString const& reason);
  void fail (QString const& message);
  void teardown ();

  // A QPointer because a BORROWED connection is another object's child and
  // may be destroyed before this one -- the router parents both, in that
  // order -- and nothing here may touch it once it is gone.
  QPointer<NetworkAudioConnection> m_connection;
  QPointer<AudioDevice> m_sink;
  // The connection generation this source started against.
  quint64 m_generation {0};
  bool m_started {false};
  // Only ever touched on this object's thread: the audio arrives here queued.
  bool m_suspended {true};
  int m_grantedRate {0};
  int m_grantedChannels {0};
  qint64 m_discontinuities {0};

  QTimer * m_statsTimer {nullptr};
  // Transport counters as of the previous poll, so each poll sees a delta
  // rather than a running total. Owned by this object's thread.
  long long m_lastUnrecoveredGaps {0};
  long long m_lastLocalDrops {0};
};

#endif
