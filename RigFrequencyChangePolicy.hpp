#ifndef RIGFREQUENCYCHANGEPOLICY_HPP__
#define RIGFREQUENCYCHANGEPOLICY_HPP__

namespace RigFrequencyChangePolicy
{
  enum class ChangeKind
  {
    NominalQsy,
    TxPathCorrection
  };

  enum class BlockReason
  {
    None,
    NominalQsyWhileRfActive,
    TxCorrectionDisabled
  };

  struct Activity
  {
    bool transmitRequested {false};
    bool transmitCommandPending {false};
    bool transmitting {false};
    bool tuning {false};
    bool jttyTransmitting {false};
    bool startDelayActive {false};
    bool stopDelayActive {false};
    bool rigPtt {false};
    bool rigTune {false};
  };

  struct Decision
  {
    bool allowed;
    BlockReason reason;
  };

  constexpr bool nominal_qsy_locked (Activity const& activity)
  {
    return activity.transmitRequested
      || activity.transmitCommandPending
      || activity.transmitting
      || activity.tuning
      || activity.jttyTransmitting
      || activity.startDelayActive
      || activity.stopDelayActive
      || activity.rigPtt
      || activity.rigTune;
  }

  constexpr bool tx_correction_requires_permission (Activity const& activity)
  {
    return activity.transmitCommandPending
      || activity.transmitting
      || activity.tuning
      || activity.jttyTransmitting
      || activity.startDelayActive
      || activity.stopDelayActive
      || activity.rigPtt
      || activity.rigTune;
  }

  constexpr Decision evaluate (ChangeKind kind, Activity const& activity,
                               bool correctionsAllowedWhileTransmitting)
  {
    return kind == ChangeKind::NominalQsy
      ? (nominal_qsy_locked (activity)
         ? Decision{false, BlockReason::NominalQsyWhileRfActive}
         : Decision{true, BlockReason::None})
      : (tx_correction_requires_permission (activity)
         && !correctionsAllowedWhileTransmitting
         ? Decision{false, BlockReason::TxCorrectionDisabled}
         : Decision{true, BlockReason::None});
  }

  constexpr bool tx_path_frequency_supported (bool hasTxFrequency, bool splitMode)
  {
    return !hasTxFrequency || splitMode;
  }

  template<typename Action>
  bool applyIfAllowed (ChangeKind kind, Activity const& activity,
                       bool correctionsAllowedWhileTransmitting, Action action)
  {
    if (!evaluate (kind, activity, correctionsAllowedWhileTransmitting).allowed) return false;
    action ();
    return true;
  }

  template<typename Request, typename Commit>
  bool commitIfAccepted (Request request, Commit commit)
  {
    if (!request ()) return false;
    commit ();
    return true;
  }
}

#endif
