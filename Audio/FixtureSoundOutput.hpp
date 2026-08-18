// -*- Mode: C++ -*-
#ifndef FIXTURE_SOUND_OUTPUT_HPP__
#define FIXTURE_SOUND_OUTPUT_HPP__

#include "Audio/soundout.h"

#include <atomic>
#include <memory>

#include <QElapsedTimer>
#include <QPointer>
#include <QString>
#include <QVector>

class AudioDevice;
class BWFFile;
class QIODevice;
class QTimer;

class FixtureSoundOutput final
  : public SoundOutput
{
  Q_OBJECT

public:
  enum class Profile
  {
    JttyStrict,
    Ft8Period
  };

  explicit FixtureSoundOutput (QString capturePath,
                               Profile profile = Profile::JttyStrict,
                               QObject * parent = nullptr);
  ~FixtureSoundOutput () override;

  int bufferSize () const override;
  qint64 capturedFrames () const {return m_capturedFrames.load ();}
  qint64 maxInternalSilentFrames () const
  {
    return m_maxInternalSilentFrames.load ();
  }
  int restartCount () const {return m_restartCount.load ();}
  int stopCount () const {return m_stopCount.load ();}
  QString const& capturePath () const {return m_capturePath;}

public Q_SLOTS:
  void setFormat (QAudioDeviceInfo const&, unsigned, int = 0) override;
  void restart (QIODevice * source) override;
  void restart (QIODevice * source, qint64 periodOffsetMs) override;
  void stop () override;

Q_SIGNALS:
  void captureStarted (QString path) const;
  void nonSilentAudioStarted (qint64 frame) const;
  void captureStopped (QString path, qint64 frames) const;
  void captureFailed (QString message) const;

private:
  bool appendCapturedPcm (QByteArray const& pcm);
  bool appendSilence (qint64 frames);
  void finishCapture ();
  void fail (QString const& message);
  void scheduleNextPull ();
  Q_SLOT void pullAudio ();

  static constexpr int sampleRate = 48000;
  static constexpr qint64 ft8PeriodFrames = 15 * sampleRate;
  static constexpr int bytesPerCapturedFrame = 2;
  static constexpr int simulatedBufferBytes = 9600;

  QString m_capturePath;
  Profile m_profile;
  QPointer<QIODevice> m_source;
  std::unique_ptr<BWFFile> m_capture;
  QTimer * m_timer;
  QElapsedTimer m_elapsed;
  QVector<int> m_chunkFrames {257, 509, 1021, 2039};
  qint64 m_framesPulled {0};
  qint64 m_scheduleOriginFrame {0};
  std::atomic<qint64> m_capturedFrames {0};
  std::atomic<qint64> m_maxInternalSilentFrames {0};
  std::atomic<int> m_restartCount {0};
  std::atomic<int> m_stopCount {0};
  int m_chunkIndex {0};
  int m_sourceBytesPerFrame {bytesPerCapturedFrame};
  int m_sourceSampleOffset {0};
  qint64 m_silentFrames {0};
  bool m_active {false};
  bool m_nonSilentAudioSeen {false};
};

#endif
