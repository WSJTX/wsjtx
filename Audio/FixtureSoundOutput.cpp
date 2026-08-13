#include "Audio/FixtureSoundOutput.hpp"

#include "Audio/AudioDevice.hpp"
#include "Audio/BWFFile.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include <QAudioFormat>
#include <QIODevice>
#include <QSysInfo>
#include <QTimer>
#include <QtEndian>

#include "moc_FixtureSoundOutput.cpp"

FixtureSoundOutput::FixtureSoundOutput (QString capturePath, QObject * parent)
  : SoundOutput {parent}
  , m_capturePath {std::move (capturePath)}
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
  m_capturedFrames.store (0);
  m_maxInternalSilentFrames.store (0);
  m_chunkIndex = 0;
  m_silentFrames = 0;
  m_nonSilentAudioSeen = false;
  m_active = true;
  m_elapsed.start ();
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

  m_timer->stop ();
  m_source.clear ();
  if (m_capture)
    {
      m_capture->close ();
      m_capture.reset ();
    }
  m_active = false;
  auto const frames = m_capturedFrames.load ();
  Q_EMIT audioOutputIdle ();
  Q_EMIT captureStopped (m_capturePath, frames);
}

void FixtureSoundOutput::fail (QString const& message)
{
  Q_EMIT captureFailed (message);
  Q_EMIT error (message);
  finishCapture ();
}

void FixtureSoundOutput::scheduleNextPull ()
{
  auto const targetMs = m_framesPulled * 1000 / sampleRate;
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

  auto const frames = m_chunkFrames.at (m_chunkIndex % m_chunkFrames.size ());
  auto const requestedBytes = qint64 (frames) * m_sourceBytesPerFrame;
  auto const sourcePcm = m_source->read (requestedBytes);
  if (sourcePcm.size () != requestedBytes)
    {
      fail (tr ("Synthetic audio output received %1 of %2 source bytes.")
            .arg (sourcePcm.size ()).arg (requestedBytes));
      return;
    }

  QByteArray capturedPcm (frames * bytesPerCapturedFrame, Qt::Uninitialized);
  for (int frame = 0; frame < frames; ++frame)
    {
      qint16 sample;
      std::memcpy (&sample,
                   sourcePcm.constData () + frame * m_sourceBytesPerFrame
                     + m_sourceSampleOffset,
                   sizeof sample);
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

  auto const written = m_capture->write (capturedPcm);
  if (written != capturedPcm.size ())
    {
      fail (tr ("Synthetic audio output wrote %1 of %2 capture bytes: %3")
            .arg (written).arg (capturedPcm.size ()).arg (m_capture->errorString ()));
      return;
    }

  m_framesPulled += frames;
  m_capturedFrames.store (m_framesPulled);
  ++m_chunkIndex;
  scheduleNextPull ();
}
