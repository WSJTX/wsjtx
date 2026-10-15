#include "Audio/FixtureSoundOutput.hpp"

#include "Audio/AudioDevice.hpp"
#include "Audio/BWFFile.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include <QAudioFormat>
#include <QDateTime>
#include <QIODevice>
#include <QSysInfo>
#include <QTimer>
#include <QtEndian>
#include <QtMath>

#include "moc_FixtureSoundOutput.cpp"

FixtureSoundOutput::FixtureSoundOutput (QString capturePath, Profile profile,
                                        unsigned pacingSpeed,
                                        QObject * parent)
  : AudioOutputSink {parent}
  , m_capturePath {std::move (capturePath)}
  , m_profile {profile}
  , m_pacingSpeed {pacingSpeed}
  , m_timer {new QTimer {this}}
{
  Q_ASSERT (pacingSpeed >= 1 && pacingSpeed <= 4);
  Q_ASSERT (Profile::JttyStrict == profile || pacingSpeed == 1);
  m_timer->setSingleShot (true);
  connect (m_timer, &QTimer::timeout, this, &FixtureSoundOutput::pullAudio);
}

FixtureSoundOutput::~FixtureSoundOutput ()
{
  finishCapture ();
}

int FixtureSoundOutput::bufferSize () const
{
  return simulatedBufferBytes;
}

void FixtureSoundOutput::setFormat (QAudioDeviceInfo const&, unsigned, int)
{
}

void FixtureSoundOutput::configureJttyCapture (qint64 expectedAudioFrames)
{
  if (m_active || Profile::JttyStrict != m_profile || expectedAudioFrames <= 0)
    {
      fail (tr ("Invalid synthetic JTTY capture configuration."));
      return;
    }
  m_configuredAudioFrames = expectedAudioFrames;
}

void FixtureSoundOutput::restart (QIODevice * source)
{
  restart (source, -1);
}

void FixtureSoundOutput::restart (QIODevice * source, qint64 periodOffsetMs)
{
  auto const revision = ++m_revision;
  ++m_restartCount;
  finishCapture ();
  if (revision != m_revision) return;

  if (m_pacingSpeed > 1 && m_configuredAudioFrames <= 0)
    {
      fail (tr ("Accelerated JTTY capture requires its expected audio extent."));
      return;
    }

  if (!source || !source->isReadable ())
    {
      fail (tr ("Synthetic audio output requires a readable source."));
      return;
    }
  if (m_capturePath.isEmpty ())
    {
      fail (tr ("Synthetic audio output requires a capture path."));
      return;
    }
  if (QSysInfo::ByteOrder != QSysInfo::LittleEndian)
    {
      fail (tr ("Synthetic audio output does not yet support big-endian hosts."));
      return;
    }

  m_sourceBytesPerFrame = bytesPerCapturedFrame;
  m_sourceSampleOffset = 0;
  if (auto const audioSource = dynamic_cast<AudioDevice *> (source))
    {
      m_sourceBytesPerFrame = static_cast<int> (audioSource->bytesPerFrame ());
      if (audioSource->channel () == AudioDevice::Right)
        {
          m_sourceSampleOffset = bytesPerCapturedFrame;
        }
    }
  if (m_sourceBytesPerFrame != bytesPerCapturedFrame
      && m_sourceBytesPerFrame != 2 * bytesPerCapturedFrame)
    {
      fail (tr ("Synthetic audio output received an unsupported source frame size."));
      return;
    }

  QAudioFormat format;
  format.setByteOrder (QAudioFormat::LittleEndian);
  format.setChannelCount (1);
  format.setCodec ("audio/pcm");
  format.setSampleRate (sampleRate);
  format.setSampleSize (16);
  format.setSampleType (QAudioFormat::SignedInt);
  m_capture.reset (new BWFFile {format, m_capturePath});
  if (!m_capture->open (BWFFile::WriteOnly))
    {
      auto const detail = m_capture->errorString ();
      m_capture.reset ();
      fail (tr ("Unable to open synthetic audio output capture %1: %2")
            .arg (m_capturePath, detail));
      return;
    }

  m_source = source;
  m_framesPulled = 0;
  m_scheduleOriginFrame = 0;
  m_expectedAudioFrames = m_configuredAudioFrames;
  m_configuredAudioFrames = 0;
  m_checkpointFrame = -1;
  m_realtimeTail = false;
  m_capturedFrames.store (0);
  m_maxInternalSilentFrames.store (0);
  m_chunkIndex = 0;
  m_silentFrames = 0;
  m_nonSilentAudioSeen = false;
  m_active = true;
  m_suspended = false;
  m_pausedDurationMs = 0;
  m_elapsed.start ();
  if (Profile::Ft8Period == m_profile)
    {
      constexpr qint64 periodMs = 15000;
      auto const capturedOffsetMs = periodOffsetMs >= 0
        ? periodOffsetMs : QDateTime::currentMSecsSinceEpoch () % periodMs;
      auto const prefixFrames = capturedOffsetMs * sampleRate / 1000;
      if (!appendSilence (prefixFrames)) return;
      m_framesPulled = prefixFrames;
      m_scheduleOriginFrame = prefixFrames;
    }
  Q_EMIT captureStarted (m_capturePath);
  if (revision != m_revision || !m_active) return;
  Q_EMIT audioOutputActive ();
  if (revision == m_revision && m_active && !m_suspended)
    m_timer->start (Profile::JttyStrict == m_profile ? 1 : 0);
}

