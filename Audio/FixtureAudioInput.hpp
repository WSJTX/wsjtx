// -*- Mode: C++ -*-
#ifndef FIXTURE_AUDIO_INPUT_HPP__
#define FIXTURE_AUDIO_INPUT_HPP__

#include "Audio/AudioInputSource.hpp"

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QVector>

class QTimer;

class FixtureAudioInput final
  : public AudioInputSource
{
  Q_OBJECT

public:
  explicit FixtureAudioInput (QString path, QObject * parent = nullptr);

  Q_SLOT void start (QAudioDeviceInfo const&, int framesPerBuffer,
                     AudioDevice * sink, unsigned downSampleFactor,
                     AudioDevice::Channel channel = AudioDevice::Mono) override;
  Q_SLOT void suspend () override;
  Q_SLOT void resume () override;
  Q_SLOT void stop () override;
  Q_SLOT void reset (bool reportDroppedFrames) override;
  Q_SLOT void arm ();

  Q_SIGNAL void emissionStarted (qint64 utcStartMilliseconds) const;
  Q_SIGNAL void emissionFinished (qint64 frames) const;

private:
  qint64 totalFrames () const {return m_pcm.size () / bytesPerFrame;}
  void fail (QString const& message);
  void maybeSchedule ();
  void scheduleNextChunk ();
  Q_SLOT void emitNextChunk ();

  static constexpr int sampleRate = 12000;
  static constexpr int bytesPerFrame = 2;

  QString m_path;
  QByteArray m_pcm;
  QPointer<AudioDevice> m_sink;
  QTimer * m_timer;
  QVector<int> m_chunkFrames {257, 509, 1021, 2039};
  qint64 m_framesEmitted {0};
  qint64 m_periodStartMs {0};
  int m_chunkIndex {0};
  bool m_started {false};
  bool m_armed {false};
  bool m_suspended {true};
  bool m_emitting {false};
};

#endif
