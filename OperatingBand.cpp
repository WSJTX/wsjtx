#include "OperatingBand.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
  using Frequency = OperatingBand::Frequency;

  enum class MonitorPolicy {Preserve, Start, Stop};

  MonitorPolicy monitorPolicyFor (QString const& mode, OperatingBand::Event event, bool acceptedRetune)
  {
    if (mode == "Echo") return MonitorPolicy::Stop;
    if (event == OperatingBand::Event::Selection
        || (event == OperatingBand::Event::ModeConfigured && (mode == "JTTY" || acceptedRetune)))
      return MonitorPolicy::Start;
    return MonitorPolicy::Preserve;
  }

  std::pair<int, Frequency> offsetFrequency (Frequency frequency, qint64 offset)
  {
    if (offset >= 0)
      {
        auto const result = frequency + static_cast<Frequency> (offset);
        return {result < frequency ? 1 : 0, result};
      }
    auto const magnitude = static_cast<Frequency> (-(offset + 1)) + 1;
    return {frequency < magnitude ? -1 : 0, frequency - magnitude};
  }

  bool outsideWaterfall (Frequency previous, Frequency current,
                         OperatingBand::Context const& context)
  {
    auto const transmitted = offsetFrequency (previous, context.txAudio);
    auto const lower = offsetFrequency (current, context.waterfallStart);
    auto const upper = offsetFrequency (current,
      qint64 {context.waterfallStart} + context.waterfallSpan);
    return transmitted < lower || transmitted >= upper;
  }

  int interval (double period, double factor)
  {
    return static_cast<int> (std::min (period * factor,
                                      double {std::numeric_limits<int>::max ()}));
  }
}

namespace OperatingBand
{
  Frequency displayedDial (OperatingFrequency::Snapshot const& frequency, bool ptt, bool split)
  {
    return ptt ? (split ? frequency.correctedTx : frequency.tx) : frequency.correctedRx;
  }

  std::optional<Frequency> calibrationDial (Frequency reference, int audioOffset)
  {
    if (audioOffset < 0 || reference <= static_cast<Frequency> (audioOffset)) return {};
    return reference - static_cast<Frequency> (audioOffset);
  }

  std::optional<Frequency> calibrationReference (Frequency dial, int audioOffset)
  {
    if (audioOffset < 0 || dial > std::numeric_limits<Frequency>::max () - static_cast<Frequency> (audioOffset))
      return {};
    return dial + static_cast<Frequency> (audioOffset);
  }

  Plan Controller::transition (State const& state, Context const& context, Event event)
  {
    Plan plan;
    auto const previousRx = state_ ? state_->frequency.rx : state.frequency.rx;
    plan.rxChanged = !state_ || state_->frequency.rx != state.frequency.rx;
    plan.rxBandChanged = state_ && state_->rxBand != state.rxBand;
    plan.bandChanged = !state_ || state_->band != state.band;
    plan.nominalChanged = plan.rxChanged || state_->frequency.tx != state.frequency.tx;
    auto const dialChanged = !state_ || state_->dial != state.dial;
    auto const modeChanged = !context_ || context_->mode != context.mode;
    auto const periodChanged = !context_ || context_->period != context.period;
    auto const configured = event == Event::ModeConfigured;
    auto const selected = event == Event::Selection
      || (configured && context.acceptedRetune && context.selectionEffects);
    auto const monitorPolicy = monitorPolicyFor (context.mode, event, context.acceptedRetune);
    auto const changed = plan.rxChanged || dialChanged || modeChanged || periodChanged
      || configured || selected;

    if (changed) ++revision_;
    plan.revision = revision_;
    plan.refreshRxPresentation = state.frequency.rx
      && (!publishedRx_ || *publishedRx_ != state.frequency.rx)
      && !(context.mode == "MSK144" && context.callingFrequency);
    if (plan.refreshRxPresentation) publishedRx_ = state.frequency.rx;
    plan.refreshWidegraph = plan.bandChanged || modeChanged || configured || selected;
    auto const controlsChanged = !context_ || context_->callingFrequency != context.callingFrequency
      || context_->bandButtonsVisible != context.bandButtonsVisible;
    plan.refreshButtons = (changed || controlsChanged) && context.bandButtonsVisible;
    plan.frequencyEffects = selected || plan.bandChanged;
    plan.disarmAutoRespond = plan.frequencyEffects || modeChanged || configured;
    plan.selectionEffects = selected;
    plan.stopAutoTx = selected && context.mode != "WSPR"
      && !context.bandHopping && !context.waitAndCall
      && outsideWaterfall (previousRx, state.frequency.rx, context);
    plan.flushSpots = selected && context.reportingEnabled;
    if (selected || configured || (plan.bandChanged && monitorPolicy == MonitorPolicy::Stop))
      {
        if (monitorPolicy == MonitorPolicy::Stop && context.monitoring)
          plan.monitor = false;
        else if (monitorPolicy == MonitorPolicy::Start
                 && !context.monitoring && !context.transmitting)
          plan.monitor = true;
      }
    plan.resetFox = selected && context.fox;
    plan.clearLastCall = selected;
    plan.initializeFreqCal = selected && context.mode == "FreqCal";
    plan.updateTxPlan = selected || configured;
    plan.clearTemporaryMskQsy = selected || configured
      || (plan.frequencyEffects && !context.temporaryMskQsy);
    if (context.mode == "MSK144")
      {
        if (changed && (selected || configured
                        || (!context.temporaryMskQsy && !context.preserveMskBase)))
          plan.mskBase = state.dial;
      }
    else if (modeChanged || configured)
      {
        plan.mskBase = Frequency {0};
      }
    plan.restorePower = plan.frequencyEffects && context.rememberTxPower && !context.tuning;
    plan.eraseActivity = plan.bandChanged && context.eraseActivity && !context.preserveActivity;
    plan.setSkedFrequency = plan.bandChanged && !context.starting;

    if (modeChanged || periodChanged || plan.rxBandChanged || configured || selected)
      mskRestore_.reset ();
    auto const validPeriod = std::isfinite (context.period) && context.period > 0;
    if (validPeriod && (plan.rxChanged || plan.bandChanged))
      {
        reporting_ = Deadline {++deadlineIdentity_, interval (context.period, 600),
                               state.frequency.rx, state.rxBand};
        plan.reporting = reporting_;
      }
    if (validPeriod && (plan.frequencyEffects || modeChanged || configured))
      {
        apSuppression_ = Deadline {++deadlineIdentity_, interval (context.period, 1500),
                                   state.frequency.rx, state.rxBand};
        plan.apSuppression = apSuppression_;
      }
    if (plan.bandChanged && context.mode == "MSK144" && !context.starting)
      {
        mskRestore_ = Deadline {++deadlineIdentity_, 750, state.frequency.rx, state.rxBand};
        plan.mskRestore = mskRestore_;
      }

    state_ = state;
    context_ = context;
    return plan;
  }

  std::optional<Reporting> Controller::completeReporting (quint64 identity, Frequency currentRx)
  {
    if (!reporting_ || reporting_->identity != identity || reporting_->rx != currentRx) return {};
    Reporting const result {reporting_->rx, reporting_->band};
    reporting_.reset ();
    return result;
  }

  bool Controller::completeAP (quint64 identity)
  {
    if (!apSuppression_ || apSuppression_->identity != identity) return false;
    apSuppression_.reset ();
    return true;
  }

  bool Controller::completeMskRestore (quint64 identity)
  {
    if (!mskRestore_ || mskRestore_->identity != identity) return false;
    mskRestore_.reset ();
    return true;
  }
}
