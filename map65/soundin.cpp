#include "soundin.h"
#include <math.h>

#ifdef Q_OS_WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#endif

#define NFFT 32768
#define FRAMES_PER_BUFFER 1024

#include <portaudio.h>
extern "C"
{
  struct
  {
    double d8[2*60*96000];   //This is "common/datcom/..." in fortran
    float ss[4*322*NFFT];
    float savg[4*NFFT];
    double fcenter;
    int nutc;
    int idphi;                        //Phase correction for Y pol'n, degrees
    int mousedf;                      //User-selected DF
    int mousefqso;                    //User-selected QSO freq (kHz)
    int nagain;                       //1 ==> decode only at fQSO +/- Tol
    int ndepth;                       //How much hinted decoding to do?
    int ndiskdat;                     //1 ==> data read from *.tf2 or *.iq file
    int neme;                         //Hinted decoding tries only for EME calls
    int newdat;                       //1 ==> new data, must do long FFT
    int nfa;                          //Low decode limit (kHz)
    int nfb;                          //High decode limit (kHz)
    int nfcal;                        //Frequency correction, for calibration (Hz)
    int nfshift;                      //Shift of displayed center freq (kHz)
    int mcall3;                       //1 ==> CALL3.TXT has been modified
    int ntimeout;                     //Max for timeouts in Messages and BandMap
    int ntol;                         //+/- decoding range around fQSO (Hz)
    int nxant;                        //1 ==> add 45 deg to measured pol angle
    int map65RxLog;                   //Flags to control log files
    int nfsample;                     //Input sample rate
    int nxpol;                        //1 if using xpol antennas, 0 otherwise
    int mode65;                       //JT65 sub-mode: A=1, B=2, C=4
    int nfast;                        //1No longer used
    int nsave;                        //Number of s3(64,63) spectra saved
    char mycall[12];
    char mygrid[6];
    char hiscall[12];
    char hisgrid[6];
    char datetime[20];
  } datcom_;
}

typedef struct
{
  int kin;          //Parameters sent to/from the portaudio callback function
  int nrx;
  int dB;
  bool bzero;
  bool iqswap;
} paUserData;

//--------------------------------------------------------------- a2dCallback
extern "C" int a2dCallback( const void *inputBuffer, void *outputBuffer,
                         unsigned long framesToProcess,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void *userData )

// This routine called by the PortAudio engine when samples are available.
// It may be called at interrupt level, so don't do anything
// that could mess up the system like calling malloc() or free().

{
  paUserData *udata=(paUserData*)userData;
  (void) outputBuffer;          //Prevent unused variable warnings.
  (void) timeInfo;
  (void) userData;
  int nbytes,i,j;
  float d4[4*FRAMES_PER_BUFFER];
  float d4a[4*FRAMES_PER_BUFFER];
  float tmp;
  float fac;

  if( (statusFlags&paInputOverflow) != 0) {
    qDebug() << "Input Overflow";
  }
  if(udata->bzero) {           //Start of a new minute
    udata->kin=0;              //Reset buffer pointer
    udata->bzero=false;
  }

  nbytes=udata->nrx*8*framesToProcess;        //Bytes per frame
  memcpy(d4,inputBuffer,nbytes);              //Copy all samples to d4

  fac=32767.0 * pow(10.0,0.05*udata->dB);

  if(udata->nrx==2) {
    for(i=0; i<4*int(framesToProcess); i++) {     //Negate odd-numbered frames
      d4[i]=fac*d4[i];
      j=i/4;
      if((j%2)==1) d4[i]=-d4[i];
    }
    if(!udata->iqswap) {
      for(i=0; i<int(framesToProcess); i++) {
        j=4*i;
        tmp=d4[j];
        d4[j]=d4[j+1];
        d4[j+1]=tmp;
        tmp=d4[j+2];
        d4[j+2]=d4[j+3];
        d4[j+3]=tmp;
      }
    }
    memcpy(&datcom_.d8[2*udata->kin],d4,nbytes); //Copy from d4 to dd()
  } else {
    int k=0;
    for(i=0; i<2*int(framesToProcess); i+=2) {    //Negate odd-numbered frames
      j=i/2;
      if(j%2==0) {
        d4a[k++]=fac*d4[i];
        d4a[k++]=fac*d4[i+1];
      } else {
        d4a[k++]=-fac*d4[i];
        d4a[k++]=-fac*d4[i+1];
      }
      d4a[k++]=0.0;
      d4a[k++]=0.0;
    }
    if(!udata->iqswap) {
      for(i=0; i<int(framesToProcess); i++) {
        j=4*i;
        tmp=d4a[j];
        d4a[j]=d4a[j+1];
        d4a[j+1]=tmp;
      }
    }
    memcpy(&datcom_.d8[2*udata->kin],d4a,2*nbytes); //Copy from d4a to dd()
  }
  udata->kin += framesToProcess;
  return paContinue;
}

