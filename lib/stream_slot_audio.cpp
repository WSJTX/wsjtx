// SPDX-License-Identifier: GPL-3.0-or-later
// The audio the GUI plays for a transmission, for the stream's render
// (lib/streaming_encode.f90): the request MainWindow::transmit gives the
// Modulator, the waveforms MainWindow::guiUpdate and MainWindow::transmit
// precompute for it (widgets/mainwindow.cpp), and the samples
// Modulator::startAt and Modulator::readDataAt make of them
// (Modulator/Modulator.cpp), following the same timing and numerical
// conventions.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "wsjtx_config.h"
#include "waveform_kernels.h"

// Modulator.cpp's, which sees wsjtx_config.h.
#if defined (WSJT_SOFT_KEYING)
# define SOFT_KEYING WSJT_SOFT_KEYING
#else
# define SOFT_KEYING 1
#endif

extern "C" {
  void gen_ft8wave_ (int itone[], int * nsym, int * nsps, float * bt, float * fsample, float * f0,
                     float xjunk[], float wave[], int * icmplx, int * nwave);
  void stream_ft8wave_12k (std::int32_t const tones[], float frequency, float wave[]);
  void gen_ft4wave_ (int itone[], int * nsym, int * nsps, float * fsample, float * f0,
                     float xjunk[], float wave[], int * icmplx, int * nwave);
  void gen_fst4wave_ (int itone[], int * nsym, int * nsps, int * nwave, float * fsample,
                      int * hmod, float * f0, int * icmplx, float xjunk[], float wave[]);
  std::int32_t stream_fst4_wave_window (std::int32_t const itone[], std::int32_t nsym,
                                        std::int32_t nsps, float fsample, std::int32_t hmod,
                                        float f0, std::int32_t out_first, std::int32_t nout,
                                        float wave[], std::int32_t * first, double * phi);
}

namespace
{
  // jt9's mode numbers, and lib/streaming_control.f90's for the others.
  enum Mode : std::int32_t
  {
    JT4 = 4, FT4 = 5, FT8 = 8, JT9 = 9, JT65 = 65, Q65 = 66, MSK144 = 144,
    FST4 = 240, FST4W = 241, WSPR = 1001, ECHO = 1002, TUNE = 1003, FOX = 1004,
    SUPERFOX = 1005
  };

  // lib/streaming_encode.f90's stream_tx.
  struct Tx
  {
    std::int32_t mode;
    std::int32_t submode;
    std::int32_t spacing;       // Echo: Hz
    std::int32_t trperiod;
    std::int32_t nsym;          // MSK144: 40 for a short message
    std::int32_t fast;
    std::int32_t seconds;       // Tune
    std::int32_t kind;          // Echo: 1 fixed tone, 2 message, 3 CW
    std::int32_t rate;
    double freq;
    double wavefreq;            // FST4, FST4W: the freq the waveform is made at; 0 for freq
  };

  constexpr unsigned frameRate = 48000;     // TX_SAMPLE_RATE
  constexpr double twoPi = 2.0 * 3.141592653589793238462;
  constexpr std::int16_t rampIncrement = 64;  // RAMP_INCREMENT

  // Qt 5's qRound (double).
  int qtRound (double d)
  {
    return d >= 0.0 ? int (d + 0.5) : int (d - double (int (d - 1)) + 0.5) + int (d - 1);
  }

  // The TxEvidence::TxRequest members Modulator reads, without test noise.
  struct Request
  {
    bool valid {};
    unsigned symbols_length {};
    double frames_per_symbol {};
    double frequency_hz {};
    double tone_spacing {};                 // below 0: the precomputed waveform
    bool fast_mode {};
    double tr_period_s {};
    bool tuning {};
  };

  int fst4Nsps (int trperiod)
  {
    int nsps=720;
    if(trperiod==30) nsps=1680;
    if(trperiod==60) nsps=3888;
    if(trperiod==120) nsps=8200;
    if(trperiod==300) nsps=21504;
    if(trperiod==900) nsps=66560;
    if(trperiod==1800) nsps=134400;
    return nsps;
  }

