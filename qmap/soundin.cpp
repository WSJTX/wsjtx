#include "soundin.h"
#include "qmap_runtime_config.h"
#include <math.h>

#ifdef Q_OS_WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#endif

#define FRAMES_PER_BUFFER 1024

extern "C"
{
  extern struct
  {
    double d8[qmap_runtime::MAX_NSMAX];   //This is "common/datcom/..." in fortran
    float ss[400*qmap_runtime::MAX_NFFT];
    float savg[qmap_runtime::MAX_NFFT];   //Avg spectra at 0,45,90,135 deg pol
    double fcenter;                   //Center freq from Linrad (MHz)
    int nutc;                         //UTC as integer, HHMM
    float fselected;                  //Selected frequency for nagain decodes
    int mousedf;                      //User-selected DF
    int mousefqso;                    //User-selected QSO freq (kHz)
    int nagain;                       //1 ==> decode only at fQSO +/- Tol
    int ndepth;                       //How much hinted decoding to do?
    int ndiskdat;                     //1 ==> data read from *.iq file
    int ntx60;                        //Number of seconds transmitted in Q65-60x
    int newdat;                       //1 ==> new data, must do long FFT
    int nfa;                          //Low decode limit (kHz)
    int nfb;                          //High decode limit (kHz)
    int nfcal;                        //Frequency correction, for calibration (Hz)
    int nfshift;                      //Shift of displayed center freq (kHz)
    int ntx30a;                       //Number of seconds transmitted in first half minute , Q65-30x
    int ntx30b;                       //Number of seconds transmitted in second half minute, Q65-30x
    int ntol;                         //+/- decoding range around fQSO (Hz)
    int junk5;                        //
    int junk4;                        //
    int nfsample;                     //Input sample rate
    int junk3;                        //
    int nBaseSubmode;                 //Base submode for Q65-60x (aka m_modeQ65)
    int ndop00;                       //EME Self Doppler
    int nsave;                        //Number of s3(64,63) spectra saved
    int max_drift;                    //Maximum Q65 drift: units symbol_rate/TxT
    int offset;                       //Offset in Hz
    int nhsym;                        //Number of available JT65 half-symbols
    char mycall[12];
    char mygrid[6];
    char hiscall[12];
    char hisgrid[6];
    char datetime[20];
    int junk1;                        //Used to test extent of copy to shared memory
    int junk2;
    bool bAlso30;                     //Process for 30-second submode as well as 60-second
  } datcom_;
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
    inputUDP();
    return;
  }
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
  // (the same time span of data as at 96 kHz, whatever the rate)
  udpSocket->setSocketOption (QUdpSocket::ReceiveBufferSizeSocketOption,
                              141600 * (qmap_runtime::activeRateHz() / 96000.0));

  bool qe = quitExecution;
  struct linradBuffer {
    double cfreq;
    int msec;
    float userfreq;
    int iptr;
    quint16 iblk;
    qint8 nrx;
    char iusb;
    double d8[512];                  //Up to 1024 i*2 IQ pairs (4096 bytes)
  } b;
  constexpr int header_bytes = 24;   //Bytes in the packet ahead of d8

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
//      msleep(2);                  // Sleep if no packet available
      QObject().thread()->usleep(2000);
    } else {
      // The packet length depends on the sender and on the sample rate: 1416 bytes
      // (348 IQ pairs) at 96 kHz, more at the higher rates.
      int nBytesRead = udpSocket->readDatagram((char *)&b,sizeof(b));
      int payload_bytes = nBytesRead - header_bytes;
      if (payload_bytes < 8) {
        qDebug() << "UDP Read Error:" << nBytesRead;
        continue;
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
        // Number of IQ pairs in this datagram, from its payload size
        if(m_nrx == +1) iz=payload_bytes/4;                    //One RF channel, i*2 data (348 at 96 kHz)
        if(m_nrx == -1 or m_nrx == +2) iz=payload_bytes/8;     //One Rf channel, r*4 data
                                                               // or 2 RF channels, i*2 data
        if(m_nrx == -2) iz=payload_bytes/16;                   // Two RF channels, r*4 data

        // If buffer will not overflow, move data into datcom_
        if ((k+iz) <= qmap_runtime::activeNsmax()) {
          int nsam=-1;
          qmap_set_npairs_active(iz);
          recvpkt_(&nsam, &b.iblk, &b.nrx, &k, b.d8, b.d8, &m_dB);
          datcom_.fcenter=b.cfreq + m_fAdd;
        }
        m_hsym=(k-2048)/(qmap_runtime::activeRateHz()*3/20);   //0.15 s * rate = 14400 at 96 kHz
        if(m_hsym != nhsym0) {
          if(!m_dataSinkBusy) {
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
