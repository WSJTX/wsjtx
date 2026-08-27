// -*- Mode: C++ -*-
#ifndef AUDIO_INPUT_SOURCE_HPP__
#define AUDIO_INPUT_SOURCE_HPP__

#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

#include "Audio/AudioDevice.hpp"
#include "Audio/AudioStreamDescriptor.hpp"

class QAudioDeviceInfo;

class AudioInputSource
  : public QObject
{
  Q_OBJECT

public:
  explicit AudioInputSource (QObject * parent = nullptr)
    : QObject {parent}
  {
  }

  ~AudioInputSource () override = default;

  // Return a consistent snapshot that is safe to read from another thread.
  AudioStreamDescriptor streamDescriptor () const
  {
    QMutexLocker locker {&stream_descriptor_mutex_};
    return stream_descriptor_;
  }

  Q_SLOT virtual void start (QAudioDeviceInfo const&, int framesPerBuffer,
                             AudioDevice * sink, unsigned downSampleFactor,
                             AudioDevice::Channel = AudioDevice::Mono) = 0;
  Q_SLOT virtual void suspend () = 0;
  Q_SLOT virtual void resume () = 0;
  Q_SLOT virtual void stop () = 0;
  Q_SLOT virtual void reset (bool reportDroppedFrames) = 0;

  Q_SIGNAL void error (QString message) const;
  Q_SIGNAL void status (QString message) const;
  Q_SIGNAL void streamDescriptorChanged (AudioStreamDescriptor descriptor) const;

protected:
  void setStreamDescriptor (AudioStreamDescriptor descriptor)
  {
    {
      QMutexLocker locker {&stream_descriptor_mutex_};
      if (descriptor == stream_descriptor_)
        {
          return;
        }
      stream_descriptor_ = descriptor;
    }
    Q_EMIT streamDescriptorChanged (descriptor);
  }

  void clearStreamDescriptor ()
  {
    setStreamDescriptor ({});
  }

private:
  mutable QMutex stream_descriptor_mutex_;
  AudioStreamDescriptor stream_descriptor_;
};

#endif
