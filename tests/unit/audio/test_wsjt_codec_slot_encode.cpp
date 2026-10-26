// SPDX-License-Identifier: GPL-3.0-or-later
// wsjt-codec's pack and render for the slot modes against the GUI's own
// transmit path: each message generated as MainWindow::guiUpdate generates
// it, and the audio the GUI's Modulator plays for it as MainWindow::transmit
// requests it (widgets/mainwindow.cpp, Modulator/Modulator.cpp). The stream's
// contract is lib/streaming_encode.f90.

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <algorithm>
#include <cmath>

#include "wsjtx_config.h"
#include "commons.h"
#include "Audio/AudioOutputSink.hpp"
#include "Modulator/Modulator.hpp"
#include "TxStartPolicy.hpp"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () { return 0; }

#define FCL fortran_charlen_t

// As widgets/mainwindow.cpp declares them.
extern "C" {
  void genft8_(char* msg, int* i3, int* n3, char* msgsent, char ft8msgbits[],
               int itone[], fortran_charlen_t, fortran_charlen_t);
  void genft4_(char* msg, int* ichk, char* msgsent, char ft4msgbits[], int itone[],
               fortran_charlen_t, fortran_charlen_t);
  void genfst4_(char* msg, int* ichk, char* msgsent, char fst4msgbits[],
                 int itone[], int* iwspr, fortran_charlen_t, fortran_charlen_t);
  void gen_ft8wave_(int itone[], int* nsym, int* nsps, float* bt, float* fsample, float* f0,
                    float xjunk[], float wave[], int* icmplx, int* nwave);
  void gen_ft4wave_(int itone[], int* nsym, int* nsps, float* fsample, float* f0,
                    float xjunk[], float wave[], int* icmplx, int* nwave);
  void gen_fst4wave_(int itone[], int* nsym, int* nsps, int* nwave, float* fsample,
                       int* hmod, float* f0, int* icmplx, float xjunk[], float wave[]);
  void genwave_(int itone[], int* nsym, int* nsps, int* nwave, float* fsample,
                double* toneSpacing, float* f0, int* icmplx, float xjunk[], float wave[]);
  void gen4_(char* msg, int* ichk, char* msgsent, int itone[],
               int* itext, fortran_charlen_t, fortran_charlen_t);
  void gen9_(char* msg, int* ichk, char* msgsent, int itone[],
               int* itext, fortran_charlen_t, fortran_charlen_t);
  void genmsk_128_90_(char* msg, int* ichk, char* msgsent, int itone[], int* itype,
                      fortran_charlen_t, fortran_charlen_t);
  void gen65(char* msg, int* ichk, char msgsent[], int itone[], int* itext);
  void genq65_(char* msg, int* ichk, char* msgsent, int itone[],
              int* i3, int* n3, int* iflag, fortran_charlen_t, fortran_charlen_t);
  void genwspr_(char* msg, char* msgsent, int itone[], fortran_charlen_t, fortran_charlen_t);
  void gen_echocall_(char* basecall, int itone[], fortran_charlen_t);
  void gen_cw_wave_(char const * msg, int* ifreq, float wave[], fortran_charlen_t);
  void morse_(char* msg, int* icw, int* ncw, fortran_charlen_t);
}

// Modulator's friend: its start and reads at a chosen clock.
class TestModulator
{
public:
  static void startAt (Modulator& modulator, TxEvidence::TxRequest const& request,
                       AudioOutputSink * sink, qint64 ms)
  {
    modulator.startAt (request, sink, ms);
  }

  static qint64 readDataAt (Modulator& modulator, char * data, qint64 size, qint64 ms)
  {
    return modulator.readDataAt (data, size, ms);
  }
};

