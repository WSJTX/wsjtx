// SPDX-License-Identifier: GPL-3.0-or-later
// The audio the GUI plays for a slot-mode transmission, for the stream's
// render (lib/streaming_encode.f90): the request MainWindow::transmit gives
// the Modulator, the waveforms MainWindow::guiUpdate precomputes for it
// (widgets/mainwindow.cpp), and the samples Modulator::readDataAt makes of
// them (Modulator/Modulator.cpp) from the first to the last it commits, with
// the arithmetic in the same form so that the samples are the same.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

extern "C" {
  void gen_ft8wave_ (int itone[], int * nsym, int * nsps, float * bt, float * fsample, float * f0,
                     float xjunk[], float wave[], int * icmplx, int * nwave);
  void gen_ft4wave_ (int itone[], int * nsym, int * nsps, float * fsample, float * f0,
                     float xjunk[], float wave[], int * icmplx, int * nwave);
  void gen_fst4wave_ (int itone[], int * nsym, int * nsps, int * nwave, float * fsample,
                      int * hmod, float * f0, int * icmplx, float xjunk[], float wave[]);
}

namespace
{
  // jt9's mode numbers, and lib/streaming_control.f90's for WSPR.
  enum Mode : std::int32_t
  {
    JT4 = 4, FT4 = 5, FT8 = 8, JT9 = 9, JT65 = 65, Q65 = 66, MSK144 = 144,
    FST4 = 240, FST4W = 241, WSPR = 1001
  };

  constexpr unsigned frameRate = 48000;     // TX_SAMPLE_RATE

  // Qt 5's qRound (double).
  int qtRound (double d)
  {
    return d >= 0.0 ? int (d + 0.5) : int (d - double (int (d - 1)) + 0.5) + int (d - 1);
  }

  // The TxEvidence::TxRequest members Modulator reads for a transmission
  // without tuning, CW ID or test noise.
  struct Request
  {
    bool valid {};
    unsigned symbols_length {};
    double frames_per_symbol {};
    double frequency_hz {};
    double tone_spacing {};                 // below 0: the precomputed waveform
    bool fast_mode {};
    double tr_period_s {};
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

  // MainWindow::transmit's request; freq is the lowest tone.
  Request transmitRequest (std::int32_t mode, std::int32_t nsym, std::int32_t submode,
                           std::int32_t spacing, std::int32_t trperiod, double freq)
  {
    Request request;
    request.valid = true;
    request.frequency_hz = freq;
    request.tr_period_s = trperiod;
    double toneSpacing=0.0;
    switch (mode)
      {
      case JT65:
        if(submode==0) toneSpacing=11025.0/4096.0;
        if(submode==1) toneSpacing=2*11025.0/4096.0;
        if(submode==2) toneSpacing=4*11025.0/4096.0;
        request.symbols_length = 126;
        request.frames_per_symbol = 4096.0*12000.0/11025.0;
        request.tone_spacing = toneSpacing;
        break;
      case FT8:
        toneSpacing=-3;
        if(spacing==2) toneSpacing=2*12000.0/1920.0;
        if(spacing==4) toneSpacing=4*12000.0/1920.0;
        request.symbols_length = 79;
        request.frames_per_symbol = 1920.0;
        request.tone_spacing = toneSpacing;
        break;
      case FT4:
        request.symbols_length = 105;
        request.frames_per_symbol = 576.0;
        request.tone_spacing = -2.0;
        break;
      case FST4:
      case FST4W:
        request.symbols_length = 160;
        request.frames_per_symbol = double(fst4Nsps (trperiod));
        request.tone_spacing = -2.0;
        break;
      case Q65:
        {
          int nsps=1800;
          if(trperiod==30) nsps=3600;
          if(trperiod==60) nsps=7200;
          if(trperiod==120) nsps=16000;
          if(trperiod==300) nsps=41472;
          int mode65=std::pow(2.0,double(submode));
          toneSpacing=mode65*12000.0/nsps;
          request.symbols_length = 85;
          request.frames_per_symbol = double(nsps);
          request.tone_spacing = toneSpacing;
        }
        break;
      case JT9:
        {
          int nsub=std::pow(2,submode);
          double toneSpacing9=nsub*12000.0/6912.0;
          if(spacing==2) toneSpacing9=2.0*toneSpacing9;
          if(spacing==4) toneSpacing9=4.0*toneSpacing9;
          request.symbols_length = 85;
          request.frames_per_symbol = 6912;
          request.tone_spacing = toneSpacing9;
        }
        break;
      case MSK144:
        request.symbols_length = nsym == 40 ? 40 : 144;
        request.frames_per_symbol = double(6);
        request.frequency_hz = 1000.0;
        request.tone_spacing = 6000.0/6;
        request.fast_mode = true;
        break;
      case JT4:
        if(submode==0) toneSpacing=4.375;
        if(submode==1) toneSpacing=2*4.375;
        if(submode==2) toneSpacing=4*4.375;
        if(submode==3) toneSpacing=9*4.375;
        if(submode==4) toneSpacing=18*4.375;
        if(submode==5) toneSpacing=36*4.375;
        if(submode==6) toneSpacing=72*4.375;
        request.symbols_length = 206;
        request.frames_per_symbol = 2520.0*12000.0/11025.0;
        request.tone_spacing = toneSpacing;
        break;
      case WSPR:
        request.symbols_length = 162;
        request.frames_per_symbol = 8192.0;
        request.tone_spacing = 12000.0/8192.0*spacing;
        break;
      default:
        request.valid = false;
      }
    return request;
  }

