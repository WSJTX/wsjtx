#ifndef OPERATING_FREQUENCY_HPP
#define OPERATING_FREQUENCY_HPP

#include <functional>
#include <optional>
#include "Radio.hpp"

class OperatingFrequency
{
public:
  using Frequency = Radio::Frequency;
  using Delta = Radio::FrequencyDelta;
  // Acceptance permits a local commit; hardware confirmation arrives separately.
  using Request = std::function<bool (Frequency)>;
  struct Snapshot { Frequency rx; Frequency tx; Frequency remembered; };
  // Configuration supplies RF frequencies with Doppler correction still applied.
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
    Delta rxCorrection;
    Delta txCorrection;
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
  Snapshot snapshot () const { return {rx_, tx_, remembered_}; }
  void loadRemembered (Frequency value) { remembered_ = value; }
  bool requestNominal (Frequency, Delta, Request const&);
  Transition enableMonitor (Context const&, Request const&);
  Transition reconcile (Observation const& previous, Observation const& current,
                        Context const&, Request const&);
  void commitAcceptedTx (Frequency value) { tx_ = value; }
  Frequency correctedRx (Delta delta) const { return rx_ + delta; }
  Frequency correctedTx (Delta delta) const { return tx_ + delta; }

private:
  void observe (Observation const&, bool monitoring, Delta rxCorrection,
                Delta txCorrection);
  Frequency rx_ = 0;
  Frequency tx_ = 0;
  Frequency remembered_;
};

#endif