  // MainWindow::transmit's request; tx.freq is the lowest tone.
  Request transmitRequest (Tx const& tx)
  {
    Request request;
    request.valid = true;
    request.frequency_hz = tx.freq;
    request.tr_period_s = tx.trperiod;
    double toneSpacing=0.0;
    switch (tx.mode)
      {
      case JT65:
        if(tx.submode==0) toneSpacing=11025.0/4096.0;
        if(tx.submode==1) toneSpacing=2*11025.0/4096.0;
        if(tx.submode==2) toneSpacing=4*11025.0/4096.0;
        request.symbols_length = 126;
        request.frames_per_symbol = 4096.0*12000.0/11025.0;
        request.tone_spacing = toneSpacing;
        request.tr_period_s = 60;
        break;
      case FT8:
        toneSpacing=-3;
        if(tx.spacing==2) toneSpacing=2*12000.0/1920.0;
        if(tx.spacing==4) toneSpacing=4*12000.0/1920.0;
        request.symbols_length = 79;
        request.frames_per_symbol = 1920.0;
        request.tone_spacing = toneSpacing;
        request.tr_period_s = 15;
        break;
      case FT4:
        request.symbols_length = 105;
        request.frames_per_symbol = 576.0;
        request.tone_spacing = -2.0;
        request.tr_period_s = 7.5;
        break;
      case FST4:
      case FST4W:
        {
          int nsps=fst4Nsps (tx.trperiod);
          double dfreq=tx.spacing*12000.0/nsps;
          request.symbols_length = 160;
          request.frames_per_symbol = double(nsps);
          request.frequency_hz = tx.freq + 1.5*dfreq;
          request.tone_spacing = -2.0;
        }
        break;
      case Q65:
        {
          int nsps=1800;
          if(tx.trperiod==30) nsps=3600;
          if(tx.trperiod==60) nsps=7200;
          if(tx.trperiod==120) nsps=16000;
          if(tx.trperiod==300) nsps=41472;
          int mode65=std::pow(2.0,double(tx.submode));
          toneSpacing=mode65*12000.0/nsps;
          request.symbols_length = 85;
          request.frames_per_symbol = double(nsps);
          request.tone_spacing = toneSpacing;
        }
        break;
      case JT9:
        {
          int nsub=std::pow(2,tx.submode);
          int nsps[]={480,240,120,60};
          double sps=6912;
          double toneSpacing9=nsub*12000.0/6912.0;
          if(tx.spacing==2) toneSpacing9=2.0*toneSpacing9;
          if(tx.spacing==4) toneSpacing9=4.0*toneSpacing9;
          bool fastmode=false;
          if(tx.fast and (tx.submode>=4)) {
            fastmode=true;
            sps=nsps[tx.submode-4];
            toneSpacing9=12000.0/sps;
          }
          request.symbols_length = 85;
          request.frames_per_symbol = sps;
          request.tone_spacing = toneSpacing9;
          request.fast_mode = fastmode;
          if (!fastmode) request.tr_period_s = 60;
        }
        break;
      case MSK144:
        request.symbols_length = tx.nsym == 40 ? 40 : 144;
        request.frames_per_symbol = double(6);
        request.frequency_hz = 1000.0;
        request.tone_spacing = 6000.0/6;
        request.fast_mode = true;
        break;
      case JT4:
        if(tx.submode==0) toneSpacing=4.375;
        if(tx.submode==1) toneSpacing=2*4.375;
        if(tx.submode==2) toneSpacing=4*4.375;
        if(tx.submode==3) toneSpacing=9*4.375;
        if(tx.submode==4) toneSpacing=18*4.375;
        if(tx.submode==5) toneSpacing=36*4.375;
        if(tx.submode==6) toneSpacing=72*4.375;
        request.symbols_length = 206;
        request.frames_per_symbol = 2520.0*12000.0/11025.0;
        request.tone_spacing = toneSpacing;
        request.tr_period_s = 60;
        break;
      case WSPR:
        request.symbols_length = 162;
        request.frames_per_symbol = 8192.0;
        request.tone_spacing = 12000.0/8192.0*tx.spacing;
        request.tr_period_s = 120;
        break;
      case ECHO:
        request.symbols_length = 6;
        request.frames_per_symbol = 4096;
        request.tone_spacing = tx.kind == 1 ? 0.0 : -5.0;
        request.tr_period_s = 3.0;
        break;
      case FOX:
        request.symbols_length = 79;
        request.frames_per_symbol = 1920.0;
        request.tone_spacing = -1;
        request.tr_period_s = 15;
        break;
      case SUPERFOX:
        request.symbols_length = 153;         // NUM_SUPERFOX_SYMBOLS
        request.frames_per_symbol = 1024.0;
        request.frequency_hz = 750.0;
        request.tone_spacing = -1;
        request.tr_period_s = 15;
        break;
      case TUNE:
        // The carrier is the same in every mode; a WSPR request's own limit,
        // 9999 x 8192 samples at 48000 Hz (1706 s), is longer than any render.
        request.symbols_length = 162;
        request.frames_per_symbol = 8192.0;
        request.tone_spacing = 12000.0/8192.0;
        request.tr_period_s = 120;
        request.tuning = true;
        break;
      default:
        request.valid = false;
      }
    return request;
  }

  // The last sample of the message Modulator::readDataAt commits.
  unsigned lastSample (Request const& request)
  {
    unsigned i1 = request.symbols_length * 4.0 * request.frames_per_symbol;
    if (request.fast_mode) i1 = request.tr_period_s*48000.0 - 24000.0;
    return i1;
  }

  // In a fast mode the GUI replaces the message with the CW ID at the first
  // audio buffer it reads more than TR - 5 s into the period, the first at or
  // after this sample of a transmission that starts with the period.
  unsigned fastCwStart (Request const& request)
  {
    return unsigned ((request.tr_period_s - 5.0) * 48000.0) + 48;
  }

