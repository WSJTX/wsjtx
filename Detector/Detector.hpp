#ifndef DETECTOR_HPP__
#define DETECTOR_HPP__
#include "Audio/AudioDevice.hpp"
#include "Audio/AudioStreamClock.hpp"
#include "ReceiveAudio.hpp"
#include <QScopedArrayPointer>
#include <array>

//
// output device that distributes data in predefined chunks via a signal
//
// the underlying device for this abstraction is just the buffer that
// stores samples throughout a receiving period
//
class Detector : public AudioDevice
{
  Q_OBJECT;

public:
  //
  // if the data buffer were not global storage and fixed size then we
  // might want maximum size passed as constructor arguments
  //
  // we down sample by a factor of 4
  //
  // the samplesPerFFT argument is the number after down sampling
  //
  Detector (unsigned frameRate, double periodLengthInSeconds, unsigned downSampleFactor = 4u,
            QObject * parent = 0);

  void setTRPeriod (double period);
  bool reset () override;

  Q_SIGNAL void framesWritten (qint64) const;
  Q_SIGNAL void audioBlock (ReceiveAudio) const;
  Q_SLOT void setBlockSize (unsigned);
  Q_SLOT void flushBufferedFrames (qint64 frameLimit);
  Q_SLOT void setStreamDescriptor (AudioStreamDescriptor);

protected:
  qint64 readData (char * /* data */, qint64 /* maxSize */) override
  {
    return -1;			// we don't produce data
  }

  qint64 writeData (char const * data, qint64 maxSize) override;

private:
  void clear ();		// discard buffer contents
  void resetPeriodBuffer ();

  unsigned m_frameRate;
  double   m_period;
  unsigned m_downSampleFactor;
  qint32 m_samplesPerFFT;	// after any down sampling
  AudioStreamClock m_stream_clock;
  ReceiveAudioProducer m_receiveAudioProducer;
  std::array<float, 49> m_downsampleState {};
  qint64 m_last_period_offset_ms {-1};
  static size_t const max_buffer_size {7 * 512};
  QScopedArrayPointer<short> m_buffer; // de-interleaved sample buffer
  // big enough for all the
  // samples for one increment of
  // data (a signals worth) at
  // the input sample rate
  unsigned m_bufferPos;
};

#endif
