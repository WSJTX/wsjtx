#include "soundout.h"

#include <QDateTime>
#include <QAudioDeviceInfo>
#include <QAudioOutput>
#include <QSysInfo>
#include <qmath.h>
#include <QDebug>

#include "Logger.hpp"
#include "Audio/AudioDevice.hpp"

#include "moc_soundout.cpp"

namespace
{
  TxEvidence::TxBackendState txBackendState (QAudio::State state) noexcept
  {
    switch (state)
      {
      case QAudio::ActiveState: return TxEvidence::TxBackendState::Active;
      case QAudio::IdleState: return TxEvidence::TxBackendState::Idle;
      case QAudio::SuspendedState: return TxEvidence::TxBackendState::Suspended;
      case QAudio::StoppedState: return TxEvidence::TxBackendState::Stopped;
#if QT_VERSION >= QT_VERSION_CHECK (5, 10, 0)
      case QAudio::InterruptedState: return TxEvidence::TxBackendState::Interrupted;
#endif
      }
    return TxEvidence::TxBackendState::Unknown;
  }

  TxEvidence::TxBackendError txBackendError (QAudio::Error error) noexcept
  {
    switch (error)
      {
      case QAudio::NoError: return TxEvidence::TxBackendError::None;
      case QAudio::OpenError: return TxEvidence::TxBackendError::Open;
      case QAudio::IOError: return TxEvidence::TxBackendError::Io;
      case QAudio::UnderrunError: return TxEvidence::TxBackendError::Underrun;
      case QAudio::FatalError: return TxEvidence::TxBackendError::Fatal;
      }
    return TxEvidence::TxBackendError::Unknown;
  }
}

bool SoundOutput::checkStream () const
{
  bool result {false};

  Q_ASSERT_X (m_stream, "SoundOutput", "programming error");
  if (m_stream) {
    switch (m_stream->error ())
      {
      case QAudio::OpenError:
        Q_EMIT error (tr ("An error opening the audio output device has occurred."));
        break;

      case QAudio::IOError:
        Q_EMIT error (tr ("An error occurred during write to the audio output device."));
        break;

      case QAudio::UnderrunError:
        Q_EMIT error (tr ("Audio data not being fed to the audio output device fast enough."));
        break;

      case QAudio::FatalError:
        Q_EMIT error (tr ("Non-recoverable error, audio output device not usable at this time."));
        break;

      case QAudio::NoError:
        result = true;
        break;
      }
  }
  return result;
}

void SoundOutput::setFormat (QAudioDeviceInfo const& device, unsigned channels, int frames_buffered)
{
  Q_ASSERT (0 < channels && channels < 3);
  m_device = device;
  m_channels = channels;
  m_framesBuffered = frames_buffered;
}

void SoundOutput::restart (QIODevice * source)
{
  ++m_backendStartSequence;
  bool failed {false};
  if (!m_device.isNull ())
    {
      QAudioFormat format (m_device.preferredFormat ());
      //  qDebug () << "Preferred audio output format:" << format;
      format.setChannelCount (m_channels);
      format.setCodec ("audio/pcm");
      format.setSampleRate (48000);
      format.setSampleType (QAudioFormat::SignedInt);
      format.setSampleSize (16);
      format.setByteOrder (QAudioFormat::Endian (QSysInfo::ByteOrder));
      if (!format.isValid ())
        {
          Q_EMIT error (tr ("Requested output audio format is not valid."));
          failed = true;
        }
      else if (!m_device.isFormatSupported (format))
        {
          Q_EMIT error (tr ("Requested output audio format is not supported on device."));
          failed = true;
        }
      else
        {
          // qDebug () << "Selected audio output format:" << format;
          m_stream.reset (new QAudioOutput (m_device, format));
          checkStream ();
          m_stream->setVolume (m_volume);
          m_stream->setNotifyInterval(1000);
          error_ = false;

          connect (m_stream.data(), &QAudioOutput::stateChanged, this, &SoundOutput::handleStateChanged);
          connect (m_stream.data(), &QAudioOutput::notify, [this] () {
              checkStream ();
              publishRawTxPlayoutSnapshot ();
            });

          //      qDebug() << "A" << m_volume << m_stream->notifyInterval();
        }
    }
  else
    {
      failed = true;
    }
  if (failed)
    {
      publishUnavailableTxPlayoutSnapshot (true, tr ("Audio output restart attempt failed."));
    }
  if (!m_stream)
    {
      if (!error_)
        {
          error_ = true;        // only signal error once
          Q_EMIT error (tr ("No audio output device configured."));
        }
      if (!failed)
        {
          publishUnavailableTxPlayoutSnapshot (true, tr ("No audio output device configured."));
        }
      return;
    }
  else
    {
      error_ = false;
    }

  // we have to set this before every start on the stream because the
  // Windows implementation seems to forget the buffer size after a
  // stop.
  //qDebug () << "SoundOut default buffer size (bytes):" << m_stream->bufferSize () << "period size:" << m_stream->periodSize ();
  if (m_framesBuffered > 0)
    {
      m_stream->setBufferSize (m_stream->format().bytesForFrames (m_framesBuffered));
    }
  m_stream->setCategory ("production");
  m_stream->start (source);
  publishRawTxPlayoutSnapshot (true);
//  LOG_DEBUG ("Selected buffer size (bytes): " << m_stream->bufferSize () << " period size: " << m_stream->periodSize ());
}