  // What a range render keeps of the Modulator between ranges: its members
  // that change while it plays, the frequency the client gave last, and the
  // FST4 waveform generator's block and phase.
  struct State
  {
    std::int64_t pos {};                    // the next sample at 48000 Hz
    std::uint32_t ic {};
    double phi {}, dphi {}, amp {}, frequency {}, frequency0 {}, toneFrequency0 {}, given {};
    std::uint32_t isym0 {};
    std::int16_t ramp {};
    bool cwLevel {}, fastCwActive {}, idle {};
    std::int32_t genFirst {-1};
    double genPhi {};
    std::uint32_t genF0 {}, genTones {};
  };

  // Modulator::startAt, then Modulator::readDataAt for a transmission without
  // synchronization, frequency spread or test noise: the tones and the
  // waveform are the GUI's itone[] and foxcom_.wave[], zero past their ends
  // in a fresh GUI process. The waveform may be a window of it, from sample
  // waveFirst.
  class Modulator
  {
  public:
    Modulator (Request const& request, std::int32_t const * tones, std::int32_t ntones,
               std::int32_t const * cw, std::int32_t ncw)
      : m_tones {tones}
      , m_ntones {ntones}
      , m_cwId (cw, cw + ncw)
      , m_symbolsLength {request.symbols_length}
      , m_nsps {request.frames_per_symbol}
      , m_frequency {request.frequency_hz}
      , m_toneSpacing {request.tone_spacing}
      , m_TRperiod {request.tr_period_s}
      , m_tuning {request.tuning}
      , m_bFastMode {request.fast_mode}
    {
    }

    bool idle () const {return m_idle;}
    bool fastCwActive () const {return m_fastCwActive;}

    void setWave (float const * wave, std::size_t first, std::size_t n)
    {
      m_wave = wave;
      m_waveFirst = first;
      m_nwave = n;
    }

    // Modulator::setFrequency.
    void setFrequency (double frequency) {m_frequency = frequency;}

    // Modulator::startAt's late start: the message from sample ic.
    void startAt (unsigned ic) {m_ic = ic;}

    void save (State& state) const
    {
      state.ic = m_ic;
      state.phi = m_phi;
      state.dphi = m_dphi;
      state.amp = m_amp;
      state.frequency = m_frequency;
      state.frequency0 = m_frequency0;
      state.toneFrequency0 = m_toneFrequency0;
      state.isym0 = m_isym0;
      state.ramp = m_ramp;
      state.cwLevel = m_cwLevel;
      state.fastCwActive = m_fastCwActive;
      state.idle = m_idle;
    }

    // As saved; a fast mode's CW ID sets what Modulator::readDataAt sets
    // for it.
    void restore (State const& state)
    {
      m_ic = state.ic;
      m_phi = state.phi;
      m_dphi = state.dphi;
      m_amp = state.amp;
      m_frequency = state.frequency;
      m_frequency0 = state.frequency0;
      m_toneFrequency0 = state.toneFrequency0;
      m_isym0 = state.isym0;
      m_ramp = state.ramp;
      m_cwLevel = state.cwLevel;
      m_fastCwActive = state.fastCwActive;
      m_idle = state.idle;
      m_continuing = true;
      if (m_fastCwActive)
        {
          m_symbolsLength=126;
          m_nsps=4096.0*12000.0/11025.0;
          m_nspd=2560;
          if(m_cwId.size ()*m_nspd/48000.0 > 4.0) m_nspd=4.0*48000.0/m_cwId.size ();
        }
    }

