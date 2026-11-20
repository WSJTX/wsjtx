#include "ModeEntry.hpp"

#include <utility>

ModeEntry::Policy ModeEntry::policyFor (Request const& request, bool hound)
{
  auto target = TargetKind::None;
  if (request.retune == RetunePolicy::Explicit) target = TargetKind::Explicit;
  else if (request.retune == RetunePolicy::Working
           && !(request.mode == "FT8" && hound))
    target = request.mode == "FreqCal" ? TargetKind::CalibrationStart : TargetKind::Working;
  return {target, request.selectionEffects && request.mode != "FreqCal"};
}

quint64 ModeEntry::Queue::submit (Request request)
{
  auto const identity = ++latest_;
  pending_.push_back ({identity, std::move (request)});
  return identity;
}

std::optional<ModeEntry::Operation> ModeEntry::Queue::takeNext ()
{
  if (active_ || pending_.empty ()) return std::nullopt;
  active_ = std::move (pending_.front ());
  pending_.pop_front ();
  return active_;
}

void ModeEntry::Queue::accept (quint64 identity)
{
  if (active_ && active_->identity == identity) accepted_ = identity;
}

void ModeEntry::Queue::finish (quint64 identity)
{
  if (active_ && active_->identity == identity) active_.reset ();
}

void ModeEntry::PendingRetune::defer (Operation operation, quint64 connection,
                                    Radio::Frequency rx)
{
  pending_ = Pending {std::move (operation), connection, rx};
}

void ModeEntry::PendingRetune::follow (Radio::Frequency rx)
{
  if (!pending_ || !pending_->rx
      || policyFor (pending_->operation.request, false).target != TargetKind::CalibrationStart) return;
  pending_->rx = rx;
}

std::optional<ModeEntry::Operation> ModeEntry::PendingRetune::resume (
  quint64 acceptedIdentity, quint64 connection, OperatingFrequency::Snapshot const& state)
{
  if (!pending_) return std::nullopt;
  if (pending_->operation.identity != acceptedIdentity
      || (pending_->rx && (pending_->connection != connection || pending_->rx != state.rx)))
    {
      cancel ();
      return std::nullopt;
    }
  if (!state.rx || state.pendingRx || state.pendingTx) return std::nullopt;
  auto operation = std::move (pending_->operation);
  cancel ();
  return operation;
}
