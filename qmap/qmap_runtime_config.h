// Runtime sample-rate / FFT-size configuration for QMAP.
//
// Companion to qmap/libqmap/qmap_params.f90, which declares the same
// compile-time maxima.  The /datcom/ common block is sized at the maxima
// (it cannot be allocatable); the active values say how much of it is in
// use for the selected IQ sample rate.  The defaults are the original
// 96 kHz / NFFT=32768 behaviour.
#pragma once

#include <cstddef>

namespace qmap_runtime {

constexpr int MAX_RATE_HZ    = 256000;
constexpr int MAX_NSMAX      = 60 * MAX_RATE_HZ;          // 15,360,000 complex samples
constexpr int MAX_NFFT       = 131072;                    // next power of 2 above 32768 * 256/96
constexpr int BASELINE_RATE  = 96000;
constexpr int BASELINE_NFFT  = 32768;

inline int g_active_rate_hz = BASELINE_RATE;
inline int g_active_nfft    = BASELINE_NFFT;
inline int g_active_nsmax   = 60 * BASELINE_RATE;

inline int activeRateHz() { return g_active_rate_hz; }
inline int activeNfft()   { return g_active_nfft; }
inline int activeNsmax()  { return g_active_nsmax; }

// IQ sample rates the user can select.
inline bool isSupportedRate(int rate_hz)
{
  return rate_hz == 96000 || rate_hz == 128000 || rate_hz == 192000 || rate_hz == 256000;
}

// Narrowest and widest frequency span (kHz) the waterfall can show: 90 kHz at 96 kHz,
// and proportionally more at the higher rates.
constexpr int MIN_SPAN_KHZ = 5;
inline int maxSpanKHz(int rate_hz) { return 90 * rate_hz / 96000; }
inline int clampSpanKHz(int span_khz, int rate_hz)
{
  int const hi = maxSpanKHz(rate_hz);
  return span_khz < MIN_SPAN_KHZ ? MIN_SPAN_KHZ : (span_khz > hi ? hi : span_khz);
}

// FFT length used by symspec at a given rate: the power of 2 nearest above
// BASELINE_NFFT * rate / BASELINE_RATE, which keeps the bin width near 2-3 Hz.
inline int nfftForRate(int rate_hz)
{
  long long const want = static_cast<long long>(BASELINE_NFFT) * rate_hz / BASELINE_RATE;
  int n = 1;
  while (n < want) n <<= 1;
  return n;
}

}  // namespace qmap_runtime

extern "C" {
  // Defined in qmap/libqmap/qmap_params.f90.
  // Call once at startup, before any other call into libqmap.  Values
  // outside the compile-time maxima are ignored.
  void qmap_set_runtime_params(int rate_hz, int nfft);

  // Called by soundin.cpp after each received datagram so that recvpkt
  // knows how many IQ pairs the packet carried (348 at 96 kHz).
  void qmap_set_npairs_active(int npairs);

  // Sample rate (Hz) stored in the header of the .qm file that read_qm last examined.
  int qmap_qm_file_rate(void);
}

namespace qmap_runtime {

// Set the active rate and FFT size on both the C++ and Fortran sides.
inline void setActive(int rate_hz, int nfft)
{
  if (rate_hz <= 0 || rate_hz > MAX_RATE_HZ) return;
  if (nfft <= 0 || nfft > MAX_NFFT) return;
  g_active_rate_hz = rate_hz;
  g_active_nfft    = nfft;
  g_active_nsmax   = 60 * rate_hz;
  qmap_set_runtime_params(rate_hz, nfft);
}

}  // namespace qmap_runtime