namespace
{
constexpr unsigned TX_SAMPLE_RATE = 48000;          // widgets/mainwindow.h
constexpr int NUM_MSK144_SYMBOLS = 144;              // widgets/mainwindow.h
constexpr int ProcessTimeoutMs = 240000;

class Sink final : public AudioOutputSink
{
public:
  void setFormat (QAudioDeviceInfo const&, unsigned, int) override {}
  void restart (QIODevice *, TxEvidence::TxPlaybackContext, qint64) override {}
  void suspend () override {}
  void resume () override {}
  void reset () override {}
  void stop () override {}
  void setAttenuation (qreal) override {}
  void resetAttenuation () override {}
  int bufferSize () const override { return 0; }
};

// One transmission: the GUI's settings for it and the codec request's keys.
struct Row
{
  QString mode;
  QString text;
  int submode {-1};             // m_nSubMode; -1 for none
  int spacing {1};              // x2ToneSpacing, x4ToneSpacing; Echo: sbToneSpacing
  int trperiod {0};             // m_TRperiod where the mode has a choice; 0 for none
  bool copied {};               // Q65 pileup: the bit-78 flag
  int txFreq {1500};            // TxFreqSpinBox (WSPRfreqSpinBox for FST4W)
  bool fallback {};             // the 13-character free-text fallback applies
  bool fast {};                 // m_bFast9
  QString kind;                 // Echo: rbFixedTone, rbEchoMessage or rbEchoCW
  double dither {};             // Echo: m_fDither
  QString tuneMode;             // Tune: m_tune in this m_mode
  int seconds {};               // Tune: how long it runs
  QString cwid;                 // icw[0]=m_ncw, with this my_callsign
};

struct Transmission
{
  bool refused {};
  QString msgsent;
  QVector<int> tones;
  int type {};
  QVector<qint16> audio;        // 48 kHz
};

// MainWindow::ba2msg.
void ba2msg(QByteArray ba, char message[])
{
  int iz=ba.length();
  for(int i=0; i<37; i++) {
    if(i<iz) {
      if(int(ba[i])>=97 and int(ba[i])<=122) ba[i]=int(ba[i])-32;
      message[i]=ba[i];
    } else {
      message[i]=32;
    }
  }
  message[37]=0;
}

// The FST4 and FST4W samples per symbol at 12 kHz for m_TRperiod.
int fst4Nsps (double m_TRperiod)
{
  int nsps=720;
  if(m_TRperiod==30) nsps=1680;
  if(m_TRperiod==60) nsps=3888;
  if(m_TRperiod==120) nsps=8200;
  if(m_TRperiod==300) nsps=21504;
  if(m_TRperiod==900) nsps=66560;
  if(m_TRperiod==1800) nsps=134400;
  return nsps;
}

// The GUI's transmission of row, MainWindow::guiUpdate's generation in this
// process's GUI state, then (with audio) MainWindow::transmit's request to the
// Modulator, read from its first sample to the last it commits. synchronize
// is off, which removes only the leading silence that starts the audio on
// time. A Tune is read for its seconds; with a CW ID the Modulator is read as
// the GUI's sound card would read it with a buffer that ends where the
// message does (in a fast mode where the first buffer more than TR - 5 s into
// the period, which starts with the audio, begins), and from there until the
// Modulator goes idle.
Transmission gui (Row const& row, bool audio = true)
{
  bool const m_tune = row.mode == "TUNE";
  QString const m_mode = m_tune ? row.tuneMode : row.mode == "ECHO" ? QString {"Echo"} : row.mode;
  int const m_nSubMode = row.submode;
  double m_TRperiod = row.trperiod;
  if (m_mode == "FT8") m_TRperiod = 15;
  if (m_mode == "FT4") m_TRperiod = 7.5;
  if ((m_mode == "JT4" || m_mode == "JT9" || m_mode == "JT65") && !row.fast) m_TRperiod = 60;
  if (m_mode == "WSPR") m_TRperiod = 120;
  if (m_mode == "Echo") m_TRperiod = 3.0;
  bool const m_bFast9 = row.fast;
  int const m_XIT = 0;
  bool const x2ToneSpacing = row.spacing == 2;
  bool const x4ToneSpacing = row.spacing == 4;
  int const TxFreqSpinBox = row.txFreq;
  int const WSPRfreqSpinBox = row.txFreq;
  double const m_fDither = row.dither;
  bool const rbEchoMessage = row.kind == "message";
  bool const rbEchoCW = row.kind == "cw";

  // Modulator reads the tone and the waveform sample just past the message,
  // which a fresh GUI process has as zero.
  for (auto& tone : itone) tone = 0;
  int waveSamples = 0;
  if (m_mode == "FT8") waveSamples = 79 * 4 * 1920 + 1;
  if (m_mode == "FT4") waveSamples = 105 * 4 * 576 + 1;
  if (m_mode == "FST4" || m_mode == "FST4W") waveSamples = 162 * 4 * fst4Nsps (m_TRperiod) + 1;
  if (m_mode == "Echo") waveSamples = 6 * 4 * 4096 + 1;
  std::fill_n (foxcom_.wave, waveSamples, 0.f);

  // The codec takes the text less its outer blanks, which the GUI's Tx box
  // keeps (its FST4 and MSK144 encoders then drop characters after leading
  // blanks): the GUI here holds the stripped text.
  QString text = row.text;
  while (text.startsWith (' ')) text.remove (0, 1);
  while (text.endsWith (' ')) text.chop (1);
  static char message[38];
  static char msgsent[38];
  QByteArray ba;
  if (m_mode == "WSPR") {
    ba=text.toLatin1();
  } else {
    ba=text.toLocal8Bit();
  }
  ba2msg(ba,message);
  int ichk=0;
  int msgsent_length=37;
  std::fill_n(msgsent, sizeof msgsent, ' ');
  msgsent[37]=0;
  int m_currentMessageType = 0;
  // CallsignValidator upper-cases the Echo message.
  QString const leEchoMessage = text.toUpper ();
  if(m_tune or m_mode=="Echo") {
    itone[0]=0;
    auto const generated_label = m_tune ? QByteArrayLiteral("TUNE") : QByteArrayLiteral("ECHO");
    std::copy(generated_label.cbegin(), generated_label.cend(), msgsent);
    if(rbEchoMessage or rbEchoCW) {
      QString echoMsg=(leEchoMessage+"      ").left(6);
      gen_echocall_(const_cast <char *> (echoMsg.toLatin1().constData()),const_cast<int *>(itone),(FCL)6);
    }
  } else {
  if(m_mode=="JT4") gen4_(message, &ichk , msgsent, const_cast<int *> (itone),
                            &m_currentMessageType, (FCL)22, (FCL)22);
  if(m_mode=="JT9") gen9_(message, &ichk, msgsent, const_cast<int *> (itone),
                            &m_currentMessageType, (FCL)22, (FCL)22);
  if(m_mode=="JT65") gen65(message, &ichk, msgsent, const_cast<int *> (itone),
                              &m_currentMessageType);
  if(m_mode=="WSPR") genwspr_(message, msgsent, const_cast<int *> (itone),
                                (FCL)22, (FCL)22);
  if(m_mode=="JT4" or m_mode=="JT9" or m_mode=="JT65" or m_mode=="WSPR") msgsent_length=22;
  if(m_mode=="MSK144" or m_mode=="FT8" or m_mode=="FT4"
     or m_mode=="FST4" or m_mode=="FST4W" || "Q65" == m_mode) {
    if(m_mode=="MSK144") {
      genmsk_128_90_(message, &ichk, msgsent, const_cast<int *> (itone),
                     &m_currentMessageType, (FCL)37, (FCL)37);
    }
    if(m_mode=="FT8") {
      int i3=0;
      int n3=0;
      char ft8msgbits[77];
      genft8_(message, &i3, &n3, msgsent, const_cast<char *> (ft8msgbits),
              const_cast<int *> (itone), (FCL)37, (FCL)37);
      if (!should_block_generated_transmit (QString::fromLatin1 (msgsent), m_tune)) {
        int nsym=79;
        int nsps=4*1920;
        float fsample=48000.0;
        float bt=2.0;
        float f0=TxFreqSpinBox - m_XIT;
        int icmplx=0;
        int nwave=nsym*nsps;
        gen_ft8wave_(const_cast<int *>(itone),&nsym,&nsps,&bt,&fsample,&f0,foxcom_.wave,
                     foxcom_.wave,&icmplx,&nwave);
      }
    }
    if(m_mode=="FT4") {
      int ichk=0;
      char ft4msgbits[77];
      genft4_(message, &ichk, msgsent, const_cast<char *> (ft4msgbits),
              const_cast<int *>(itone), (FCL)37, (FCL)37);
      if (!should_block_generated_transmit (QString::fromLatin1 (msgsent), m_tune)) {
        int nsym=103;
        int nsps=4*576;
        float fsample=48000.0;
        float f0=TxFreqSpinBox - m_XIT;
        int nwave=(nsym+2)*nsps;
        int icmplx=0;
        gen_ft4wave_(const_cast<int *>(itone),&nsym,&nsps,&fsample,&f0,foxcom_.wave,
                     foxcom_.wave,&icmplx,&nwave);
      }
    }
    if(m_mode=="FST4" or m_mode=="FST4W") {
      int ichk=0;
      int iwspr=0;
      char fst4msgbits[101];
      if(m_mode=="FST4W") {
        iwspr = 1;
        ba=text.toLatin1();
        ba2msg(ba,message);
      }
      genfst4_(message,&ichk,msgsent,const_cast<char *> (fst4msgbits),
               const_cast<int *>(itone), &iwspr, (FCL)37, (FCL)37);
      if (!should_block_generated_transmit (QString::fromLatin1 (msgsent), m_tune)) {
        int hmod=1;
        if(x2ToneSpacing) hmod=2;
        if(x4ToneSpacing) hmod=4;
        int nsps=fst4Nsps (m_TRperiod);
        nsps=4*nsps;                           //48000 Hz sampling
        int nsym=160;
        float fsample=48000.0;
        float dfreq=hmod*fsample/nsps;
        float f0=TxFreqSpinBox - m_XIT + 1.5*dfreq;
        if(m_mode=="FST4W") f0=WSPRfreqSpinBox - m_XIT + 1.5*dfreq;
        int nwave=(nsym+2)*nsps;
        int icmplx=0;
        gen_fst4wave_(const_cast<int *>(itone),&nsym,&nsps,&nwave,
                      &fsample,&hmod,&f0,&icmplx,foxcom_.wave,foxcom_.wave);
      }
    }
    if(m_mode=="Q65") {
      int i3=-1;
      int n3=-1;
      int iflag=row.copied ? 1 : 0;
      genq65_(message, &ichk,msgsent, const_cast<int *>(itone), &i3, &n3, &iflag, (FCL)37, (FCL)37);
      if (!should_block_generated_transmit (QString::fromLatin1 (msgsent), m_tune)) {
        int nsps=1800;
        if(m_TRperiod==30) nsps=3600;
        if(m_TRperiod==60) nsps=7200;
        if(m_TRperiod==120) nsps=16000;
        if(m_TRperiod==300) nsps=41472;
        int nsps4=4*nsps;                           //48000 Hz sampling
        int nsym=85;
        float fsample=48000.0;
        int nwave=(nsym+2)*nsps4;
        int icmplx=0;
        float f0=TxFreqSpinBox-m_XIT;
        double toneSpacing=fsample/nsps4;
        genwave_(const_cast<int *>(itone),&nsym,&nsps4,&nwave,
                 &fsample,&toneSpacing,&f0,&icmplx,foxcom_.wave,foxcom_.wave);
      }
    }
  }
  msgsent[msgsent_length]=0;
  }

  Transmission result;
  QString const generated_message = QString::fromLatin1(msgsent);
  result.refused = should_block_generated_transmit (generated_message, m_tune);
  result.msgsent = generated_message;
  while (result.msgsent.endsWith (' ')) result.msgsent.chop (1);
  result.type = m_currentMessageType;
  if (result.refused) return result;

  int ntones = 0;
  if (m_mode == "JT4") ntones = 206;
  if (m_mode == "JT9" || m_mode == "Q65") ntones = 85;
  if (m_mode == "JT65") ntones = 126;
  if (m_mode == "WSPR") ntones = 162;
  if (m_mode == "MSK144") ntones = itone[40] < 0 ? 40 : 144;
  if (m_mode == "FT8") ntones = 79;
  if (m_mode == "FT4") ntones = 103;
  if (m_mode == "FST4" || m_mode == "FST4W") ntones = 160;
  if (m_tune || m_mode == "Echo") ntones = rbEchoMessage || rbEchoCW ? 6 : 1;
  for (int i = 0; i < ntones; ++i) result.tones << int (itone[i]);
  if (!audio) return result;

  // MainWindow::morse_ for the station's call, and guiUpdate's icw[0]=m_ncw.
  int icw[NUM_CW_SYMBOLS] {};
  int m_ncw = 0;
  if (!row.cwid.isEmpty ()) {
    morse_(const_cast<char *> (row.cwid.toLatin1().constData()),
           icw, &m_ncw, (FCL)row.cwid.length());
    icw[0]=m_ncw;
  }

  TxEvidence::TxRequest request;
  request.mode = m_mode;
  request.channel = AudioDevice::Mono;
  request.snr_db = 99;
  request.tr_period_s = m_TRperiod;
  request.synchronize = false;
  request.tuning = m_tune;
  int const cwSymbols = qBound (0, int (icw[0]), NUM_CW_SYMBOLS - 1);
  request.cw_id.reserve (cwSymbols);
  for (int i = 1; i <= cwSymbols; ++i) request.cw_id.append (int (icw[i]));
  double m_toneSpacing = 12000.0/8192.0;        // set on entering WSPR
  double toneSpacing=0.0;
  if (m_mode == "JT65") {
    if(m_nSubMode==0) toneSpacing=11025.0/4096.0;
    if(m_nSubMode==1) toneSpacing=2*11025.0/4096.0;
    if(m_nSubMode==2) toneSpacing=4*11025.0/4096.0;
    request.symbols_length = 126;
    request.frames_per_symbol = 4096.0*12000.0/11025.0;
    request.frequency_hz = TxFreqSpinBox - m_XIT;
    request.tone_spacing = toneSpacing;
  }
  if (m_mode == "FT8") {
    toneSpacing=-3;
    if(x2ToneSpacing) toneSpacing=2*12000.0/1920.0;
    if(x4ToneSpacing) toneSpacing=4*12000.0/1920.0;
    request.symbols_length = 79;
    request.frames_per_symbol = 1920.0;
    request.frequency_hz = TxFreqSpinBox - m_XIT;
    request.tone_spacing = toneSpacing;
  }
  if (m_mode == "FT4") {
    toneSpacing=-2.0;
    request.symbols_length = 105;
    request.frames_per_symbol = 576.0;
    request.frequency_hz = TxFreqSpinBox - m_XIT;
    request.tone_spacing = toneSpacing;
  }
  if (m_mode == "FST4" or m_mode == "FST4W") {
    toneSpacing=-2.0;
    int nsps=fst4Nsps (m_TRperiod);
    int hmod=1;
    if(x2ToneSpacing) hmod=2;
    if(x4ToneSpacing) hmod=4;
    double dfreq=hmod*12000.0/nsps;
    double f0=WSPRfreqSpinBox - m_XIT;
    if(m_mode=="FST4") f0=TxFreqSpinBox - m_XIT;
    if(!m_tune) f0 += 1.5*dfreq;
    request.symbols_length = 160;
    request.frames_per_symbol = double(nsps);
    request.frequency_hz = f0;
    request.tone_spacing = toneSpacing;
  }
  if (m_mode == "Q65") {
    int nsps=1800;
    if(m_TRperiod==30) nsps=3600;
    if(m_TRperiod==60) nsps=7200;
    if(m_TRperiod==120) nsps=16000;
    if(m_TRperiod==300) nsps=41472;
    int mode65=pow(2.0,double(m_nSubMode));
    toneSpacing=mode65*12000.0/nsps;
    request.symbols_length = 85;
    request.frames_per_symbol = double(nsps);
    request.frequency_hz = TxFreqSpinBox - m_XIT;
    request.tone_spacing = toneSpacing;
  }
  if (m_mode == "JT9") {
    int nsub=pow(2,m_nSubMode);
    int nsps[]={480,240,120,60};
    double sps=6912;
    m_toneSpacing=nsub*12000.0/6912.0;
    if(x2ToneSpacing) m_toneSpacing=2.0*m_toneSpacing;
    if(x4ToneSpacing) m_toneSpacing=4.0*m_toneSpacing;
    bool fastmode=false;
    if(m_bFast9 and (m_nSubMode>=4)) {
      fastmode=true;
      sps=nsps[m_nSubMode-4];
      m_toneSpacing=12000.0/sps;
    }
    request.symbols_length = 85;
    request.frames_per_symbol = sps;
    request.frequency_hz = TxFreqSpinBox - m_XIT;
    request.tone_spacing = m_toneSpacing;
    request.fast_mode = fastmode;
  }
  if (m_mode == "MSK144") {
    int m_nsps=6;
    double f0=1000.0;
    m_toneSpacing=6000.0/m_nsps;
    int nsym;
    nsym=NUM_MSK144_SYMBOLS;
    if(itone[40] < 0) nsym=40;
    request.symbols_length = nsym;
    request.frames_per_symbol = double(m_nsps);
    request.frequency_hz = f0;
    request.tone_spacing = m_toneSpacing;
    request.fast_mode = true;
  }
  if (m_mode == "JT4") {
    if(m_nSubMode==0) toneSpacing=4.375;
    if(m_nSubMode==1) toneSpacing=2*4.375;
    if(m_nSubMode==2) toneSpacing=4*4.375;
    if(m_nSubMode==3) toneSpacing=9*4.375;
    if(m_nSubMode==4) toneSpacing=18*4.375;
    if(m_nSubMode==5) toneSpacing=36*4.375;
    if(m_nSubMode==6) toneSpacing=72*4.375;
    request.symbols_length = 206;
    request.frames_per_symbol = 2520.0*12000.0/11025.0;
    request.frequency_hz = TxFreqSpinBox - m_XIT;
    request.tone_spacing = toneSpacing;
  }
  if (m_mode=="WSPR") {
    int nToneSpacing=1;
    if(x2ToneSpacing) nToneSpacing=2;
    if(x4ToneSpacing) nToneSpacing=4;
    request.symbols_length = 162;
    request.frames_per_symbol = 8192.0;
    request.frequency_hz = TxFreqSpinBox - 1.5 * 12000 / 8192;
    request.tone_spacing = m_toneSpacing*nToneSpacing;
  }
  if(m_mode=="Echo") {
    unsigned int numEchoSymbols=6;
    double framesPerSymbol=4096;
    double freq=1500.0+m_fDither;
    double toneSpacing=0.0;

    if(rbEchoMessage or rbEchoCW) {

      if(rbEchoCW) {
        freq=1500.0;
        int ifreq=freq;
        auto const message = leEchoMessage.toLatin1();
        gen_cw_wave_(message.constData(), &ifreq, foxcom_.wave, (FCL)message.size());
      } else {
        toneSpacing=row.spacing;
        int nsps4=4*framesPerSymbol;                           //48000 Hz sampling
        int nsym=numEchoSymbols;
        float fsample=48000.0;
        int nwave=nsym*nsps4;
        int icmplx=0;
        float f0=freq;
        genwave_(const_cast<int *>(itone),&nsym,&nsps4,&nwave,
             &fsample,&toneSpacing,&f0,&icmplx,foxcom_.wave,foxcom_.wave);
      }
      toneSpacing=-5.0;  //Flag Modulator to use precomputed foxcom_.wave[].
    }

    request.symbols_length = numEchoSymbols;
    request.frames_per_symbol = framesPerSymbol;
    request.frequency_hz = freq;
    request.tone_spacing = toneSpacing;
    request.synchronize = false;
  }

  Sink sink;
  Modulator modulator {TX_SAMPLE_RATE, NTMAX};
  modulator.setTRPeriod (m_TRperiod);
  qint64 last = -1;
  QObject::connect (&modulator, &Modulator::txSourceCommitted,
                    [&] (TxEvidence::TxStartSnapshot snapshot) {
                      if (snapshot.target_known) last = snapshot.committed_end_sample;
                    });
  TestModulator::startAt (modulator, request, &sink, 0);
  // Reads frames from the clock ms; the Modulator returns fewer when it goes
  // idle.
  auto const read = [&] (qint64 frames, qint64 ms) {
    QVector<qint16> samples (static_cast<int> (frames));
    auto const bytes = TestModulator::readDataAt (modulator, reinterpret_cast<char *> (samples.data ()),
                                                  frames * qint64 (sizeof (qint16)), ms);
    samples.resize (int (bytes / qint64 (sizeof (qint16))));
    result.audio += samples;
    return samples.size ();
  };
  if (m_tune) {
    read (qint64 (row.seconds) * TX_SAMPLE_RATE, 0);
  } else if (!request.cw_id.isEmpty ()) {
    unsigned const message = request.fast_mode ? unsigned ((m_TRperiod - 5.0) * 48000.0) + 48
      : unsigned (request.symbols_length * 4.0 * request.frames_per_symbol) + 1;
    if (read (message, 0) == int (message)) read (30 * TX_SAMPLE_RATE, request.fast_mode ? message / 48 : 0);
  } else if (last >= 0) {
    if (read (last + 1, 0) != last + 1) result.audio.clear ();
  }
  modulator.stop (true);
  return result;
}

// The codec request's freq for row: its lowest tone, where MainWindow::transmit
// puts tone 0.
double lowestTone (Row const& row)
{
  auto const mode = row.mode == "TUNE" ? row.tuneMode : row.mode;
  if (mode == "WSPR") return row.txFreq - 1.5 * 12000 / 8192;
  if (mode == "MSK144") return 1000;
  if (mode == "ECHO") return row.kind == "cw" ? 1500 : 1500 + row.dither;
  return row.txFreq;
}

// Whether row's text is a single tone, which takes no freq.
bool single (Row const& row)
{
  return (row.mode == "JT4" || row.mode == "JT9" || row.mode == "JT65" || row.mode == "Q65")
    && row.text.trimmed ().startsWith ('@');
}

// --- the stream ---------------------------------------------------------------

void appendLittleEndian (QByteArray& bytes, quint32 value, int size)
{
  for (int i = 0; i < size; ++i) bytes.append (char ((value >> (8 * i)) & 0xffu));
}

QByteArray frame (quint8 type, QByteArray const& body)
{
  QByteArray bytes;
  bytes.append (char (type));
  appendLittleEndian (bytes, quint32 (body.size ()), 4);
  return bytes + body;
}

QByteArray control (QJsonObject const& object)
{
  return frame (0x02u, QJsonDocument {object}.toJson (QJsonDocument::Compact));
}

QByteArray header ()
{
  QByteArray bytes {"WSJT", 4};
  bytes.append (char (0));
  bytes.append (char (1));
  appendLittleEndian (bytes, 12, 2);
  return bytes;
}

QJsonObject request (QString const& verb, qint64 id, Row const& row)
{
  QJsonObject object {{"t", verb}, {"id", id}, {"mode", row.mode}};
  if (row.mode != "TUNE" && row.kind != "tone") object.insert ("text", row.text);
  // A Tune's submode and period are the GUI's mode's.
  if (row.submode >= 0 && row.mode != "TUNE") object.insert ("submode", QString {QChar ('A' + row.submode)});
  if (row.spacing != 1) object.insert ("spacing", row.spacing);
  if (row.trperiod && row.mode != "TUNE") object.insert ("trperiod", row.trperiod);
  if (row.copied) object.insert ("copied", true);
  if (row.fast) object.insert ("fast", true);
  if (!row.kind.isEmpty ()) object.insert ("kind", row.kind);
  return object;
}

QJsonObject render (qint64 id, Row const& row, int rate)
{
  auto object = request ("render", id, row);
  if (!single (row)) object.insert ("freq", lowestTone (row));
  object.insert ("rate", rate);
  if (row.seconds) object.insert ("seconds", row.seconds);
  if (!row.cwid.isEmpty ()) object.insert ("cwid", true);
  return object;
}

// The configure that gives the codec row's station call for its CW ID.
QByteArray station (Row const& row)
{
  return control ({{"t", "configure"}, {"mycall", row.cwid}});
}

struct Reply
{
  QJsonObject encoded;
  QVector<qint16> audio;
  QJsonObject terminal;         // packed, rendered or rejected
};

struct Run
{
  bool finished {};
  int exitCode {-1};
  QList<QJsonObject> events;    // stdout lines that are not replies
  QMap<qint64, Reply> replies;
  QString error;                // the first protocol violation found
  QByteArray standardError;
};

QVector<qint16> decodePcm (QString const& data)
{
  auto const bytes = QByteArray::fromBase64 (data.toLatin1 ());
  QVector<qint16> samples (bytes.size () / 2);
  for (int i = 0; i < samples.size (); ++i)
    samples[i] = qFromLittleEndian<qint16> (reinterpret_cast<uchar const *> (bytes.constData ()) + 2 * i);
  return samples;
}

// The documented members of an encoded line for row.
QStringList encodedKeys (Row const& row, bool rendered)
{
  auto const& mode = row.mode;
  QStringList keys {"id", "mode", "msgsent", "t", "tones", "v"};
  if (mode == "JT4" || mode == "JT9" || mode == "JT65" || mode == "MSK144") keys << "type";
  if (mode == "FT8" || mode == "FT4" || mode == "FST4" || mode == "FST4W" || mode == "Q65" || mode == "MSK144")
    keys << "fallback";
  if (mode == "JT4" || mode == "JT9" || mode == "JT65" || mode == "Q65") keys << "submode";
  if (mode == "FST4" || mode == "FST4W" || mode == "Q65" || mode == "MSK144" || row.fast) keys << "trperiod";
  if (mode == "FT8" || (mode == "JT9" && !row.fast) || mode == "FST4" || mode == "FST4W" || mode == "WSPR"
      || row.kind == "message") keys << "spacing";
  if (mode == "Q65") keys << "copied";
  if (row.fast) keys << "fast";
  if (!row.kind.isEmpty ()) keys << "kind";
  if (single (row) || rendered) keys << "freq";
  if (rendered) keys << "rate" << "samples";
  if (rendered && mode == "TUNE") keys << "seconds";
  if (rendered && !row.cwid.isEmpty ()) keys << "cwid";
  std::sort (keys.begin (), keys.end ());
  return keys;
}

// Every member an encoded line may carry.
QStringList const allEncodedKeys {"id", "mode", "msgsent", "t", "tones", "v", "type", "fallback", "submode",
  "trperiod", "spacing", "copied", "fast", "kind", "freq", "rate", "samples", "seconds", "cwid"};

// Parses the output strictly: each reply's lines are consecutive and in the
// documented order, and only replies carry an id.
void parse (QByteArray const& output, Run& run)
{
  auto lines = output.split ('\n');
  if (!lines.isEmpty () && lines.last ().isEmpty ()) lines.removeLast ();
  qint64 open = -1;
  int seq = 0;
  for (auto const& line : lines)
    {
      QJsonParseError error;
      auto const document = QJsonDocument::fromJson (line, &error);
      if (error.error != QJsonParseError::NoError || !document.isObject ())
        {
          run.error = "a line is not a JSON object: " + QString::fromUtf8 (line.left (80));
          return;
        }
      auto const object = document.object ();
      auto const type = object.value ("t").toString ();
      if (type != "encoded" && type != "pcm" && type != "packed" && type != "rendered" && type != "rejected")
        {
          if (open >= 0)
            {
              run.error = "a line interrupts a reply";
              return;
            }
          run.events << object;
          continue;
        }
      auto const id = qint64 (object.value ("id").toDouble ());
      if (object.value ("v").toInt () != 1 || !object.value ("id").isDouble ())
        {
          run.error = "a reply line lacks v or id";
          return;
        }
      auto& reply = run.replies[id];
      if (type == "encoded")
        {
          if (open >= 0 || !reply.encoded.isEmpty () || !reply.terminal.isEmpty ())
            {
              run.error = "an encoded line is out of place";
              return;
            }
          for (auto const& key : object.keys ())
            if (!allEncodedKeys.contains (key))
              {
                run.error = "an encoded line has an undocumented member: " + key;
                return;
              }
          reply.encoded = object;
          open = id;
          seq = 0;
        }
      else if (type == "pcm")
        {
          if (open != id || !reply.encoded.contains ("samples") || object.value ("seg").toInt (-1) != 0
              || object.value ("seq").toInt (-1) != seq++)
            {
              run.error = "a pcm line is out of place";
              return;
            }
          reply.audio += decodePcm (object.value ("data").toString ());
        }
      else
        {
          if (!reply.terminal.isEmpty () || (open >= 0 && open != id)
              || (type == "rejected") != reply.encoded.isEmpty ())
            {
              run.error = "a reply's last line is out of place";
              return;
            }
          if (type == "rejected")
            {
              auto keys = object.keys ();
              keys.removeOne ("key");
              std::sort (keys.begin (), keys.end ());
              if (keys != QStringList {"detail", "id", "reason", "t", "v"})
                {
                  run.error = "a rejected line's members are not the documented ones";
                  return;
                }
            }
          else
            {
              auto keys = object.keys ();
              std::sort (keys.begin (), keys.end ());
              auto const rendered = reply.encoded.contains ("samples");
              if (keys != QStringList {"id", "t", "v"} || type != (rendered ? "rendered" : "packed"))
                {
                  run.error = "a packed or rendered line is not the documented one";
                  return;
                }
              if (rendered && reply.audio.size () != reply.encoded.value ("samples").toInt ())
                {
                  run.error = "a render's pcm lines do not carry its samples";
                  return;
                }
            }
          reply.terminal = object;
          open = -1;
        }
    }
  if (open >= 0) run.error = "the output ends inside a reply";
}

QTemporaryDir& dataDirectory ()
{
  static QTemporaryDir directory;
  return directory;
}

Run runCodec (QByteArray const& input)
{
  Run run;
  if (!dataDirectory ().isValid ()) return run;
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.setWorkingDirectory (dataDirectory ().path ());
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                 {"-a", dataDirectory ().path (), "-t", dataDirectory ().path (), "--stream"});
  if (!process.waitForStarted (5000)) return run;
  process.write (header () + input + control ({{"t", "halt"}}));
  process.closeWriteChannel ();
  run.finished = process.waitForFinished (ProcessTimeoutMs);
  if (!run.finished)
    {
      process.kill ();
      process.waitForFinished (5000);
    }
  run.exitCode = process.exitCode ();
  run.standardError = process.readAllStandardError ();
  parse (process.readAllStandardOutput (), run);
  return run;
}