void SoundOutput::suspend ()
{
  if (m_stream && QAudio::ActiveState == m_stream->state ())
    {
      m_stream->suspend ();
      checkStream ();
      publishRawTxPlayoutSnapshot ();
    }
}

void SoundOutput::resume ()
{
  if (m_stream && QAudio::SuspendedState == m_stream->state ())
    {
      m_stream->resume ();
      checkStream ();
      publishRawTxPlayoutSnapshot ();
    }
}

void SoundOutput::reset ()
{
  if (m_stream)
    {
      auto const beforeReset = makeRawTxPlayoutSnapshot ();
      m_stream->reset ();
      checkStream ();
      Q_EMIT rawTxPlayoutSnapshot (beforeReset);
      publishRawTxPlayoutSnapshot ();
    }
  else
    {
      publishRawTxPlayoutSnapshot ();
    }
}

void SoundOutput::stop ()
{
  if (m_stream)
    {
      auto const beforeStop = makeRawTxPlayoutSnapshot ();
      m_stream->reset ();
      m_stream->stop ();
      Q_EMIT rawTxPlayoutSnapshot (beforeStop);
      publishRawTxPlayoutSnapshot ();
    }
  else
    {
      publishRawTxPlayoutSnapshot ();
    }
#ifdef __APPLE__
  // this code is here to help certain rigs not drop audio, however on Sequoia this causes audio to drop, so we don't do it!
  if( QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 15) )
    m_stream.reset ();
#endif
}

qreal SoundOutput::attenuation () const
{
  return -(20. * qLn (m_volume) / qLn (10.));
}

int SoundOutput::bufferSize () const
{
  return m_stream ? m_stream->bufferSize () : 0;
}

TxEvidence::TxRawPlayoutSnapshot SoundOutput::makeRawTxPlayoutSnapshot (bool startEvent) const
{
  TxEvidence::TxRawPlayoutSnapshot snapshot;
  if (!m_stream) return snapshot;

  QAudioFormat const format {m_stream->format ()};
  qint64 const capacity {m_stream->bufferSize ()};
  qint64 const available {m_stream->bytesFree ()};
  snapshot.tier = TxEvidence::TxPlayoutTier::DeviceClock;
  snapshot.backend_start_sequence = m_backendStartSequence;
  snapshot.start_event = startEvent;
  snapshot.available = true;
  snapshot.state = txBackendState (m_stream->state ());
  snapshot.error = txBackendError (m_stream->error ());
  snapshot.available_bytes = available;
  snapshot.buffered_bytes = capacity >= 0 && available >= 0 ? qMax<qint64> (0, capacity - available) : -1;
  snapshot.capacity_bytes = capacity;
  snapshot.processed_usecs = m_stream->processedUSecs ();
  snapshot.elapsed_usecs = m_stream->elapsedUSecs ();
  snapshot.channel_count = format.channelCount ();
  snapshot.bytes_per_frame = format.bytesPerFrame ();
  snapshot.sample_rate_hz = format.sampleRate ();
  snapshot.report_interval_ms = m_stream->notifyInterval ();
  return snapshot;
}

void SoundOutput::publishRawTxPlayoutSnapshot (bool startEvent) const
{
  if (!m_stream)
    {
      publishUnavailableTxPlayoutSnapshot (startEvent, tr ("No audio output stream is active."));
      return;
    }
  auto const snapshot = makeRawTxPlayoutSnapshot (startEvent);
  Q_EMIT rawTxPlayoutSnapshot (snapshot);
}

void SoundOutput::publishUnavailableTxPlayoutSnapshot (bool startEvent, QString const& diagnostic) const
{
  TxEvidence::TxRawPlayoutSnapshot snapshot;
  snapshot.backend_start_sequence = m_backendStartSequence;
  snapshot.start_event = startEvent;
  snapshot.state = TxEvidence::TxBackendState::Unavailable;
  snapshot.error = TxEvidence::TxBackendError::Open;
  snapshot.diagnostic = diagnostic;
  Q_EMIT rawTxPlayoutSnapshot (snapshot);
}

void SoundOutput::setAttenuation (qreal a)
{
  Q_ASSERT (0. <= a && a <= 999.);
  m_volume = qPow(10.0, -a/20.0);
  //  qDebug () << "SoundOut: attn = " << a << ", vol = " << m_volume;
  if (m_stream)
    {
      m_stream->setVolume (m_volume);
    }
}

void SoundOutput::resetAttenuation ()
{
  m_volume = 1.;
  if (m_stream)
    {
      m_stream->setVolume (m_volume);
    }
}

void SoundOutput::handleStateChanged (QAudio::State newState)
{
  switch (newState)
    {
    case QAudio::IdleState:
      Q_EMIT status (tr ("Idle"));
      Q_EMIT audioOutputIdle ();
      break;

    case QAudio::ActiveState:
      Q_EMIT status (tr ("Sending"));
      Q_EMIT audioOutputActive ();
      break;

    case QAudio::SuspendedState:
      Q_EMIT status (tr ("Suspended"));
      break;

#if QT_VERSION >= QT_VERSION_CHECK (5, 10, 0)
    case QAudio::InterruptedState:
      Q_EMIT status (tr ("Interrupted"));
      break;
#endif

    case QAudio::StoppedState:
      if (!checkStream ())
        {
          Q_EMIT status (tr ("Error"));
        }
      else
        {
          Q_EMIT status (tr ("Stopped"));
        }
      break;
    }
  publishRawTxPlayoutSnapshot ();
}
