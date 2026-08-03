// -*- Mode: C++ -*-
#ifndef AUDIO_INPUT_SOURCE_HPP__
#define AUDIO_INPUT_SOURCE_HPP__

#include <QObject>
#include <QString>

#include "Audio/AudioDevice.hpp"

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

  Q_SLOT virtual void start (QAudioDeviceInfo const&, int framesPerBuffer,
                             AudioDevice * sink, unsigned downSampleFactor,
                             AudioDevice::Channel = AudioDevice::Mono) = 0;
  Q_SLOT virtual void suspend () = 0;
  Q_SLOT virtual void resume () = 0;
  Q_SLOT virtual void stop () = 0;
  Q_SLOT virtual void reset (bool reportDroppedFrames) = 0;

  Q_SIGNAL void error (QString message) const;
  Q_SIGNAL void status (QString message) const;
};

#endif
