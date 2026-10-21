#include "OperatingFrequency.hpp"

OperatingFrequency::Snapshot OperatingFrequency::snapshot () const
{
  return {rx_, tx_, remembered_, correctedRx (), correctedTx (),
          bool (rxState_.pending), bool (txState_.pending), recoveryRequired_,
          rxState_.established ? rxState_.established->basis : RigFrequency::Basis::Retained,
          txState_.established ? txState_.established->basis : RigFrequency::Basis::Retained,
          audioOffset_};
}

OperatingFrequency::Frequency OperatingFrequency::correctedRx () const
{
  return rx_ + (rxState_.established ? rxState_.established->correction : 0);
}

OperatingFrequency::Frequency OperatingFrequency::correctedTx () const
{
  return tx_ + (txState_.established ? txState_.established->correction : 0);
}

void OperatingFrequency::setDesiredCorrection (Delta rx, Delta tx)
{
  rxState_.desiredCorrection = rx;
  txState_.desiredCorrection = tx;
}

bool OperatingFrequency::dispatch (Path path, Frequency nominal, Dispatch const& send)
{
  auto& pathState = state (path);
  if (!online_ || recoveryRequired_ || pathState.pending) return false;
  auto const target = nominal + pathState.desiredCorrection;
  if (!pathState.needsReapplication && pathState.established
      && pathState.established->request.generation == generation_
      && pathState.established->nominal == nominal
      && pathState.established->correction == pathState.desiredCorrection)
    return true;

  Mapping mapping {{generation_, ++nextRequest_, path, target}, nominal,
                   pathState.desiredCorrection};
  pathState.pending = mapping;
  if (send (mapping.request)) return true;
  if (pathState.pending
      && RigFrequency::same_operation (pathState.pending->request, mapping.request))
    pathState.pending.reset ();
  return false;
}

bool OperatingFrequency::requestNominal (Frequency value, Dispatch const& send, bool dispatchToRig)
{
  if (!value || (dispatchToRig && !dispatch (Path::Rx, value, send))) return false;
  rx_ = tx_ = value;
  return true;
}

bool OperatingFrequency::requestTx (Frequency value, Dispatch const& send, bool dispatchToRig)
{
  if (!value || (dispatchToRig && !dispatch (Path::Tx, value, send))) return false;
  tx_ = value;
  return true;
}

bool OperatingFrequency::requestTxPlan (TxPlan const& plan, Dispatch const& send,
                                       bool dispatchToRig, bool updateNominal)
{
  if (updateNominal && !requestTx (plan.nominal, send, dispatchToRig)) return false;
  audioOffset_ = plan.audioOffset;
  return true;
}

bool OperatingFrequency::applyCorrection (Path path,
  RigFrequencyChangePolicy::Activity const& activity, bool correctionsAllowed,
  Dispatch const& send)
{
  if (!RigFrequencyChangePolicy::evaluate (
        RigFrequencyChangePolicy::ChangeKind::TxPathCorrection,
        activity, correctionsAllowed).allowed) return false;
  auto const nominal = path == Path::Rx ? rx_ : tx_;
  return nominal && dispatch (path, nominal, send);
}

OperatingFrequency::Transition OperatingFrequency::enableMonitor (
  Context const& context, Dispatch const& send, bool dispatchToRig)
{
  auto result = transition ();
  if (!context.monitorAllowed) return result;
  result.monitor = true;
  result.restorationAccepted = !context.monitoring && context.restore && !context.echo
    && requestNominal (remembered_, send, dispatchToRig);
  result.after = snapshot ();
  return result;
}

OperatingFrequency::Transition OperatingFrequency::reconcile (
  Observation const& previous, Observation const& current,
  Context const& context, Dispatch const& send, bool dispatchToRig)
{
  auto result = transition ();
  bool const startup = !previous.online && current.online && context.monitorAllowed;
  bool const monitoring = startup ? context.monitorAtStartup && !context.echo : context.monitoring;

  // Initial discovery has no earlier application mapping. Subsequent frequency
  // changes require fresh evidence rather than retained whole-state fields.
  if (current.online && current.rx && !rx_ && !recoveryRequired_)
    {
      rx_ = tx_ = current.rx;
      rxState_.observed = current.rx;
      if (monitoring && !(startup && context.restore && !context.echo)) remembered_ = rx_;
    }
  if (startup)
    {
      result.monitor = monitoring;
      if (monitoring && !context.restore && !context.echo && !recoveryRequired_
          && !resynchronizing_ && !rxState_.pending) remembered_ = rx_;
      if (monitoring && !context.monitoring && context.restore && !context.echo)
        result.restorationAccepted = enableMonitor (context, send, dispatchToRig).restorationAccepted;
    }
  result.after = snapshot ();
  return result;
}

void OperatingFrequency::requireRecovery ()
{
  recoveryRequired_ = true;
}

