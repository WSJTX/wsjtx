#ifndef OPERATING_FREQUENCY_HPP
#define OPERATING_FREQUENCY_HPP

#include <functional>
#include "Radio.hpp"

class OperatingFrequency
{
public:
  using Frequency = Radio::Frequency;
  using Delta = Radio::FrequencyDelta;
  using Request = std::function<bool (Frequency)>;
  struct Snapshot { Frequency rx; Frequency tx; Frequency remembered; };
  struct Observation
  {
    bool online;
    bool transmitting;
    bool split;
    Frequency rx;
    Frequency tx;
  };

  explicit OperatingFrequency (Frequency remembered = 0) : remembered_ {remembered} {}
  Frequency const& rx () const { return rx_; }
  Frequency tx () const { return tx_; }
  Frequency remembered () const { return remembered_; }
  Snapshot snapshot () const { return {rx_, tx_, remembered_}; }
  void loadRemembered (Frequency value) { remembered_ = value; }
  void initialize (Frequency value) { if (!rx_) rx_ = value; }
  bool requestNominal (Frequency, Delta, Request const&);
  bool enableMonitor (bool prior, bool restore, bool echo, Delta, Request const&);
  void observe (Observation const&, bool monitoring, Delta rxCorrection,
                Delta txCorrection, Frequency previousNominal);
  void commitAcceptedTx (Frequency value) { tx_ = value; }
  Frequency correctedRx (Delta delta) const { return rx_ + delta; }
  Frequency correctedTx (Delta delta) const { return tx_ + delta; }

private:
  Frequency rx_ = 0;
  Frequency tx_ = 0;
  Frequency remembered_;
};

#endif