    // Appends up to frames samples from utcMs, which a transmission that
    // starts with the period reads at the sample count it has made, and stops
    // where the GUI's buffer would carry on with silence or the transmission
    // ends.
    void read (std::vector<std::int16_t>& out, std::size_t frames, std::int64_t utcMs)
    {
      if(m_nsps==6) {
        m_frequency=1000.0;
        m_frequency0=1000.0;
      }
      std::size_t const end = out.size () + frames;

      unsigned int isym=0;

      if(!m_tuning) isym=m_ic/(4.0*m_nsps);
      bool slowCwId=((isym >= m_symbolsLength) && !m_cwId.empty ()) && (!m_bFastMode);
      // A range that goes on from one that stopped just before the
      // message's last sample sends it, as the whole render does; the GUI
      // starting there (a late start) goes to the CW ID.
      if(m_continuing && m_ic <= unsigned (m_symbolsLength * 4.0 * m_nsps)) slowCwId=false;
      if(m_TRperiod==3.0) slowCwId=false;
      bool fastCwId=false;
      float tsec=0.001*(utcMs % int(1000*m_TRperiod));
      if(m_bFastMode and !m_cwId.empty () and (tsec > (m_TRperiod-5.0))) fastCwId=true;
      if(!m_bFastMode) m_nspd=2560;

      if(slowCwId or fastCwId) {
        m_dphi = twoPi*m_frequency/frameRate;
        if(m_bFastMode and !m_fastCwActive) {
          m_frequency=1500;
          m_dphi = twoPi*m_frequency/frameRate;
          m_symbolsLength=126;
          m_nsps=4096.0*12000.0/11025.0;
          m_ic=2246949;
          m_nspd=2560;
          if(m_cwId.size ()*m_nspd/48000.0 > 4.0) m_nspd=4.0*48000.0/m_cwId.size ();
        }
        m_fastCwActive=m_bFastMode;
        unsigned ic0 = m_symbolsLength * 4 * m_nsps;
        unsigned j(0);

        while (out.size () != end) {
          j = (m_ic - ic0)/m_nspd + 1;
          if (j == 0 || j > static_cast<unsigned> (m_cwId.size ()))
            {
              m_idle = true;
              return;
            }
          bool level {bool (m_cwId.at (j - 1))};
          m_phi += m_dphi;
          if (m_phi > twoPi) m_phi -= twoPi;
          std::int16_t sample=0;
          float amp=32767.0;
          float x=0;
          if(m_ramp!=0) {
            // Qt 5's qSin: the double sin of the float phase.
            x=std::sin(double(float(m_phi)));
            if(SOFT_KEYING) {
              amp=std::abs(std::int32_t(m_ramp));
              if(amp>32767.0) amp=32767.0;
            }
            sample=std::round(amp*x);
          }
          if(m_bFastMode) {
            sample=0;
            if(level) sample=32767.0*x;
          }
          out.push_back (sample);
          ++m_ic;

          if ((m_ramp != 0 && m_ramp != std::numeric_limits<std::int16_t>::min ()) || level != m_cwLevel) {
            m_ramp += rampIncrement;
          }
          m_cwLevel = level;
        }
        return;
      } else {
        m_fastCwActive=false;
      }

      double const baud (12000.0 / m_nsps);
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

      std::int16_t sample;

      while (out.size () != end && m_ic <= i1) {
        isym=0;
        if(!m_tuning and m_TRperiod!=3.0) isym=m_ic/(4.0*m_nsps);
        if(m_bFastMode) isym=isym%m_symbolsLength;
        if (isym != m_isym0 || m_frequency != m_frequency0) {
          if(tone (0)>=100) {
            m_toneFrequency0=tone (0);
          } else {
            if(m_toneSpacing==0.0) {
              m_toneFrequency0=m_frequency + tone (isym)*baud;
            } else {
              m_toneFrequency0=m_frequency + tone (isym)*m_toneSpacing;
            }
          }
          m_dphi = twoPi * m_toneFrequency0 / frameRate;
          m_isym0 = isym;
          m_frequency0 = m_frequency;
        }

        m_phi += m_dphi;
        if (m_phi > twoPi) m_phi -= twoPi;
        if (m_ic > i0) m_amp = 0.98 * m_amp;
        if (m_ic > i1) m_amp = 0.0;

        if(!m_tuning and (m_toneSpacing < 0) and (tone (0)<100)) {
          m_amp=32767.0;
          sample=qtRound(m_amp*(m_ic >= m_waveFirst && m_ic - m_waveFirst < m_nwave
                                 ? m_wave[m_ic - m_waveFirst] : 0.f));
        } else {
          sample=qtRound(m_amp*std::sin(m_phi));
        }
        out.push_back (sample);
        ++m_ic;
      }

      if(m_TRperiod==3 and m_ic >i1) {
        m_idle = true;
        return;
      }
      if (m_amp == 0.0) {
        if (m_cwId.empty ()) {
          m_idle = true;
          return;
        }
        m_phi = 0.0;
      }
      m_frequency0 = m_frequency;
    }

  private:
    int tone (unsigned i) const {return i < unsigned (m_ntones) ? m_tones[i] : 0;}

    std::int32_t const * m_tones;
    std::int32_t m_ntones;
    float const * m_wave {};
    std::size_t m_waveFirst {};
    std::size_t m_nwave {};
    std::vector<int> m_cwId;
    unsigned m_symbolsLength;
    double m_nsps;
    double m_frequency;
    double m_frequency0 {0.};
    double m_toneSpacing;
    double m_TRperiod;
    bool m_tuning;
    bool m_bFastMode;
    unsigned m_nspd = 2048 + 512;
    double m_phi {0.};
    double m_dphi {0.};
    double m_amp = std::numeric_limits<std::int16_t>::max ();
    std::int16_t m_ramp {0};
    bool m_cwLevel {false};
    bool m_fastCwActive {false};
    unsigned m_ic {0};
    unsigned m_isym0 = std::numeric_limits<unsigned>::max ();
    double m_toneFrequency0 {1500.0};
    bool m_idle {false};
    bool m_continuing {false};
  };

