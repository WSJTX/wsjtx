#include "BeaconTxController.hpp"

#include <locale>
#include <sstream>

namespace BeaconTx
{
  // Non-fixed text falls back to Random for compatibility with previously
  // persisted translated Random labels.
  RoundRobinPolicy parseRoundRobinPolicy (std::string const& text)
  {
    std::istringstream input {text};
    input.imbue (std::locale::classic ());
    int selected;
    int count;
    char separator;
    if (!(input >> selected >> separator >> count) || separator != '/'
        || selected <= 0 || count <= 0 || selected > count)
      {
        return RoundRobinPolicy::random ();
      }
    input >> std::ws;
    if (!input.eof ()) return RoundRobinPolicy::random ();
    return RoundRobinPolicy::fixed (selected - 1, count);
  }

  std::string formatRoundRobinPolicy (RoundRobinPolicy const& policy)
  {
    if (policy.kind == RoundRobinPolicy::Kind::Random) return "random";
    return std::to_string (policy.selectedSlot + 1) + "/" + std::to_string (policy.slotCount);
  }

  Action Action::setTransmitWindow (PlanId id, bool enabled)
  {
    Action result;
    result.kind = ActionKind::SetTransmitWindow;
    result.planId = id;
    result.enabled = enabled;
    return result;
  }

  Action Action::forPlan (ActionKind kind, PlanId id)
  {
    Action result;
    result.kind = kind;
    result.planId = id;
    return result;
  }

  Controller::Actions Controller::enterMode (std::int64_t periodMilliseconds,
                                              RoundRobinPolicy policy)
  {
    m_active = true;
    m_roundRobinPolicy = policy;
    return setPeriod (periodMilliseconds);
  }

  Controller::Actions Controller::exitMode ()
  {
    Actions actions;
    m_active = false;
    m_initialized = false;
    m_currentCompleted = false;
    m_preparationSuppressed = false;
    m_currentFromTxNext = false;
    clearFuturePlan ();
    if (m_transmitWindow)
      {
        m_transmitWindow = false;
        actions.push_back (Action::setTransmitWindow (m_currentPlanId, false));
      }
    if (!m_messageSessionActive)
      {
        m_txLifecycle = TxLifecycle::None;
        m_txStartAccepted = false;
        m_txPlanId = {};
        m_currentPlanId = {};
      }
    return actions;
  }

  Controller::Actions Controller::setPeriod (std::int64_t periodMilliseconds)
  {
    Actions actions;
    if (periodMilliseconds == m_periodMilliseconds && m_initialized) return actions;

    m_periodMilliseconds = periodMilliseconds;
    m_initialized = false;
    m_currentCompleted = false;
    m_preparationSuppressed = false;
    m_currentFromTxNext = false;
    clearFuturePlan ();
    if (m_transmitWindow)
      {
        m_transmitWindow = false;
        actions.push_back (Action::setTransmitWindow (m_currentPlanId, false));
      }
    if (!m_messageSessionActive)
      {
        m_txLifecycle = TxLifecycle::None;
        m_txStartAccepted = false;
        m_txPlanId = {};
      }
    return actions;
  }

  void Controller::setAutoEnabled (bool enabled, bool explicitOperatorChange)
  {
    m_autoEnabled = enabled;
    if (!enabled && explicitOperatorChange) m_restoreAuto = false;
  }

  void Controller::setRoundRobinPolicy (RoundRobinPolicy policy)
  {
    m_roundRobinPolicy = policy;
  }

  // Tx Next overlays a prepared decision without replacing it; if preparation was
  // suppressed, withdrawing Tx Next leaves that target period as Receive.
  Controller::Actions Controller::setTxNext (bool enabled)
  {
    Actions actions;
    if (m_txNextPending == enabled) return actions;
    m_txNextPending = enabled;
    if (!enabled && m_currentFromTxNext
        && (m_txLifecycle == TxLifecycle::Decided
            || m_txLifecycle == TxLifecycle::StartRequested))
      {
        m_currentFromTxNext = false;
        auto const transmit = m_txNextUnderlyingDisposition == Disposition::Transmit;
        m_currentPlanSource = m_txNextUnderlyingSource;
        if (!transmit)
          {
            if (m_txLifecycle == TxLifecycle::StartRequested)
              {
                m_currentCompleted = true;
              }
            else
              {
                m_txLifecycle = TxLifecycle::None;
                m_txStartAccepted = false;
                m_txPlanId = {};
              }
          }
        m_transmitWindow = transmit;
        actions.push_back (Action::setTransmitWindow (m_currentPlanId, transmit));
      }
    return actions;
  }

