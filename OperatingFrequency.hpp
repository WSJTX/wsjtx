#ifndef OPERATING_FREQUENCY_HPP
#define OPERATING_FREQUENCY_HPP

#include <functional>
#include <optional>
#include "FrequencyControl.hpp"
#include "RigFrequencyChangePolicy.hpp"

class OperatingFrequency
{
public:
  using Frequency = Radio::Frequency;
  using Delta = Radio::FrequencyDelta;
  using Path = RigFrequency::Path;
  using Dispatch = std::function<bool (RigFrequency::Request const&)>;
  struct TxPlan
  {
    Frequency nominal;
    Delta audioOffset;
  };
  struct Snapshot
  {
    Frequency rx;
    Frequency tx;
    Frequency remembered;
    Frequency correctedRx;
    Frequency correctedTx;
    bool pendingRx;
    bool pendingTx;
    bool recoveryRequired;
    RigFrequency::Basis establishedRxBasis;
    RigFrequency::Basis establishedTxBasis;
    Delta audioOffset;
  };
  struct Observation
  {
    bool online;
    bool transmitting;
    bool split;
    Frequency rx;
    Frequency tx;
  };
  struct Context
  {
    bool monitoring;
    bool monitorAllowed;
    bool monitorAtStartup;
    bool restore;
    bool echo;
    bool transmitting = false;
    bool split = false;
  };
  struct Transition
  {
    Snapshot before;
    Snapshot after;
    std::optional<bool> monitor;
    bool restorationAccepted = false;
  };

  explicit OperatingFrequency (Frequency remembered = 0) : remembered_ {remembered} {}
  Frequency const& rx () const { return rx_; }
  Frequency tx () const { return tx_; }
  Frequency remembered () const { return remembered_; }
  Delta audioOffset () const { return audioOffset_; }
  Snapshot snapshot () const;
  void loadRemembered (Frequency value) { remembered_ = value; }
  void setDesiredCorrection (Delta rx, Delta tx);
  Delta desiredRxCorrection () const { return rxState_.desiredCorrection; }
  Delta desiredTxCorrection () const { return txState_.desiredCorrection; }
  Frequency correctedRx () const;
  Frequency correctedTx () const;
  bool requestNominal (Frequency, Dispatch const&, bool dispatchToRig = true);
  bool requestTx (Frequency, Dispatch const&, bool dispatchToRig = true);
  bool requestTxPlan (TxPlan const&, Dispatch const&, bool dispatchToRig = true,
                      bool updateNominal = true);
  bool applyCorrection (Path, RigFrequencyChangePolicy::Activity const&,
                        bool correctionsAllowed, Dispatch const&);
  Transition enableMonitor (Context const&, Dispatch const&, bool dispatchToRig = true);
  Transition reconcile (Observation const& previous, Observation const& current,
                        Context const&, Dispatch const&, bool dispatchToRig = true);
  Transition handleResult (RigFrequency::Result const&);
  Transition observe (RigFrequency::Evidence const&, Context const&);
  void setConnectionGeneration (quint64, bool online = true);
  void disconnect ();
  void retry (quint64 generation);
  void requireRecovery ();
  bool recoveryRequired () const { return recoveryRequired_; }

private:
  struct Mapping
  {
    RigFrequency::Request request;
    Frequency nominal = 0;
    Delta correction = 0;
    RigFrequency::Basis basis = RigFrequency::Basis::Retained;
  };
  struct PathState
  {
    Delta desiredCorrection = 0;
    std::optional<Mapping> pending;
    std::optional<Mapping> established;
    quint64 lastEvidenceSerial = 0;
    quint64 lastExternalResult = 0;
    std::optional<Frequency> observed;
    bool needsReapplication = false;
  };
  PathState& state (Path path) { return path == Path::Rx ? rxState_ : txState_; }
  PathState const& state (Path path) const { return path == Path::Rx ? rxState_ : txState_; }
  bool dispatch (Path, Frequency nominal, Dispatch const&);
  Transition transition () const { return {snapshot (), snapshot (), std::nullopt}; }
  Frequency rx_ = 0;
  Frequency tx_ = 0;
  Frequency remembered_;
  Delta audioOffset_ = 0;
  PathState rxState_;
  PathState txState_;
  quint64 generation_ = 0;
  quint64 nextRequest_ = 0;
  bool online_ = false;
  bool recoveryRequired_ = false;
  bool resynchronizing_ = false;
};

#endif
