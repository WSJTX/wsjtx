// -*- Mode: C++ -*-
#ifndef NETWORK_SOUND_OUTPUT_HPP__
#define NETWORK_SOUND_OUTPUT_HPP__

#include "Audio/AudioOutputSink.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QString>

class NetworkAudioConnection;
class QTimer;

//
// An AudioOutputSink that sends the modulator's audio to a network audio
// server (net-audio 0xAF01, via libnaudio) instead of to a local sound card.
//
// The sink is the clock. A sound card pulls audio from the modulator at the
// DAC's rate; nothing on the wire pulls, so this class reads from the source
// itself, one 20 ms block at a time, paced by a monotonic clock and kept a
// few blocks ahead of it. Too slow and the library's TX ring underruns
// (silence on the air, and after 500 ms of it the server releases our TX
// ownership); too fast and the ring overwrites its oldest bytes. The lead is
// small on purpose so that stop () leaves almost nothing unsent.
//
// It borrows its NetworkAudioConnection -- the one the receive path opened --
// because the wire carries both directions on one transport and the transmit
// lane exists only if the connection was opened with it (setTransmitEnabled
// before open ()). The connection must be open and at the modulator's fixed
// 48 kHz: net-audio does not resample injected audio, so a server provisioned
// at another rate is refused with a named error rather than fed garbage.
//
// Every fault is an error () carrying the context of the restart () it
// belongs to, and of a kind (Open, Io, Fatal) on which MainWindow stops the
// transmission: no transmitter is left keyed on a broken path.
//
// PTT is not here. Opening the lane (NA_DUPLEX_TALK) lets audio reach the
// server; keying the radio is rig control's job, exactly as with a sound card.
//
class NetworkSoundOutput final
  : public AudioOutputSink
{
  Q_OBJECT

public:
  // The modulator's rate (TX_SAMPLE_RATE) and net-audio's native frame.
  static constexpr int sampleRate = 48000;
  static constexpr int blockFrames = 960;     // 20 ms
  // How far ahead of the clock the library's ring is kept.
  static constexpr int leadBlocks = 3;

  // What lies past this side once a frame has been injected: the wire, the
  // server's TX ring, its playback device and the codec. MEASURED at the
  // station, not modelled -- the radio's own data VOX was the detector and its
  // PTT was read through rigctld, so the figure is the time from a client's
  // last inject to the radio unkeying, with the VOX hang calibrated out.
  //
  //   27 tone bursts of 3 to 60 s: median 306 ms, p90 425, worst 516.
  //
  // 500 covers the worst burst, and JTTY's own 100 ms margin carries its guard
  // past it. Erring high costs a longer PTT tail after JTTY's last message;
  // erring low cuts symbols. The end of a transmission is deeper than its
  // start (about 100 ms) and it MOVES with the link: naudio's mixer writes
  // 20 ms of silence to the device whenever its ring is empty for two frame
  // durations and never takes it back, so every stall during a transmission
  // pushes the rest of it later. That is why this is a worst case over enough
  // bursts on a real link and not a fixed depth -- a wired link should sit
  // nearer the 100 ms start.
  static constexpr int playoutDepthMs = 500;

  explicit NetworkSoundOutput (QObject * parent = nullptr);
  ~NetworkSoundOutput () override;

  // Borrowed, never owned: the router re-sets it before every restart () so an
  // endpoint change in the receive path is picked up without coordination.
  void setConnection (NetworkAudioConnection *);
  NetworkAudioConnection * connection () const {return m_connection;}

  // True between a successful restart () and stop ()/reset ()/failure, even
  // after the source has been exhausted (the lane stays open until stop ()).
  bool isActive () const {return m_active;}
  bool isExhausted () const {return m_sourceExhausted;}
  qint64 framesSent () const {return m_framesSent;}
  // Frames the library accepted; what the snapshots report as written.
  qint64 framesAccepted () const {return m_framesAccepted;}
  // Blocks the library accepted only in part or not at all: the ring was
  // full, or the lane was closed under us.
  qint64 shortInjects () const {return m_shortInjects;}
  // The server's TX-arbitration owner as last read. Another client holding TX
  // makes the mixer drop ours with a normal return from inject, so it is
  // logged on every restart and whenever it changes.
  QString transmitOwner () const {return m_owner;}
  qreal attenuation () const {return m_attenuation;}

  int bufferSize () const override;
  int unplayedBufferBytes () const override;

  Q_SLOT void setFormat (QAudioDeviceInfo const&, unsigned channels, int frames_buffered = 0) override;
  Q_SLOT void restart (QIODevice *, TxEvidence::TxPlaybackContext,
                       qint64 period_offset_ms = -1) override;
  Q_SLOT void suspend () override;
  Q_SLOT void resume () override;
  Q_SLOT void reset () override;
  Q_SLOT void stop () override;
  Q_SLOT void setAttenuation (qreal) override;
  Q_SLOT void resetAttenuation () override;

  Q_SIGNAL void transmitOwnerChanged (QString owner) const;

private:
  Q_SLOT void pump ();
  void onLinkLost (QString reason, quint64 generation);
  bool pushBlock ();
  void scheduleNextPump ();
  void noteOwner ();
  void closeLane ();
  void fail (TxEvidence::TxBackendError, QString const& message);
  TxEvidence::TxRawPlayoutSnapshot makeSnapshot (bool startEvent, TxEvidence::TxBackendState) const;
  void publishSnapshot (bool startEvent, TxEvidence::TxBackendState) const;
  void publishUnavailableSnapshot (bool startEvent, TxEvidence::TxBackendError,
                                   QString const& diagnostic) const;

  QPointer<NetworkAudioConnection> m_connection;
  QPointer<QIODevice> m_source;
  QTimer * m_timer;
  QElapsedTimer m_elapsed;
  QByteArray m_sourceBuffer;
  QByteArray m_wireBuffer;
  QMetaObject::Connection m_linkLost;
  TxEvidence::TxPlaybackContext m_context;
  quint64 m_generation {0};
  qint64 m_startSequence {0};
  // Frames read and paced since the pacing origin, including blocks the
  // library refused, so an outage does not make the pump spin to catch up.
  // The origin moves on resume () so a suspended transmission does not burst.
  qint64 m_framesSent {0};
  qint64 m_frameOrigin {0};
  qint64 m_framesAccepted {0};
  qint64 m_shortInjects {0};
  qint64 m_zeroRun {0};
  qint64 m_lastReportMs {0};
  qint64 m_lastOwnerMs {0};
  int m_sourceBytesPerFrame {2};
  int m_sourceChannel {0};      // AudioDevice::Channel
  int m_serverChannels {0};
  qreal m_attenuation {0.};
  // The fault that ended the lane's transmission, kept in its later snapshots.
  TxEvidence::TxBackendError m_fault {TxEvidence::TxBackendError::None};
  QString m_faultMessage;
  qreal m_gain {1.};
  QString m_owner;
  bool m_active {false};
  // Whether the last restart () opened the lane; its counts stay readable
  // after stop () or a failure, until the next restart ().
  bool m_laneOpened {false};
  bool m_suspended {false};
  bool m_sourceExhausted {false};
};

#endif