// stdout's lines, unparsed.
QList<QByteArray> runRaw (QByteArray const& input)
{
  QList<QByteArray> lines;
  if (!dataDirectory ().isValid ()) return lines;
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.setWorkingDirectory (dataDirectory ().path ());
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                 {"-a", dataDirectory ().path (), "-t", dataDirectory ().path (), "--stream"});
  if (!process.waitForStarted (5000)) return lines;
  process.write (header () + input + control ({{"t", "halt"}}));
  process.closeWriteChannel ();
  if (!process.waitForFinished (ProcessTimeoutMs)) process.kill ();
  lines = process.readAllStandardOutput ().split ('\n');
  if (!lines.isEmpty () && lines.last ().isEmpty ()) lines.removeLast ();
  return lines;
}

QByteArray audioFrames (QVector<qint16> const& samples)
{
  QByteArray bytes;
  for (int first = 0; first < samples.size (); first += 4096)
    {
      QByteArray body;
      for (int i = first; i < std::min (first + 4096, samples.size ()); ++i)
        appendLittleEndian (body, quint16 (samples[i]), 2);
      bytes += frame (0x01u, body);
    }
  return bytes;
}

QVector<qint16> every4th (QVector<qint16> const& audio)
{
  QVector<qint16> samples;
  for (int i = 0; i < audio.size (); i += 4) samples << audio[i];
  return samples;
}