  // MainWindow::guiUpdate's waveform for FT8, FT4, FST4 and FST4W, one sample
  // longer than the message so that Modulator reads zero past it.
  std::vector<float> precomputed (Tx const& tx, std::int32_t const tones[], unsigned i1)
  {
    std::vector<int> itone (tones, tones + (tx.mode == FT8 ? 79 : tx.mode == FT4 ? 103 : 160));
    std::vector<float> wave;
    int icmplx=0;
    if (tx.mode == FT8)
      {
        int nsym=79;
        int nsps=4*1920;
        float fsample=48000.0;
        float bt=2.0;
        float f0=tx.freq;
        int nwave=nsym*nsps;
        wave.assign (std::max (std::size_t (nwave), std::size_t (i1) + 1), 0.f);
        gen_ft8wave_(itone.data (),&nsym,&nsps,&bt,&fsample,&f0,wave.data (),
                     wave.data (),&icmplx,&nwave);
      }
    else if (tx.mode == FT4)
      {
        int nsym=103;
        int nsps=4*576;
        float fsample=48000.0;
        float f0=tx.freq;
        int nwave=(nsym+2)*nsps;
        wave.assign (std::max (std::size_t (nwave), std::size_t (i1) + 1), 0.f);
        gen_ft4wave_(itone.data (),&nsym,&nsps,&fsample,&f0,wave.data (),
                     wave.data (),&icmplx,&nwave);
      }
    else
      {
        int hmod=tx.spacing;
        int nsps=fst4Nsps (tx.trperiod);
        nsps=4*nsps;
        int nsym=160;
        float fsample=48000.0;
        float dfreq=hmod*fsample/nsps;
        float f0=tx.freq + 1.5*dfreq;
        int nwave=(nsym+2)*nsps;
        wave.assign (std::max (std::size_t (nwave), std::size_t (i1) + 1), 0.f);
        gen_fst4wave_(itone.data (),&nsym,&nsps,&nwave,
                      &fsample,&hmod,&f0,&icmplx,wave.data (),wave.data ());
      }
    return wave;
  }

  std::int64_t samples48 (Tx const& tx, Request const& request, std::int32_t ncw)
  {
    if (request.tuning) return std::int64_t (tx.seconds) * frameRate;
    auto const i1 = std::int64_t (lastSample (request));
    if (ncw <= 0) return i1 + 1;
    if (request.fast_mode)
      {
        // Modulator::readDataAt's CW ID in a fast mode, from its m_ic.
        unsigned nspd=2560;
        if(ncw*nspd/48000.0 > 4.0) nspd=4.0*48000.0/ncw;
        unsigned const ic0 = 126u * 4 * (4096.0*12000.0/11025.0);
        return std::int64_t (fastCwStart (request)) + std::int64_t (ic0) + std::int64_t (ncw) * nspd - 2246949;
      }
    return i1 + std::int64_t (ncw) * 2560;
  }

  // A Tune's longest render: the GUI's longest Tune, FST4 at 1800 s, stops
  // after 9999 x 134400 samples at 48000 Hz (27,997.2 s).
  constexpr std::int64_t tuneLimit = 9999LL * 134400;

  // FNV-1a of the tones, which say whether a saved FST4 generator phase is
  // still this message's.
  std::uint32_t toneSum (std::int32_t const tones[], std::int32_t ntones)
  {
    std::uint32_t h = 2166136261u;
    for (int i = 0; i < ntones; ++i)
      for (int b = 0; b < 4; ++b)
        {
          h ^= (std::uint32_t (tones[i]) >> (8 * b)) & 0xffu;
          h *= 16777619u;
        }
    return h;
  }

  std::uint64_t bits (double d) {std::uint64_t u; std::memcpy (&u, &d, sizeof u); return u;}
  double fromBits (std::uint64_t u) {double d; std::memcpy (&d, &u, sizeof d); return d;}
  std::uint32_t bits (float f) {std::uint32_t u; std::memcpy (&u, &f, sizeof u); return u;}

  // The state's text: version 1, the transmission it belongs to, then the
  // state, each a lower-case hex number, joined by '.'; doubles as their
  // 64-bit patterns, so that it comes back exactly.
  constexpr int stateFields = 26;
  constexpr int fingerprintFields = 9;

  std::string encodeState (State const& st, Tx const& tx, std::int32_t const cw[], std::int32_t ncw)
  {
    std::uint64_t const v[stateFields] = {
      1, std::uint32_t (tx.mode), std::uint32_t (tx.submode), std::uint32_t (tx.spacing),
      std::uint32_t (tx.trperiod), std::uint32_t (tx.fast), std::uint32_t (tx.kind),
      std::uint32_t (tx.rate), std::uint32_t (ncw), toneSum (cw, ncw), std::uint64_t (st.pos), st.ic,
      bits (st.phi), bits (st.dphi), bits (st.amp), bits (st.frequency), bits (st.frequency0),
      bits (st.toneFrequency0), bits (st.given), st.isym0, std::uint16_t (st.ramp),
      std::uint64_t (st.cwLevel) | std::uint64_t (st.fastCwActive) << 1 | std::uint64_t (st.idle) << 2,
      std::uint32_t (st.genFirst), bits (st.genPhi), st.genF0, st.genTones};
    std::string text;
    char field[24];
    for (int i = 0; i < stateFields; ++i)
      {
        std::snprintf (field, sizeof field, i ? ".%llx" : "%llx", (unsigned long long) v[i]);
        text += field;
      }
    return text;
  }

