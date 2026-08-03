#include "Audio/FixtureAudioInput.hpp"

#include "Audio/BWFFile.hpp"

#include <algorithm>
#include <utility>

#include <QAudioFormat>
#include <QDateTime>
#include <QSysInfo>
#include <QTimer>

#include "moc_FixtureAudioInput.cpp"

FixtureAudioInput::FixtureAudioInput (QString path, QObject * parent)
  : AudioInputSource {parent}
  , m_path {std::move (path)}
  , m_timer {new QTimer {this}}
{
  m_timer->setSingleShot (true);
  connect (m_timer, &QTimer::timeout, this, &FixtureAudioInput::emitNextChunk);
}

void FixtureAudioInput::start (QAudioDeviceInfo const&, int, AudioDevice * sink,
                               unsigned downSampleFactor, AudioDevice::Channel channel)
{
  stop ();
  if (!sink)
    {
      fail (tr ("Synthetic audio input has no detector sink."));
      return;
    }
  if (downSampleFactor != 1)
    {
      fail (tr ("Synthetic audio input requires a downsample factor of 1."));
      return;
    }
  if (channel != AudioDevice::Mono)
    {
      fail (tr ("Synthetic audio input requires the mono input channel."));
      return;
    }

  BWFFile file {QAudioFormat {}, m_path};
  if (!file.open (BWFFile::ReadOnly))
    {
      fail (tr ("Unable to open synthetic audio fixture %1: %2")
            .arg (m_path, file.errorString ()));
      return;
    }

  auto const& format = file.format ();
  if (format.codec () != "audio/pcm" || format.channelCount () != 1
      || format.sampleRate () != sampleRate || format.sampleSize () != 16
      || format.sampleType () != QAudioFormat::SignedInt
      || format.byteOrder () != QAudioFormat::LittleEndian)
    {
      fail (tr ("Synthetic audio fixture must be 12 kHz, mono, signed 16-bit little-endian PCM."));
      return;
    }

  constexpr qint64 expectedFrames = 15 * sampleRate;
  if (file.size () != expectedFrames * bytesPerFrame)
    {
      fail (tr ("Synthetic FT8 audio fixture must contain exactly %1 frames; found %2.")
            .arg (expectedFrames).arg (file.size () / bytesPerFrame));
      return;
    }

  m_pcm = file.readAll ();
  if (m_pcm.size () != file.size ())
    {
      fail (tr ("Unable to read all PCM data from synthetic audio fixture."));
      return;
    }
  if (QSysInfo::ByteOrder != QSysInfo::LittleEndian)
    {
      fail (tr ("Synthetic audio input does not yet support big-endian hosts."));
      return;
    }
  if (!sink->initialize (QIODevice::WriteOnly, channel))
    {
      fail (tr ("Unable to initialize the detector for synthetic audio input."));
      return;
    }

  m_sink = sink;
  m_started = true;
  Q_EMIT status (tr ("Synthetic audio fixture ready"));
  maybeSchedule ();
}

void FixtureAudioInput::suspend ()
{
  m_suspended = true;
  m_timer->stop ();
  Q_EMIT status (tr ("Synthetic audio input suspended"));
}

void FixtureAudioInput::resume ()
{
  if (m_sink)
    {
      m_sink->reset ();
    }
  m_suspended = false;
  if (m_emitting && m_framesEmitted)
    {
      m_periodStartMs = QDateTime::currentMSecsSinceEpoch ()
        - (m_framesEmitted * 1000 / sampleRate);
    }
  Q_EMIT status (tr ("Synthetic audio input receiving"));
  maybeSchedule ();
}

void FixtureAudioInput::stop ()
{
  m_timer->stop ();
  m_sink.clear ();
  m_pcm.clear ();
  m_framesEmitted = 0;
  m_periodStartMs = 0;
  m_chunkIndex = 0;
  m_started = false;
  m_suspended = true;
  m_emitting = false;
}

void FixtureAudioInput::reset (bool)
{
}

void FixtureAudioInput::arm ()
{
  m_armed = true;
  maybeSchedule ();
}

void FixtureAudioInput::fail (QString const& message)
{
  stop ();
  Q_EMIT error (message);
}

void FixtureAudioInput::maybeSchedule ()
{
  if (!m_started || !m_armed || m_suspended || m_emitting)
    {
      return;
    }

  auto const now = QDateTime::currentMSecsSinceEpoch ();
  constexpr qint64 periodMs = 15000;
  m_periodStartMs = ((now / periodMs) + 1) * periodMs;
  m_emitting = true;
  auto const delay = std::max<qint64> (0, m_periodStartMs - now);
  m_timer->start (static_cast<int> (delay));
}

void FixtureAudioInput::scheduleNextChunk ()
{
  auto const target = m_periodStartMs + m_framesEmitted * 1000 / sampleRate;
  auto const delay = std::max<qint64> (0, target - QDateTime::currentMSecsSinceEpoch ());
  m_timer->start (static_cast<int> (delay));
}

void FixtureAudioInput::emitNextChunk ()
{
  if (!m_emitting || m_suspended || !m_sink)
    {
      return;
    }

  if (!m_framesEmitted)
    {
      Q_EMIT emissionStarted (m_periodStartMs);
    }

  auto const remainingFrames = totalFrames () - m_framesEmitted;
  auto const chunkFrames = std::min<qint64> (
    remainingFrames, m_chunkFrames.at (m_chunkIndex % m_chunkFrames.size ()));
  auto const byteOffset = m_framesEmitted * bytesPerFrame;
  auto const chunkBytes = chunkFrames * bytesPerFrame;
  auto const written = m_sink->write (m_pcm.constData () + byteOffset, chunkBytes);
  if (written != chunkBytes)
    {
      fail (tr ("Detector accepted %1 of %2 synthetic PCM bytes.")
            .arg (written).arg (chunkBytes));
      return;
    }

  m_framesEmitted += chunkFrames;
  ++m_chunkIndex;
  if (m_framesEmitted == totalFrames ())
    {
      m_emitting = false;
      Q_EMIT status (tr ("Synthetic audio fixture exhausted"));
      Q_EMIT emissionFinished (m_framesEmitted);
      return;
    }
  scheduleNextChunk ();
}