// Independent sine synthesis may round adjacent int16 values at a sample boundary.
int firstDifference (QVector<qint16> const& a, QVector<qint16> const& b, int tolerance = 0)
{
  for (int i = 0; i < std::min (a.size (), b.size ()); ++i)
    if (std::abs (int (a[i]) - int (b[i])) > tolerance) return i;
  return a.size () == b.size () ? -1 : std::min (a.size (), b.size ());
}

// Whether encoded gives back row's settings, and on a render its freq and rate.
bool echoes (QJsonObject const& encoded, Row const& row, int rate)
{
  auto const value = [&] (char const * key) { return encoded.value (key); };
  if (encoded.keys () != encodedKeys (row, rate != 0)) return false;
  if (encoded.contains ("submode") && value ("submode") != QString {QChar ('A' + row.submode)}) return false;
  if (encoded.contains ("trperiod") && value ("trperiod").toInt () != row.trperiod) return false;
  if (encoded.contains ("spacing") && value ("spacing").toInt () != row.spacing) return false;
  if (encoded.contains ("copied") && value ("copied").toBool () != row.copied) return false;
  if (encoded.contains ("fast") && value ("fast").toBool () != row.fast) return false;
  if (encoded.contains ("kind") && value ("kind").toString () != row.kind) return false;
  if (encoded.contains ("seconds") && value ("seconds").toInt () != row.seconds) return false;
  if (encoded.contains ("cwid") && value ("cwid").toString () != row.cwid) return false;
  if (rate && ((!single (row) && value ("freq").toDouble () != lowestTone (row)) || value ("rate").toInt () != rate))
    return false;
  return true;
}

QVector<int> tones (QJsonObject const& encoded)
{
  QVector<int> result;
  for (auto const& tone : encoded.value ("tones").toArray ()) result << tone.toInt (-1);
  return result;
}

// --- the message tables -------------------------------------------------------

Row row (QString const& mode, QString const& text)
{
  Row r;
  r.mode = mode;
  r.text = text;
  return r;
}

Row with (Row r, int submode, int trperiod = 0, int spacing = 1)
{
  r.submode = submode;
  r.trperiod = trperiod;
  r.spacing = spacing;
  return r;
}

Row fallback (Row r)
{
  r.fallback = true;
  return r;
}

Row at (Row r, int txFreq)
{
  r.txFreq = txFreq;
  return r;
}

// The 77-bit message types (lib/77bit/packjt77.f90) every 77-bit mode sends.
QList<Row> messages77 (QString const& mode, int submode = -1, int trperiod = 0)
{
  auto const r = [&] (QString const& text) { return with (row (mode, text), submode, trperiod); };
  return {
    r ("CQ K1ABC FN42"),
    r ("K1ABC W9XYZ EN37"),
    r ("W9XYZ K1ABC -12"),
    r ("K1ABC W9XYZ R-07"),
    r ("W9XYZ K1ABC RR73"),
    r ("K1ABC W9XYZ RRR"),
    r ("K1ABC W9XYZ 73"),
    r ("cq k1abc fn42"),
    r ("TNX BOB 73 GL"),
    fallback (r ("THIS FREE TEXT IS LONGER THAN 13")),
    fallback (r ("K1ABC W9XYZ FN42 AND MORE TEXT ADDED!")),
    r ("CQ PJ4/K1ABC"),
    r ("<PJ4/K1ABC> W9XYZ"),
    r ("<W9XYZ> PJ4/K1ABC RR73"),
    r ("G4ABC/P PA9XYZ JO22"),
    r ("K1ABC W9XYZ 6A WI"),
    r ("K1ABC W9XYZ 579 WI"),
    r ("<PA3XYZ> <G4ABC/P> R 590003 IO91NP"),
    r ("K1ABC RR73; W9XYZ <KH1/KH7Z> -08"),
    r ("123456789ABCDEF012"),
    r ("{BAD} MESSAGE"),
  };
}

// JT4's, JT9's and JT65's message types (lib/packjt.f90 packmsg).
QList<Row> messages72 (QString const& mode, int submode)
{
  auto const r = [&] (QString const& text) { return with (row (mode, text), submode); };
  return {
    r ("CQ K1ABC FN42"),
    r ("K1ABC W9XYZ EN37"),
    r ("K1ABC W9XYZ -15"),
    r ("K1ABC W9XYZ R-15"),
    r ("K1ABC W9XYZ RRR"),
    r ("W9XYZ K1ABC 73"),
    r ("CQ PJ4/K1ABC"),
    r ("CQ K1ABC/P"),
    r ("tnx bob 73 gl"),
    r ("THIS IS LONGER THAN 13"),
    r ("K1ABC W9XYZ FN42 EXTRA"),
  };
}

}

class TestWsjtCodecSlotEncode : public QObject
{
  Q_OBJECT

private:
  QVector<QVector<Transmission>> m_expected;     // the GUI's, by row and request
  Run m_run;                                     // the codec's

  // Every row packed, then rendered at 12 and 48 kHz, in one codec process in
  // row order, each request against a generation by the GUI in turn: a
  // message's hashed calls change the tables later ones use.
  void parity (QList<Row> const& rows)
  {
    auto& expected = m_expected;
    expected.clear ();
    QByteArray input;
    QString call;
    QStringList jt65;                            // gen65's last message, by row
    for (int i = 0; i < rows.size (); ++i)
      {
        expected << QVector<Transmission> {gui (rows[i], false), gui (rows[i]), gui (rows[i])};
        jt65 << (rows[i].mode == "JT65" && !single (rows[i]) ? expected[i][0].msgsent
                 : jt65.isEmpty () ? QString {} : jt65.last ());
        if (!rows[i].cwid.isEmpty () && rows[i].cwid != call) input += station (rows[i]);
        if (!rows[i].cwid.isEmpty ()) call = rows[i].cwid;
        input += control (request ("pack", 3 * i, rows[i]));
        input += control (render (3 * i + 1, rows[i], 12000));
        input += control (render (3 * i + 2, rows[i], 48000));
      }
    m_run = runCodec (input);
    auto const& run = m_run;
    QVERIFY2 (run.finished, run.standardError.constData ());
    QCOMPARE (run.exitCode, 0);
    QVERIFY2 (run.error.isEmpty (), qPrintable (run.error));
    QVERIFY2 (run.events.size () == 1 && run.events[0].value ("t") == "ready",
              "the codec sent lines other than ready and replies");
    for (int i = 0; i < rows.size (); ++i)
      {
        auto const& row = rows[i];
        auto const where = QString {"%1 \"%2\""}.arg (row.mode, row.text);
        for (int k = 0; k < 3; ++k)
          {
            auto const& gui = expected[i][k];
            auto const reply = run.replies.value (3 * i + k);
            auto const what = where + QStringList {" pack", " render 12000", " render 48000"}[k];
            QVERIFY2 (gui.refused || k == 0 || !gui.audio.isEmpty (), qPrintable (what + ": the GUI committed no audio"));
            if (gui.refused)
              {
                QVERIFY2 (reply.terminal.value ("t") == "rejected", qPrintable (what + " was not rejected"));
                QCOMPARE (reply.terminal.value ("reason").toString (), QString {"bad_request"});
                QCOMPARE (reply.terminal.value ("key").toString (), QString {"text"});
                continue;
              }
            QVERIFY2 (!reply.encoded.isEmpty (),
                      qPrintable (what + ": " + QJsonDocument {reply.terminal}.toJson (QJsonDocument::Compact)));
            QCOMPARE (reply.encoded.value ("mode").toString (), row.mode);
            if (single (row))
              {
                // The single tone's frequency is the GUI's itone[0]. JT65's
                // encoder gives its last message, which the codec does not.
                QVERIFY2 (!gui.tones.isEmpty () && reply.encoded.value ("freq").toInt () == gui.tones[0],
                          qPrintable (what + ": freq"));
                QCOMPARE (tones (reply.encoded), QVector<int> {});
                if (row.mode == "JT65")
                  {
                    QCOMPARE (gui.msgsent, i > 0 ? jt65[i - 1] : QString {});
                    QCOMPARE (reply.encoded.value ("msgsent").toString (), QString {});
                  }
                else
                  {
                    QCOMPARE (reply.encoded.value ("msgsent").toString (), gui.msgsent);
                  }
              }
            else
              {
                QCOMPARE (reply.encoded.value ("msgsent").toString (), gui.msgsent);
                // The GUI keys Echo CW from its Morse, not from the tones it
                // makes of the message.
                QCOMPARE (tones (reply.encoded), row.kind == "cw" ? QVector<int> {} : gui.tones);
              }
            if (reply.encoded.contains ("type")) QCOMPARE (reply.encoded.value ("type").toInt (), gui.type);
            if (reply.encoded.contains ("fallback"))
              QVERIFY2 (reply.encoded.value ("fallback").toBool () == row.fallback, qPrintable (what + ": fallback"));
            QVERIFY2 (echoes (reply.encoded, row, k == 0 ? 0 : k == 1 ? 12000 : 48000),
                      qPrintable (what + ": " + QJsonDocument {reply.encoded}.toJson (QJsonDocument::Compact).left (200)));
            if (k == 0) continue;
            auto const audio = k == 1 ? every4th (gui.audio) : gui.audio;
            auto const precomputed = (row.mode == "FT8" && row.spacing == 1) || row.mode == "FT4"
              || row.mode == "FST4" || row.mode == "FST4W" || (row.mode == "ECHO" && row.kind != "tone");
            auto const difference = firstDifference (reply.audio, audio, precomputed ? 0 : 1);
            QVERIFY2 (difference < 0, qPrintable (QString {"%1: sample %2 of %3 (%4) differs from the GUI's (%5 of %6)"}
                                                  .arg (what).arg (difference).arg (reply.audio.size ())
                                                  .arg (reply.audio.value (difference)).arg (audio.value (difference))
                                                  .arg (audio.size ())));
          }
      }
  }

