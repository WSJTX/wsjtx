#include "Ft8MtdDecodeScheduler.hpp"

#include <algorithm>

constexpr int Ft8MtdDecodeScheduler::BackoffPeriods[];

Ft8MtdDecodeScheduler::Decision Ft8MtdDecodeScheduler::request (
    Stage stage, qint64 period, int earlyStageCount, bool decoderBusy,
    bool finalAlreadyPending)
{
  Decision result;
  recoverOnFinalPublish_ = false;

  if (Stage::None == stage) return result;

  if (Stage::Final == stage)
    {
      if (decoderBusy)
        {
          result.supersedeActiveEarly = activePeriod_ == period
            && (Stage::EarlyOne == activeStage_ || Stage::EarlyTwo == activeStage_);
          auto const wasDegraded = degraded_;
          auto const failedProbe = probePeriod_ == period
            && !probeComplete (period);
          degrade (period, wasDegraded && failedProbe);
          result.enteredDegraded = !wasDegraded;
          result.action = finalAlreadyPending ? Action::ReplaceFinal
                                              : Action::DeferFinal;
          result.skippedPeriods = skippedPeriods ();
          return result;
        }

      recoverOnFinalPublish_ = degraded_
        && (0 == earlyStageCount || probeComplete (period));
      return result;
    }

  if (!degraded_)
    {
      if (!decoderBusy) return result;
      degrade (period, false);
      result.enteredDegraded = true;
      result.action = Action::SkipEarly;
      result.skippedPeriods = skippedPeriods ();
      return result;
    }

  if (Stage::EarlyOne == stage)
    {
      if (period < nextProbePeriod_)
        {
          result.action = Action::SkipEarly;
          result.skippedPeriods = skippedPeriods ();
          return result;
        }
      if (decoderBusy)
        {
          degrade (period, true);
          result.action = Action::SkipEarly;
          result.skippedPeriods = skippedPeriods ();
          return result;
        }
      probePeriod_ = period;
      probeEarlyStageCount_ = std::max (earlyStageCount, 1);
      completedEarlyStages_ = 0;
      return result;
    }

  auto const precedingStageCompleted = probePeriod_ == period
    && completedEarlyStages_ >= 1;
  if (!precedingStageCompleted || decoderBusy)
    {
      if (probePeriod_ == period) degrade (period, true);
      result.action = Action::SkipEarly;
      result.skippedPeriods = skippedPeriods ();
    }
  return result;
}

bool Ft8MtdDecodeScheduler::published (Stage stage, qint64 period)
{
  activeStage_ = stage;
  activePeriod_ = period;
  if (Stage::Final != stage || !recoverOnFinalPublish_) return false;

  degraded_ = false;
  backoffIndex_ = 0;
  nextProbePeriod_ = 0;
  recoverOnFinalPublish_ = false;
  clearProbe ();
  return true;
}

void Ft8MtdDecodeScheduler::completed (Stage stage, qint64 period)
{
  if (stage == activeStage_ && period == activePeriod_)
    {
      activeStage_ = Stage::None;
      activePeriod_ = -1;
    }
  if (period != probePeriod_) return;
  if (Stage::EarlyOne == stage)
    {
      completedEarlyStages_ = std::max (completedEarlyStages_, 1);
    }
  else if (Stage::EarlyTwo == stage && completedEarlyStages_ >= 1)
    {
      completedEarlyStages_ = std::max (completedEarlyStages_, 2);
    }
}

void Ft8MtdDecodeScheduler::publicationFailed (Stage stage, qint64 period)
{
  if (period == probePeriod_ && Stage::Final != stage)
    {
      degrade (period, true);
    }
  activeStage_ = Stage::None;
  activePeriod_ = -1;
  recoverOnFinalPublish_ = false;
}

void Ft8MtdDecodeScheduler::cancel ()
{
  degraded_ = false;
  backoffIndex_ = 0;
  nextProbePeriod_ = 0;
  activeStage_ = Stage::None;
  activePeriod_ = -1;
  recoverOnFinalPublish_ = false;
  clearProbe ();
}

int Ft8MtdDecodeScheduler::skippedPeriods () const
{
  return degraded_ ? BackoffPeriods[backoffIndex_] : 0;
}

void Ft8MtdDecodeScheduler::degrade (qint64 period, bool advance)
{
  if (!degraded_)
    {
      degraded_ = true;
      backoffIndex_ = 0;
    }
  else if (advance)
    {
      backoffIndex_ = std::min (backoffIndex_ + 1, BackoffCount - 1);
    }
  nextProbePeriod_ = period + BackoffPeriods[backoffIndex_] + 1;
  recoverOnFinalPublish_ = false;
  clearProbe ();
}

bool Ft8MtdDecodeScheduler::probeComplete (qint64 period) const
{
  return probePeriod_ == period
    && completedEarlyStages_ >= probeEarlyStageCount_;
}

void Ft8MtdDecodeScheduler::clearProbe ()
{
  probePeriod_ = -1;
  probeEarlyStageCount_ = 0;
  completedEarlyStages_ = 0;
}
