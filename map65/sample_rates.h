#ifndef MAP65_SAMPLE_RATES_H
#define MAP65_SAMPLE_RATES_H

// Sample-rate codes and the input/output device choices that depend on them.
//
// The "FSam96000" setting (MainWindow::m_fs96000) is a code, not a rate:
//   0 = 95238 Hz (legacy slow 96k), 1 = 96000, 2 = 192000, 3 = 128000, 4 = 256000.
// The numbering is fixed by existing map65.ini files, so new rates take new codes.
#include <cstddef>
#include <string>

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

// getDev records the lowest and highest standard rate a card accepts, so a
// card is in range at 128 kHz whenever it accepts both 96 and 192 kHz, whether
// or not it accepts 128 kHz itself. The device list therefore also asks the
// device about 128 and 256 kHz, and about every rate when getDev found no range.
inline bool sampleRateNeedsExactProbe(int rate)
{
  return rate == 128000 || rate == 256000;
}

// Busy: the device could not be asked because it is open (by another program,
// or through another device on the same ALSA card).
enum class RateProbe { Supported, NotSupported, Busy };

// Whether an input device belongs in the list, and if not, why.
enum class InputFit { Listed, TooFewChannels, RateNotSupported, Busy };

// The device needs the channels and its range must include the rate. At 128
// and 256 kHz the device is also asked (getDev's range can't tell). maxSpeed 0
// means getDev found no range: the device is asked at every rate.
template <class Probe>
inline InputFit inputDeviceFit(int rate, int channels, int maxChan,
                               int minSpeed, int maxSpeed, Probe probe)
{
  if (maxChan < channels) return InputFit::TooFewChannels;
  bool const rangeKnown = maxSpeed > 0;
  if (rangeKnown && (rate < minSpeed || rate > maxSpeed)) return InputFit::RateNotSupported;
  if (rangeKnown && !sampleRateNeedsExactProbe(rate)) return InputFit::Listed;
  switch (probe()) {
  case RateProbe::Supported: return InputFit::Listed;
  case RateProbe::Busy:      return InputFit::Busy;
  default:                   return InputFit::RateNotSupported;
  }
}

// The rate the input device list is built for: what the radio buttons select,
// with 95238 Hz (network mode only) listed like 96 kHz.
inline int inputListSampleRate(int code)
{
  return code == 0 ? 96000 : sampleRateForCode(code);
}

// Row of device in a list of PortAudio device numbers, or -1 when it isn't
// there. Settings picks devices by number so a list that changes length or
// order doesn't move the choice onto another device.
inline int rowOfDevice(const int* devices, int count, int device)
{
  for (int row = 0; row < count; ++row) {
    if (devices[row] == device) return row;
  }
  return -1;
}

// The input device once Settings is accepted. SoundIn restarts at the running
// rate, so a selected device that can't run now (runsNow false: not at the
// running rate, or not with the new channel count) waits for the restart.
// pendingDevice gets the device waiting for the restart, or -1.
inline int inputDeviceToApply(int runningDevice, int selectedDevice, bool runsNow,
                              bool ratePending, int previousPendingDevice, int& pendingDevice)
{
  if (selectedDevice < 0) {
    pendingDevice = ratePending ? previousPendingDevice : -1;
    return runningDevice;
  }
  if (!runsNow && selectedDevice != runningDevice) {
    pendingDevice = selectedDevice;
    return runningDevice;
  }
  pendingDevice = -1;
  return selectedDevice;
}

// The input device map65.ini keeps and Settings shows: one waiting for the
// restart, otherwise the running one.
inline int savedInputDevice(int runningDevice, int pendingDevice)
{
  return pendingDevice >= 0 ? pendingDevice : runningDevice;
}

// A device name without the parts the system changes when the same hardware
// comes back: ALSA's trailing "(hw:2,0)" card number, and the "2- " Windows
// puts in front of the name of a second identical device.
inline std::string stableDeviceName(std::string name)
{
  auto digits = [&](std::size_t from) {
    std::size_t k = from;
    while (k < name.size() && name[k] >= '0' && name[k] <= '9') ++k;
    return k;
  };
  std::size_t const open = name.rfind(" (");
  if (open != std::string::npos && name.back() == ')') {
    std::string const inner = name.substr(open + 2, name.size() - open - 3);
    std::size_t k = 0;
    if (inner.compare(0, 4, "plug") == 0) k = 4;
    if (inner.compare(k, 3, "hw:") == 0) {
      std::size_t const card = digits(open + 2 + k + 3);
      std::size_t end = card;
      if (end > open + 2 + k + 3 && end < name.size() && name[end] == ',') end = digits(end + 1);
      if (card > open + 2 + k + 3 && end == name.size() - 1) name.erase(open);
    }
  }
  for (std::size_t p = name.find('('); p != std::string::npos; p = name.find('(', p + 1)) {
    std::size_t const end = digits(p + 1);
    if (end > p + 1 && name.compare(end, 2, "- ") == 0) name.erase(p + 1, end + 2 - (p + 1));
  }
  return name;
}

// Which device a saved name and number mean now. map65.ini keeps the number
// and name from the run that saved them; PortAudio numbers the devices
// afresh each run, and the numbers move when a device is missing or added, so
// the number counts only while its name still matches; otherwise the one
// device with that name; otherwise the one whose stableDeviceName matches
// (the number breaks a tie between identical devices); otherwise -1. An empty
// name (map65.ini from before names were saved) keeps the number. nameOf(i)
// is the name of device i, or empty when i can't be used (e.g. no inputs).
template <class NameOf>
inline int resolveSavedDevice(int number, std::string const& name, int count, NameOf nameOf)
{
  if (name.empty()) return number;
  if (number >= 0 && number < count && nameOf(number) == name) return number;
  int found = -1;
  for (int i = 0; i < count; ++i) {
    if (nameOf(i) != name) continue;
    if (found >= 0) return -1;
    found = i;
  }
  if (found >= 0) return found;

  std::string const key = stableDeviceName(name);
  if (number >= 0 && number < count) {
    std::string const here = nameOf(number);
    if (!here.empty() && stableDeviceName(here) == key) return number;
  }
  for (int i = 0; i < count; ++i) {
    std::string const candidate = nameOf(i);
    if (candidate.empty() || stableDeviceName(candidate) != key) continue;
    if (found >= 0) return -1;
    found = i;
  }
  return found;
}

// A saved code the build can't hold falls back to 192 kHz instead of leaving
// an installation that cannot start.
inline int usableSampleRateCode(int code, std::size_t pointerSize = sizeof(void*))
{
  return sampleRateCodeFitsAddressSpace(code, pointerSize) ? code : 2;
}

#endif