  // Initialization and clock discontinuities receive through the observed partial
  // period; preparation resumes only after that period completes normally.
  Controller::Actions Controller::resynchronize (std::int64_t periodSerial)
  {
    Actions actions;
    ++m_epoch;
    if (m_epoch == 0) ++m_epoch;
    m_initialized = true;
    m_currentPeriodSerial = periodSerial;
    m_currentPlanId = {m_epoch, periodSerial};
    m_currentCompleted = false;
    m_preparationSuppressed = false;
    m_currentFromTxNext = false;
    clearFuturePlan ();
    if (!m_messageSessionActive)
      {
        m_txLifecycle = TxLifecycle::None;
        m_txStartAccepted = false;
        m_txPlanId = {};
      }
    m_transmitWindow = false;
    actions.push_back (Action::setTransmitWindow (m_currentPlanId, false));
    return actions;
  }

  Controller::Actions Controller::observeUtc (std::int64_t utcMilliseconds)
  {
    Actions actions;
    if (!m_active || m_periodMilliseconds <= 0 || utcMilliseconds < 0) return actions;

    auto const periodSerial = utcMilliseconds / m_periodMilliseconds;
    if (!m_initialized)
      {
        m_lastObservedUtc = utcMilliseconds;
        return resynchronize (periodSerial);
      }

    if (m_lastObservedUtc >= 0 && utcMilliseconds < m_lastObservedUtc)
      {
        m_lastObservedUtc = utcMilliseconds;
        return resynchronize (periodSerial);
      }

    auto const delta = periodSerial - m_currentPeriodSerial;
    m_lastObservedUtc = utcMilliseconds;
    if (delta == 0) return actions;
    if (delta != 1) return resynchronize (periodSerial);

    auto const failedPreviousStart = !m_messageSessionActive
      && (m_txLifecycle == TxLifecycle::Decided
          || m_txLifecycle == TxLifecycle::StartRequested);
    if (failedPreviousStart)
      {
        m_txLifecycle = TxLifecycle::None;
        m_txStartAccepted = false;
        m_txPlanId = {};
      }
    m_currentPeriodSerial = periodSerial;
    m_currentPlanId = {m_epoch, periodSerial};
    m_currentCompleted = false;
    m_preparationSuppressed = false;

    auto underlyingDisposition = Disposition::Receive;
    auto underlyingSource = PlanSource::Percentage;
    if (m_autoEnabled && m_tuneKind == TuneKind::None)
      {
        if (m_roundRobinPolicy.kind == RoundRobinPolicy::Kind::Fixed)
          {
            underlyingDisposition = fixedRoundRobinTransmits (utcMilliseconds)
              ? Disposition::Transmit : Disposition::Receive;
          }
        else if (m_hasFuturePlan && m_futurePlan.id == m_currentPlanId
                 && m_futurePlan.applicationStatus != ApplicationStatus::AwaitingProposal
                 && m_futurePlan.applicationStatus != ApplicationStatus::Applying)
          {
            underlyingDisposition = m_futurePlan.disposition;
            underlyingSource = m_futurePlan.source;
          }
      }

    auto disposition = m_txNextPending && m_autoEnabled
      && m_tuneKind == TuneKind::None
      ? Disposition::Transmit : underlyingDisposition;
    if (m_messageSessionActive) disposition = Disposition::Receive;
    m_currentFromTxNext = disposition == Disposition::Transmit && m_txNextPending;
    m_txNextUnderlyingDisposition = underlyingDisposition;
    m_txNextUnderlyingSource = underlyingSource;
    m_currentPlanSource = m_currentFromTxNext ? PlanSource::TxNext : underlyingSource;

    clearFuturePlan ();
    m_transmitWindow = disposition == Disposition::Transmit;
    if (!m_messageSessionActive)
      {
        m_txLifecycle = m_transmitWindow ? TxLifecycle::Decided : TxLifecycle::None;
      }
    actions.push_back (Action::setTransmitWindow (m_currentPlanId, m_transmitWindow));
    if (failedPreviousStart)
      {
        m_currentCompleted = true;
        appendPreparation (actions);
      }
    return actions;
  }

