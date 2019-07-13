#include "Modulator.hpp"
#include <limits>
#include <qmath.h>
#include <QDateTime>
#include <QDebug>
#include "widgets/mainwindow.h" // TODO: G4WJS - break this dependency
#include "soundout.h"
#include "commons.h"

#include "moc_Modulator.cpp"

extern float gran();		// Noise generator (for tests only)

#define RAMP_INCREMENT 64  // MUST be an integral factor of 2^16

#if defined (WSJT_SOFT_KEYING)
# define SOFT_KEYING WSJT_SOFT_KEYING
#else
# define SOFT_KEYING 1
#endif

double constexpr Modulator::m_twoPi;

//    float wpm=20.0;
//    unsigned m_nspd=1.2*48000.0/wpm;
//    m_nspd=3072;                           //18.75 WPM

Modulator::Modulator (unsigned frameRate, double periodLengthInSeconds,
                      QObject * parent)
  : AudioDevice {parent}
  , m_quickClose {false}
  , m_phi {0.0}
  , m_toneSpacing {0.0}
  , m_fSpread {0.0}
  , m_period {periodLengthInSeconds}
  , m_frameRate {frameRate}
  , m_state {Idle}
  , m_tuning {false}
  , m_cwLevel {false}
  , m_j0 {-1}
  , m_toneFrequency0 {1500.0}
{
}

void Modulator::start (unsigned symbolsLength, double framesPerSymbol,
                       double frequency, double toneSpacing,
                       SoundOutput * stream, Channel channel,
                       bool synchronize, bool fastMode, double dBSNR, double TRperiod)
{
  Q_ASSERT (stream);
// Time according to this computer which becomes our base time
  qint64 ms0 = QDateTime::currentMSecsSinceEpoch() % 86400000;
  unsigned mstr = ms0 % int(1000.0*m_period); // ms into the nominal Tx start time

  if(m_state != Idle) stop();
  m_quickClose = false;
  m_symbolsLength = symbolsLength;
  m_isym0 = std::numeric_limits<unsigned>::max (); // big number
  m_frequency0 = 0.;
  m_phi = 0.;
  m_addNoise = dBSNR < 0.;
  m_nsps = framesPerSymbol;
  m_frequency = frequency;
  m_amp = std::numeric_limits<qint16>::max ();
  m_toneSpacing = toneSpacing;
  m_bFastMode=fastMode;
  m_TRperiod=TRperiod;
  unsigned delay_ms=1000;
  if(m_nsps==1920) delay_ms=500;   //FT8
  if(m_nsps==576)  delay_ms=300;   //FT4

// noise generator parameters
  if (m_addNoise) {
    m_snr = qPow (10.0, 0.05 * (dBSNR - 6.0));
    m_fac = 3000.0;
    if (m_snr > 1.0) m_fac = 3000.0 / m_snr;
  }

// round up to an exact portion of a second that allows for startup delays
  m_ic = (mstr / delay_ms) * m_frameRate * delay_ms / 1000;

  if(m_bFastMode) m_ic=0;

  m_silentFrames = 0;
// calculate number of silent frames to send, so that audio will start at
// the nominal time "delay_ms" into the Tx sequence.
  if (synchronize && !m_tuning && !m_bFastMode)	{
    m_silentFrames = m_ic + m_frameRate / (1000 / delay_ms) - (mstr * (m_frameRate / 1000));
  }

//  qDebug() << "aa" << QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz")
//           << m_ic << m_silentFrames << m_silentFrames/48000.0
//           << mstr << fmod(double(ms0),1000.0*m_period);


  initialize (QIODevice::ReadOnly, channel);
  Q_EMIT stateChanged ((m_state = (synchronize && m_silentFrames) ?
                        Synchronizing : Active));
  m_stream = stream;
  if (m_stream) m_stream->restart (this);
}

void Modulator::tune (bool newState)
{
  m_tuning = newState;
  if (!m_tuning) stop (true);
}

void Modulator::stop (bool quick)
{
  m_quickClose = quick;
  close ();
}

void Modulator::close ()
{
  if (m_stream)
    {
      if (m_quickClose)
        {
          m_stream->reset ();
        }
      else
        {
          m_stream->stop ();
        }
    }
  if (m_state != Idle)
    {
      Q_EMIT stateChanged ((m_state = Idle));
    }
  AudioDevice::close ();
}