  // The codec's reply to a request, sent as written, expected to be refused.
  void refusal (QByteArray const& request, QString const& reason, QString const& key)
  {
    auto const run = runCodec (frame (0x02u, request));
    QVERIFY2 (run.error.isEmpty (), qPrintable (run.error));
    auto const what = request;
    // Every row's id is 1, which a request that is not UTF-8 still carries.
    auto const reply = run.replies.value (QJsonDocument::fromJson (request).object ().value ("id").toInt (1));
    QVERIFY2 (reply.terminal.value ("t") == "rejected", what.constData ());
    QVERIFY2 (reply.terminal.value ("reason").toString () == reason, what.constData ());
    QVERIFY2 (reply.terminal.value ("key").toString () == key && reply.terminal.contains ("key") == !key.isEmpty (),
              what.constData ());
  }

private slots:
  void ft8 ()
  {
    auto rows = messages77 ("FT8");
    rows << at (row ("FT8", "CQ K1ABC FN42"), 2200) << row ("FT8", " CQ K1ABC FN42")
         << with (row ("FT8", "K1ABC W9XYZ R-07"), -1, 0, 2)
         << at (with (row ("FT8", "CQ K1ABC FN42"), -1, 0, 4), 700);
    parity (rows);
  }

  void ft4 ()
  {
    auto rows = messages77 ("FT4");
    rows << at (row ("FT4", "CQ K1ABC FN42"), 3000) << row ("FT4", " CQ K1ABC FN42");
    parity (rows);
  }

  void fst4 ()
  {
    auto rows = messages77 ("FST4", -1, 15);
    rows << at (with (row ("FST4", "K1ABC W9XYZ -12"), -1, 30, 2), 1200)
         << with (row ("FST4", "W9XYZ K1ABC RR73"), -1, 60, 4)
         << with (row ("FST4", "CQ K1ABC FN42"), -1, 120)
         << with (row ("FST4", "K1ABC W9XYZ 73"), -1, 300)
         // The shared encoder's own blank strip skips characters; the codec
         // strips the outer blanks first.
         << with (row ("FST4", "  K1ABC W9XYZ EN37"), -1, 15)
         << with (row ("FST4", "    K1ABC W9XYZ EN37   "), -1, 15)
         << with (row ("FST4", QString {"K1ABC W9XYZ EN37"} + QChar (0) + "ZZ"), -1, 15);
    parity (rows);
  }

  void fst4w ()
  {
    auto const r = [] (QString const& text, int trperiod = 120, int spacing = 1)
    {
      return at (with (row ("FST4W", text), -1, trperiod, spacing), 1500);
    };
    parity ({r ("K1ABC FN42 37"), r ("PJ4/K1ABC 37"), r ("K1ABC/P 30"), r ("<PJ4/K1ABC> FN42AB"),
             r ("CQ K1ABC FN42"), r ("K1ABC FN42 23", 120, 2), r ("K1ABC FN42 0", 300, 4)});
  }

  void q65 ()
  {
    auto rows = messages77 ("Q65", 0, 30);
    auto const r = [] (QString const& text, int submode, int trperiod)
    {
      return with (row ("Q65", text), submode, trperiod);
    };
    for (int submode = 0; submode <= 2; ++submode) rows << r ("K1ABC W9XYZ -12", submode, 15);
    rows << r ("K1ABC W9XYZ R-07", 3, 30) << r ("W9XYZ K1ABC RR73", 4, 60) << r ("CQ K1ABC FN42", 5, 120)
         << r ("K1ABC W9XYZ 73", 0, 300);
    auto copied = r ("W9XYZ K1ABC R-10", 0, 60);
    copied.copied = true;
    rows << copied << r ("W9XYZ K1ABC R-10", 0, 60);
    parity (rows);
  }

  void msk144 ()
  {
    auto rows = messages77 ("MSK144", -1, 15);
    auto const r = [] (QString const& text, int trperiod = 15)
    {
      return with (row ("MSK144", text), -1, trperiod);
    };
    rows << r ("<K1ABC W9XYZ> R+06") << r ("<K1ABC W9XYZ> 73") << r ("<K1ABC W9XYZ> RRR")
         << r ("<K1ABC W9XYZ> XYZ") << r ("K1ABC W9XYZ R-07", 5) << r ("<K1ABC W9XYZ> -03", 10)
         << r ("W9XYZ K1ABC RR73", 30) << r ("  K1ABC W9XYZ EN37") << r ("     W9XYZ K1ABC RR73 ");
    parity (rows);
  }

  void jt4 ()
  {
    QList<Row> rows;
    auto const messages = messages72 ("JT4", 0);
    for (int i = 0; i < messages.size (); ++i) rows << with (messages[i], i % 7);
    parity (rows);
  }

  void jt9 ()
  {
    QList<Row> rows;
    auto const messages = messages72 ("JT9", 0);
    for (int i = 0; i < messages.size (); ++i) rows << with (messages[i], i % 8, 0, i < 8 ? 1 : 1 << (i - 8));
    parity (rows);
  }

  void jt65 ()
  {
    QList<Row> rows;
    auto const messages = messages72 ("JT65", 0);
    for (int i = 0; i < messages.size (); ++i) rows << with (messages[i], i % 3);
    for (auto const& text : {"K1ABC W9XYZ FN42 OOO", "RO", "RRR", "73"}) rows << with (row ("JT65", text), 1);
    parity (rows);
  }

  void wspr ()
  {
    auto const r = [] (QString const& text, int spacing = 1)
    {
      return with (row ("WSPR", text), -1, 0, spacing);
    };
    parity ({r ("K1ABC FN42 37"), r ("PJ4/K1ABC 37"), r ("K1ABC/P 30"), r ("K1ABC/10 0"),
             r ("<PJ4/K1ABC> FN42AB 37"), r ("w9xyz en37 60", 2), r ("K1ABC FN42 3", 4)});
  }

  // The Tune button in a mode, a steady tone at the mode's lowest tone, which
  // the GUI's Modulator ends itself after 9999 times the mode's samples per
  // symbol at 12 kHz, counted at 48 kHz (FT4: 120 s).
  void tune ()
  {
    auto const r = [] (QString const& tuneMode, int txFreq, int seconds, int trperiod = 0)
    {
      auto t = at (with (row ("TUNE", ""), -1, trperiod), txFreq);
      t.tuneMode = tuneMode;
      t.seconds = seconds;
      return t;
    };
    parity ({r ("FT8", 1500, 10), r ("FT4", 2350, 3), r ("MSK144", 1000, 4, 15), r ("WSPR", 1500, 2),
             r ("FST4", 1200, 2, 60), r ("JT65", 700, 2), r ("Q65", 1500, 1, 30), r ("JT9", 1000, 600)});
    if (QTest::currentTestFailed ()) return;
    auto const audio = gui (r ("FT4", 1500, 121)).audio;
    int const end = 9999 * 576;
    QCOMPARE (audio.size (), 121 * 48000);
    QVERIFY (std::any_of (audio.begin () + end - 100, audio.begin () + end + 1, [] (qint16 v) { return v != 0; }));
    QVERIFY (std::all_of (audio.begin () + end + 1, audio.end (), [] (qint16 v) { return v == 0; }));
  }

  // The GUI's Echo mode: its fixed tone, message and CW, at 1500 Hz dithered
  // as the GUI dithers it.
  void echo ()
  {
    auto const r = [] (QString const& kind, QString const& text, int spacing = 1, double dither = 0)
    {
      auto e = row ("ECHO", text);
      e.kind = kind;
      e.spacing = spacing;
      e.dither = dither;
      return e;
    };
    parity ({r ("tone", ""), r ("tone", "", 1, -10), r ("tone", "", 1, 9.99), r ("tone", "", 1, 3.25),
             r ("message", "K1JT", 10), r ("message", "W9XYZ", 30, -7.5), r ("message", "AB12CD", 15, 9.75),
             r ("message", "k1jt", 20), r ("message", "Q", 25), r ("cw", "K1JT"), r ("cw", "Q"),
             r ("cw", "123456"), r ("cw", "w9xyz")});
  }

  // The @ single tones of JT4, JT9, JT65 and Q65.
  void singleTones ()
  {
    auto fast = with (row ("JT9", "@1500"), 7, 15);
    fast.fast = true;
    parity ({with (row ("JT65", "@1000"), 0), with (row ("JT4", "@1000"), 0), with (row ("JT4", "@"), 2),
             with (row ("JT4", "@1000 HELLO"), 6), with (row ("JT4", "@ 700"), 1), with (row ("JT9", "@1200"), 0),
             with (row ("JT9", "@2500"), 3, 0, 2), fast, with (row ("JT65", "CQ K1ABC FN42"), 0),
             with (row ("JT65", "@1500"), 1), with (row ("JT65", "@ABCD"), 2), with (row ("JT65", "K1ABC W9XYZ -15"), 0),
             with (row ("JT65", "@800"), 0), with (row ("Q65", "@800"), 0, 30), with (row ("Q65", "@3000"), 2, 60),
             with (row ("Q65", "@"), 0, 15), with (row ("Q65", "@5950"), 0, 30),
             with (row ("JT4", "@12345"), 0)});
  }

  // JT9's fast submodes, E to H, at each period.
  void fastJt9 ()
  {
    QList<Row> rows;
    auto const messages = messages72 ("JT9", 4);
    for (int i = 0; i < messages.size (); ++i)
      {
        auto r = at (with (messages[i], 4 + i % 4, QList<int> {5, 10, 15, 30}[i % 4]), i % 2 ? 700 : 1500);
        r.fast = true;
        rows << r;
      }
    parity (rows);
  }

