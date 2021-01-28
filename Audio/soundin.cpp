#include "soundin.h"

#include <cstdlib>
#include <cmath>
#include <iomanip>
#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioInput>
#include <QSysInfo>
#include <QDebug>

#include "Logger.hpp"

#include "moc_soundin.cpp"

bool SoundInput::checkStream ()
{
  bool result (false);
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

        // case QAudio::UnderrunError:
        //   Q_EMIT error (tr ("Audio data not being fed to the audio input device fast enough."));
        //   break;

        case QAudio::FatalError:
          Q_EMIT error (tr ("Non-recoverable error, audio input device not usable at this time."));
          break;

        case QAudio::UnderrunError: // TODO G4WJS: stop ignoring this
                                    // when we find the cause on macOS
        case QAudio::NoError:
          result = true;
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
  if (!checkStream ())
    {
      return;
    }

  connect (m_stream.data(), &QAudioInput::stateChanged, this, &SoundInput::handleStateChanged);
  connect (m_stream.data(), &QAudioInput::notify, [this] () {checkStream ();});

  //qDebug () << "SoundIn default buffer size (bytes):" << m_stream->bufferSize () << "period size:" << m_stream->periodSize ();
  // the Windows MME version of QAudioInput uses 1/5 of the buffer
  // size for period size other platforms seem to optimize themselves
  if (framesPerBuffer > 0)
    {
      m_stream->setBufferSize (m_stream->format ().bytesForFrames (framesPerBuffer));
    }
  if (m_sink->initialize (QIODevice::WriteOnly, channel))
    {
      m_stream->start (sink);
      checkStream ();
      cummulative_lost_usec_ = -1;
      LOG_DEBUG ("Selected buffer size (bytes): " << m_stream->bufferSize () << " period size: " << m_stream->periodSize ());
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
      checkStream ();
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
      checkStream ();
    }
}

void SoundInput::handleStateChanged (QAudio::State newState)
{
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
}

void SoundInput::reset (bool report_dropped_frames)
{
  if (m_stream)
    {
      auto elapsed_usecs = m_stream->elapsedUSecs ();
      while (std::abs (elapsed_usecs - m_stream->processedUSecs ())
             > 24 * 60 * 60 * 500000ll) // half day
        {
          // QAudioInput::elapsedUSecs() wraps after 24 hours
          elapsed_usecs += 24 * 60 * 60 * 1000000ll;
        }
      // don't report first time as we don't yet known latency
      if (cummulative_lost_usec_ != std::numeric_limits<qint64>::min () && report_dropped_frames)
        {
          auto lost_usec = elapsed_usecs - m_stream->processedUSecs () - cummulative_lost_usec_;
          if (std::abs (lost_usec) > 48000 / 5)
            {
              LOG_WARN ("Detected dropped audio source samples: "
                        << m_stream->format ().framesForDuration (lost_usec)
                        << " (" << std::setprecision (4) << lost_usec / 1.e6 << " S)");
            }
          else if (std::abs (lost_usec) > 5 * 48000)
            {
              LOG_ERROR ("Detected excessive dropped audio source samples: "
                        << m_stream->format ().framesForDuration (lost_usec)
                         << " (" << std::setprecision (4) << lost_usec / 1.e6 << " S)");
            }
        }
      cummulative_lost_usec_ = elapsed_usecs - m_stream->processedUSecs ();
    }
}

void SoundInput::stop()
{
  if (m_stream)
    {
      m_stream->stop ();
    }
  m_stream.reset ();
}

SoundInput::~SoundInput ()
{
  stop ();
}
