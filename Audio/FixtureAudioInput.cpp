#include "Audio/FixtureAudioInput.hpp"

#include "Audio/BWFFile.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include <QAudioFormat>
#include <QDateTime>
#include <QDebug>
#include <QSysInfo>
#include <QTimer>

#include "moc_FixtureAudioInput.cpp"

FixtureAudioInput::FixtureAudioInput (QString path, Profile profile,
                                      QObject * parent)
  : AudioInputSource {parent}
  , m_path {std::move (path)}
  , m_profile {profile}
  , m_timer {new QTimer {this}}
{
  m_timer->setSingleShot (true);
  m_timer->setTimerType (Qt::PreciseTimer);
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
  if (Profile::Ft8 == m_profile
      && downSampleFactor != 1 && downSampleFactor != 4)
    {
      fail (tr ("Synthetic FT8 audio input requires a downsample factor of 1 or 4."));
      return;
    }
  if (Profile::Jtty == m_profile
      && downSampleFactor != 1 && downSampleFactor != 4)
    {
      fail (tr ("Synthetic JTTY audio input requires a downsample factor of 1 or 4."));
      return;
    }
  if (channel != AudioDevice::Mono)
    {
      fail (tr ("Synthetic audio input requires the mono input channel."));
      return;
    }

  if (Profile::ReceiveHandoff == m_profile)
    {
      if (downSampleFactor != 1)
        {
          fail (tr ("Receive handoff fixture requires a downsample factor of 1."));
          return;
        }
      if (!sink->initialize (QIODevice::WriteOnly, channel))
        {
          fail (tr ("Unable to initialize the detector for receive handoff input."));
          return;
        }
      constexpr qint64 periodFrames = 15 * detectorSampleRate;
      m_pcm.resize (static_cast<int> (2 * periodFrames * bytesPerFrame));
      auto * samples = reinterpret_cast<qint16 *> (m_pcm.data ());
      std::fill_n (samples, periodFrames, qint16 {1234});
      std::fill_n (samples + periodFrames, periodFrames, qint16 {5678});
      m_sink = sink;
      m_inputSampleRate = detectorSampleRate;
      m_chunkFrames = {3456};
      m_started = true;
      m_suspended = false;
      AudioStreamDescriptor descriptor;
      descriptor.sample_rate_hz = detectorSampleRate;
      descriptor.sample_encoding = AudioStreamDescriptor::SampleEncoding::SignedInteger;
      descriptor.sample_size_bits = 16;
      descriptor.byte_order = AudioStreamDescriptor::ByteOrder::LittleEndian;
      descriptor.channel_count = 1;
      descriptor.channel_layout = AudioStreamDescriptor::ChannelLayout::Mono;
      descriptor.clock_domain = AudioStreamDescriptor::ClockDomain::SystemClock;
      descriptor.timing_evidence =
        AudioStreamDescriptor::TimingEvidence::CaptureTimeAnchored;
      setStreamDescriptor (descriptor);
      Q_EMIT status (tr ("Synthetic receive handoff fixture ready"));
      maybeSchedule ();
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
      || format.sampleSize () != 16
      || format.sampleType () != QAudioFormat::SignedInt
      || format.byteOrder () != QAudioFormat::LittleEndian)
    {
      fail (tr ("Synthetic audio fixture must be mono, signed 16-bit little-endian PCM."));
      return;
    }
  auto const expectedSampleRate = detectorSampleRate
    * static_cast<int> (downSampleFactor);
  if (format.sampleRate () != expectedSampleRate)
    {
      fail (tr ("Synthetic audio fixture must be %1 Hz for downsample factor %2; found %3 Hz.")
            .arg (expectedSampleRate).arg (downSampleFactor).arg (format.sampleRate ()));
      return;
    }

  if (Profile::Ft8 == m_profile)
    {
      auto const expectedFrames = qint64 {15} * format.sampleRate ();
      if (file.size () != expectedFrames * bytesPerFrame)
        {
          fail (tr ("Synthetic FT8 audio fixture must contain exactly %1 frames; found %2.")
                .arg (expectedFrames).arg (file.size () / bytesPerFrame));
          return;
        }
    }
  else if (file.size () <= 0 || file.size () % bytesPerFrame)
    {
      fail (tr ("Synthetic JTTY audio fixture must contain complete PCM frames."));
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
  m_inputSampleRate = format.sampleRate ();
  if (Profile::Jtty == m_profile)
    {
      m_leadInFrames = m_inputSampleRate / 2;
      m_tailFrames = 2 * m_inputSampleRate;
    }
  m_started = true;
  auto descriptor = audioStreamDescriptorFromQAudioFormat (format);
  if (descriptor.isValid ())
    {
      descriptor.clock_domain = AudioStreamDescriptor::ClockDomain::SystemClock;
      descriptor.timing_evidence =
        AudioStreamDescriptor::TimingEvidence::CaptureTimeAnchored;
      descriptor.can_report_discontinuities = false;
      setStreamDescriptor (descriptor);
    }
  else
    {
      qWarning () << "Opened fixture audio stream has an unrecognized format";
    }
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
        - (m_framesEmitted * 1000 / m_inputSampleRate);
      publishCaptureAnchor (m_framesEmitted);
    }
  Q_EMIT status (tr ("Synthetic audio input receiving"));
  maybeSchedule ();
}

qint64 FixtureAudioInput::captureTimestamp (qint64 frameIndex) const
{
  return m_periodStartMs + frameIndex * 1000 / m_inputSampleRate;
}

