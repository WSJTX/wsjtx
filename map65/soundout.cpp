#include "soundout.h"

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

#include <portaudio.h>

extern float gran();
extern short int (&iwave)[2*60*12000];
extern int nwave;
extern bool btxok;
extern bool bTune;
extern bool bIQxt;
extern int iqAmp;
extern int iqPhase;
extern int txPower;
extern double outputLatency;

typedef struct
{
  int nTRperiod;
  int actualChannelCount; 
  double streamSampleRate; // Added to help callback calculate pitch
} paUserData;

//--------------------------------------------------------------- d2aCallback
extern "C" int d2aCallback(const void * /*inputBuffer*/, void *outputBuffer,
                           unsigned long framesToProcess,
                           const PaStreamCallbackTimeInfo* /*timeInfo*/,
                           PaStreamCallbackFlags /*statusFlags*/,
                           void *userData )
{
  paUserData *udata = (paUserData*)userData;
  short *wptr = (short*)outputBuffer;
  int channels = udata->actualChannelCount;
  
  // step is ~0.229 for 48k ASIO, and exactly 1.0 for 11k MME
    // Calculate step once outside the loop
  double step = 11025.0 / udata->streamSampleRate; 

  // Debug: Check this value in your console once to ensure it's ~0.229 for ASIO
  static bool debugOnce = true;
  if(debugOnce) {
      qDebug() << "Stream Rate:" << udata->streamSampleRate << "Step:" << step;
      qDebug() << "channels:" << channels;
      debugOnce = false;
  }


  static double ic_exact = 0.0; 
  static bool btxok0 = false;
  static bool bTune0 = false;
  static int nStart = 0;
  static double phi = 0.;

  double tsec, tstart, dphi;
  int nsec;
  int nTRperiod = udata->nTRperiod;

  qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
  tsec = 0.001 * ms;
  nsec = ms / 1000;
  
  qreal dPhase = iqPhase / 5729.57795131;
  qreal amp = 1.0 + 0.0001 * iqAmp;
  qreal xAmp = txPower * 295.00 * qSqrt(2.0 - amp * amp);
  qreal yAmp = txPower * 295.00 * amp;
  static int nsec0 = 0;

    if(bTune) {
    ic_exact = 0;
    // CHANGE: Use the actual stream sample rate, not 11025
    dphi = 6.28318530718 * 1270.46 / udata->streamSampleRate;
  }

  if(bTune0 && !bTune) btxok = false;
  bTune0 = bTune;
  if(nsec != nsec0) nsec0 = nsec;

  // Handle Tx Start/Restart logic
  if(btxok && !btxok0) {
    int n = nsec / nTRperiod;
    tstart = tsec - n * nTRperiod - 1.0;
    if(tstart < 1.0) {
      ic_exact = 0;
      nStart = n;
    } else {
      if(n != nStart) {
        ic_exact = (tstart * 11025.0);
        nStart = n;
      }
    }
  }
  btxok0 = btxok;


  for(unsigned int i = 0; i < framesToProcess; i++) {
    // 1. Get current fractional position
    int i0 = (int)ic_exact;
    int i1 = i0 + 1;
    double frac = ic_exact - (double)i0;

    short int i2a = 0;
    short int i2b = 0;

    // 2. Linear Interpolation to clean up the noise
if (btxok && (2 * i1 + 1) < 1440000) {

    // Interpolate message waveform
    qreal I = (1.0 - frac) * iwave[2*i0]     + frac * iwave[2*i1];
    qreal Q = (1.0 - frac) * iwave[2*i0 + 1] + frac * iwave[2*i1 + 1];

    if (bTune) {
        // --- TUNE MODE (unchanged) ---
        phi += dphi;
        I = xAmp * qCos(phi);
        Q = yAmp * qSin(phi + dPhase);

    } else {
        // --- MESSAGE TX: scale using the Tune slider percentage ---
        qreal gain = txPower / 100.0;   // 0.0 ? 1.0

        I *= gain;
        Q *= gain;
    }

    // Clamp to 16-bit
    i2a = short(qBound(-32768.0, I, 32767.0));
    i2b = short(qBound(-32768.0, Q, 32767.0));
}



    *wptr++ = i2b; // Left
    *wptr++ = i2a; // Right

    // Pad remaining 6 channels for Voicemeeter
    for(int c = 2; c < channels; c++) {
      *wptr++ = 0;
    }

    // Advance index by the ratio (0.229)
    ic_exact += step;

    if((2 * (int)ic_exact) >= nwave) {
      btxok = false;
      ic_exact = 0;
    }
  }

  return paContinue;
}