  // The GUI's CW ID of the station's call after the message, or in a fast
  // mode instead of its last 5 s.
  void cwId ()
  {
    auto const r = [] (Row r, QString const& call)
    {
      r.cwid = call;
      return r;
    };
    auto fastG = with (row ("JT9", "CQ K1ABC FN42"), 6, 10);
    fastG.fast = true;
    auto fastE = at (with (row ("JT9", "K1ABC W9XYZ RRR"), 4, 30), 700);
    fastE.fast = true;
    parity ({r (with (row ("JT4", "CQ K1ABC FN42"), 0), "KJ5HST"),
             r (with (row ("JT9", "K1ABC W9XYZ -15"), 0), "K1ABC"),
             r (with (row ("JT65", "CQ K1ABC FN42"), 1), "PJ4/K1ABC"),
             r (with (row ("Q65", "K1ABC W9XYZ R-07"), 0, 30), "KJ5HST"),
             r (with (row ("FST4", "CQ K1ABC FN42"), -1, 60), "K1ABC"),
             r (with (row ("FST4", "K1ABC W9XYZ 73"), -1, 15, 2), "KJ5HST"),
             r (with (row ("FST4W", "K1ABC FN42 37"), -1, 120), "K1ABC"),
             r (row ("WSPR", "K1ABC FN42 37"), "K1ABC"),
             r (with (row ("MSK144", "CQ K1ABC FN42"), -1, 15), "KJ5HST"),
             r (with (row ("MSK144", "K1ABC W9XYZ R-07"), -1, 5), "K1ABC"),
             r (with (row ("MSK144", "<K1ABC W9XYZ> R+06"), -1, 30), "VK9/KH6ABC"),
             r (fastG, "KJ5HST"),
             r (fastE, "3D2/W9XYZ/P"),
             // Morse's 250 elements, which the GUI's request cuts to 249, and 248.
             r (with (row ("JT9", "CQ K1ABC FN42"), 0), "00000000001"),
             r (with (row ("MSK144", "CQ K1ABC FN42"), -1, 15), "00000000002")});
  }

  // The CW ID's call comes from the last configure that named one, and is
  // refused, the detail naming the cause, unless a configure kept it whole
  // and Morse sends all of it.
  void cwIdStation ()
  {
    auto const configure = [] (QString const& call) {
      return control ({{"t", "configure"}, {"mycall", call}});
    };
    auto const jt65 = [] (qint64 id) {
      return control ({{"t", "render"}, {"id", id}, {"mode", "JT65"}, {"submode", "A"}, {"text", "CQ K1ABC FN42"},
                       {"freq", 1500}, {"rate", 12000}, {"cwid", true}});
    };
    auto const run = runCodec (jt65 (1) + configure ("K1#BC") + jt65 (2) + configure ("K1ABC") + jt65 (3)
                               + configure ("") + jt65 (4) + configure (QString (65, 'K')) + jt65 (5)
                               + configure ("ABCDEFGHIJ0123456789") + jt65 (6) + configure (" k1abc ") + jt65 (7)
                               + control ({{"t", "configure"}, {"mygrid", "FN42"}}) + jt65 (8)
                               + frame (0x02u, R"({"t":"configure","mycall":"K1\qBC"})") + jt65 (9)
                               + configure ("K1ABC") + jt65 (10));
    QVERIFY2 (run.error.isEmpty (), qPrintable (run.error));
    for (qint64 id : {1, 2, 4, 5, 6, 9})
      {
        auto const terminal = run.replies.value (id).terminal;
        QVERIFY2 (terminal.value ("reason") == "not_configured" && terminal.value ("key") == "mycall",
                  qPrintable (QString::number (id) + ": " + QJsonDocument {terminal}.toJson (QJsonDocument::Compact)));
      }
    struct Detail { qint64 id; char const * text; };
    QStringList wrong;
    for (auto const& d : {Detail {1, "no configure has set it"}, Detail {2, "a character Morse"},
                          Detail {5, "longer than the 64 characters a configure keeps"},
                          Detail {6, "250 Morse elements"}, Detail {9, "unreadable"}})
      {
        auto const detail = run.replies.value (d.id).terminal.value ("detail").toString ();
        if (!detail.contains (d.text)) wrong << QString::number (d.id) + ": " + detail;
      }
    QVERIFY2 (wrong.isEmpty (), qPrintable (wrong.join ("; ")));
    for (qint64 id : {3, 7, 8, 10})
      QCOMPARE (run.replies.value (id).encoded.value ("cwid").toString (), QString {"K1ABC"});
  }

  // The GUI's two sets of transmit hash tables, each kept across requests:
  // K0AAA and K0BAP have the same 12-bit hash, so after one of them a type 4
  // message with the other does not encode in the same set until a message
  // carries the other in full.
  void transmitState ()
  {
    QList<Row> rows {
      row ("FT8", "PJ4/K1ABC <K0AAA>"),
      row ("FT4", "PJ4/K1ABC <K0BAP>"),
      with (row ("Q65", "PJ4/K1ABC <K0BAP>"), 0, 15),
      with (row ("MSK144", "PJ4/K1ABC <K0AAA>"), -1, 5),
      row ("FT8", "PJ4/K1ABC <K0AAA>"),
      row ("FT4", "K0BAP W9XYZ EN37"),
      row ("FT4", "PJ4/K1ABC <K0BAP>"),
    };
    parity (rows);
    if (QTest::currentTestFailed ()) return;
    QVector<bool> refused;
    for (auto const& transmissions : m_expected) refused << transmissions[0].refused;
    QCOMPARE (refused, (QVector<bool> {false, true, false, true, false, false, false}));
    for (int i : {1, 3})
      QVERIFY2 (m_run.replies.value (3 * i).terminal.value ("detail").toString ().startsWith ("a hashed call in it"),
                qPrintable (QJsonDocument {m_run.replies.value (3 * i).terminal}.toJson (QJsonDocument::Compact)));
  }

  // ready names what pack and render take.
  void readyListsEncodes ()
  {
    auto const run = runCodec ({});
    QVERIFY (!run.events.isEmpty ());
    QCOMPARE (run.events[0].value ("encodes").toArray (),
              (QJsonArray {"FT8", "FT4", "JT4", "JT9", "JT65", "Q65", "FST4", "FST4W", "WSPR", "MSK144", "ECHO",
                           "TUNE", "JTTY"}));
  }

  // A request with no mode, or mode JTTY, is answered as JTTY's always was.
  void jttyUnchanged ()
  {
    QList<QByteArray> replies[3];
    QJsonObject const request {{"t", "pack"}, {"id", 7}, {"text", "CQ K1ABC FN42"}};
    auto const named = [&] (QString const& mode) { auto r = request; r.insert ("mode", mode); return r; };
    QJsonObject const requests[3] {request, named ("JTTY"), named ("jtty")};
    for (int i = 0; i < 3; ++i) replies[i] = runRaw (control (requests[i])).mid (1);
    QVERIFY (replies[0].size () >= 2);
    QVERIFY (replies[0].first ().contains ("\"t\":\"segment\""));
    QCOMPARE (replies[1], replies[0]);
    QCOMPARE (replies[2], replies[0]);
  }

  // A 12 kHz render decodes in the codec where the GUI would start it.
  void decodesItsOwnRender_data ()
  {
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<QString> ("text");
    QTest::addColumn<int> ("submode");
    QTest::addColumn<int> ("trperiod");
    QTest::newRow ("FT8") << "FT8" << "K1ABC W9XYZ EN37" << -1 << 15;
    QTest::newRow ("Q65-15A") << "Q65" << "CQ K1ABC FN42" << 0 << 15;
  }

  void decodesItsOwnRender ()
  {
    QFETCH (QString, mode);
    QFETCH (QString, text);
    QFETCH (int, submode);
    QFETCH (int, trperiod);
    auto r = with (row (mode, text), submode, mode == "Q65" ? trperiod : 0);
    auto const rendered = runCodec (control (render (1, r, 12000)));
    auto const reply = rendered.replies.value (1);
    QVERIFY2 (!reply.audio.isEmpty (), "no render");
    QVector<qint16> period (6000);                       // the GUI starts 0.5 s in
    period += reply.audio;
    period.resize (trperiod * 12000);
    QJsonObject configure {{"t", "configure"}, {"mode", mode}, {"trperiod", trperiod}, {"rxfreq", 1500},
                           {"nutc", 1200}};
    if (submode >= 0) configure.insert ("submode", submode);
    auto const lines = runRaw (control (configure) + audioFrames (period));
    QStringList messages;
    for (auto const& line : lines)
      {
        auto const event = QJsonDocument::fromJson (line).object ();
        if (event.value ("t") == "decode") messages << event.value ("message").toString ();
      }
    QVERIFY2 (messages.contains (reply.encoded.value ("msgsent").toString ()), qPrintable (messages.join ('|')));
  }

  // The decoders' subtraction generates FT8's and FT4's waveforms at 12 kHz in
  // the same process; a render after a decode is still the GUI's.
  void renderAfterDecode_data ()
  {
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<double> ("trperiod");
    QTest::addColumn<int> ("lead");
    QTest::newRow ("FT8") << "FT8" << 15. << 6000;
    QTest::newRow ("FT4") << "FT4" << 7.5 << 3600;
  }

  void renderAfterDecode ()
  {
    QFETCH (QString, mode);
    QFETCH (double, trperiod);
    QFETCH (int, lead);
    auto const r = row (mode, "K1ABC W9XYZ EN37");
    auto const signal = runCodec (control (render (1, row (mode, "W9XYZ K1ABC -12"), 12000)));
    auto audio = signal.replies.value (1).audio;
    QVERIFY (!audio.isEmpty ());
    QVector<qint16> period (lead);
    period += audio;
    period.resize (int (trperiod * 12000));
    QJsonObject const configure {{"t", "configure"}, {"mode", mode}, {"trperiod", trperiod}, {"rxfreq", 1500},
                                 {"nutc", 1200}, {"depth", 3}};
    auto const run = runCodec (control (configure) + audioFrames (period) + control (render (2, r, 48000)));
    QVERIFY2 (run.error.isEmpty (), qPrintable (run.error));
    QStringList messages;
    for (auto const& event : run.events)
      if (event.value ("t") == "decode") messages << event.value ("message").toString ();
    QVERIFY2 (messages.contains ("W9XYZ K1ABC -12"), qPrintable (messages.join ('|')));
    auto const expected = gui (r);
    QCOMPARE (firstDifference (run.replies.value (2).audio, expected.audio), -1);
  }