void FixtureSoundOutput::suspend ()
{
  if (!m_active || m_suspended) return;
  m_suspended = true;
  m_pauseStartedMs = m_elapsed.elapsed ();
  m_timer->stop ();
}

void FixtureSoundOutput::resume ()
{
  if (!m_active || !m_suspended) return;
  m_pausedDurationMs += m_elapsed.elapsed () - m_pauseStartedMs;
  m_suspended = false;
  scheduleNextPull ();
}

void FixtureSoundOutput::reset ()
{
  ++m_revision;
  m_configuredAudioFrames = 0;
  finishCapture ();
}

void FixtureSoundOutput::setAttenuation (qreal attenuation)
{
  Q_ASSERT (0. <= attenuation && attenuation <= 999.);
  m_volume = qPow (10., -attenuation / 20.);
}

void FixtureSoundOutput::resetAttenuation ()
{
  m_volume = 1.;
}

void FixtureSoundOutput::stop ()
{
  ++m_revision;
  m_configuredAudioFrames = 0;
  if (!m_active)
    {
      return;
    }
  ++m_stopCount;
  finishCapture (true);
}

void FixtureSoundOutput::finishCapture (bool padPeriod)
{
  if (!m_active && !m_capture)
    {
      return;
    }

  m_active = false;
  m_suspended = false;
  m_timer->stop ();
  m_source.clear ();
  if (padPeriod && Profile::Ft8Period == m_profile && m_capture
      && m_capturedFrames.load () < ft8PeriodFrames)
    {
      if (!appendSilence (ft8PeriodFrames - m_capturedFrames.load ())) return;
      m_framesPulled = ft8PeriodFrames;
    }
  if (m_capture)
    {
      m_capture->close ();
      m_capture.reset ();
    }
  auto const frames = m_capturedFrames.load ();
  m_checkpointFrame = -1;
  m_expectedAudioFrames = 0;
  auto const revision = m_revision;
  Q_EMIT audioOutputIdle ();
  if (revision == m_revision) Q_EMIT captureStopped (m_capturePath, frames);
}

bool FixtureSoundOutput::appendCapturedPcm (QByteArray const& pcm)
{
  auto const frames = pcm.size () / bytesPerCapturedFrame;
  if (pcm.size () % bytesPerCapturedFrame)
    {
      fail (tr ("Synthetic audio output received a partial captured frame."));
      return false;
    }
  if (Profile::Ft8Period == m_profile
      && m_capturedFrames.load () + frames > ft8PeriodFrames)
    {
      fail (tr ("Synthetic FT8 audio output exceeded its 15-second capture period."));
      return false;
    }
  auto const written = m_capture->write (pcm);
  if (written != pcm.size ())
    {
      fail (tr ("Synthetic audio output wrote %1 of %2 capture bytes: %3")
            .arg (written).arg (pcm.size ()).arg (m_capture->errorString ()));
      return false;
    }
  m_capturedFrames.store (m_capturedFrames.load () + frames);
  return true;
}

bool FixtureSoundOutput::appendSilence (qint64 frames)
{
  while (frames > 0)
    {
      auto const chunkFrames = static_cast<int> (std::min<qint64> (frames, 4096));
      if (!appendCapturedPcm (QByteArray {chunkFrames * bytesPerCapturedFrame, '\0'}))
        {
          return false;
        }
      frames -= chunkFrames;
    }
  return true;
}

void FixtureSoundOutput::fail (QString const& message)
{
  auto const revision = ++m_revision;
  m_configuredAudioFrames = 0;
  finishCapture ();
  if (revision != m_revision) return;
  Q_EMIT captureFailed (message);
  if (revision == m_revision) Q_EMIT error (message);
}

