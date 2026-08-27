#ifndef QMAP_CLICK_POLICY_H
#define QMAP_CLICK_POLICY_H

struct QMapClickPolicy
{
  bool disarmBeforeQsy {};
  bool enableAutoTx {};
  bool disableAutoTx {};
  bool restartTransmission {};
};

constexpr QMapClickPolicy qmapClickPolicy (bool doubleClick,
                                           bool frequencyChanged,
                                           bool autoTxEnabled,
                                           bool transmitting) noexcept
{
  return {
    doubleClick && autoTxEnabled,
    doubleClick && frequencyChanged,
    !doubleClick && autoTxEnabled,
    frequencyChanged && transmitting
  };
}

#endif // QMAP_CLICK_POLICY_H
