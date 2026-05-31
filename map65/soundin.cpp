#include "soundin.h"
#include "commons.h"
#include <math.h>
#include <complex>
#include "mainwindow.h"

#ifdef Q_OS_WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#endif

#define NFFT 32768
#define FRAMES_PER_BUFFER 1024

#include <portaudio.h>
#include <QDebug>


typedef struct
{
  int kin;          //Parameters sent to/from the portaudio callback function
  int nrx;
  int dB;
  bool bzero;
  bool iqswap;
} paUserData;

//--------------------------------------------------------------- a2dCallback
extern "C" int a2dCallback(const void* inputBuffer, void* outputBuffer,
                           unsigned long framesToProcess,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData)
{
    paUserData* udata = static_cast<paUserData*>(userData);

    (void) outputBuffer;
    (void) timeInfo;
    (void) userData;

    int   nbytes,i,j;
    float d4[4 * FRAMES_PER_BUFFER];
    float d4a[4 * FRAMES_PER_BUFFER];
    float tmp;
    float fac;

    if ((statusFlags & paInputOverflow) != 0) {
        qDebug() << "Input Overflow";
    }

    if (udata->bzero) {           // Start of a new minute
        udata->kin   = 0;         // Reset buffer pointer
        udata->bzero = false;
    }

    // Bytes per frame, mirroring legacy intent but for float32
    // legacy used: nbytes = udata->nrx * 8 * framesToProcess;
    // here: nrx * 8 bytes per frame (4 floats per frame, each 4 bytes)
    nbytes = udata->nrx * 8 * framesToProcess;

    memcpy(d4, inputBuffer, nbytes);

    fac = 32767.0f * pow(10.0f, 0.05f * float(udata->dB));

    if (udata->nrx == 2) {
        // Two RF channels, r*4 data (complex I/Q for X and Y)
        for (i = 0; i < 4 * int(framesToProcess); i++) {
            d4[i] = fac * d4[i];
            j = i / 4;
            if ((j % 2) == 1) d4[i] = -d4[i];
        }

        if (!udata->iqswap) {
            for (i = 0; i < int(framesToProcess); i++) {
                j = 4 * i;
                tmp = d4[j];     d4[j] = d4[j+1];     d4[j+1] = tmp;
                tmp = d4[j+2];   d4[j+2] = d4[j+3];   d4[j+3] = tmp;
            }
        }

        memcpy(&dd[4 * udata->kin], d4, nbytes); 

    } else {
        // One RF channel
        int k = 0;
        for (i = 0; i < 2 * int(framesToProcess); i += 2) {
            j = i / 2;
            if (j % 2 == 0) {
                d4a[k++] = fac * d4[i];
                d4a[k++] = fac * d4[i+1];
            } else {
                d4a[k++] = -fac * d4[i];
                d4a[k++] = -fac * d4[i+1];
            }
            d4a[k++] = 0.0;
            d4a[k++] = 0.0;
        }

        if (!udata->iqswap) {
            for (i = 0; i < int(framesToProcess); i++) {
                j = 4 * i;
                tmp = d4a[j];     d4a[j] = d4a[j+1];     d4a[j+1] = tmp;
            }
        }

        // Legacy wrote 2*nbytes into datcom_.d8 for this case (4 floats per frame)
        memcpy(&dd[4 * udata->kin], d4a, 2 * nbytes);
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

//SoundInThread::SoundInThread(MainWindow* mw, QObject* parent)
//   : QThread(parent), m_mainWindow(mw) {}

void SoundInThread::run()                           //SoundInThread::run()
{
  quitExecution = false;
    
  if (m_net) {
//    qDebug() << "Start inputUDP()";
    inputUDP();
    return;

//    qDebug() << "Finished inputUDP()";
    return;
  }

  COMWrapper c;

  //---------------------------------------------------- Soundcard Setup
  qDebug() << "START SOUNDCARD INPUT ";

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
  }
 paerr = Pa_OpenStream(
    &inStream,
    &inParam,
    NULL,
    96000.0,
    FRAMES_PER_BUFFER,
    paClipOff,
    a2dCallback,
    &udata);

    paerr=Pa_StartStream(inStream);
    if(paerr<0) {
      emit error("Failed to start audio input stream.");
      return;
    }

  bool qe = quitExecution;
  int ntr0=99;
  int k=0;
  int nsec;
  int ntr;
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
    udata.iqswap= (m_IQswap != 0);
    udata.dB=m_dB;
    if(m_monitoring) {
      double fcenter;
      if(m_bForceCenterFreq) {
        fcenter = m_dForceCenterFreq;
      } else {
          fcenter = 144.125;   // default center
      }
      set_fcenter(fcenter);

      m_hsym=(k-2048)*11025.0/(2048.0*m_rate);
      if(m_hsym != nhsym0) {
        if(m_dataSinkBusy) {
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
  //qDebug() << "soundin line 333 SET m_nrx = " << n;
}

int SoundInThread::nrx()
{
  //qDebug() << "soundin line 338 RETURNED m_nrx = " << m_nrx;
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
    quint16 iblk;  //was quint16
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

  // Main loop for input of UDP packets over the network:
  while (!qe) {
    qe = quitExecution;
    if (qe) break;
    if (!udpSocket->hasPendingDatagrams()) {
      msleep(2);                  // Sleep if no packet available
    } else {
      int nBytesRead = udpSocket->readDatagram(reinterpret_cast<char*>(&b), 1416);
            if (nBytesRead != 1416) {
                qDebug() << "UDP Read Error:" << nBytesRead;
            }


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
          double fcenter;
          if(m_bForceCenterFreq) {
            fcenter = m_dForceCenterFreq;
          } else {
            fcenter = b.cfreq + m_fAdd;
          }
          set_fcenter(fcenter);
        }

m_hsym = (k-2048)*11025.0/(2048.0*m_rate);
if (m_hsym != nhsym0) {
    if (m_dataSinkBusy) {
    } else {
        m_dataSinkBusy = true;

        QDateTime now = QDateTime::currentDateTime();
        QString ts = now.toString("yyyy-MM-dd hh:mm:ss.zzz");

        emit readyForFFT(k);
    }
    nhsym0 = m_hsym;
}

      }
    }
  }
  delete udpSocket;
  udpSocket =  nullptr;
}
