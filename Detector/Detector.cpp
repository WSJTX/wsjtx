#include "Detector.hpp"
#include <algorithm>
#include <QDateTime>
#include <QtAlgorithms>
#include <QDebug>
#include <QMutexLocker>
#include <QVector>
#include <cmath>
#include <cstddef>
#include "commons.h"
#include "DecDataMutex.hpp"

#include "moc_Detector.cpp"

extern "C" {
  void   fil4_(qint16*, qint32*, qint16*, qint32*);
}

extern dec_data_t& dec_data;

Detector::Detector (unsigned frameRate, double periodLengthInSeconds,
                    unsigned downSampleFactor, QObject * parent)
  : AudioDevice (parent)
  , m_frameRate (frameRate)
  , m_period (periodLengthInSeconds)
  , m_downSampleFactor (downSampleFactor)
  , m_samplesPerFFT {max_buffer_size}
  , m_buffer ((downSampleFactor > 1) ?
              new short [max_buffer_size * downSampleFactor] : nullptr)
  , m_bufferPos (0)
{
  (void)m_frameRate;            // quell compiler warning
  clear ();
}

void Detector::setBlockSize (unsigned n)
{
  m_samplesPerFFT = n;
}

void Detector::setStreamDescriptor (AudioStreamDescriptor descriptor)
{
  m_stream_clock.setDescriptor (descriptor);
  m_last_period_offset_ms = -1;
  clear ();
}

void Detector::flushBufferedFrames (qint64 frameLimit)
{
  qint64 framesWritten {0};
  {
    QMutexLocker lock {&dec_data_mutex ()};
    if (m_downSampleFactor <= 1 || !m_bufferPos
        || dec_data_input_blocked () || dec_data.params.kin >= frameLimit)
      {
        return;
      }

    auto const blockFrames = m_samplesPerFFT * m_downSampleFactor;
    std::fill (m_buffer.data () + m_bufferPos,
               m_buffer.data () + blockFrames, 0);
    qint32 framesToProcess = blockFrames;
    qint32 framesAfterDownSample = m_samplesPerFFT;
    fil4_ (m_buffer.data (), &framesToProcess,
           &dec_data.d2[dec_data.params.kin], &framesAfterDownSample);
    dec_data.params.kin = std::min<qint64> (
      frameLimit, dec_data.params.kin + framesAfterDownSample);
    framesWritten = dec_data.params.kin;
    m_bufferPos = 0;
  }
  Q_EMIT this->framesWritten (framesWritten);
}

bool Detector::reset ()
{
  clear ();
  // don't call base class reset because it calls seek(0) which causes
  // a warning
  return isOpen ();
}

void Detector::clear ()
{
  QMutexLocker lock {&dec_data_mutex ()};
  m_bufferPos = 0;
  if (dec_data_input_blocked ()) return;

  resetPeriodBuffer ();
}

void Detector::resetPeriodBuffer ()
{
  auto const capacity = sizeof dec_data.d2 / sizeof dec_data.d2[0];
  auto const periodFrames = static_cast<std::size_t> (
      std::ceil (m_period * RX_SAMPLE_RATE));
  std::fill_n (dec_data.d2, std::min (capacity, periodFrames), qint16 {0});
  dec_data.params.kin = 0;
  m_bufferPos = 0;
}

qint64 Detector::writeData (char const * data, qint64 maxSize)
{
  auto const bytes_per_frame = static_cast<qint64> (bytesPerFrame ());
  Q_ASSERT (!(maxSize % bytes_per_frame));
  qint64 const frames_received = maxSize / bytes_per_frame;
  QVector<qint64> frame_counts;
  qint64 const now_ms = m_stream_clock.timestamp (
    QDateTime::currentMSecsSinceEpoch ());
  m_stream_clock.advance (frames_received);
  qint64 const period_ms = static_cast<qint64> (1000.0 * m_period);
  qint64 const day_ms = now_ms % 86400000;
  qint64 const mstr = day_ms % period_ms; // ms into the nominal Tx start time

  if (dec_data_input_blocked ()) return maxSize;

  {
    QMutexLocker lock {&dec_data_mutex ()};
    if (dec_data_input_blocked ()) return maxSize;
    if(m_last_period_offset_ms >= 0 && mstr < m_last_period_offset_ms) {
      resetPeriodBuffer ();
    }
    m_last_period_offset_ms = mstr;

    // these are in terms of input frames (not down sampled)
    size_t framesAcceptable ((sizeof (dec_data.d2) /
                              sizeof (dec_data.d2[0]) - dec_data.params.kin) * m_downSampleFactor);
    size_t framesAccepted (qMin (static_cast<size_t> (frames_received), framesAcceptable));

    if (framesAccepted < static_cast<size_t> (frames_received)) {
      auto const frames_dropped = frames_received
        - static_cast<qint64> (framesAccepted);
      qDebug () << "dropped " << frames_dropped
                  << " frames of data on the floor!"
                  << dec_data.params.kin << mstr;
    }

    for (unsigned remaining = framesAccepted; remaining; ) {
      size_t numFramesProcessed (qMin (m_samplesPerFFT *
                                       m_downSampleFactor - m_bufferPos, remaining));

      if(m_downSampleFactor > 1) {
        store (&data[(framesAccepted - remaining) * bytesPerFrame ()],
               numFramesProcessed, &m_buffer[m_bufferPos]);
        m_bufferPos += numFramesProcessed;

        if(m_bufferPos==m_samplesPerFFT*m_downSampleFactor) {
          qint32 framesToProcess (m_samplesPerFFT * m_downSampleFactor);
          qint32 framesAfterDownSample (m_samplesPerFFT);
          if(m_downSampleFactor > 1 && dec_data.params.kin>=0 &&
             dec_data.params.kin < (NTMAX*12000 - framesAfterDownSample)) {
            fil4_(&m_buffer[0], &framesToProcess, &dec_data.d2[dec_data.params.kin],
                &framesAfterDownSample);
            dec_data.params.kin += framesAfterDownSample;
          } else {
            // qDebug() << "framesToProcess     = " << framesToProcess;
            // qDebug() << "dec_data.params.kin = " << dec_data.params.kin;
            // qDebug() << "secondInPeriod      = " << secondInPeriod();
            // qDebug() << "framesAfterDownSample" << framesAfterDownSample;
          }
          frame_counts << dec_data.params.kin;
          m_bufferPos = 0;
        }

      } else {
        store (&data[(framesAccepted - remaining) * bytesPerFrame ()],
               numFramesProcessed, &dec_data.d2[dec_data.params.kin]);
        m_bufferPos += numFramesProcessed;
        dec_data.params.kin += numFramesProcessed;
        if (m_bufferPos == static_cast<unsigned> (m_samplesPerFFT)) {
          frame_counts << dec_data.params.kin;
          m_bufferPos = 0;
        }
      }
      remaining -= numFramesProcessed;
    }
  }

  for (auto frames : frame_counts) {
    Q_EMIT framesWritten (frames);
  }

    // we drop any data past the end of the buffer on the floor until
    // the next period starts
    return maxSize;
}