void FixtureAudioInput::publishCaptureAnchor (qint64 firstFrame)
{
  auto descriptor = streamDescriptor ();
  if (descriptor.isValid () && m_periodStartMs > 0)
    {
      descriptor.capture_anchor_utc_ms = captureTimestamp (firstFrame);
      setStreamDescriptor (descriptor);
    }
}

void FixtureAudioInput::stop ()
{
  m_timer->stop ();
  m_sink.clear ();
  m_pcm.clear ();
  m_leadInFrames = 0;
  m_tailFrames = 0;
  m_framesEmitted = 0;
  m_periodStartMs = 0;
  m_jttyAcknowledgedInputFrames = 0;
  m_chunkIndex = 0;
  m_started = false;
  m_suspended = true;
  m_emitting = false;
  m_jttyDecoderReady = false;
  m_waitingForJttyDecoder = false;
  clearStreamDescriptor ();
}

void FixtureAudioInput::reset (bool)
{
}

void FixtureAudioInput::arm ()
{
  if (Profile::ReceiveHandoff == m_profile) m_suspended = false;
  m_armed = true;
  maybeSchedule ();
}

void FixtureAudioInput::acknowledgeJttyFrames (qint64 detectorFrames)
{
  if (Profile::Jtty != m_profile || detectorFrames < 0) return;

  auto const inputFrames = detectorFrames * m_inputSampleRate / detectorSampleRate;
  m_jttyAcknowledgedInputFrames = std::max (
    m_jttyAcknowledgedInputFrames, inputFrames);
  m_jttyDecoderReady = true;
  if (m_waitingForJttyDecoder)
    {
      m_waitingForJttyDecoder = false;
      scheduleNextChunk ();
    }
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
  if (Profile::Ft8 == m_profile || Profile::ReceiveHandoff == m_profile)
    {
      constexpr qint64 periodMs = 15000;
      m_periodStartMs = ((now / periodMs) + 1) * periodMs;
    }
  else
    {
      constexpr qint64 periodMs = 180000;
      auto const fixtureDurationMs = (totalFrames () * 1000 + m_inputSampleRate - 1)
        / m_inputSampleRate;
      auto const periodOffsetMs = now % periodMs;
      m_periodStartMs = periodOffsetMs + fixtureDurationMs <= periodMs
        ? now : ((now / periodMs) + 1) * periodMs;
    }
  m_emitting = true;
  publishCaptureAnchor (m_framesEmitted);
  auto const delay = Profile::ReceiveHandoff == m_profile ? qint64 {0}
    : std::max<qint64> (0, m_periodStartMs - now);
  m_timer->start (static_cast<int> (delay));
}

void FixtureAudioInput::scheduleNextChunk ()
{
  if (Profile::ReceiveHandoff == m_profile)
    {
      m_timer->start (0);
      return;
    }
  auto const target = captureTimestamp (m_framesEmitted);
  auto const delay = std::max<qint64> (0, target - QDateTime::currentMSecsSinceEpoch ());
  m_timer->start (static_cast<int> (delay));
}

void FixtureAudioInput::emitNextChunk ()
{
  if (!m_emitting || m_suspended || !m_sink)
    {
      return;
    }

  auto const target = captureTimestamp (m_framesEmitted);
  auto const now = QDateTime::currentMSecsSinceEpoch ();
  if (Profile::ReceiveHandoff != m_profile && now < target)
    {
      m_timer->start (static_cast<int> (target - now));
      return;
    }

  if (!m_framesEmitted)
    {
      Q_EMIT emissionStarted (m_periodStartMs);
    }

  auto remainingFrames = totalFrames () - m_framesEmitted;
  if (Profile::Jtty == m_profile)
    {
      constexpr qint64 maxDetectorLeadFrames = 10240;
      auto const maxInputLeadFrames =
        maxDetectorLeadFrames * m_inputSampleRate / detectorSampleRate;
      auto const allowedFrames = m_jttyDecoderReady
        ? m_jttyAcknowledgedInputFrames + maxInputLeadFrames
        : m_leadInFrames;
      remainingFrames = std::min (
        remainingFrames, allowedFrames - m_framesEmitted);
      if (remainingFrames <= 0)
        {
          m_waitingForJttyDecoder = true;
          return;
        }
    }
  auto chunkFrames = std::min<qint64> (
    remainingFrames, m_chunkFrames.at (m_chunkIndex % m_chunkFrames.size ()));
  if (Profile::ReceiveHandoff == m_profile)
    {
      constexpr qint64 periodFrames = 15 * detectorSampleRate;
      auto const untilBoundary = periodFrames - m_framesEmitted % periodFrames;
      chunkFrames = std::min (chunkFrames, untilBoundary);
    }
  auto const chunkBytes = chunkFrames * bytesPerFrame;
  QByteArray chunk (static_cast<int> (chunkBytes), '\0');
  auto const chunkStart = m_framesEmitted;
  auto const chunkEnd = chunkStart + chunkFrames;
  auto const fixtureStart = m_leadInFrames;
  auto const fixtureEnd = fixtureStart + fixtureFrames ();
  auto const copyStart = std::max (chunkStart, fixtureStart);
  auto const copyEnd = std::min (chunkEnd, fixtureEnd);
  if (copyStart < copyEnd)
    {
      auto const sourceOffset = (copyStart - fixtureStart) * bytesPerFrame;
      auto const targetOffset = (copyStart - chunkStart) * bytesPerFrame;
      auto const copyBytes = (copyEnd - copyStart) * bytesPerFrame;
      std::memcpy (chunk.data () + targetOffset,
                   m_pcm.constData () + sourceOffset,
                   static_cast<size_t> (copyBytes));
    }
  auto const written = m_sink->write (chunk.constData (), chunkBytes);
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
