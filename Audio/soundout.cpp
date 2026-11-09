#include "soundout.h"

#include <QDateTime>
#include <QAudioDeviceInfo>
#include <QAudioOutput>
#include <QSysInfo>
#include <qmath.h>
#include <QDebug>
#include <QScopedValueRollback>

#include "Logger.hpp"
#include "Audio/AudioDevice.hpp"

#include "moc_soundout.cpp"

namespace
{
  bool playbackMetricsAvailable (QAudio::State state, QAudio::Error error) noexcept
  {
    return (state == QAudio::ActiveState || state == QAudio::IdleState)
      && (error == QAudio::NoError || error == QAudio::UnderrunError);
  }

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

bool SoundOutput::isCurrentStream (TxEvidence::TxPlaybackContext context, qint64 sequence) const
{
  return m_stream && context == m_context && sequence == m_backendStartSequence;
}

void SoundOutput::logStartupFailure (char const * phase, QAudioFormat const& format,
                                    QAudio::State state, QAudio::Error streamError,
                                    QString const& diagnostic) const
{
  auto deviceName = m_device.deviceName ();
  deviceName.replace ('\n', ' ').replace ('\r', ' ');
  LOG_ERROR ("Audio output startup failed phase=" << phase
             << " backend=qt-audio qt=" << qVersion ()
             << " platform=" << QSysInfo::productType ().toStdString ()
             << " os_version=" << QSysInfo::productVersion ().toStdString ()
             << " device=\"" << deviceName.toStdString () << '"'
             << " rate_hz=" << format.sampleRate ()
             << " channels=" << format.channelCount ()
             << " sample_bits=" << format.sampleSize ()
             << " sample_type=" << int (format.sampleType ())
             << " byte_order=" << int (format.byteOrder ())
             << " buffer_frames=" << m_framesBuffered
             << " buffer_bytes=" << (m_framesBuffered > 0 && format.isValid ()
                                      ? format.bytesForFrames (m_framesBuffered) : 0)
             << " state=" << int (state) << " error=" << int (streamError)
             << " session=" << m_context.session_id.value ()
             << " generation=" << m_context.generation.value ()
             << " backend_start_sequence=" << m_backendStartSequence
             << " diagnostic=" << diagnostic.toStdString ());
}

bool SoundOutput::checkStream (bool startup)
{
  if (!m_stream) return false;
  return reportStreamError (m_stream->error (), startup);
}

bool SoundOutput::reportStreamError (QAudio::Error streamError, bool startup)
{
  if (streamError == QAudio::NoError) return true;
  // A backend may emit Stopped synchronously from start(). Report its final result once.
  if (m_startingSequence == m_backendStartSequence)
    {
      if (streamError != QAudio::UnderrunError) m_deferredStartupError = streamError;
      return false;
    }
  if (streamError == m_startupError)
    return false;

  QString message;
  switch (streamError)
    {
    case QAudio::OpenError:
      message = tr ("An error opening the audio output device has occurred.");
      break;

    case QAudio::IOError:
      message = tr ("An error occurred during write to the audio output device.");
      break;

    case QAudio::UnderrunError:
      message = tr ("Audio data not being fed to the audio output device fast enough.");
      break;

    case QAudio::FatalError:
      message = tr ("Non-recoverable error, audio output device not usable at this time.");
      break;

    case QAudio::NoError:
      return true;
    }
  if (startup && streamError != QAudio::UnderrunError) m_startupError = streamError;
  Q_EMIT error ({m_context, txBackendError (streamError), message});
  return false;
}

void SoundOutput::setFormat (QAudioDeviceInfo const& device, unsigned channels, int frames_buffered)
{
  Q_ASSERT (0 < channels && channels < 3);
  m_device = device;
  m_channels = channels;
  m_framesBuffered = frames_buffered;
}

void SoundOutput::restart (QIODevice * source, TxEvidence::TxPlaybackContext context,
                           qint64 period_offset_ms)
{
  Q_UNUSED (period_offset_ms);
  if (m_stream)
    {
      disconnect (m_stream.data (), nullptr, this, nullptr);
      m_stream.reset ();
    }
  m_context = context;
  auto const backendStartSequence = ++m_backendStartSequence;
  m_startupError = QAudio::NoError;
  m_deferredStartupError = QAudio::NoError;
  QString failure;
  QAudioFormat format;
  if (!m_device.isNull ())
    {
      format = m_device.preferredFormat ();
      //  qDebug () << "Preferred audio output format:" << format;
      format.setChannelCount (m_channels);
      format.setCodec ("audio/pcm");
      format.setSampleRate (48000);
      format.setSampleType (QAudioFormat::SignedInt);
      format.setSampleSize (16);
      format.setByteOrder (QAudioFormat::Endian (QSysInfo::ByteOrder));
      if (!format.isValid ())
        {
          failure = tr ("Requested output audio format is not valid.");
        }
      else if (!m_device.isFormatSupported (format))
        {
          failure = tr ("Requested output audio format is not supported on device.");
        }
      else
        {
          // qDebug () << "Selected audio output format:" << format;
          m_stream.reset (new QAudioOutput (m_device, format));
          if (m_stream->error () != QAudio::NoError)
            {
              auto const streamError = m_stream->error ();
              logStartupFailure ("construction", format, m_stream->state (), streamError);
              {
                QScopedValueRollback<qint64> starting {m_startingSequence, backendStartSequence};
                publishRawTxPlayoutSnapshot (true);
              }
              if (isCurrentStream (context, backendStartSequence)) reportStreamError (streamError, true);
              return;
            }
          m_stream->setVolume (m_volume);
          m_stream->setNotifyInterval(1000);
          connect (m_stream.data (), &QAudioOutput::stateChanged, this,
                   [this, context, backendStartSequence] (QAudio::State state) {
                     handleStateChanged (state, context, backendStartSequence);
                   });
          connect (m_stream.data(), &QAudioOutput::notify, this,
                   [this, context, backendStartSequence] () {
              if (!isCurrentStream (context, backendStartSequence)) return;
              if (m_startingSequence == backendStartSequence)
                {
                  checkStream ();
                  return;
                }
              publishRawTxPlayoutSnapshot ();
              if (isCurrentStream (context, backendStartSequence)) checkStream ();
            });

          //      qDebug() << "A" << m_volume << m_stream->notifyInterval();
        }
    }
  else
    {
      failure = tr ("No audio output device configured.");
    }
  if (!failure.isEmpty ())
    {
      logStartupFailure ("configuration", format, QAudio::StoppedState, QAudio::OpenError, failure);
      publishUnavailableTxPlayoutSnapshot (true, failure);
      if (context == m_context && backendStartSequence == m_backendStartSequence)
        Q_EMIT error ({context, TxEvidence::TxBackendError::Open, failure});
      return;
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
  {
    QScopedValueRollback<qint64> starting {m_startingSequence, backendStartSequence};
    m_stream->start (source);
  }
  if (!isCurrentStream (context, backendStartSequence)) return;
  auto const streamError = m_deferredStartupError != QAudio::NoError
    ? m_deferredStartupError : m_stream->error ();
  auto snapshot = makeRawTxPlayoutSnapshot (true);
  if (m_deferredStartupError != QAudio::NoError)
    {
      snapshot.state = TxEvidence::TxBackendState::Stopped;
      snapshot.error = txBackendError (streamError);
      snapshot.tier = TxEvidence::TxPlayoutTier::Unavailable;
      snapshot.available = false;
    }
  if (streamError != QAudio::NoError && streamError != QAudio::UnderrunError)
    logStartupFailure ("start", format,
                       m_deferredStartupError != QAudio::NoError ? QAudio::StoppedState : m_stream->state (),
                       streamError);
  {
    QScopedValueRollback<qint64> starting {m_startingSequence, backendStartSequence};
    Q_EMIT rawTxPlayoutSnapshot (snapshot);
  }
  if (isCurrentStream (context, backendStartSequence)) reportStreamError (streamError, true);
//  LOG_DEBUG ("Selected buffer size (bytes): " << m_stream->bufferSize () << " period size: " << m_stream->periodSize ());
}

void SoundOutput::suspend ()
{
  if (m_stream && QAudio::ActiveState == m_stream->state ())
    {
      auto const context = m_context;
      auto const sequence = m_backendStartSequence;
      m_stream->suspend ();
      if (!isCurrentStream (context, sequence)) return;
      checkStream ();
      if (isCurrentStream (context, sequence)) publishRawTxPlayoutSnapshot ();
    }
}

void SoundOutput::resume ()
{
  if (m_stream && QAudio::SuspendedState == m_stream->state ())
    {
      auto const context = m_context;
      auto const sequence = m_backendStartSequence;
      m_stream->resume ();
      if (!isCurrentStream (context, sequence)) return;
      checkStream ();
      if (isCurrentStream (context, sequence)) publishRawTxPlayoutSnapshot ();
    }
}

void SoundOutput::reset ()
{
  if (m_stream)
    {
      auto const context = m_context;
      auto const sequence = m_backendStartSequence;
      auto const beforeReset = makeRawTxPlayoutSnapshot ();
      m_stream->reset ();
      if (!isCurrentStream (context, sequence)) return;
      checkStream ();
      if (!isCurrentStream (context, sequence)) return;
      Q_EMIT rawTxPlayoutSnapshot (beforeReset);
      if (isCurrentStream (context, sequence)) publishRawTxPlayoutSnapshot ();
    }
  else
    {
      publishRawTxPlayoutSnapshot ();
    }
}

void SoundOutput::stop ()
{
  auto const context = m_context;
  auto const sequence = m_backendStartSequence;
  if (m_stream)
    {
      auto const beforeStop = makeRawTxPlayoutSnapshot ();
      m_stream->reset ();
      if (!isCurrentStream (context, sequence)) return;
      m_stream->stop ();
      if (!isCurrentStream (context, sequence)) return;
      Q_EMIT rawTxPlayoutSnapshot (beforeStop);
      if (!isCurrentStream (context, sequence)) return;
      publishRawTxPlayoutSnapshot ();
    }
  else
    {
      publishRawTxPlayoutSnapshot ();
    }
#ifdef __APPLE__
  // this code is here to help certain rigs not drop audio, however on Sequoia this causes audio to drop, so we don't do it!
  if (context == m_context && sequence == m_backendStartSequence
      && QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 15))
    m_stream.reset ();
#endif
}

qreal SoundOutput::attenuation () const
{
  return -(20. * qLn (m_volume) / qLn (10.));
}

int SoundOutput::bufferSize () const
{
  return m_stream && playbackMetricsAvailable (m_stream->state (), m_stream->error ())
    ? m_stream->bufferSize () : 0;
}

TxEvidence::TxRawPlayoutSnapshot SoundOutput::makeRawTxPlayoutSnapshot (bool startEvent) const
{
  TxEvidence::TxRawPlayoutSnapshot snapshot;
  snapshot.context = m_context;
  snapshot.backend_start_sequence = m_backendStartSequence;
  snapshot.start_event = startEvent;
  if (!m_stream) return snapshot;

  QAudioFormat const format {m_stream->format ()};
  auto const state = m_stream->state ();
  auto const streamError = m_stream->error ();
  snapshot.state = txBackendState (state);
  snapshot.error = txBackendError (streamError);
  snapshot.channel_count = format.channelCount ();
  snapshot.bytes_per_frame = format.bytesPerFrame ();
  snapshot.sample_rate_hz = format.sampleRate ();
  snapshot.report_interval_ms = m_stream->notifyInterval ();
  if (!playbackMetricsAvailable (state, streamError))
    {
      snapshot.diagnostic = QStringLiteral ("Audio output playback metrics are unavailable in the current state.");
      return snapshot;
    }
  qint64 const capacity {m_stream->bufferSize ()};
  qint64 const available {m_stream->bytesFree ()};
  snapshot.tier = TxEvidence::TxPlayoutTier::DeviceClock;
  snapshot.available = true;
  snapshot.available_bytes = available;
  snapshot.buffered_bytes = capacity >= 0 && available >= 0 ? qMax<qint64> (0, capacity - available) : -1;
  snapshot.capacity_bytes = capacity;
  snapshot.processed_usecs = m_stream->processedUSecs ();
  snapshot.elapsed_usecs = m_stream->elapsedUSecs ();
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
  snapshot.context = m_context;
  snapshot.backend_start_sequence = m_backendStartSequence;
  snapshot.start_event = startEvent;
  snapshot.state = TxEvidence::TxBackendState::Unavailable;
  snapshot.error = startEvent ? TxEvidence::TxBackendError::Open : TxEvidence::TxBackendError::None;
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

void SoundOutput::handleStateChanged (QAudio::State newState,
                                      TxEvidence::TxPlaybackContext context,
                                      qint64 backendStartSequence)
{
  if (!isCurrentStream (context, backendStartSequence)) return;
  switch (newState)
    {
    case QAudio::IdleState:
      Q_EMIT status (tr ("Idle"));
      if (!isCurrentStream (context, backendStartSequence)) return;
      Q_EMIT audioOutputIdle ();
      break;

    case QAudio::ActiveState:
      Q_EMIT status (tr ("Sending"));
      if (!isCurrentStream (context, backendStartSequence)) return;
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
      {
        auto const streamOk = checkStream ();
        if (!isCurrentStream (context, backendStartSequence)) return;
        if (!streamOk)
          {
            Q_EMIT status (tr ("Error"));
          }
        else
          {
            Q_EMIT status (tr ("Stopped"));
          }
        break;
      }
    }
  if (isCurrentStream (context, backendStartSequence)) publishRawTxPlayoutSnapshot ();
}
