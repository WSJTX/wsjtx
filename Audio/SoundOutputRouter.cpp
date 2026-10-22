#include "Audio/SoundOutputRouter.hpp"

#include <QAudioDeviceInfo>
#include <QIODevice>
#include <QMetaObject>
#include <QThread>

#if defined (WSJT_ENABLE_NETWORK_AUDIO)
#include "Audio/NetworkAudioConnection.hpp"
#include "Audio/NetworkSoundOutput.hpp"
#endif

#include "moc_SoundOutputRouter.cpp"

SoundOutputRouter::SoundOutputRouter (QObject * parent)
  : AudioOutputSink {parent}
  , m_device {new SoundOutput {this}}
{
  // The children are parented to the router, so MainWindow's moveToThread ()
  // carries them along and every sink stays on the audio thread.
  //
  // Connected ahead of adopt () so a sound-card fault reaches deviceError ()
  // before error (): MainWindow judges a device fault stale against the
  // transmission that is current, and the stop that error () requests ends it.
  connect (m_device, &AudioOutputSink::error, this, &SoundOutputRouter::deviceError);
  adopt (m_device);
#if defined (WSJT_ENABLE_NETWORK_AUDIO)
  // Built unconditionally on a build with the feature: it holds no resource
  // until a restart () reaches it, and needs no endpoint of its own.
  m_network = new NetworkSoundOutput {this};
  adopt (m_network);
#endif
}

void SoundOutputRouter::adopt (AudioOutputSink * child)
{
  // Re-emit only what the running or selected sink says; a stale sink's
  // status must not masquerade as the current one's.
  auto const current = [this, child] {return child == running ();};
  connect (child, &AudioOutputSink::error, this,
           [this, current] (TxEvidence::TxOutputError const& fault) {if (current ()) Q_EMIT error (fault);});
  connect (child, &AudioOutputSink::status, this,
           [this, current] (QString const& message) {if (current ()) Q_EMIT status (message);});
  connect (child, &AudioOutputSink::audioOutputActive, this,
           [this, current] {if (current ()) Q_EMIT audioOutputActive ();});
  connect (child, &AudioOutputSink::audioOutputIdle, this,
           [this, current] {if (current ()) Q_EMIT audioOutputIdle ();});
  connect (child, &AudioOutputSink::rawTxPlayoutSnapshot, this,
           [this, current] (TxEvidence::TxRawPlayoutSnapshot const& snapshot) {
             if (current ()) Q_EMIT rawTxPlayoutSnapshot (snapshot);
           });
}

AudioOutputSink * SoundOutputRouter::active () const
{
  if (Sink::Network == m_sink && m_network)
    {
      return m_network;
    }
  return m_device;
}

AudioOutputSink * SoundOutputRouter::running () const
{
  return m_running ? m_running.data () : active ();
}

void SoundOutputRouter::setSourceRouter (AudioSourceRouter * router)
{
  m_sourceRouter = router;
}

void SoundOutputRouter::selectSink (Sink sink)
{
  if (QThread::currentThread () != thread ())
    {
      QMetaObject::invokeMethod (this, [this, sink] {selectSink (sink);}, Qt::QueuedConnection);
      return;
    }
  m_sink = sink;
}

void SoundOutputRouter::connectOutputFaults (AudioOutputSink * sink, QObject * receiver,
                                             FaultHandler invalidateDevice, FaultHandler report)
{
  // Connected first: an injected sink's queued calls are posted in connection
  // order. A router emits deviceError () ahead of error () itself.
  if (auto * const router = qobject_cast<SoundOutputRouter *> (sink))
    {
      connect (router, &SoundOutputRouter::deviceError, receiver, std::move (invalidateDevice));
    }
  else
    {
      connect (sink, &AudioOutputSink::error, receiver, std::move (invalidateDevice));
    }
  connect (sink, &AudioOutputSink::error, receiver, std::move (report));
}

void SoundOutputRouter::prepareNetworkSink ()
{
#if defined (WSJT_ENABLE_NETWORK_AUDIO)
  // Read fresh at every restart: the source router replaces its connection
  // whenever the endpoint changes, and a borrower must not cache it.
  if (auto * const network = qobject_cast<NetworkSoundOutput *> (m_network))
    {
      network->setConnection (m_sourceRouter ? m_sourceRouter->networkConnection () : nullptr);
    }
#endif
}

int SoundOutputRouter::bufferSize () const
{
  return running ()->bufferSize ();
}

int SoundOutputRouter::unplayedBufferBytes () const
{
  return running ()->unplayedBufferBytes ();
}

void SoundOutputRouter::setFormat (QAudioDeviceInfo const& device, unsigned channels, int frames_buffered)
{
  // Recording only: neither sink opens anything here. The device keeps its
  // format for the next time it is selected; the network sink ignores it.
  m_device->setFormat (device, channels, frames_buffered);
  if (m_network) m_network->setFormat (device, channels, frames_buffered);
}

void SoundOutputRouter::restart (QIODevice * source, TxEvidence::TxPlaybackContext context,
                                 qint64 period_offset_ms)
{
  if (m_running && m_running != active ())
    {
      m_running->stop ();
    }
  m_running = active ();
  if (m_running == m_network)
    {
      prepareNetworkSink ();
    }
  m_running->restart (source, context, period_offset_ms);
}

void SoundOutputRouter::suspend ()
{
  running ()->suspend ();
}

void SoundOutputRouter::resume ()
{
  running ()->resume ();
}

void SoundOutputRouter::reset ()
{
  running ()->reset ();
  m_running.clear ();
}

void SoundOutputRouter::stop ()
{
  running ()->stop ();
  m_running.clear ();
}

void SoundOutputRouter::setAttenuation (qreal a)
{
  m_device->setAttenuation (a);
  if (m_network) m_network->setAttenuation (a);
}

void SoundOutputRouter::resetAttenuation ()
{
  m_device->resetAttenuation ();
  if (m_network) m_network->resetAttenuation ();
}
