// -*- Mode: C++ -*-
#ifndef SOUNDIN_H__
#define SOUNDIN_H__

#include <limits>
#include <QString>
#include <QDateTime>
#include <QScopedPointer>
#include <QPointer>
#include <QAudioInput>

#include "Audio/AudioInputSource.hpp"

class QAudioDeviceInfo;
class QAudioInput;

// Gets audio data from sound sample source and passes it to a sink device
class SoundInput
  final : public AudioInputSource
{
  Q_OBJECT;

public:
  SoundInput (QObject * parent = nullptr)
    : AudioInputSource {parent}
    , cummulative_lost_usec_ {std::numeric_limits<qint64>::min ()}
  {
  }

  ~SoundInput () override;

  // sink must exist from the start call until the next start call or
  // stop call
  Q_SLOT void start(QAudioDeviceInfo const&, int framesPerBuffer, AudioDevice * sink, unsigned downSampleFactor, AudioDevice::Channel = AudioDevice::Mono) override;
  Q_SLOT void suspend () override;
  Q_SLOT void resume () override;
  Q_SLOT void stop () override;
  Q_SLOT void reset (bool report_dropped_frames) override;

private:
  // used internally
  Q_SLOT void handleStateChanged (QAudio::State);

  bool checkStream ();

  QScopedPointer<QAudioInput> m_stream;
  QPointer<AudioDevice> m_sink;
  qint64 cummulative_lost_usec_;
};

#endif
