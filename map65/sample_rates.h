#ifndef MAP65_SAMPLE_RATES_H
#define MAP65_SAMPLE_RATES_H

// The "FSam96000" setting (MainWindow::m_fs96000) is a code, not a rate:
//   0 = 95238 Hz (legacy slow 96k), 1 = 96000, 2 = 192000, 3 = 128000, 4 = 256000.
// The numbering is fixed by existing map65.ini files, so new rates take new codes.
#include <cstddef>

constexpr int kMaxSampleRateCode = 4;

inline int sampleRateForCode(int code)
{
  switch (code) {
  case 0:  return 95238;
  case 2:  return 192000;
  case 3:  return 128000;
  case 4:  return 256000;
  default: return 96000;
  }
}

// Largest wide-graph span (kHz) that fits inside the sampled bandwidth.
inline int maxFreqSpanKHz(int rate)
{
  switch (rate) {
  case 128000: return 125;
  case 192000: return 190;
  case 256000: return 250;
  default:     return 90;
  }
}

// Percentage of a half-symbol's samples the noise blanker zeroed: nzap/178.3 at
// 96 kHz, and the number of samples in a half-symbol grows with the rate.
// symspec stops updating its RMS at half that many (8900 * rate / 96000).
inline double zapPercent(int nzap, int rate)
{
  return nzap / (178.3 * rate / 96000.0);
}

// At 256 kHz the main and snapshot sample buffers alone take about 3.2 GiB,
// more than a 32-bit process can address.
inline bool sampleRateCodeFitsAddressSpace(int code, std::size_t pointerSize = sizeof(void*))
{
  return pointerSize > 4 || code != 4;
}

// The rate code Settings shows: a change waiting for a restart
// (pendingCode >= 0), otherwise the running one.
inline int dialogSampleRateCode(int runningCode, int pendingCode)
{
  return pendingCode >= 0 ? pendingCode : runningCode;
}

// The rate code left waiting for a restart once Settings is accepted with
// selectedCode, or -1 when that is the running rate.
inline int pendingSampleRateCode(int runningCode, int selectedCode)
{
  return selectedCode != runningCode ? selectedCode : -1;
}

// Whether accepting Settings should show the restart notice: only when the
// pending rate is new or has changed, not each time an unchanged one is kept.
inline bool restartNoticeNeeded(int previousPendingCode, int pendingCode)
{
  return pendingCode >= 0 && pendingCode != previousPendingCode;
}

// A saved code the build can't hold falls back to 192 kHz instead of leaving
// an installation that cannot start.
inline int usableSampleRateCode(int code, std::size_t pointerSize = sizeof(void*))
{
  return sampleRateCodeFitsAddressSpace(code, pointerSize) ? code : 2;
}

#endif