  // 0 with st read from text, -1 when it is not a state, -2 when it is
  // another transmission's.
  int decodeState (char const text[], std::int32_t n, Tx const& tx, std::int32_t const cw[], std::int32_t ncw,
                   State& st)
  {
    std::uint64_t v[stateFields];
    int field = 0;
    std::int32_t i = 0;
    while (field < stateFields)
      {
        std::int32_t const start = i;
        std::uint64_t value = 0;
        while (i < n && text[i] != '.')
          {
            char const c = text[i];
            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (digit < 0 || i - start >= 16) return -1;
            value = value << 4 | std::uint64_t (digit);
            ++i;
          }
        if (i == start) return -1;
        v[field++] = value;
        if (i < n) ++i;                 // the '.'
        if ((i == n) != (field == stateFields) || (i == n && text[n - 1] == '.')) return -1;
      }
    if (v[0] != 1) return -1;
    std::uint64_t const want[fingerprintFields] = {
      std::uint32_t (tx.mode), std::uint32_t (tx.submode), std::uint32_t (tx.spacing),
      std::uint32_t (tx.trperiod), std::uint32_t (tx.fast), std::uint32_t (tx.kind),
      std::uint32_t (tx.rate), std::uint32_t (ncw), toneSum (cw, ncw)};
    for (int k = 0; k < fingerprintFields; ++k)
      if (v[k + 1] != want[k]) return -2;
    std::uint64_t const * const f = v + fingerprintFields + 1;   // the state proper
    auto const finite = [] (double d, double limit) {return std::isfinite (d) && std::abs (d) <= limit;};
    st.pos = std::int64_t (f[0]);
    st.ic = std::uint32_t (f[1]);
    st.phi = fromBits (f[2]);
    st.dphi = fromBits (f[3]);
    st.amp = fromBits (f[4]);
    st.frequency = fromBits (f[5]);
    st.frequency0 = fromBits (f[6]);
    st.toneFrequency0 = fromBits (f[7]);
    st.given = fromBits (f[8]);
    st.isym0 = std::uint32_t (f[9]);
    st.ramp = std::int16_t (std::uint16_t (f[10]));
    st.cwLevel = f[11] & 1;
    st.fastCwActive = f[11] & 2;
    st.idle = f[11] & 4;
    st.genFirst = std::int32_t (std::uint32_t (f[12]));
    st.genPhi = fromBits (f[13]);
    st.genF0 = std::uint32_t (f[14]);
    st.genTones = std::uint32_t (f[15]);
    if (f[0] > std::uint64_t (tuneLimit) || f[1] > 0x7fffffffu || f[9] > 0xffffffffu
        || f[10] > 0xffffu || f[11] > 7 || f[12] > 0xffffffffu || f[14] > 0xffffffffu
        || f[15] > 0xffffffffu || st.genFirst < -1
        || !finite (st.phi, 8) || !finite (st.dphi, 8) || !finite (st.amp, 32767)
        || st.amp < 0 || !finite (st.frequency, 1e6) || !finite (st.frequency0, 1e6)
        || !finite (st.toneFrequency0, 1e6) || !finite (st.given, 1e6)
        || !finite (st.genPhi, 65536) || st.genPhi < 0 || st.genPhi >= 65536)
      return -1;
    return 0;
  }

