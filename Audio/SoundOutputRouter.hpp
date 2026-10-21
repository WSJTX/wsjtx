// -*- Mode: C++ -*-
#ifndef SOUND_OUTPUT_ROUTER_HPP__
#define SOUND_OUTPUT_ROUTER_HPP__

#include "Audio/AudioOutputSink.hpp"
#include "Audio/AudioSourceRouter.hpp"
#include "Audio/soundout.h"

#include <QPointer>

#include <functional>

//
// Chooses which AudioOutputSink the modulators actually play into, and hides
// that choice from everything upstream -- the twin of AudioSourceRouter on the
// transmit side.
//
// MainWindow holds exactly one AudioOutputSink, wired once at construction and
// handed to the modulator at every transmission. This composite owns the
// sound card's SoundOutput and (when built with network audio) a
// NetworkSoundOutput, and forwards the interface to whichever is selected.
// Every existing connection keeps working unchanged.
//
// The network sink borrows the NetworkAudioConnection that the receive
// path's AudioSourceRouter owns, because one connection per WSJT-X instance
// carries both directions. That router rebuilds its connection when the
// endpoint changes, so nothing is cached here: the current connection is
// re-read at every restart ().
//
// Unlike the receive side, selecting the network sink does not fall back to
// the sound card when the network is not ready. Silence on the air with the
// audio going to a local device instead is the wrong kind of quiet; the
// network sink reports a named error and the transmission does not start.
//
class SoundOutputRouter final
  : public AudioOutputSink
{
  Q_OBJECT

public:
  // The same two-way choice as the receive side, so one setting can drive
  // both routers.
  using Sink = AudioSourceRouter::Source;

  explicit SoundOutputRouter (QObject * parent = nullptr);

  Sink sink () const {return m_sink;}
  static bool networkAudioAvailable () {return AudioSourceRouter::networkAudioAvailable ();}

  // The sink currently delegated to. Never null.
  AudioOutputSink * active () const;
  // The sound card's sink -- always present.
  SoundOutput * device () const {return m_device;}
  // The network sink, null on a build without network audio.
  AudioOutputSink * network () const {return m_network;}

  // Where the network sink's connection comes from. Must live on this
  // object's thread and outlive it. Null until set: a network restart ()
  // before then fails with a named error.
  void setSourceRouter (AudioSourceRouter *);
  AudioSourceRouter * sourceRouter () const {return m_sourceRouter;}

  // May be called from any thread; runs on this object's thread. A running
  // transmission keeps the sink it started on until it stops; the next
  // restart () takes the new route.
  Q_SLOT void selectSink (Sink);

  // The sound card's own errors, and only those. MainWindow treats an output
  // error as "this device is gone" and clears the device selection; a network
  // fault routed there would blank the sound-card setting the operator falls
  // back to. Network faults arrive through error () like every other. For a
  // sound-card fault this is emitted before error ().
  Q_SIGNAL void deviceError (TxEvidence::TxOutputError error) const;

  // MainWindow's output-fault wiring, for this router or an injected sink.
  // invalidateDevice receives the sound card's faults only -- deviceError ()
  // of a router, every error () of an injected sink -- and each one before
  // report receives it, because the stop report requests ends the
  // transmission invalidateDevice judges staleness against. Both run on
  // receiver's thread.
  using FaultHandler = std::function<void (TxEvidence::TxOutputError const&)>;
  static void connectOutputFaults (AudioOutputSink * sink, QObject * receiver,
                                   FaultHandler invalidateDevice, FaultHandler report);

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

private:
  void adopt (AudioOutputSink * child);
  // The sink a call about the running transmission should reach: the one
  // restart () went to while it runs, otherwise the selected one.
  AudioOutputSink * running () const;
  void prepareNetworkSink ();

  SoundOutput * m_device;
  // Held as the interface on purpose: only its construction is conditional,
  // so the class layout does not depend on WSJT_ENABLE_NETWORK_AUDIO (see
  // AudioSourceRouter for the ODR and unused-field traps this avoids).
  AudioOutputSink * m_network {nullptr};
  QPointer<AudioSourceRouter> m_sourceRouter;
  QPointer<AudioOutputSink> m_running;
  Sink m_sink {Sink::Device};
};

#endif
