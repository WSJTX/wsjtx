#ifndef OPERATING_BAND_HPP
#define OPERATING_BAND_HPP

#include <optional>
#include <QString>
#include "OperatingFrequency.hpp"

namespace OperatingBand
{
  using Frequency = Radio::Frequency;

  enum class Event {Observation, NominalRequest, Selection, ModeConfigured};

  struct State
  {
    OperatingFrequency::Snapshot frequency {};
    Frequency dial {0};
    QString rxBand;
    QString band;
  };

  struct Context
  {
    QString mode;
    double period {0};
    bool starting {false};
    bool transmitting {false};
    bool monitoring {false};
    bool tuning {false};
    bool bandHopping {false};
    bool waitAndCall {false};
    bool callingFrequency {false};
    bool rememberTxPower {false};
    bool eraseActivity {false};
    bool preserveActivity {false};
    bool reportingEnabled {false};
    bool bandButtonsVisible {false};
    bool temporaryMskQsy {false};
    bool preserveMskBase {false};
    bool fox {false};
    bool acceptedRetune {false};
    bool selectionEffects {false};
    int waterfallStart {0};
    int waterfallSpan {0};
    int txAudio {0};
  };

  struct Deadline
  {
    quint64 identity {0};
    int interval {0};
    Frequency rx {0};
    QString band;
  };

  struct Reporting
  {
    Frequency rx {0};
    QString band;
  };

  struct Plan
  {
    quint64 revision {0};
    bool rxChanged {false};
    bool rxBandChanged {false};
    bool bandChanged {false};
    bool nominalChanged {false};
    bool refreshRxPresentation {false};
    bool refreshWidegraph {false};
    bool refreshButtons {false};
    bool frequencyEffects {false};
    bool disarmAutoRespond {false};
    bool selectionEffects {false};
    bool stopAutoTx {false};
    bool flushSpots {false};
    std::optional<bool> monitor;
    bool resetFox {false};
    bool clearLastCall {false};
    bool initializeFreqCal {false};
    bool updateTxPlan {false};
    bool clearTemporaryMskQsy {false};
    std::optional<Frequency> mskBase;
    bool restorePower {false};
    bool eraseActivity {false};
    bool setSkedFrequency {false};
    std::optional<Deadline> reporting;
    std::optional<Deadline> apSuppression;
    std::optional<Deadline> mskRestore;
  };

  Frequency displayedDial (OperatingFrequency::Snapshot const&, bool ptt, bool split);
  std::optional<Frequency> calibrationDial (Frequency reference, int audioOffset);
  std::optional<Frequency> calibrationReference (Frequency dial, int audioOffset);

  class Controller
  {
  public:
    Plan transition (State const&, Context const&, Event);
    std::optional<Reporting> completeReporting (quint64 identity, Frequency currentRx);
    bool completeAP (quint64 identity);
    bool completeMskRestore (quint64 identity);
    bool current (quint64 revision) const {return revision == revision_;}
    std::optional<State> const& state () const {return state_;}
    std::optional<Context> const& context () const {return context_;}

  private:
    quint64 revision_ {0};
    quint64 deadlineIdentity_ {0};
    std::optional<State> state_;
    std::optional<Context> context_;
    std::optional<Frequency> publishedRx_;
    std::optional<Deadline> reporting_;
    std::optional<Deadline> apSuppression_;
    std::optional<Deadline> mskRestore_;
  };
}

#endif
