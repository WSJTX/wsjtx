// -*- Mode: C++ -*-
#ifndef AUDIO_SOURCE_ROUTER_HPP__
#define AUDIO_SOURCE_ROUTER_HPP__

#include "Audio/AudioInputSource.hpp"

#include <QAudioDeviceInfo>
#include <QPointer>
#include <QString>

class SoundInput;
class NetworkAudioConnection;
class NetworkAudioInput;

//
// Chooses which AudioInputSource actually feeds the detector, and hides that
// choice from everything upstream.
//
// MainWindow holds exactly one AudioInputSource, wired once at construction.
// Rather than add a second predicate to every start/suspend/resume site -- the
// way TCI audio is threaded through MainWindow on 49 lines -- this composite
// owns the sound card and (when built and configured) a network source, and
// delegates the interface to whichever is selected. Every existing connection
// keeps working unchanged.
//
// The router also remembers the arguments of the last start (), so selecting a
// different source while running restarts the new one against the same sink.
// That is the one thing a restart-only selection cannot do.
//
class AudioSourceRouter final
  : public AudioInputSource
{
  Q_OBJECT

public:
  enum class Source
  {
    Device,   // the local sound card -- the default, and the only one on a
              // build without network audio
    Network,  // a net-audio stream
  };
  Q_ENUM (Source)

  explicit AudioSourceRouter (QObject * parent = nullptr);

  Source source () const {return m_source;}
  QString networkHost () const {return m_host;}
  quint16 networkPort () const {return m_port;}

  // True when this build can actually provide Source::Network.
  static bool networkAudioAvailable ();

  // The source currently delegated to. Never null.
  AudioInputSource * active () const;

  // The connection Source::Network uses, for the transmit path to share.
  // Null until a server is set, and always null on a build without network
  // audio. Lives on this object's thread and is replaced by a new object when
  // the endpoint changes, so a borrower must not cache it across
  // setNetworkServer ().
  NetworkAudioConnection * networkConnection () const {return m_connection;}

  // Switching while stopped just records the choice; switching while running
  // stops the old source and starts the new one against the same sink.
  //
  // This and setNetworkServer may be called from any thread: each runs on
  // this object's thread, asynchronously when called from another. Nothing
  // reads the result back synchronously, and MainWindow's follow-up start ()
  // is queued to the same thread behind them.
  Q_SLOT void selectSource (Source);

  // Endpoint for Source::Network. Applied at the next start of that source.
  Q_SLOT void setNetworkServer (QString host, quint16 port);

  // Whether the network connection is opened with its transmit lane, for a
  // SoundOutputRouter to send on. libnaudio decides the lane at connect, so
  // changing this while a connection exists rebuilds it (and restarts a
  // running network source against the same sink). Same threading rule as
  // the two slots above.
  Q_SLOT void setNetworkTransmit (bool enabled);
  bool networkTransmit () const {return m_transmit;}

  // The sound card's own errors, and only those. MainWindow treats an input
  // error as "this device is gone" and clears the device selection; a network
  // fault routed there would blank the sound-card setting the operator falls
  // back to. Network faults arrive through error () like every other, and stop
  // there.
  Q_SIGNAL void deviceError (QString message) const;

  Q_SLOT void start (QAudioDeviceInfo const&, int framesPerBuffer,
                     AudioDevice * sink, unsigned downSampleFactor,
                     AudioDevice::Channel = AudioDevice::Mono) override;
  Q_SLOT void suspend () override;
  Q_SLOT void resume () override;
  Q_SLOT void stop () override;
  Q_SLOT void reset (bool reportDroppedFrames) override;

private:
  void adopt (AudioInputSource * child);
  void rebuildNetworkSource ();
  void restartActive ();

  SoundInput * m_device;
  // Held as the base type on purpose. Only its construction is conditional, so
  // the class layout does not depend on WSJT_ENABLE_NETWORK_AUDIO -- a member
  // that changed type or vanished with the macro would be an ODR trap for any
  // translation unit that saw the header with a different setting, and would
  // also trip -Wunused-private-field when it is off.
  AudioInputSource * m_network {nullptr};
  // Forward-declared for the same reason: only its construction is conditional.
  NetworkAudioConnection * m_connection {nullptr};
  Source m_source {Source::Device};

  QString m_host;
  quint16 m_port {4533};
  bool m_transmit {false};

  // The last start () arguments, so a live source switch can re-issue them.
  // The device is among them: the sound card is the one source that needs
  // it, and a switch back to it with no device to give restarts nothing.
  QAudioDeviceInfo m_deviceInfo;
  QPointer<AudioDevice> m_sink;
  int m_framesPerBuffer {0};
  unsigned m_downSampleFactor {1};
  AudioDevice::Channel m_channel {AudioDevice::Mono};
  bool m_started {false};
  bool m_suspended {true};
};

#endif
