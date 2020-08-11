#include "soundin.h"

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioInput>
#include <QSysInfo>
#include <QDebug>

#include "moc_soundin.cpp"

bool SoundInput::audioError () const
{
  bool result (true);

  Q_ASSERT_X (m_stream, "SoundInput", "programming error");
  if (m_stream)
    {
      switch (m_stream->error ())
        {
        case QAudio::OpenError:
          Q_EMIT error (tr ("An error opening the audio input device has occurred."));
          break;

        case QAudio::IOError:
          Q_EMIT error (tr ("An error occurred during read from the audio input device."));
          break;

        case QAudio::UnderrunError:
          Q_EMIT error (tr ("Audio data not being fed to the audio input device fast enough."));
          break;

        case QAudio::FatalError:
          Q_EMIT error (tr ("Non-recoverable error, audio input device not usable at this time."));
          break;

        case QAudio::NoError:
          result = false;
          break;
        }
    }
  return result;
}

void SoundInput::start(QAudioDeviceInfo const& device, int framesPerBuffer, AudioDevice * sink
                       , unsigned downSampleFactor, AudioDevice::Channel channel)
{
  Q_ASSERT (sink);

  stop ();

  m_sink = sink;

  QAudioFormat format (device.preferredFormat());
//  qDebug () << "Preferred audio input format:" << format;
  format.setChannelCount (AudioDevice::Mono == channel ? 1 : 2);
  format.setCodec ("audio/pcm");
  format.setSampleRate (12000 * downSampleFactor);
  format.setSampleType (QAudioFormat::SignedInt);
  format.setSampleSize (16);
  format.setByteOrder (QAudioFormat::Endian (QSysInfo::ByteOrder));
  if (!format.isValid ())
    {
      Q_EMIT error (tr ("Requested input audio format is not valid."));
      return;
    }
  else if (!device.isFormatSupported (format))
    {
//      qDebug () << "Nearest supported audio format:" << device.nearestFormat (format);
      Q_EMIT error (tr ("Requested input audio format is not supported on device."));
      return;
    }
  // qDebug () << "Selected audio input format:" << format;

  m_stream.reset (new QAudioInput {device, format});
  if (audioError ())
    {
      return;
    }

  connect (m_stream.data(), &QAudioInput::stateChanged, this, &SoundInput::handleStateChanged);

  //qDebug () << "SoundIn default buffer size (bytes):" << m_stream->bufferSize () << "period size:" << m_stream->periodSize ();
  // the Windows MME version of QAudioInput uses 1/5 of the buffer
  // size for period size other platforms seem to optimize themselves
#if defined (Q_OS_WIN)
  m_stream->setBufferSize (m_stream->format ().bytesForFrames (framesPerBuffer * 5));
#else
  Q_UNUSED (framesPerBuffer);
#endif
  if (sink->initialize (QIODevice::WriteOnly, channel))
    {
      m_stream->start (sink);
      audioError ();
      cummulative_lost_usec_ = -1;
      //qDebug () << "SoundIn selected buffer size (bytes):" << m_stream->bufferSize () << "peirod size:" << m_stream->periodSize ();
    }
  else
    {
      Q_EMIT error (tr ("Failed to initialize audio sink device"));
    }
}

void SoundInput::suspend ()
{
  if (m_stream)
    {
      m_stream->suspend ();
      audioError ();
    }
}

void SoundInput::resume ()
{
//  qDebug() << "Resume" << fmod(0.001*QDateTime::currentMSecsSinceEpoch(),6.0);
  if (m_sink)
    {
      m_sink->reset ();
    }

  if (m_stream)
    {
      m_stream->resume ();
      audioError ();
    }
}

void SoundInput::handleStateChanged (QAudio::State newState)
{
  //qDebug () << "SoundInput::handleStateChanged: newState:" << newState;

  switch (newState)
    {
    case QAudio::IdleState:
      Q_EMIT status (tr ("Idle"));
      break;

    case QAudio::ActiveState:
      reset (false);
      Q_EMIT status (tr ("Receiving"));
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

void SoundInput::reset (bool report_dropped_frames)
{
  if (m_stream)
    {
      if (cummulative_lost_usec_ >= 0 // don't report first time as we
                                      // don't yet known latency
          && report_dropped_frames)
        {
          auto lost_usec = m_stream->elapsedUSecs () - m_stream->processedUSecs () - cummulative_lost_usec_;
          Q_EMIT dropped_frames (m_stream->format ().framesForDuration (lost_usec), lost_usec);
          //qDebug () << "SoundInput::reset: frames dropped:" << m_stream->format ().framesForDuration (lost_usec) << "sec:" << lost_usec / 1.e6;
        }
      cummulative_lost_usec_ = m_stream->elapsedUSecs () - m_stream->processedUSecs ();
    }
}

void SoundInput::stop()
{
  if (m_stream)
    {
      m_stream->stop ();
    }
  m_stream.reset ();

  if (m_sink)
    {
      m_sink->close ();
    }
}

SoundInput::~SoundInput ()
{
  stop ();
}
