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
  enum class Profile
  {
    Ft8,
    Jtty,
    ReceiveHandoff
  };

  explicit FixtureAudioInput (QString path, Profile profile = Profile::Ft8,
                              QObject * parent = nullptr);

  Q_SLOT void start (QAudioDeviceInfo const&, int framesPerBuffer,
                     AudioDevice * sink, unsigned downSampleFactor,
                     AudioDevice::Channel channel = AudioDevice::Mono) override;
  Q_SLOT void suspend () override;
  Q_SLOT void resume () override;
  Q_SLOT void stop () override;
  Q_SLOT void reset (bool reportDroppedFrames) override;
  Q_SLOT void arm ();
  Q_SLOT void acknowledgeJttyFrames (qint64 detectorFrames);

  Q_SIGNAL void emissionStarted (qint64 utcStartMilliseconds) const;
  Q_SIGNAL void emissionFinished (qint64 frames) const;

private:
  qint64 fixtureFrames () const {return m_pcm.size () / bytesPerFrame;}
  qint64 totalFrames () const
  {
    return m_leadInFrames + fixtureFrames () + m_tailFrames;
  }
  void fail (QString const& message);
  qint64 captureTimestamp (qint64 frameIndex) const;
  void publishCaptureAnchor (qint64 firstFrame);
  void maybeSchedule ();
  void scheduleNextChunk ();
  Q_SLOT void emitNextChunk ();

  static constexpr int detectorSampleRate = 12000;
  static constexpr int bytesPerFrame = 2;

  QString m_path;
  Profile m_profile;
  QByteArray m_pcm;
  QPointer<AudioDevice> m_sink;
  QTimer * m_timer;
  QVector<int> m_chunkFrames {257, 509, 1021, 2039};
  int m_inputSampleRate {detectorSampleRate};
  qint64 m_leadInFrames {0};
  qint64 m_tailFrames {0};
  qint64 m_framesEmitted {0};
  qint64 m_periodStartMs {0};
  qint64 m_jttyAcknowledgedInputFrames {0};
  int m_chunkIndex {0};
  bool m_started {false};
  bool m_armed {false};
  bool m_suspended {true};
  bool m_emitting {false};
  bool m_jttyDecoderReady {false};
  bool m_waitingForJttyDecoder {false};
};

#endif
