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

#include "moc_FixtureSoundOutput.cpp"

FixtureSoundOutput::FixtureSoundOutput (QString capturePath, Profile profile,
                                        QObject * parent)
  : SoundOutput {parent}
  , m_capturePath {std::move (capturePath)}
  , m_profile {profile}
  , m_timer {new QTimer {this}}
{
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

void FixtureSoundOutput::restart (QIODevice * source)
{
  ++m_restartCount;
  finishCapture ();

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
  m_capturedFrames.store (0);
  m_maxInternalSilentFrames.store (0);
  m_chunkIndex = 0;
  m_silentFrames = 0;
  m_nonSilentAudioSeen = false;
  m_active = true;
  m_elapsed.start ();
  if (Profile::Ft8Period == m_profile)
    {
      constexpr qint64 periodMs = 15000;
      auto const periodOffsetMs = QDateTime::currentMSecsSinceEpoch () % periodMs;
      auto const prefixFrames = periodOffsetMs * sampleRate / 1000;
      if (!appendSilence (prefixFrames)) return;
      m_framesPulled = prefixFrames;
      m_scheduleOriginFrame = prefixFrames;
    }
  Q_EMIT captureStarted (m_capturePath);
  Q_EMIT audioOutputActive ();
  m_timer->start (0);
}

void FixtureSoundOutput::stop ()
{
  if (!m_active)
    {
      return;
    }
  ++m_stopCount;
  finishCapture ();
}

void FixtureSoundOutput::finishCapture ()
{
  if (!m_active)
    {
      return;
    }

  m_active = false;
  m_timer->stop ();
  m_source.clear ();
  if (Profile::Ft8Period == m_profile && m_capture
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
  Q_EMIT audioOutputIdle ();
  Q_EMIT captureStopped (m_capturePath, frames);
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
  Q_EMIT captureFailed (message);
  Q_EMIT error (message);
  finishCapture ();
}

void FixtureSoundOutput::scheduleNextPull ()
{
  auto const targetMs = (m_framesPulled - m_scheduleOriginFrame) * 1000
    / sampleRate;
  auto const delayMs = std::max<qint64> (0, targetMs - m_elapsed.elapsed ());
  m_timer->start (static_cast<int> (delayMs));
}

void FixtureSoundOutput::pullAudio ()
{
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
  auto const sourcePcm = m_source->read (requestedBytes);
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
          Q_EMIT nonSilentAudioStarted (m_framesPulled + frame);
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
      auto const littleEndianSample = qToLittleEndian (sample);
      std::memcpy (capturedPcm.data () + frame * bytesPerCapturedFrame,
                   &littleEndianSample, sizeof littleEndianSample);
    }

  if (!appendCapturedPcm (capturedPcm)) return;

  m_framesPulled += frames;
  ++m_chunkIndex;
  scheduleNextPull ();
}
