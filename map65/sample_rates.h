#ifndef MAP65_SAMPLE_RATES_H
#define MAP65_SAMPLE_RATES_H

// The "FSam96000" setting (MainWindow::m_fs96000) is a code, not a rate:
//   0 = 95238 Hz (legacy slow 96k), 1 = 96000, 2 = 192000, 3 = 128000, 4 = 256000.
// The numbering is fixed by existing map65.ini files, so new rates take new codes.
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

#endif