qint64 Modulator::readData (char * data, qint64 maxSize)
{
  double toneFrequency=1500.0;
  if(m_nsps==6) {
    toneFrequency=1000.0;
    m_frequency=1000.0;
    m_frequency0=1000.0;
  }
  if(maxSize==0) return 0;
  Q_ASSERT (!(maxSize % qint64 (bytesPerFrame ()))); // no torn frames
  Q_ASSERT (isOpen ());

  qint64 numFrames (maxSize / bytesPerFrame ());
  qint16 * samples (reinterpret_cast<qint16 *> (data));
  qint16 * end (samples + numFrames * (bytesPerFrame () / sizeof (qint16)));
  qint64 framesGenerated (0);

//  if(m_ic==0) qDebug() << "Modulator::readData" << 0.001*(QDateTime::currentMSecsSinceEpoch() % (1000*m_TRperiod));

  switch (m_state)
    {
    case Synchronizing:
      {
        if (m_silentFrames)	{  // send silence up to first second
          framesGenerated = qMin (m_silentFrames, numFrames);
          for ( ; samples != end; samples = load (0, samples)) { // silence
          }
          m_silentFrames -= framesGenerated;
          return framesGenerated * bytesPerFrame ();
        }

        Q_EMIT stateChanged ((m_state = Active));
        m_cwLevel = false;
        m_ramp = 0;		// prepare for CW wave shaping
      }
      // fall through

    case Active:
      {
        unsigned int isym=0;

        if(!m_tuning) isym=m_ic/(4.0*m_nsps);            // Actual fsample=48000
        bool slowCwId=((isym >= m_symbolsLength) && (icw[0] > 0)) && (!m_bFastMode);
        if(m_TRperiod==3.0) slowCwId=false;
        bool fastCwId=false;
        static bool bCwId=false;
        qint64 ms = QDateTime::currentMSecsSinceEpoch();
        float tsec=0.001*(ms % int(1000*m_TRperiod));
        if(m_bFastMode and (icw[0]>0) and (tsec > (m_TRperiod-5.0))) fastCwId=true;
        if(!m_bFastMode) m_nspd=2560;                 // 22.5 WPM

//        qDebug() << "Mod A" << m_ic << isym << tsec;

        if(slowCwId or fastCwId) {     // Transmit CW ID?
          m_dphi = m_twoPi*m_frequency/m_frameRate;
          if(m_bFastMode and !bCwId) {
            m_frequency=1500;          // Set params for CW ID
            m_dphi = m_twoPi*m_frequency/m_frameRate;
            m_symbolsLength=126;
            m_nsps=4096.0*12000.0/11025.0;
            m_ic=2246949;
            m_nspd=2560;               // 22.5 WPM
            if(icw[0]*m_nspd/48000.0 > 4.0) m_nspd=4.0*48000.0/icw[0];  //Faster CW for long calls
          }
          bCwId=true;
          unsigned ic0 = m_symbolsLength * 4 * m_nsps;
          unsigned j(0);

          while (samples != end) {
            j = (m_ic - ic0)/m_nspd + 1; // symbol of this sample
            bool level {bool (icw[j])};
            m_phi += m_dphi;
            if (m_phi > m_twoPi) m_phi -= m_twoPi;
            qint16 sample=0;
            float amp=32767.0;
            float x=0;
            if(m_ramp!=0) {
              x=qSin(float(m_phi));
              if(SOFT_KEYING) {
                amp=qAbs(qint32(m_ramp));
                if(amp>32767.0) amp=32767.0;
              }
              sample=round(amp*x);
            }
            if(m_bFastMode) {
              sample=0;
              if(level) sample=32767.0*x;
            }
            if (int (j) <= icw[0] && j < NUM_CW_SYMBOLS) { // stop condition
              samples = load (postProcessSample (sample), samples);
              ++framesGenerated;
              ++m_ic;
            } else {
              Q_EMIT stateChanged ((m_state = Idle));
              return framesGenerated * bytesPerFrame ();
            }

            // adjust ramp
            if ((m_ramp != 0 && m_ramp != std::numeric_limits<qint16>::min ()) || level != m_cwLevel) {
              // either ramp has terminated at max/min or direction has changed
              m_ramp += RAMP_INCREMENT; // ramp
            }
            m_cwLevel = level;
          }
          return framesGenerated * bytesPerFrame ();
        } else {
          bCwId=false;
        } //End of code for CW ID

        double const baud (12000.0 / m_nsps);
        // fade out parameters (no fade out for tuning)
        unsigned int i0,i1;
        if(m_tuning) {
          i1 = i0 = (m_bFastMode ? 999999 : 9999) * m_nsps;
        } else {
          i0=(m_symbolsLength - 0.017) * 4.0 * m_nsps;
          i1= m_symbolsLength * 4.0 * m_nsps;
        }
        if(m_bFastMode and !m_tuning) {
          i1=m_TRperiod*48000.0 - 24000.0;
          i0=i1-816;
        }

        qint16 sample;

        for (unsigned i = 0; i < numFrames && m_ic <= i1; ++i) {
          isym=0;
          if(!m_tuning and m_TRperiod!=3.0) isym=m_ic/(4.0*m_nsps);   //Actual fsample=48000
          if(m_bFastMode) isym=isym%m_symbolsLength;
          if (isym != m_isym0 || m_frequency != m_frequency0) {
            if(itone[0]>=100) {
              m_toneFrequency0=itone[0];
            } else {
              if(m_toneSpacing==0.0) {
                m_toneFrequency0=m_frequency + itone[isym]*baud;
              } else {
                m_toneFrequency0=m_frequency + itone[isym]*m_toneSpacing;
              }
            }
            m_dphi = m_twoPi * m_toneFrequency0 / m_frameRate;
            m_isym0 = isym;
            m_frequency0 = m_frequency;         //???
          }

          int j=m_ic/480;
          if(m_fSpread>0.0 and j!=m_j0) {
            float x1=(float)qrand()/RAND_MAX;
            float x2=(float)qrand()/RAND_MAX;
            toneFrequency = m_toneFrequency0 + 0.5*m_fSpread*(x1+x2-1.0);
            m_dphi = m_twoPi * toneFrequency / m_frameRate;
            m_j0=j;
          }

          m_phi += m_dphi;
          if (m_phi > m_twoPi) m_phi -= m_twoPi;
          if (m_ic > i0) m_amp = 0.98 * m_amp;
          if (m_ic > i1) m_amp = 0.0;

          sample=qRound(m_amp*qSin(m_phi));

//Here's where we transmit from a precomputed wave[] array:
          if(!m_tuning and (m_toneSpacing < 0)) {
            m_amp=32767.0;
            sample=qRound(m_amp*foxcom_.wave[m_ic]);
          }

          samples = load(postProcessSample(sample), samples);
          ++framesGenerated;
          ++m_ic;
        }

        if (m_amp == 0.0) { // TODO G4WJS: compare double with zero might not be wise
          if (icw[0] == 0) {
            // no CW ID to send
            Q_EMIT stateChanged ((m_state = Idle));
            return framesGenerated * bytesPerFrame ();
          }
          m_phi = 0.0;
        }

        m_frequency0 = m_frequency;
// done for this chunk - continue on next call

//        qDebug() << "Mod B" << m_ic << i1 << 0.001*(QDateTime::currentMSecsSinceEpoch() % (1000*m_TRperiod));

        while (samples != end)  // pad block with silence
          {
            samples = load (0, samples);
            ++framesGenerated;
          }
        return framesGenerated * bytesPerFrame ();
      }
      // fall through

    case Idle:
      break;
    }

  Q_ASSERT (Idle == m_state);
  return 0;
}

qint16 Modulator::postProcessSample (qint16 sample) const
{
  if (m_addNoise) {  // Test frame, we'll add noise
    qint32 s = m_fac * (gran () + sample * m_snr / 32768.0);
    if (s > std::numeric_limits<qint16>::max ()) {
      s = std::numeric_limits<qint16>::max ();
    }
    if (s < std::numeric_limits<qint16>::min ()) {
      s = std::numeric_limits<qint16>::min ();
    }
    sample = s;
  }
  return sample;
}