  // The samples from to from+count-1 at tx.rate of the GUI's audio, every
  // fourth of its 48000 Hz samples at 12000 Hz, into samples: continuing from
  // in when given, else with the Modulator started at from (the GUI's late
  // start; at 0 the transmission's start). from+count must not pass the
  // transmission's end. out, when given, gets what the next range continues
  // from. Returns the count, or 0 when capacity is too small or there is no
  // memory.
  std::int32_t renderRange (Tx const& tx, std::int32_t const tones[], std::int32_t ntones,
                            float const wave[], std::int32_t nwave,
                            std::int32_t const cw[], std::int32_t ncw,
                            std::int64_t from, std::int64_t count, State const * in, State * out,
                            std::int16_t * samples, std::int32_t capacity)
  {
    auto const request = transmitRequest (tx);
    if (!request.valid || (tx.rate != 12000 && tx.rate != 48000)) return 0;
    int const step = tx.rate == 48000 ? 1 : 4;
    auto const total = samples48 (tx, request, ncw);
    auto const pos = from * step;
    auto const n48 = std::min<std::int64_t> (count * step, total - pos);
    if (from < 0 || count <= 0 || count > capacity || n48 <= 0 || (n48 + step - 1) / step != count)
      return 0;
    if (out) *out = State {};

    if (request.tuning)
      {
        if (total > tuneLimit) return 0;
        int const tone = ntones > 0 ? tones[0] : 0;
        double const frequency = tone >= 100 ? tone : request.frequency_hz + tone * request.tone_spacing;
        double const dphi = twoPi * frequency / frameRate;
        double phi = in ? in->phi : 0;
        constexpr int block = 256;
        float buffer[block];
        for (std::int64_t i = 0; i < count; i += block)
          {
            int const n = int (std::min<std::int64_t> (block, count - i));
            wsjt_render_tone (&phi, dphi, n, step, buffer);
            for (int j = 0; j < n; ++j)
              samples[i + j] = qtRound (32767.0 * buffer[j]);
          }
        if (out)
          {
            out->pos = pos + n48;
            out->ic = std::uint32_t (pos + n48);
            out->phi = phi;
            out->amp = std::numeric_limits<std::int16_t>::max ();
            out->frequency = frequency;
            out->given = tx.freq;
          }
        return std::int32_t (count);
      }

    auto const i1 = lastSample (request);
    std::vector<float> made;
    float const * w = wave;
    std::size_t wFirst = 0, wCount = std::size_t (std::max (nwave, 0));
    State gen;
    if (request.tone_spacing < 0 && nwave <= 0 && (tx.mode == FST4 || tx.mode == FST4W))
      {
        // As precomputed, but only the window this range reads. The
        // waveform is made at wavefreq when given: after a Tx frequency
        // typed into the GUI's box, the GUI's Modulator::setFrequency (freq)
        // without the waveform made again.
        Tx madeAt = tx;
        if (tx.wavefreq > 0) madeAt.freq = tx.wavefreq;
        int const hmod=tx.spacing;
        int const nsps=4*fst4Nsps (tx.trperiod);
        float const fsample=48000.0;
        float const dfreq=hmod*fsample/nsps;
        float const f0=madeAt.freq + 1.5*dfreq;
        gen.genF0 = bits (f0);
        gen.genTones = toneSum (tones, 160);
        // Sample i1, past the message, is zero.
        auto const end = std::min<std::int64_t> (pos + n48, std::int64_t (i1));
        if (pos < end)
          {
            std::int32_t first = 0;
            double phi = 0;
            if (in && in->genFirst >= 0 && in->genF0 == gen.genF0 && in->genTones == gen.genTones)
              {
                first = in->genFirst;
                phi = in->genPhi;
              }
            made.assign (std::size_t (end - pos), 0.f);
            auto status = stream_fst4_wave_window (tones, 160, nsps, fsample, hmod, f0, std::int32_t (pos),
                                                   std::int32_t (end - pos), made.data (), &first, &phi);
            if (status != 0)
              {
                first = 0;
                phi = 0;
                status = stream_fst4_wave_window (tones, 160, nsps, fsample, hmod, f0, std::int32_t (pos),
                                                  std::int32_t (end - pos), made.data (), &first, &phi);
              }
            if (status == 0)
              {
                wFirst = std::size_t (pos);
                gen.genFirst = first;
                gen.genPhi = phi;
              }
            else
              {
                made = precomputed (madeAt, tones, i1);
              }
            w = made.data ();
            wCount = made.size ();
          }
      }
    else if (request.tone_spacing < 0 && nwave <= 0 && (tx.mode == FT8 || tx.mode == FT4))
      {
        made = precomputed (tx, tones, i1);
        w = made.data ();
        wCount = made.size ();
      }

    Modulator modulator {request, tones, ntones, cw, ncw};
    modulator.setWave (w, wFirst, wCount);
    if (in)
      {
        modulator.restore (*in);
        if (tx.freq != in->given) modulator.setFrequency (tx.freq);   // the GUI's retune
      }
    else
      {
        modulator.startAt (unsigned (pos));
      }
    std::vector<std::int16_t> audio;
    audio.reserve (std::size_t (n48));
    auto const fastStart = std::int64_t (fastCwStart (request));
    std::int64_t at = pos;
    while (std::int64_t (audio.size ()) < n48 && !modulator.idle ())
      {
        auto want = n48 - std::int64_t (audio.size ());
        // In a fast mode the CW ID starts with the first buffer read more
        // than TR - 5 s into the period, the clock being the sample count.
        if (ncw > 0 && request.fast_mode && !modulator.fastCwActive () && at < fastStart)
          want = std::min (want, fastStart - at);
        auto const before = audio.size ();
        modulator.read (audio, std::size_t (want), at / 48);
        at += std::int64_t (audio.size () - before);
        if (audio.size () == before) break;
      }
    if (std::int64_t (audio.size ()) != n48) return 0;
    std::int32_t k = 0;
    for (std::size_t i = 0; i < audio.size (); i += step) samples[k++] = audio[i];
    if (out)
      {
        modulator.save (*out);
        out->pos = pos + n48;
        out->given = tx.freq;
        out->genFirst = gen.genFirst;
        out->genPhi = gen.genPhi;
        out->genF0 = gen.genF0;
        out->genTones = gen.genTones;
      }
    return k;
  }
}