  bool Controller::fixedRoundRobinTransmits (std::int64_t utcMilliseconds) const
  {
    if (m_roundRobinPolicy.slotCount <= 0 || m_roundRobinPolicy.selectedSlot < 0
        || m_roundRobinPolicy.selectedSlot >= m_roundRobinPolicy.slotCount)
      {
        return false;
      }
    static std::int64_t const dayMilliseconds = 86400000;
    auto const millisecondsOfDay = utcMilliseconds % dayMilliseconds;
    auto const slotOfDay = millisecondsOfDay / m_periodMilliseconds;
    return slotOfDay % m_roundRobinPolicy.slotCount == m_roundRobinPolicy.selectedSlot;
  }

  void Controller::appendPreparation (Actions& actions)
  {
    if (!m_active || !m_initialized || m_tuneKind != TuneKind::None) return;

    PlanId const target {m_epoch, m_currentPeriodSerial + 1};
    if ((m_hasFuturePlan && m_futurePlan.id == target)
        || (m_preparationSuppressed && target.periodSerial == m_currentPeriodSerial + 1))
      {
        return;
      }
    if (m_txNextPending)
      {
        m_preparationSuppressed = true;
        return;
      }
    if (m_roundRobinPolicy.kind == RoundRobinPolicy::Kind::Fixed) return;

    m_futurePlan = {};
    m_futurePlan.id = target;
    m_futurePlan.applicationStatus = ApplicationStatus::AwaitingProposal;
    m_hasFuturePlan = true;
    actions.push_back (Action::forPlan (ActionKind::RequestScheduleProposal, target));
  }

  Controller::Actions Controller::receiveCompleted ()
  {
    Actions actions;
    if (!m_active || !m_initialized || m_transmitWindow || m_currentCompleted) return actions;
    m_currentCompleted = true;
    appendPreparation (actions);
    return actions;
  }

  Controller::Actions Controller::tuneStarted (TuneKind kind, PlanId planId)
  {
    Actions actions;
    if (!m_active || kind == TuneKind::None || m_tuneKind != TuneKind::None) return actions;
    if (kind == TuneKind::Automatic
        && (!m_hasFuturePlan || m_futurePlan.id != planId
            || m_futurePlan.applicationStatus != ApplicationStatus::Applied))
      {
        return actions;
      }
    m_tuneKind = kind;
    m_tunePlanId = kind == TuneKind::Manual ? m_currentPlanId : planId;
    m_restoreAuto = m_autoEnabled;
    if (m_transmitWindow)
      {
        m_transmitWindow = false;
        actions.push_back (Action::setTransmitWindow (m_currentPlanId, false));
      }
    return actions;
  }

  Controller::Actions Controller::tuneCompleted (PlanId planId)
  {
    Actions actions;
    if (m_tuneKind == TuneKind::None || planId != m_tunePlanId) return actions;
    m_tuneKind = TuneKind::None;
    m_tunePlanId = {};
    if (m_restoreAuto && m_active)
      {
        m_autoEnabled = true;
        actions.push_back (Action::forPlan (ActionKind::RestoreAuto, m_currentPlanId));
      }
    m_restoreAuto = false;

    if (m_hasFuturePlan && m_futurePlan.id.periodSerial <= m_currentPeriodSerial)
      {
        clearFuturePlan ();
      }
    if (m_currentCompleted || !m_hasFuturePlan) appendPreparation (actions);
    return actions;
  }

  Controller::Actions Controller::txStartRequested ()
  {
    Actions actions;
    if (m_active && m_transmitWindow && m_txLifecycle == TxLifecycle::Decided)
      {
        m_txLifecycle = TxLifecycle::StartRequested;
        m_txPlanId = m_currentPlanId;
        m_txStartAccepted = false;
      }
    return actions;
  }

  Controller::Actions Controller::txStartResult (PlanId planId, bool accepted)
  {
    Actions actions;
    if (planId != m_txPlanId || m_txLifecycle != TxLifecycle::StartRequested) return actions;
    if (m_txStartAccepted) return actions;
    if (accepted)
      {
        m_txStartAccepted = true;
        return actions;
      }

    if (m_currentCompleted) return actions;
    m_transmitWindow = false;
    m_currentCompleted = true;
    actions.push_back (Action::setTransmitWindow (planId, false));
    return actions;
  }