void FixtureSoundOutput::scheduleNextPull ()
{
  if (!m_active || m_suspended) return;
  if (Profile::JttyStrict == m_profile)
    {
      if (!m_realtimeTail && m_expectedAudioFrames > 0
          && m_framesPulled >= m_expectedAudioFrames)
        {
          // Preserve the real backend drain interval after the waveform ends.
          m_realtimeTail = true;
          m_scheduleOriginFrame = m_framesPulled;
          m_elapsed.restart ();
          m_pausedDurationMs = 0;
        }
      auto const speed = m_realtimeTail ? 1u : m_pacingSpeed;
      auto const targetMs = (m_framesPulled - m_scheduleOriginFrame) * 1000
        / (sampleRate * speed);
      auto const delayMs = targetMs - (m_elapsed.elapsed () - m_pausedDurationMs);
      if (delayMs < 0)
        {
          m_scheduleOriginFrame = m_framesPulled;
          m_elapsed.restart ();
          m_pausedDurationMs = 0;
        }
      m_timer->start (static_cast<int> (std::max<qint64> (1, delayMs)));
      return;
    }
  auto const targetMs = (m_framesPulled - m_scheduleOriginFrame) * 1000
    / sampleRate;
  auto const delayMs = std::max<qint64> (
    0, targetMs - (m_elapsed.elapsed () - m_pausedDurationMs));
  m_timer->start (static_cast<int> (delayMs));
}

void FixtureSoundOutput::pullAudio ()
{
  if (m_suspended) return;
  if (!m_active || !m_source || !m_capture)
    {
      if (m_active)
        {
          fail (tr ("Synthetic audio output source disappeared during capture."));
        }
      return;
    }

  if (Profile::Ft8Period == m_profile
      && m_capturedFrames.load () == ft8PeriodFrames)
    {
      fail (tr ("Synthetic FT8 audio output continued beyond its 15-second capture period."));
      return;
    }

  auto frames = m_chunkFrames.at (m_chunkIndex % m_chunkFrames.size ());
  if (Profile::Ft8Period == m_profile)
    {
      frames = static_cast<int> (std::min<qint64> (
        frames, ft8PeriodFrames - m_capturedFrames.load ()));
    }
  auto const requestedBytes = qint64 (frames) * m_sourceBytesPerFrame;
  auto const revision = m_revision;
  auto const sourcePcm = m_source->read (requestedBytes);
  if (revision != m_revision || !m_active) return;
  if (Profile::JttyStrict == m_profile && sourcePcm.size () != requestedBytes)
    {
      fail (tr ("Synthetic audio output received %1 of %2 source bytes.")
            .arg (sourcePcm.size ()).arg (requestedBytes));
      return;
    }
  if (sourcePcm.size () < 0 || sourcePcm.size () > requestedBytes
      || sourcePcm.size () % m_sourceBytesPerFrame)
    {
      fail (tr ("Synthetic audio output received an invalid source byte count."));
      return;
    }

  QByteArray capturedPcm (frames * bytesPerCapturedFrame, '\0');
  auto const sourceFrames = sourcePcm.size () / m_sourceBytesPerFrame;
  for (int frame = 0; frame < frames; ++frame)
    {
      qint16 sample {0};
      if (frame < sourceFrames)
        {
          std::memcpy (&sample,
                       sourcePcm.constData () + frame * m_sourceBytesPerFrame
                         + m_sourceSampleOffset,
                       sizeof sample);
        }
      if (!m_nonSilentAudioSeen && sample)
        {
          m_nonSilentAudioSeen = true;
          m_checkpointFrame = m_framesPulled + frame + 72000;
          Q_EMIT nonSilentAudioStarted (m_framesPulled + frame);
          if (revision != m_revision || !m_active) return;
        }
      if (sample)
        {
          if (m_nonSilentAudioSeen)
            {
              m_maxInternalSilentFrames.store (
                std::max (m_maxInternalSilentFrames.load (), m_silentFrames));
            }
          m_silentFrames = 0;
        }
      else if (m_nonSilentAudioSeen)
        {
          ++m_silentFrames;
        }
      auto const littleEndianSample = qToLittleEndian (
        static_cast<qint16> (qRound (sample * m_volume)));
      std::memcpy (capturedPcm.data () + frame * bytesPerCapturedFrame,
                   &littleEndianSample, sizeof littleEndianSample);
    }

  if (!appendCapturedPcm (capturedPcm)) return;

  m_framesPulled += frames;
  ++m_chunkIndex;
  if (Profile::JttyStrict == m_profile && m_checkpointFrame >= 0
      && m_framesPulled >= m_checkpointFrame)
    {
      m_checkpointFrame = -1;
      Q_EMIT playbackCheckpoint (m_restartCount.load (), m_framesPulled);
      if (revision != m_revision || !m_active) return;
    }
  scheduleNextPull ();
}
