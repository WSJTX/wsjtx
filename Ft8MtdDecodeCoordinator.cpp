#include "Ft8MtdDecodeCoordinator.hpp"

Ft8MtdDecodeCoordinator::Decision Ft8MtdDecodeCoordinator::request (
    Stage stage, qint64 period, int earlyStageCount, bool decoderBusy)
{
  return scheduler_.request (stage, period, earlyStageCount, decoderBusy,
                             hasPending ());
}

bool Ft8MtdDecodeCoordinator::published (Stage stage, qint64 period)
{
  return scheduler_.published (stage, period);
}

void Ft8MtdDecodeCoordinator::completed (Stage stage, qint64 period)
{
  scheduler_.completed (stage, period);
}

void Ft8MtdDecodeCoordinator::publicationFailed (Stage stage, qint64 period)
{
  scheduler_.publicationFailed (stage, period);
}

Ft8MtdDecodeCoordinator::Action Ft8MtdDecodeCoordinator::deferFinal (
    std::unique_ptr<PendingMtdDecode> pending)
{
  Q_ASSERT (pending);
  Q_ASSERT (8 == pending->payload.params.nmode);
  Q_ASSERT (pending->payload.params.lmultift8);
  auto const action = pending_ ? Action::ReplaceFinal : Action::DeferFinal;
  pending_ = std::move (pending);
  return action;
}

Ft8MtdDecodeCoordinator::DrainOutcome Ft8MtdDecodeCoordinator::drainPending (
    PendingValidator const& validator, PendingPublisher const& publisher)
{
  if (!pending_) return {};

  auto pending = std::move (pending_);
  DrainOutcome outcome;
  outcome.period = pending->period;
  if (!validator (*pending))
    {
      scheduler_.cancel ();
      outcome.result = DrainResult::Obsolete;
      return outcome;
    }

  switch (publisher (*pending))
    {
    case PendingPublishResult::Published:
      outcome.result = DrainResult::Published;
      outcome.recovered = scheduler_.published (Stage::Final, pending->period);
      return outcome;
    case PendingPublishResult::Unavailable:
      pending_ = std::move (pending);
      outcome.result = DrainResult::Retained;
      return outcome;
    case PendingPublishResult::Failed:
      scheduler_.publicationFailed (Stage::Final, pending->period);
      outcome.result = DrainResult::Failed;
      return outcome;
    }

  return outcome;
}

void Ft8MtdDecodeCoordinator::cancel ()
{
  scheduler_.cancel ();
  pending_.reset ();
}