  Controller::Actions Controller::messageStarted (PlanId planId)
  {
    Actions actions;
    if (planId != m_txPlanId || m_txLifecycle != TxLifecycle::StartRequested
        || !m_txStartAccepted) return actions;
    m_txLifecycle = TxLifecycle::MessageStarted;
    m_messageSessionActive = true;
    if (m_txNextPending)
      {
        m_txNextPending = false;
        actions.push_back (Action::forPlan (ActionKind::ClearTxNextUi, planId));
      }
    actions.push_back (Action::forPlan (ActionKind::RecordBeaconTransmission, planId));
    return actions;
  }

  Controller::Actions Controller::transmitWindowEnded ()
  {
    Actions actions;
    if (!m_transmitWindow) return actions;
    m_transmitWindow = false;
    actions.push_back (Action::setTransmitWindow (m_currentPlanId, false));
    if (m_txLifecycle == TxLifecycle::Decided
        || (m_txLifecycle == TxLifecycle::StartRequested && !m_txStartAccepted))
      {
        m_txLifecycle = TxLifecycle::None;
        m_txStartAccepted = false;
        m_txPlanId = {};
        m_currentCompleted = true;
        appendPreparation (actions);
      }
    return actions;
  }

  Controller::Actions Controller::txStopped (PlanId planId)
  {
    Actions actions;
    if (!planId.isValid () || planId != m_txPlanId
        || m_txLifecycle == TxLifecycle::None)
      {
        return actions;
      }
    auto const sameEpoch = planId.epoch == m_epoch;
    m_messageSessionActive = false;
    m_txPlanId = {};
    m_txStartAccepted = false;
    if (m_transmitWindow)
      {
        m_transmitWindow = false;
        actions.push_back (Action::setTransmitWindow (planId, false));
      }
    if (m_txLifecycle == TxLifecycle::MessageStarted
        || m_txLifecycle == TxLifecycle::StartRequested
        || m_txLifecycle == TxLifecycle::Decided)
      {
        m_txLifecycle = TxLifecycle::None;
        if (sameEpoch)
          {
            m_currentCompleted = true;
            if (m_active) appendPreparation (actions);
          }
      }
    return actions;
  }

  Controller::Actions Controller::proposalDelivered (
    PlanId planId, ScheduleProposal const& proposal)
  {
    Actions actions;
    if (!m_active || !m_hasFuturePlan || m_futurePlan.id != planId
        || m_futurePlan.applicationStatus != ApplicationStatus::AwaitingProposal)
      {
        return actions;
      }

    m_futurePlan.disposition = proposal.disposition;
    m_futurePlan.source = proposal.source;
    m_futurePlan.hasHoppingProposal = proposal.hasHoppingProposal;
    m_futurePlan.hopping = proposal.hopping;
    if (proposal.hasHoppingProposal && proposal.hopping.frequenciesIndex >= 0)
      {
        m_futurePlan.applicationStatus = ApplicationStatus::Applying;
        Action action = Action::forPlan (ActionKind::ApplyBandChange, planId);
        action.hopping = proposal.hopping;
        actions.push_back (action);
      }
    else
      {
        m_futurePlan.applicationStatus = ApplicationStatus::Ready;
      }
    return actions;
  }

  Controller::Actions Controller::bandChangeOutcome (PlanId planId, bool succeeded)
  {
    Actions actions;
    if (!m_active || !m_hasFuturePlan || m_futurePlan.id != planId
        || m_futurePlan.applicationStatus != ApplicationStatus::Applying)
      {
        return actions;
      }

    if (!succeeded)
      {
        m_futurePlan.disposition = Disposition::Receive;
        m_futurePlan.applicationStatus = ApplicationStatus::Failed;
        return actions;
      }

    m_futurePlan.applicationStatus = ApplicationStatus::Applied;
    if (m_futurePlan.hopping.tuneRequired)
      {
        actions.push_back (Action::forPlan (ActionKind::StartAutomaticTune, planId));
      }
    return actions;
  }

  void Controller::clearFuturePlan ()
  {
    m_hasFuturePlan = false;
    m_futurePlan = {};
  }

}