namespace
{
  struct COMWrapper
  {
    explicit COMWrapper ()
    {
#ifdef Q_OS_WIN32
      // required because Qt only does this for GUI thread
      CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
#endif
    }
    ~COMWrapper ()
    {
#ifdef Q_OS_WIN32
      CoUninitialize ();
#endif
    }
  };
}

void SoundInThread::run()                           //SoundInThread::run()
{
  quitExecution = false;

  if (m_net) {
//    qDebug() << "Start inputUDP()";
    inputUDP();
//    qDebug() << "Finished inputUDP()";
    return;
  }

  COMWrapper c;

  //---------------------------------------------------- Soundcard Setup
  //  qDebug() << "Start souncard input";

  PaError paerr;
  PaStreamParameters inParam;
  PaStream *inStream;
  paUserData udata;

  udata.kin=0;                              //Buffer pointer
  udata.bzero=false;                        //Flag to request reset of kin
  udata.nrx=m_nrx;                          //Number of polarizations
  udata.iqswap=m_IQswap;
  udata.dB=m_dB;

  auto device_info = Pa_GetDeviceInfo (m_nDevIn);

  inParam.device=m_nDevIn;                  //### Input Device Number ###
  inParam.channelCount=2*m_nrx;             //Number of analog channels
  inParam.sampleFormat=paFloat32;           //Get floats from Portaudio
  inParam.suggestedLatency=device_info->defaultHighInputLatency;
  inParam.hostApiSpecificStreamInfo=NULL;

  paerr=Pa_IsFormatSupported(&inParam,NULL,96000.0);
  if(paerr<0) {
    QString error_message;
    if (paUnanticipatedHostError == paerr)
      {
        auto const * last_host_error = Pa_GetLastHostErrorInfo ();
        error_message = QString {"PortAudio Host API error: %1"}.arg (last_host_error->errorText);
      }
    else
      {
        error_message = "PortAudio says requested soundcard format not supported.";
      }
    emit error(error_message);
//    return;
  }
  paerr=Pa_OpenStream(&inStream,            //Input stream
        &inParam,                           //Input parameters
        NULL,                               //No output parameters
        96000.0,                            //Sample rate
        FRAMES_PER_BUFFER,                  //Frames per buffer
//        paClipOff+paDitherOff,              //No clipping or dithering
        paClipOff,                          //No clipping
        a2dCallback,                        //Input callbeck routine
        &udata);                            //userdata

  paerr=Pa_StartStream(inStream);
  if(paerr<0) {
    emit error("Failed to start audio input stream.");
    return;
  }
//  const PaStreamInfo* p=Pa_GetStreamInfo(inStream);

  bool qe = quitExecution;
  int ntr0=99;
  int k=0;
  int nsec;
  int ntr;
  int nBusy=0;
  int nhsym0=0;

//---------------------------------------------- Soundcard input loop
  while (!qe) {
    qe = quitExecution;
    if (qe) break;
    qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
    nsec = ms/1000;             // Time according to this computer
    ntr = nsec % m_TRperiod;

// Reset buffer pointer and symbol number at start of minute
    if(ntr < ntr0 or !m_monitoring or m_TRperiod!=m_TRperiod0) {
      nhsym0=0;
      udata.bzero=true;
      m_TRperiod0=m_TRperiod;
    }
    k=udata.kin;
    udata.iqswap=m_IQswap;
    udata.dB=m_dB;
    if(m_monitoring) {
      if(m_bForceCenterFreq) {
        datcom_.fcenter=m_dForceCenterFreq;
      } else {
        datcom_.fcenter=144.125;
      }
      m_hsym=(k-2048)*11025.0/(2048.0*m_rate);
      if(m_hsym != nhsym0) {
        if(m_dataSinkBusy) {
          nBusy++;
        } else {
          m_dataSinkBusy=true;
          emit readyForFFT(k);         //Signal to compute new FFTs
        }
        nhsym0=m_hsym;
      }
    }
    msleep(100);
    ntr0=ntr;
  }
  Pa_StopStream(inStream);
  Pa_CloseStream(inStream);
}