OperatingFrequency::Transition OperatingFrequency::handleResult (RigFrequency::Result const& result)
{
  auto change = transition ();
  if (!online_ || result.request.generation != generation_) return change;
  auto& pathState = state (result.request.path);
  if (!pathState.pending
      || !RigFrequency::same_operation (result.request, pathState.pending->request)
      || result.request.target != pathState.pending->request.target)
    {
      if (result.outcome == RigFrequency::Outcome::Failed && pathState.established
          && RigFrequency::same_operation (result.request, pathState.established->request)
          && result.request.target == pathState.established->request.target)
        requireRecovery ();
      // Configuration reserves the high request-ID range for sync and mapping
      // operations that do not originate from nominal-frequency intent.
      if (result.request.id > nextRequest_ && result.request.id > pathState.lastExternalResult
          && result.outcome == RigFrequency::Outcome::Established
          && result.basis != RigFrequency::Basis::Retained)
        {
          pathState.lastExternalResult = result.request.id;
          if (pathState.established
              && result.applied != pathState.established->nominal + pathState.established->correction)
            pathState.needsReapplication = true;
        }
      change.after = snapshot ();
      return change;
    }
  switch (result.outcome)
    {
    case RigFrequency::Outcome::Pending: break;
    case RigFrequency::Outcome::Deferred:
      pathState.pending.reset ();
      break;
    case RigFrequency::Outcome::Established:
      if (result.basis == RigFrequency::Basis::Retained
          || result.applied != pathState.pending->request.target)
        {
          requireRecovery ();
          break;
        }
      pathState.established = pathState.pending;
      pathState.established->basis = result.basis;
      pathState.pending.reset ();
      pathState.needsReapplication = false;
      if (result.request.path == Path::Rx) resynchronizing_ = false;
      break;
    case RigFrequency::Outcome::Rejected:
    case RigFrequency::Outcome::Failed:
      requireRecovery ();
      break;
    }
  change.after = snapshot ();
  return change;
}

OperatingFrequency::Transition OperatingFrequency::observe (
  RigFrequency::Evidence const& evidence, Context const& context)
{
  auto change = transition ();
  if (!online_ || evidence.generation != generation_ || !evidence.value
      || evidence.basis == RigFrequency::Basis::Retained || recoveryRequired_) return change;
  auto& pathState = state (evidence.path);
  if (evidence.serial && evidence.serial <= pathState.lastEvidenceSerial) return change;
  pathState.lastEvidenceSerial = evidence.serial;
  if (evidence.operation)
    {
      // Command-associated observations cannot be reinterpreted as dial tuning.
      if (evidence.operation.generation != evidence.generation
          || evidence.operation.path != evidence.path
          || evidence.value != evidence.operation.target) return change;
      handleResult ({evidence.operation, RigFrequency::Outcome::Established,
                     evidence.basis, evidence.value});
      if (evidence.path == Path::Rx && context.monitoring && pathState.established
          && RigFrequency::same_operation (pathState.established->request, evidence.operation)
          && pathState.established->nominal == rx_) remembered_ = rx_;
      if (pathState.established
          && RigFrequency::same_operation (pathState.established->request, evidence.operation))
        pathState.observed = evidence.value;
      change.after = snapshot ();
      return change;
    }
  if (pathState.pending || pathState.needsReapplication || resynchronizing_)
    {
      // The backend resolves observations interleaved with frequency application.
      return change;
    }
  if (evidence.path == Path::Tx && (!context.transmitting || !context.split)) return change;
  if (evidence.path == Path::Rx && context.transmitting)
    {
      if (!context.split) tx_ = evidence.value;
      change.after = snapshot ();
      return change;
    }
  if (evidence.path == Path::Rx && pathState.observed
      && *pathState.observed == evidence.value) return change;
  pathState.observed = evidence.value;
  auto const correction = pathState.established ? pathState.established->correction : 0;
  auto const nominal = evidence.value - correction;
  if (evidence.path == Path::Rx)
    {
      if (rx_ != nominal) tx_ = nominal;
      rx_ = nominal;
      if (context.monitoring) remembered_ = rx_;
    }
  else tx_ = nominal;
  if (pathState.established) pathState.established->nominal = nominal;
  change.after = snapshot ();
  return change;
}

void OperatingFrequency::setConnectionGeneration (quint64 generation, bool online)
{
  if (generation != generation_)
    {
      if (rxState_.pending || txState_.pending) requireRecovery ();
      rxState_.pending.reset ();
      txState_.pending.reset ();
      rxState_.lastEvidenceSerial = txState_.lastEvidenceSerial = 0;
      rxState_.lastExternalResult = txState_.lastExternalResult = 0;
      generation_ = generation;
    }
  online_ = online;
}

void OperatingFrequency::disconnect ()
{
  online_ = false;
  if (rxState_.pending || txState_.pending) requireRecovery ();
}

void OperatingFrequency::retry (quint64 generation)
{
  rxState_.pending.reset ();
  txState_.pending.reset ();
  rxState_.lastEvidenceSerial = txState_.lastEvidenceSerial = 0;
  rxState_.needsReapplication = txState_.needsReapplication = true;
  generation_ = generation;
  online_ = true;
  recoveryRequired_ = false;
  resynchronizing_ = true;
}