  void refusals_data ()
  {
    QTest::addColumn<QByteArray> ("request");
    QTest::addColumn<QString> ("reason");
    QTest::addColumn<QString> ("key");
    auto const add = [] (char const * tag, QByteArray const& json, QString const& reason, QString const& key)
    {
      QTest::newRow (tag) << json << reason << key;
    };
    add ("blank", R"({"t":"pack","id":1,"mode":"FT8","text":"   "})", "empty", "");
    add ("JT9 single tone 50", R"({"t":"pack","id":1,"mode":"JT9","submode":"A","text":"@50"})", "bad_request", "text");
    add ("Q65 single tone slash", R"({"t":"pack","id":1,"mode":"Q65","submode":"A","trperiod":30,"text":"@/"})",
         "bad_request", "text");
    add ("JT65 single tone null value", R"({"t":"pack","id":1,"mode":"JT65","submode":"A","text":"@2*"})",
         "bad_request", "text");
    add ("MSK144 single tone", R"({"t":"pack","id":1,"mode":"MSK144","trperiod":15,"text":"@1000"})",
         "bad_request", "text");
    for (auto text : {"K1ABC FN42", "K1ABC FN42 99", "K1ABC FN42 38", "HELLO WORLD", "K1ABC  FN42 37",
                      "<K1ABC FN42AB 37", "PJ4X/K1ABC 37", "K1ABC/QRP 37", "K1ABC FN42 037", "K1ABC FN42 3A", "PJ4/K1ABC X7",
                      "<PJ4/K1ABC> FN42 37"})
      add (text, R"({"t":"pack","id":1,"mode":"WSPR","text":")" + QByteArray {text} + R"("})", "bad_request", "text");
    add ("FST4W not WSPR", R"({"t":"pack","id":1,"mode":"FST4W","trperiod":120,"text":"CQ K1ABC FN42"})",
         "bad_request", "text");
    add ("JT65+JT9", R"({"t":"pack","id":1,"mode":"JT65+JT9","submode":"A","text":"CQ K1ABC FN42"})", "bad_request", "mode");
    add ("FT9", R"({"t":"pack","id":1,"mode":"FT9","text":"CQ K1ABC FN42"})", "bad_request", "mode");
    add ("FT8 blank", R"({"t":"pack","id":1,"mode":"FT8 ","text":"CQ K1ABC FN42"})", "bad_request", "mode");
    add ("mode number", R"({"t":"pack","id":1,"mode":8,"text":"CQ K1ABC FN42"})", "bad_request", "mode");
    add ("FT8 submode", R"({"t":"pack","id":1,"mode":"FT8","submode":"A","text":"CQ K1ABC FN42"})", "bad_request", "submode");
    add ("FT4 spacing", R"({"t":"pack","id":1,"mode":"FT4","spacing":2,"text":"CQ K1ABC FN42"})", "bad_request", "spacing");
    add ("JT65 no submode", R"({"t":"pack","id":1,"mode":"JT65","text":"CQ K1ABC FN42"})", "bad_request", "submode");
    add ("JT65 D", R"({"t":"pack","id":1,"mode":"JT65","submode":"D","text":"CQ K1ABC FN42"})", "bad_request", "submode");
    add ("JT9 8", R"({"t":"pack","id":1,"mode":"JT9","submode":8,"text":"CQ K1ABC FN42"})", "bad_request", "submode");
    add ("JT4 aa", R"({"t":"pack","id":1,"mode":"JT4","submode":"aa","text":"CQ K1ABC FN42"})", "bad_request", "submode");
    add ("Q65-15D", R"({"t":"pack","id":1,"mode":"Q65","submode":"D","trperiod":15,"text":"CQ K1ABC FN42"})",
         "bad_request", "submode");
    add ("Q65-60F", R"({"t":"pack","id":1,"mode":"Q65","submode":"F","trperiod":60,"text":"CQ K1ABC FN42"})",
         "bad_request", "submode");
    add ("Q65 no trperiod", R"({"t":"pack","id":1,"mode":"Q65","submode":"A","text":"CQ K1ABC FN42"})",
         "bad_request", "trperiod");
    add ("FST4 45", R"({"t":"pack","id":1,"mode":"FST4","trperiod":45,"text":"CQ K1ABC FN42"})", "bad_request", "trperiod");
    add ("FST4W 60", R"({"t":"pack","id":1,"mode":"FST4W","trperiod":60,"text":"K1ABC FN42 37"})", "bad_request", "trperiod");
    add ("MSK144 60", R"({"t":"pack","id":1,"mode":"MSK144","trperiod":60,"text":"CQ K1ABC FN42"})", "bad_request", "trperiod");
    add ("MSK144 15.0", R"({"t":"pack","id":1,"mode":"MSK144","trperiod":15.0,"text":"CQ K1ABC FN42"})",
         "bad_request", "trperiod");
    add ("FT8 trperiod", R"({"t":"pack","id":1,"mode":"FT8","trperiod":15,"text":"CQ K1ABC FN42"})", "bad_request", "trperiod");
    add ("FT8 copied", R"({"t":"pack","id":1,"mode":"FT8","copied":true,"text":"CQ K1ABC FN42"})", "bad_request", "copied");
    add ("Q65 copied yes", R"({"t":"pack","id":1,"mode":"Q65","submode":"A","trperiod":30,"copied":"yes","text":"CQ K1ABC FN42"})",
         "bad_request", "copied");
    add ("FT8 spacing 3", R"({"t":"pack","id":1,"mode":"FT8","spacing":3,"text":"CQ K1ABC FN42"})", "bad_request", "spacing");
    add ("FT8 spacing string", R"({"t":"pack","id":1,"mode":"FT8","spacing":"2","text":"CQ K1ABC FN42"})",
         "bad_request", "spacing");
    add ("template", R"({"t":"pack","id":1,"mode":"FT8","text":"CQ K1ABC FN42","template":"%M"})", "bad_request", "template");
    add ("serial", R"({"t":"pack","id":1,"mode":"FT8","text":"CQ K1ABC FN42","serial":5})", "bad_request", "serial");
    add ("no text", R"({"t":"pack","id":1,"mode":"FT8"})", "bad_request", "text");
    add ("text number", R"({"t":"pack","id":1,"mode":"FT8","text":5})", "bad_request", "text");
    add ("nested", R"({"t":"pack","id":1,"mode":"FT8","text":"CQ K1ABC FN42","x":{"a":1}})", "bad_request", "");
    add ("rate", R"({"t":"render","id":1,"mode":"FT8","text":"CQ K1ABC FN42","freq":1500,"rate":44100})",
         "bad_request", "rate");
    add ("freq low", R"({"t":"render","id":1,"mode":"FT8","text":"CQ K1ABC FN42","freq":100,"rate":12000})",
         "bad_request", "freq");
    add ("freq string", R"({"t":"render","id":1,"mode":"FT8","text":"CQ K1ABC FN42","freq":"1500","rate":12000})",
         "bad_request", "freq");
    add ("MSK144 freq", R"({"t":"render","id":1,"mode":"MSK144","trperiod":15,"text":"CQ K1ABC FN42","freq":1500,"rate":12000})",
         "bad_request", "freq");
    add ("FST4-900 render", R"({"t":"render","id":1,"mode":"FST4","trperiod":900,"text":"CQ K1ABC FN42","freq":1500,"rate":12000})",
         "too_long", "");
    add ("FT8 38 bytes", R"({"t":"pack","id":1,"mode":"FT8","text":"K1ABC W9XYZ FN42 AND MORE TEXT ADDED!!"})", "too_long", "text");
    add ("FT8 spaces inside", R"({"t":"pack","id":1,"mode":"FT8","text":"CQ                              KJ5HST EM18"})",
         "too_long", "text");
    add ("JT65 23 bytes", R"({"t":"pack","id":1,"mode":"JT65","submode":"A","text":"K1ABC W9XYZ FN42 EXTRA!"})", "too_long", "text");
    add ("WSPR 23 bytes", R"({"t":"pack","id":1,"mode":"WSPR","text":"<PJ4/K1ABCDE> FN42AB 37"})", "too_long", "text");
    for (auto text : {"A/K1 37", "AB/K1 37", "1A/K1 37", "PJ4/K1 37", "3D2/K1 37", "K1/A1 37", "K1A/A1 37", "K1 FN42 37",
                      "A1 FN42 37", "PJ4/A1 37", "3XA1BC FN42 37", "PJ4/3XA1B 37"})
      add (text, R"({"t":"pack","id":1,"mode":"WSPR","text":")" + QByteArray {text} + R"("})", "bad_request", "text");
    add ("freq high", R"({"t":"render","id":1,"mode":"FT8","text":"CQ K1ABC FN42","freq":5000.5,"rate":48000})",
         "bad_request", "freq");
    add ("Q65-120G", R"({"t":"pack","id":1,"mode":"Q65","submode":"G","trperiod":120,"text":"CQ K1ABC FN42"})",
         "bad_request", "submode");
    add ("Q65-60E at 4500, 12 kHz", R"({"t":"render","id":1,"mode":"Q65","submode":"E","trperiod":60,"text":"CQ K1ABC FN42","freq":4500,"rate":12000})",
         "bad_request", "freq");
    add ("JT9H x4 at 200, 12 kHz", R"({"t":"render","id":1,"mode":"JT9","submode":"H","spacing":4,"text":"CQ K1ABC FN42","freq":200,"rate":12000})",
         "bad_request", "freq");
    add ("Mode render", R"({"t":"render","id":1,"Mode":"FT8","text":"CQ K1ABC FN42","freq":1500,"rate":48000})", "bad_request", "Mode");
    add ("mdoe", R"({"t":"pack","id":1,"mdoe":"FT8","text":"CQ K1ABC FN42"})", "bad_request", "mdoe");
    add ("spacng", R"({"t":"pack","id":1,"mode":"FT8","spacng":4,"text":"CQ K1ABC FN42"})", "bad_request", "spacng");
    add ("Copied", R"({"t":"pack","id":1,"mode":"Q65","submode":"A","trperiod":30,"Copied":true,"text":"CQ K1ABC FN42"})",
         "bad_request", "Copied");
    add ("pack freq", R"({"t":"pack","id":1,"mode":"FT8","text":"CQ K1ABC FN42","freq":1500})", "bad_request", "freq");
    add ("JTTY zz", R"({"t":"pack","id":1,"text":"CQ K1ABC FN42","zz":1})", "bad_request", "zz");
    add ("JTTY pack rate", R"({"t":"pack","id":1,"text":"CQ K1ABC FN42","rate":12000})", "bad_request", "rate");
    add ("empty key", R"({"t":"pack","id":1,"mode":"FT8","text":"CQ K1ABC FN42","":1})", "bad_request", R"("")");
    add ("JTTY frequency", R"({"t":"pack","id":1,"text":"HI","frequency":1500})", "bad_request", "frequency");
    add ("FT8 frequency", R"({"t":"render","id":1,"mode":"FT8","text":"CQ K1ABC FN42","rate":12000,"frequency":1500})",
         "bad_request", "frequency");
    // Ordinary literals: moc misreads raw ones with unbalanced braces.
    add ("key not UTF-8", "{\"t\":\"pack\",\"id\":1,\"mode\":\"FT8\",\"text\":\"CQ K1ABC FN42\",\"k\xff\":1}",
         "bad_request", "k?");
    add ("escaped key", "{\"t\":\"pack\",\"id\":1,\"mode\":\"FT8\",\"\\u0074ext\":\"CQ\",\"text\":\"CQ K1ABC FN42\"}",
         "bad_request", "\\u0074ext");
    add ("WSPR type 3 power", R"({"t":"pack","id":1,"mode":"WSPR","text":"<K1ABC> FN42AB 5"})", "bad_request", "text");
    add ("JT4 23 bytes", R"({"t":"pack","id":1,"mode":"JT4","submode":"A","text":"K1ABC W9XYZ FN42 EXTRA!"})", "too_long", "text");
    add ("JT9 23 bytes", R"({"t":"pack","id":1,"mode":"JT9","submode":"A","text":"K1ABC W9XYZ FN42 EXTRA!"})", "too_long", "text");
    add ("Q65-60E at 4300, 12 kHz", R"({"t":"render","id":1,"mode":"Q65","submode":"E","trperiod":60,"text":"CQ K1ABC FN42","freq":4300,"rate":12000})",
         "bad_request", "freq");
    add ("FST4W-1800 render", R"({"t":"render","id":1,"mode":"FST4W","trperiod":1800,"text":"K1ABC FN42 37","freq":1500,"rate":48000})",
         "too_long", "");
    add ("JT4 single tone freq", R"({"t":"render","id":1,"mode":"JT4","submode":"A","text":"@1000","freq":1500,"rate":12000})",
         "bad_request", "freq");
    add ("Q65 single tone copied", R"({"t":"pack","id":1,"mode":"Q65","submode":"A","trperiod":30,"text":"@1000","copied":true})",
         "bad_request", "copied");
    add ("JT9 single tone cwid", R"({"t":"render","id":1,"mode":"JT9","submode":"A","text":"@1000","rate":12000,"cwid":true})",
         "bad_request", "cwid");
    add ("Q65 single tone 6000, 12 kHz", R"({"t":"render","id":1,"mode":"Q65","submode":"A","trperiod":30,"text":"@6000","rate":12000})",
         "bad_request", "text");
    add ("TUNE text", R"({"t":"pack","id":1,"mode":"TUNE","text":"TUNE"})", "bad_request", "text");
    add ("TUNE no seconds", R"({"t":"render","id":1,"mode":"TUNE","freq":1500,"rate":12000})", "bad_request", "seconds");
    add ("TUNE 0 s", R"({"t":"render","id":1,"mode":"TUNE","freq":1500,"rate":12000,"seconds":0})", "bad_request", "seconds");
    add ("TUNE 601 s", R"({"t":"render","id":1,"mode":"TUNE","freq":1500,"rate":12000,"seconds":601})", "bad_request", "seconds");
    add ("TUNE 1.5 s", R"({"t":"render","id":1,"mode":"TUNE","freq":1500,"rate":12000,"seconds":1.5})", "bad_request", "seconds");
    add ("TUNE pack seconds", R"({"t":"pack","id":1,"mode":"TUNE","seconds":5})", "bad_request", "seconds");
    add ("FT8 seconds", R"({"t":"render","id":1,"mode":"FT8","text":"CQ K1ABC FN42","freq":1500,"rate":12000,"seconds":5})",
         "bad_request", "seconds");
    add ("TUNE submode", R"({"t":"pack","id":1,"mode":"TUNE","submode":"A"})", "bad_request", "submode");
    add ("TUNE cwid", R"({"t":"render","id":1,"mode":"TUNE","freq":1500,"rate":12000,"seconds":1,"cwid":true})",
         "bad_request", "cwid");
    add ("ECHO no kind", R"({"t":"pack","id":1,"mode":"ECHO","text":"K1JT"})", "bad_request", "kind");
    add ("ECHO kind beep", R"({"t":"pack","id":1,"mode":"ECHO","kind":"beep"})", "bad_request", "kind");
    add ("ECHO tone 1510", R"({"t":"render","id":1,"mode":"ECHO","kind":"tone","freq":1510,"rate":12000})",
         "bad_request", "freq");
    add ("FT8 kind", R"({"t":"pack","id":1,"mode":"FT8","kind":"tone","text":"CQ K1ABC FN42"})", "bad_request", "kind");
    add ("ECHO tone text", R"({"t":"pack","id":1,"mode":"ECHO","kind":"tone","text":"K1JT"})", "bad_request", "text");
    add ("ECHO message no text", R"({"t":"pack","id":1,"mode":"ECHO","kind":"message","spacing":10})", "bad_request", "text");
    add ("ECHO message no spacing", R"({"t":"pack","id":1,"mode":"ECHO","kind":"message","text":"K1JT"})",
         "bad_request", "spacing");
    add ("ECHO message spacing 12", R"({"t":"pack","id":1,"mode":"ECHO","kind":"message","text":"K1JT","spacing":12})",
         "bad_request", "spacing");
    add ("ECHO tone spacing", R"({"t":"pack","id":1,"mode":"ECHO","kind":"tone","spacing":10})", "bad_request", "spacing");
    add ("ECHO cw spacing", R"({"t":"pack","id":1,"mode":"ECHO","kind":"cw","text":"K1JT","spacing":10})",
         "bad_request", "spacing");
    add ("ECHO message slash", R"({"t":"pack","id":1,"mode":"ECHO","kind":"message","text":"K1/JT","spacing":10})",
         "bad_request", "text");
    add ("ECHO cw 7", R"({"t":"pack","id":1,"mode":"ECHO","kind":"cw","text":"K1JTXYZ"})", "too_long", "text");
    add ("ECHO message blank", R"({"t":"pack","id":1,"mode":"ECHO","kind":"message","text":"  ","spacing":10})", "empty", "");
    add ("ECHO tone 1489.5", R"({"t":"render","id":1,"mode":"ECHO","kind":"tone","freq":1489.5,"rate":12000})",
         "bad_request", "freq");
    add ("ECHO message 1511", R"({"t":"render","id":1,"mode":"ECHO","kind":"message","text":"K1JT","spacing":10,"freq":1511,"rate":12000})",
         "bad_request", "freq");
    add ("ECHO cw 1501", R"({"t":"render","id":1,"mode":"ECHO","kind":"cw","text":"K1JT","freq":1501,"rate":12000})",
         "bad_request", "freq");
    add ("ECHO cwid", R"({"t":"render","id":1,"mode":"ECHO","kind":"tone","freq":1500,"rate":12000,"cwid":true})",
         "bad_request", "cwid");
    add ("FT8 fast", R"({"t":"pack","id":1,"mode":"FT8","fast":true,"text":"CQ K1ABC FN42"})", "bad_request", "fast");
    add ("JT9 fast yes", R"({"t":"pack","id":1,"mode":"JT9","fast":"yes","submode":"E","trperiod":15,"text":"CQ K1ABC FN42"})",
         "bad_request", "fast");
    add ("JT9 fast D", R"({"t":"pack","id":1,"mode":"JT9","fast":true,"submode":"D","trperiod":15,"text":"CQ K1ABC FN42"})",
         "bad_request", "submode");
    add ("JT9 fast no trperiod", R"({"t":"pack","id":1,"mode":"JT9","fast":true,"submode":"E","text":"CQ K1ABC FN42"})",
         "bad_request", "trperiod");
    add ("JT9 fast 60", R"({"t":"pack","id":1,"mode":"JT9","fast":true,"submode":"E","trperiod":60,"text":"CQ K1ABC FN42"})",
         "bad_request", "trperiod");
    add ("JT9 fast spacing", R"({"t":"pack","id":1,"mode":"JT9","fast":true,"submode":"E","trperiod":15,"spacing":2,"text":"CQ K1ABC FN42"})",
         "bad_request", "spacing");
    add ("JT9 slow trperiod", R"({"t":"pack","id":1,"mode":"JT9","submode":"A","trperiod":60,"text":"CQ K1ABC FN42"})",
         "bad_request", "trperiod");
    add ("JT9 fast H at 4500, 12 kHz", R"({"t":"render","id":1,"mode":"JT9","fast":true,"submode":"H","trperiod":15,"text":"CQ K1ABC FN42","freq":4500,"rate":12000})",
         "bad_request", "freq");
    add ("FT8 cwid", R"({"t":"render","id":1,"mode":"FT8","text":"CQ K1ABC FN42","freq":1500,"rate":12000,"cwid":true})",
         "bad_request", "cwid");
    add ("FT4 cwid", R"({"t":"render","id":1,"mode":"FT4","text":"CQ K1ABC FN42","freq":1500,"rate":12000,"cwid":true})",
         "bad_request", "cwid");
    add ("JT65 pack cwid", R"({"t":"pack","id":1,"mode":"JT65","submode":"A","text":"CQ K1ABC FN42","cwid":true})",
         "bad_request", "cwid");
    add ("JT65 cwid yes", R"({"t":"render","id":1,"mode":"JT65","submode":"A","text":"CQ K1ABC FN42","freq":1500,"rate":12000,"cwid":"yes"})",
         "bad_request", "cwid");
    add ("JT65 cwid unconfigured", R"({"t":"render","id":1,"mode":"JT65","submode":"A","text":"CQ K1ABC FN42","freq":1500,"rate":12000,"cwid":true})",
         "not_configured", "mycall");
  }