void SoundInThread::setSwapIQ(bool b)
{
  m_IQswap=b;
}

void SoundInThread::setScale(qint32 n)
{
  m_dB=n;
}
void SoundInThread::setPort(int n)                              //setPort()
{
  if (isRunning()) return;
  this->m_udpPort=n;
}

void SoundInThread::setInputDevice(int n)                  //setInputDevice()
{
  if (isRunning()) return;
  this->m_nDevIn=n;
}

void SoundInThread::setRate(double rate)                         //setRate()
{
  if (isRunning()) return;
  this->m_rate = rate;
}

void SoundInThread::setBufSize(unsigned n)                      //setBufSize()
{
  if (isRunning()) return;
  this->bufSize = n;
}

void SoundInThread::setFadd(double x)
{
  m_fAdd=x;
}


void SoundInThread::quit()                                       //quit()
{
  quitExecution = true;
}

void SoundInThread::setNetwork(bool b)                          //setNetwork()
{
  m_net = b;
}

void SoundInThread::setMonitoring(bool b)                    //setMonitoring()
{
  m_monitoring = b;
}

void SoundInThread::setForceCenterFreqBool(bool b)
{
  m_bForceCenterFreq=b;

}

void SoundInThread::setForceCenterFreqMHz(double d)
{
  m_dForceCenterFreq=d;
}

void SoundInThread::setNrx(int n)                              //setNrx()
{
  m_nrx = n;
}

int SoundInThread::nrx()
{
  return m_nrx;
}

int SoundInThread::mhsym()
{
  return m_hsym;
}

void SoundInThread::setPeriod(int n)
{
  m_TRperiod=n;
}

//--------------------------------------------------------------- inputUDP()
void SoundInThread::inputUDP()
{
  udpSocket = new QUdpSocket();
  if(!udpSocket->bind(m_udpPort,QUdpSocket::ShareAddress) )
  {
    emit error(tr("UDP Socket bind failed."));
    return;
  }

  // Set this socket's total buffer space for received UDP packets
  udpSocket->setSocketOption (QUdpSocket::ReceiveBufferSizeSocketOption, 141600);

  bool qe = quitExecution;
  struct linradBuffer {
    double cfreq;
    int msec;
    float userfreq;
    int iptr;
    quint16 iblk;
    qint8 nrx;
    char iusb;
    double d8[174];
  } b;

  int ntr0=99;
  int k=0;
  int nsec;
  int ntr;
  int nhsym0=0;
  int iz=174;
  int nBusy=0;

  // Main loop for input of UDP packets over the network:
  while (!qe) {
    qe = quitExecution;
    if (qe) break;
    if (!udpSocket->hasPendingDatagrams()) {
      msleep(2);                  // Sleep if no packet available
    } else {
      int nBytesRead = udpSocket->readDatagram((char *)&b,1416);
      if (nBytesRead != 1416) qDebug() << "UDP Read Error:" << nBytesRead;

      qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
      nsec = ms/1000;             // Time according to this computer
      ntr = nsec % m_TRperiod;

// Reset buffer pointer and symbol number at start of minute
      if(ntr < ntr0 or !m_monitoring or m_TRperiod!=m_TRperiod0) {
        k=0;
        nhsym0=0;
        m_TRperiod0=m_TRperiod;
      }
      ntr0=ntr;

      if(m_monitoring) {
        m_nrx=b.nrx;
        if(m_nrx == +1) iz=348;                 //One RF channel, i*2 data
        if(m_nrx == -1 or m_nrx == +2) iz=174;  //One Rf channel, r*4 data
                                                // or 2 RF channels, i*2 data
        if(m_nrx == -2) iz=87;                  // Two RF channels, r*4 data

        // If buffer will not overflow, move data into datcom_
        if ((k+iz) <= 60*96000) {
          int nsam=-1;
          recvpkt_(&nsam, &b.iblk, &b.nrx, &k, b.d8, b.d8, b.d8);
          if(m_bForceCenterFreq) {
            datcom_.fcenter=m_dForceCenterFreq;
          } else {
            datcom_.fcenter=b.cfreq + m_fAdd;
          }
        }

        m_hsym=(k-2048)*11025.0/(2048.0*m_rate);
        if(m_hsym != nhsym0) {
          if(m_dataSinkBusy) {
            nBusy++;
          } else {
            m_dataSinkBusy=true;
            emit readyForFFT(k);         //Signal to compute new FFTs
          }
          nhsym0=m_hsym;
        }
      }
    }
  }
  delete udpSocket;
}