  // Modulator::readDataAt's last sample, which it commits.
  unsigned lastSample (Request const& request)
  {
    unsigned i1 = request.symbols_length * 4.0 * request.frames_per_symbol;
    if (request.fast_mode) i1 = request.tr_period_s*48000.0 - 24000.0;
    return i1;
  }

  // Modulator::readDataAt's samples for the tones; the GUI's tone buffer is
  // zero past the message in a fresh GUI process, and the last sample reads
  // one past it.
  void modulate (Request const& request, std::int32_t const tones[], std::int32_t ntones,
                 std::vector<std::int16_t>& samples)
  {
    double const m_twoPi = 2.0 * 3.141592653589793238462;
    double m_phi = 0.;
    double m_dphi = 0.;
    double m_amp = std::numeric_limits<std::int16_t>::max ();
    double const m_nsps = request.frames_per_symbol;
    double m_frequency = request.frequency_hz;
    double m_frequency0 = 0.;
    double const m_toneSpacing = request.tone_spacing;
    unsigned m_isym0 = std::numeric_limits<unsigned>::max ();
    double m_toneFrequency0 = 1500.0;
    if(m_nsps==6) {
      m_frequency=1000.0;
      m_frequency0=1000.0;
    }
    double const baud (12000.0 / m_nsps);
    unsigned int i0,i1;
    i0=(request.symbols_length - 0.017) * 4.0 * m_nsps;
    i1= request.symbols_length * 4.0 * m_nsps;
    if(request.fast_mode) {
      i1=request.tr_period_s*48000.0 - 24000.0;
      i0=i1-816;
    }
    for (unsigned m_ic = 0; m_ic <= i1; ++m_ic) {
      unsigned int isym=m_ic/(4.0*m_nsps);
      if(request.fast_mode) isym=isym%request.symbols_length;
      if (isym != m_isym0 || m_frequency != m_frequency0) {
        int const tone = isym < unsigned (ntones) ? tones[isym] : 0;
        if(m_toneSpacing==0.0) {
          m_toneFrequency0=m_frequency + tone*baud;
        } else {
          m_toneFrequency0=m_frequency + tone*m_toneSpacing;
        }
        m_dphi = m_twoPi * m_toneFrequency0 / frameRate;
        m_isym0 = isym;
        m_frequency0 = m_frequency;
      }
      m_phi += m_dphi;
      if (m_phi > m_twoPi) m_phi -= m_twoPi;
      if (m_ic > i0) m_amp = 0.98 * m_amp;
      if (m_ic > i1) m_amp = 0.0;
      std::int16_t sample=qtRound(m_amp*std::sin(m_phi));
      samples.push_back (sample);
    }
  }