extern "C"
{
  // The transmission's highest tone in Hz, or 0 for no such transmission;
  // tones[0] at 100 or more is the GUI's single tone in Hz.
  double stream_slot_top_tone (Tx const * tx, std::int32_t const tones[], std::int32_t ntones) noexcept
  {
    auto const request = transmitRequest (*tx);
    if (!request.valid) return 0;
    if (ntones > 0 && tones[0] >= 100) return tones[0];
    double highest = 0;
    for (int i = 0; i < ntones; ++i) highest = std::max (highest, double (tones[i]));
    switch (tx->mode)
      {
      case FT8: return tx->freq + 7 * (request.tone_spacing < 0 ? 12000.0 / 1920 : request.tone_spacing);
      case FT4: return tx->freq + 3 * 12000.0 / 576;
      case FST4: case FST4W: return tx->freq + 3 * tx->spacing * 12000.0 / request.frames_per_symbol;
      case JT4: case WSPR: return request.frequency_hz + 3 * request.tone_spacing;
      case JT9: return request.frequency_hz + 8 * request.tone_spacing;
      case JT65: return request.frequency_hz + 65 * request.tone_spacing;
      case Q65: return request.frequency_hz + 64 * request.tone_spacing;
      case ECHO: return tx->freq + highest * tx->spacing;
      case TUNE: return tx->freq;
      default: return request.frequency_hz + request.tone_spacing;    // MSK144
      }
  }

  // The samples a render at tx.rate gives with a CW ID of ncw elements, or 0
  // for no such transmission.
  std::int32_t stream_slot_samples (Tx const * tx, std::int32_t ncw) noexcept
  {
    auto const request = transmitRequest (*tx);
    if (!request.valid || (tx->rate != 12000 && tx->rate != 48000)) return 0;
    auto const last = samples48 (*tx, request, ncw) - 1;
    auto const count = tx->rate == 48000 ? last + 1 : last / 4 + 1;
    return count > std::numeric_limits<std::int32_t>::max () ? 0 : std::int32_t (count);
  }

  // The GUI's audio at 48 kHz, or every fourth sample of it at 12 kHz, into
  // samples.
  // tones are the GUI's itone[], wave (nwave samples, or none for the
  // waveforms made here) its precomputed waveform, cw its CW ID; returns the
  // count, or 0 when capacity is too small or there is no memory.
  std::int32_t stream_slot_render (Tx const * tx, std::int32_t const tones[], std::int32_t ntones,
                                   float const wave[], std::int32_t nwave,
                                   std::int32_t const cw[], std::int32_t ncw,
                                   std::int16_t * samples, std::int32_t capacity) noexcept
  {
    try
      {
        auto const count = stream_slot_samples (tx, ncw);
        if (count <= 0 || count > capacity) return 0;
        if (tx->mode == FT8 && tx->rate == 12000 && tx->spacing == 1
            && ntones >= 79 && tones[0] < 100 && ncw == 0 && nwave <= 0)
          {
            constexpr std::int32_t waveCount = 79 * 1920;
            if (capacity < waveCount + 1) return 0;
            std::vector<float> wave12 (waveCount);
            stream_ft8wave_12k (tones, float (tx->freq), wave12.data ());
            for (std::int32_t i = 0; i < waveCount; ++i)
              samples[i] = qtRound (32767.0 * wave12[i]);
            samples[waveCount] = 0;
            return waveCount + 1;
          }
        return renderRange (*tx, tones, ntones, wave, nwave, cw, ncw, 0, count, nullptr, nullptr,
                            samples, capacity);
      }
    catch (...)
      {
        return 0;
      }
  }

  // The render's samples from to from+count-1 at tx.rate (from+count at most
  // its stream_slot_samples), into samples; with nstate bytes of state, a
  // range's next (none when nstate is 0), it continues that range, else it
  // starts the Modulator at from. next gets this range's next, *nnext its
  // length (0 when the transmission ends with it). Returns the count, 0 when
  // it did not render, -1 for a state it cannot read, -2 for another
  // transmission's, -3 for one that continues at another sample.
  std::int32_t stream_slot_render_range (Tx const * tx, std::int32_t const tones[], std::int32_t ntones,
                                         float const wave[], std::int32_t nwave,
                                         std::int32_t const cw[], std::int32_t ncw,
                                         std::int32_t from, std::int32_t count,
                                         char const state[], std::int32_t nstate,
                                         std::int16_t * samples, std::int32_t capacity,
                                         char next[], std::int32_t capacity_next,
                                         std::int32_t * nnext) noexcept
  {
    try
      {
        *nnext = 0;
        State in, out;
        if (nstate > 0)
          {
            int const status = decodeState (state, nstate, *tx, cw, ncw, in);
            if (status) return status;
            if (in.pos != std::int64_t (from) * (tx->rate == 48000 ? 1 : 4)) return -3;
          }
        auto const n = renderRange (*tx, tones, ntones, wave, nwave, cw, ncw, from, count,
                                    nstate > 0 ? &in : nullptr, &out, samples, capacity);
        if (n <= 0) return 0;
        if (out.pos < samples48 (*tx, transmitRequest (*tx), ncw))
          {
            auto const text = encodeState (out, *tx, cw, ncw);
            if (std::int32_t (text.size ()) > capacity_next) return 0;
            std::memcpy (next, text.data (), text.size ());
            *nnext = std::int32_t (text.size ());
          }
        return n;
      }
    catch (...)
      {
        return 0;
      }
  }
}