namespace
{
  struct COMWrapper
  {
    explicit COMWrapper ()
    {
#ifdef _WIN32
      CoInitializeEx (nullptr, 
         COINIT_APARTMENTTHREADED | 
         COINIT_DISABLE_OLE1DDE);
#endif
    }
    ~COMWrapper ()
    {
#ifdef _WIN32
      CoUninitialize ();
#endif
    }
  };
}

void SoundOutThread::run()
{
  COMWrapper c;
  PaError paerr;
  PaStreamParameters outParam;
  PaStream *outStream;
  paUserData udata;
  quitExecution = false;

  // --- SAFETY CHECK 1: Ensure device index is valid ---
  auto device_info = Pa_GetDeviceInfo(m_nDevOut);
  if (!device_info) {
      qDebug() << "ASIO Error: Could not get info for device index" << m_nDevOut;
      return;
  }

  // --- SAFETY CHECK 2: Ensure Host API is valid ---
  auto host_api_info = Pa_GetHostApiInfo(device_info->hostApi);
  if (!host_api_info) {
      qDebug() << "ASIO Error: Could not get host API info for device" << m_nDevOut;
      return;
  }
  
  bool isASIO = (host_api_info->type == paASIO);

  // Setup defaults for non-ASIO (MME, DirectSound, etc)
  double outRate = 11025.0; 
  udata.actualChannelCount = 2;
  unsigned long framesPerBuffer = 256; 

  if(isASIO) {
      outRate = 48000.0; 
      udata.actualChannelCount = 8; 
      framesPerBuffer = paFramesPerBufferUnspecified;
  }

  udata.nTRperiod = m_TRperiod;
  udata.streamSampleRate = outRate;

  outParam.device = m_nDevOut;
  outParam.channelCount = udata.actualChannelCount;
  outParam.sampleFormat = paInt16;
  
  // --- SAFETY CHECK 3: Guard latency access ---
  outParam.suggestedLatency = device_info->defaultLowOutputLatency;
  outParam.hostApiSpecificStreamInfo = NULL;

  // ASIO Fallback Logic
  paerr = Pa_IsFormatSupported(NULL, &outParam, outRate);
  if(paerr < 0 && isASIO) {
      udata.actualChannelCount = 2;
      outParam.channelCount = 2;
      paerr = Pa_IsFormatSupported(NULL, &outParam, outRate);
  }

  // --- SAFETY CHECK 4: Final guard before opening ---
  paerr = Pa_OpenStream(&outStream, NULL, &outParam, outRate, 
                        framesPerBuffer, paClipOff, 
                        d2aCallback, &udata);

  if(paerr != paNoError) {
      qDebug() << "Pa_OpenStream failed:" << Pa_GetErrorText(paerr);
      return;
  }

  paerr = Pa_StartStream(outStream);
  if(paerr != paNoError) {
      qDebug() << "Pa_StartStream failed:" << Pa_GetErrorText(paerr);
      return;
  }

  // --- SAFETY CHECK 5: Final stream info guard ---
  const PaStreamInfo* p = Pa_GetStreamInfo(outStream);
  if (p) {
      outputLatency = p->outputLatency;
  }

  while (!quitExecution) { msleep(100); }

  Pa_StopStream(outStream);
  Pa_CloseStream(outStream);
}

void SoundOutThread::setOutputDevice(int n) { this->m_nDevOut = n; }
void SoundOutThread::setPeriod(int n) { m_TRperiod = n; }