  // MainWindow::guiUpdate's waveform, read as Modulator reads foxcom_.wave: the
  // waveform buffer is zero past it in a fresh GUI process, and FT8's and FT4's
  // last sample reads one past it.
  void precomputed (Request const& request, std::int32_t mode, std::int32_t const tones[],
                    std::int32_t spacing, std::int32_t trperiod, double freq,
                    std::vector<std::int16_t>& samples)
  {
    std::vector<int> itone (tones, tones + (mode == FT8 ? 79 : mode == FT4 ? 103 : 160));
    auto const i1 = lastSample (request);
    std::vector<float> wave;
    int icmplx=0;
    if (mode == FT8)
      {
        int nsym=79;
        int nsps=4*1920;
        float fsample=48000.0;
        float bt=2.0;
        float f0=freq;
        int nwave=nsym*nsps;
        wave.assign (std::max (std::size_t (nwave), std::size_t (i1) + 1), 0.f);
        gen_ft8wave_(itone.data (),&nsym,&nsps,&bt,&fsample,&f0,wave.data (),
                     wave.data (),&icmplx,&nwave);
      }
    else if (mode == FT4)
      {
        int nsym=103;
        int nsps=4*576;
        float fsample=48000.0;
        float f0=freq;
        int nwave=(nsym+2)*nsps;
        wave.assign (std::max (std::size_t (nwave), std::size_t (i1) + 1), 0.f);
        gen_ft4wave_(itone.data (),&nsym,&nsps,&fsample,&f0,wave.data (),
                     wave.data (),&icmplx,&nwave);
      }
    else
      {
        int hmod=spacing;
        int nsps=fst4Nsps (trperiod);
        nsps=4*nsps;
        int nsym=160;
        float fsample=48000.0;
        float dfreq=hmod*fsample/nsps;
        float f0=freq + 1.5*dfreq;
        int nwave=(nsym+2)*nsps;
        wave.assign (std::max (std::size_t (nwave), std::size_t (i1) + 1), 0.f);
        gen_fst4wave_(itone.data (),&nsym,&nsps,&nwave,
                      &fsample,&hmod,&f0,&icmplx,wave.data (),wave.data ());
      }
    double const m_amp=32767.0;
    for (unsigned m_ic = 0; m_ic <= i1; ++m_ic)
      {
        std::int16_t sample=qtRound(m_amp*wave[m_ic]);
        samples.push_back (sample);
      }
  }
}

extern "C"
{
  // The transmission's highest tone in Hz, freq its lowest, or 0 for no such
  // transmission.
  double stream_slot_top_tone (std::int32_t mode, std::int32_t submode, std::int32_t spacing,
                               std::int32_t trperiod, double freq) noexcept
  {
    auto const request = transmitRequest (mode, 144, submode, spacing, trperiod, freq);
    if (!request.valid) return 0;
    switch (mode)
      {
      case FT8: return freq + 7 * (request.tone_spacing < 0 ? 12000.0 / 1920 : request.tone_spacing);
      case FT4: return freq + 3 * 12000.0 / 576;
      case FST4: case FST4W: return freq + 3 * spacing * 12000.0 / request.frames_per_symbol;
      case JT4: case WSPR: return request.frequency_hz + 3 * request.tone_spacing;
      case JT9: return request.frequency_hz + 8 * request.tone_spacing;
      case JT65: return request.frequency_hz + 65 * request.tone_spacing;
      case Q65: return request.frequency_hz + 64 * request.tone_spacing;
      default: return request.frequency_hz + request.tone_spacing;    // MSK144
      }
  }

  // The samples a render at rate gives, or 0 for no such transmission.
  std::int32_t stream_slot_samples (std::int32_t mode, std::int32_t nsym, std::int32_t submode,
                                    std::int32_t spacing, std::int32_t trperiod,
                                    std::int32_t rate) noexcept
  {
    auto const request = transmitRequest (mode, nsym, submode, spacing, trperiod, 1000.0);
    if (!request.valid || (rate != 12000 && rate != 48000)) return 0;
    auto const last = std::int64_t (lastSample (request));
    auto const count = rate == 48000 ? last + 1 : last / 4 + 1;
    return count > std::numeric_limits<std::int32_t>::max () ? 0 : std::int32_t (count);
  }

  // The GUI's audio for tones at 48 kHz, or every fourth sample of it at
  // 12 kHz, into samples; returns the count, or 0 when capacity is too small
  // or there is no memory.
  std::int32_t stream_slot_render (std::int32_t mode, std::int32_t const * tones, std::int32_t nsym,
                                   std::int32_t submode, std::int32_t spacing,
                                   std::int32_t trperiod, double freq, std::int32_t rate,
                                   std::int16_t * samples, std::int32_t capacity) noexcept
  {
    try
      {
        auto const request = transmitRequest (mode, nsym, submode, spacing, trperiod, freq);
        if (!request.valid) return 0;
        std::vector<std::int16_t> audio;
        audio.reserve (std::size_t (lastSample (request)) + 1);
        if (request.tone_spacing < 0)
          {
            precomputed (request, mode, tones, spacing, trperiod, freq, audio);
          }
        else
          {
            modulate (request, tones, nsym, audio);
          }
        std::int32_t count = 0;
        int const step = rate == 48000 ? 1 : 4;
        for (std::size_t i = 0; i < audio.size (); i += step)
          {
            if (count >= capacity) return 0;
            samples[count++] = audio[i];
          }
        return count;
      }
    catch (...)
      {
        return 0;
      }
  }
}
