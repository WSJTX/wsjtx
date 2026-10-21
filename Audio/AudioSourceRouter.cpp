#include "Audio/AudioSourceRouter.hpp"

#include <QAudioDeviceInfo>
#include <QMetaObject>
#include <QThread>

#include "Audio/soundin.h"

#if defined (WSJT_ENABLE_NETWORK_AUDIO)
#include "Audio/NetworkAudioConnection.hpp"
#include "Audio/NetworkAudioInput.hpp"
#endif

#include "moc_AudioSourceRouter.cpp"

AudioSourceRouter::AudioSourceRouter (QObject * parent)
  : AudioInputSource {parent}
  , m_device {new SoundInput {this}}
{
  adopt (m_device);
  connect (m_device, &AudioInputSource::error, this, &AudioSourceRouter::deviceError);
}

bool AudioSourceRouter::networkAudioAvailable ()
{
#if defined (WSJT_ENABLE_NETWORK_AUDIO)
  return true;
#else
  return false;
#endif
}

void AudioSourceRouter::adopt (AudioInputSource * child)
{
  // The children are parented to the router, so MainWindow's moveToThread ()
  // carries them along and every source stays on the audio thread.
  //
  // Descriptor changes are re-published rather than merely forwarded: routing
  // them through setStreamDescriptor () keeps the router's own snapshot -- the
  // one streamDescriptor () returns -- in step with the active child.
  connect (child, &AudioInputSource::streamDescriptorChanged,
           this, [this, child] (AudioStreamDescriptor const& descriptor) {
             if (child == active ()) setStreamDescriptor (descriptor);
           });
  connect (child, &AudioInputSource::error, this,
           [this, child] (QString const& message) {
             if (child == active ()) Q_EMIT error (message);
           });
  connect (child, &AudioInputSource::status, this,
           [this, child] (QString const& message) {
             if (child == active ()) Q_EMIT status (message);
           });
}

void AudioSourceRouter::rebuildNetworkSource ()
{
  // Only ever on this object's thread -- the two public entry points hop
  // here first -- so the source built below is a child constructed ON that
  // thread, which is the only way a QObject inherits its parent's thread at
  // all. Built from another thread it would be refused as a child and left
  // behind on the caller's, with every queued slot of its marshalling design
  // then running there.
  Q_ASSERT (QThread::currentThread () == thread ());
  // The only caller already early-returns on an unchanged endpoint, so this
  // rebuilds unconditionally rather than re-deriving that comparison.
  if (m_network)
    {
      m_network->stop ();
      // Not delete: a chunk queued for the old source may still be in this
      // thread's event queue, and deleteLater lets it drain first.
      m_network->deleteLater ();
      m_network = nullptr;
    }
#if defined (WSJT_ENABLE_NETWORK_AUDIO)
  // Inside the guard: on a build without the feature the type is only
  // declared, and this member is always null there.
  if (m_connection)
    {
      // After the source that borrows it. close () joins libnaudio's threads,
      // and anything it queued at the source before that carries a generation
      // the source no longer accepts.
      m_connection->close ();
      m_connection->deleteLater ();
      m_connection = nullptr;
    }
  if (!m_host.isEmpty ())
    {
      // One connection per endpoint, owned here rather than by the source so
      // that the transmit path can share it: the wire carries both directions
      // on one transport, and an RX stop must not take a transmission down.
      m_connection = new NetworkAudioConnection {m_host, m_port, this};
      m_network = new NetworkAudioInput {m_connection, this};
      adopt (m_network);
    }
#endif
}

AudioInputSource * AudioSourceRouter::active () const
{
  if (Source::Network == m_source && m_network)
    {
      return m_network;
    }
  // Falling back to the sound card is deliberate: a network source that is
  // unavailable or unconfigured must not leave the station deaf.
  return m_device;
}

void AudioSourceRouter::setNetworkServer (QString host, quint16 port)
{
  // MainWindow calls this from the GUI thread after moving the router to the
  // audio thread. Everything below touches state the audio thread owns and
  // may build a child that has to live there, so a call arriving from any
  // other thread is re-posted to this object's own. A queued call keeps its
  // place: the startAudioInputStream that MainWindow emits right after this
  // is queued to the same thread and runs behind it.
  if (QThread::currentThread () != thread ())
    {
      QMetaObject::invokeMethod (this, [this, host, port] {setNetworkServer (host, port);},
                                 Qt::QueuedConnection);
      return;
    }

  host = host.trimmed ();
  if (host == m_host && port == m_port)
    {
      return;
    }
  m_host = std::move (host);
  m_port = port;

  auto const was_active = Source::Network == m_source && m_started;
  if (was_active)
    {
      active ()->stop ();
    }
  rebuildNetworkSource ();
  if (was_active)
    {
      restartActive ();
    }
}

void AudioSourceRouter::selectSource (Source source)
{
  if (QThread::currentThread () != thread ())
    {
      QMetaObject::invokeMethod (this, [this, source] {selectSource (source);},
                                 Qt::QueuedConnection);
      return;
    }

  if (source == m_source)
    {
      return;
    }

  auto * previous = active ();
  m_source = source;
  if (Source::Network == m_source)
    {
      rebuildNetworkSource ();
    }
  auto * next = active ();
  if (previous == next)
    {
      return;
    }

  if (m_started)
    {
      previous->stop ();
      restartActive ();
    }
}

void AudioSourceRouter::restartActive ()
{
  if (!m_started || !m_sink)
    {
      return;
    }
  active ()->start (m_deviceInfo, m_framesPerBuffer, m_sink,
                    m_downSampleFactor, m_channel);
  // adopt () republishes only the active child's changes, and the selection
  // moved before the outgoing child was stopped, so its clear was dropped.
  // Take the incoming child's state as it stands -- including no stream at
  // all, if it did not start -- rather than keep the old child's.
  setStreamDescriptor (active ()->streamDescriptor ());
  if (!m_suspended)
    {
      active ()->resume ();
    }
}

void AudioSourceRouter::start (QAudioDeviceInfo const& device, int framesPerBuffer,
                               AudioDevice * sink, unsigned downSampleFactor,
                               AudioDevice::Channel channel)
{
  m_deviceInfo = device;
  m_framesPerBuffer = framesPerBuffer;
  m_sink = sink;
  m_downSampleFactor = downSampleFactor;
  m_channel = channel;
  m_started = true;
  m_suspended = true;
  // The device source is the only one that has any use for a QAudioDeviceInfo;
  // the network source ignores it, so it is passed through unexamined.
  active ()->start (device, framesPerBuffer, sink, downSampleFactor, channel);
}

void AudioSourceRouter::suspend ()
{
  m_suspended = true;
  active ()->suspend ();
}

void AudioSourceRouter::resume ()
{
  m_suspended = false;
  active ()->resume ();
}

void AudioSourceRouter::stop ()
{
  m_started = false;
  m_suspended = true;
  m_sink.clear ();
  // Stop every source, not just the active one: a source that was running when
  // the selection changed must not be left holding a device or a socket.
  m_device->stop ();
  if (m_network) m_network->stop ();
  clearStreamDescriptor ();
}

void AudioSourceRouter::reset (bool reportDroppedFrames)
{
  active ()->reset (reportDroppedFrames);
}