  void refusals ()
  {
    QFETCH (QByteArray, request);
    QFETCH (QString, reason);
    QFETCH (QString, key);
    refusal (request, reason, key);
  }

  // What is not a refusal: an FST4 pack at a period too long to render, a
  // lower-case mode and submode, a render at 48000 Hz whose top tone is above
  // 6000 Hz, WSPR compound calls that read back as written, and requests
  // without a usable id, which get an error instead.
  void accepted ()
  {
    auto const run = runCodec (
      frame (0x02u, R"({"t":"pack","id":1,"mode":"FST4","trperiod":900,"text":"CQ K1ABC FN42"})")
      + frame (0x02u, R"({"t":"pack","id":2,"mode":"fst4w","trperiod":1800,"text":"K1ABC FN42 37"})")
      + frame (0x02u, R"({"t":"pack","id":3,"mode":"Q65","submode":"b","trperiod":30,"text":"CQ K1ABC FN42"})")
      + frame (0x02u, R"({"t":"render","id":4,"mode":"JT9","submode":"H","spacing":4,"text":"CQ K1ABC FN42","freq":200,"rate":48000})")
      + frame (0x02u, R"({"t":"render","id":5,"mode":"Q65","submode":"E","trperiod":60,"text":"CQ K1ABC FN42","freq":4200,"rate":12000})")
      + frame (0x02u, R"({"t":"pack","id":6,"mode":"WSPR","text":"PJ4/K1ABC 37"})")
      + frame (0x02u, R"({"t":"pack","id":7,"mode":"WSPR","text":"1A/KH6AB 37"})")
      + frame (0x02u, R"({"t":"pack","id":8,"mode":"WSPR","text":"<PJ4/K1ABC> FN42AB 37"})")
      + frame (0x02u, R"({"t":"pack","id":9,"mode":"ECHO","kind":"Tone"})")
      + frame (0x02u, R"({"t":"pack","id":10,"mode":"Q65","submode":"A","trperiod":30,"text":"@1500","copied":false})")
      + frame (0x02u, R"({"t":"render","id":11,"mode":"Q65","submode":"A","trperiod":15,"text":"@1500","rate":12000,"cwid":false})")
      + frame (0x02u, R"({"t":"pack","id":1.5,"mode":"FT8","text":"CQ K1ABC FN42"})")
      + frame (0x02u, R"({"t":"render","mode":"FT8","text":"CQ K1ABC FN42","freq":1500,"rate":12000})"));
    QVERIFY2 (run.error.isEmpty (), qPrintable (run.error));
    QCOMPARE (run.replies.size (), 11);
    for (qint64 id = 1; id <= 11; ++id)
      QVERIFY2 (!run.replies.value (id).encoded.isEmpty (),
                qPrintable (QJsonDocument {run.replies.value (id).terminal}.toJson (QJsonDocument::Compact)));
    QCOMPARE (run.replies.value (1).encoded.value ("msgsent").toString (), QString {"CQ K1ABC FN42"});
    QCOMPARE (run.replies.value (2).encoded.value ("mode").toString (), QString {"FST4W"});
    QCOMPARE (run.replies.value (3).encoded.value ("submode").toString (), QString {"B"});
    for (qint64 id = 6; id <= 8; ++id)
      QCOMPARE (run.replies.value (id).encoded.value ("msgsent").toString (),
                QStringList ({"PJ4/K1ABC 37", "1A/KH6AB 37", "<PJ4/K1ABC> FN42AB 37"})[id - 6]);
    QCOMPARE (run.replies.value (9).encoded.value ("kind").toString (), QString {"tone"});
    QCOMPARE (run.events.size (), 3);
    for (int i = 1; i < 3; ++i)
      QCOMPARE (run.events[i].value ("code").toString (), QString {"invalid_request_id"});
  }
};

QTEST_GUILESS_MAIN (TestWsjtCodecSlotEncode)
#include "test_wsjt_codec_slot_encode.moc"
