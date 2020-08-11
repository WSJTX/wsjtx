#include "soundout.h"

#include <QDateTime>
#include <QAudioDeviceInfo>
#include <QAudioOutput>
#include <QSysInfo>
#include <qmath.h>
#include <QDebug>

#include "moc_soundout.cpp"

bool SoundOutput::audioError () const
{
  bool result (true);

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
        result = false;
        break;
      }
  }
  return result;
}

void SoundOutput::setFormat (QAudioDeviceInfo const& device, unsigned channels, int frames_buffered)
{
  Q_ASSERT (0 < channels && channels < 3);

  m_framesBuffered = frames_buffered;

  QAudioFormat format (device.preferredFormat ());
//  qDebug () << "Preferred audio output format:" << format;
  format.setChannelCount (channels);
  format.setCodec ("audio/pcm");
  format.setSampleRate (48000);
  format.setSampleType (QAudioFormat::SignedInt);
  format.setSampleSize (16);
  format.setByteOrder (QAudioFormat::Endian (QSysInfo::ByteOrder));
  if (!format.isValid ())
    {
      Q_EMIT error (tr ("Requested output audio format is not valid."));
    }
  else if (!device.isFormatSupported (format))
    {
      Q_EMIT error (tr ("Requested output audio format is not supported on device."));
    }
  qDebug () << "Selected audio output format:" << format;

  m_stream.reset (new QAudioOutput (device, format));
  audioError ();
  m_stream->setVolume (m_volume);
  m_stream->setNotifyInterval(100);

  connect (m_stream.data(), &QAudioOutput::stateChanged, this, &SoundOutput::handleStateChanged);

  //      qDebug() << "A" << m_volume << m_stream->notifyInterval();
}

void SoundOutput::restart (QIODevice * source)
{
  Q_ASSERT (m_stream);

  // we have to set this before every start on the stream because the
  // Windows implementation seems to forget the buffer size after a
  // stop.
  //qDebug () << "SoundOut default buffer size (bytes):" << m_stream->bufferSize () << "period size:" << m_stream->periodSize ();
  if (m_framesBuffered)
    {
#if defined (Q_OS_WIN)
      m_stream->setBufferSize (m_stream->format().bytesForFrames (m_framesBuffered));
#endif
    }
  m_stream->setCategory ("production");
  m_stream->start (source);
  //qDebug () << "SoundOut selected buffer size (bytes):" << m_stream->bufferSize () << "period size:" << m_stream->periodSize ();
}

void SoundOutput::suspend ()
{
  if (m_stream && QAudio::ActiveState == m_stream->state ())
    {
      m_stream->suspend ();
      audioError ();
    }
}

void SoundOutput::resume ()
{
  if (m_stream && QAudio::SuspendedState == m_stream->state ())
    {
      m_stream->resume ();
      audioError ();
    }
}

void SoundOutput::reset ()
{
  if (m_stream)
    {
      m_stream->reset ();
      audioError ();
    }
}

void SoundOutput::stop ()
{
  if (m_stream)
    {
      m_stream->stop ();
      audioError ();
    }
}

qreal SoundOutput::attenuation () const
{
  return -(20. * qLn (m_volume) / qLn (10.));
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
  // qDebug () << "SoundOutput::handleStateChanged: newState:" << newState;

  switch (newState)
    {
    case QAudio::IdleState:
      Q_EMIT status (tr ("Idle"));
      break;

    case QAudio::ActiveState:
      Q_EMIT status (tr ("Sending"));
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
      if (audioError ())
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
