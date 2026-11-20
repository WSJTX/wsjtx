#ifndef MODE_ENTRY_HPP
#define MODE_ENTRY_HPP

#include <deque>
#include <optional>
#include <QString>
#include "Radio.hpp"
#include "OperatingFrequency.hpp"

namespace ModeEntry
{
  enum class RetunePolicy {Working, Preserve, Explicit};

  struct Request
  {
    QString mode;
    RetunePolicy retune {RetunePolicy::Working};
    std::optional<Radio::Frequency> frequency {};
    bool selectionEffects {true};
    bool userInitiated {false};
  };

  enum class TargetKind {None, Working, Explicit, CalibrationStart};

  struct Policy
  {
    TargetKind target;
    bool selectionEffects;
  };

  Policy policyFor (Request const&, bool hound);

  struct Operation
  {
    quint64 identity;
    Request request;
  };

  class Queue
  {
  public:
    quint64 submit (Request);
    std::optional<Operation> takeNext ();
    void accept (quint64 identity);
    void finish (quint64 identity);
    bool active () const {return active_.has_value ();}
    quint64 activeIdentity () const {return active_ ? active_->identity : 0;}
    Request const * activeRequest () const {return active_ ? &active_->request : nullptr;}
    quint64 acceptedIdentity () const {return accepted_;}
    bool current (quint64 identity) const {return identity && identity == accepted_;}

  private:
    quint64 latest_ {0};
    quint64 accepted_ {0};
    std::optional<Operation> active_;
    std::deque<Operation> pending_;
  };

  class PendingRetune
  {
  public:
    void defer (Operation, quint64 connection, Radio::Frequency rx);
    void cancel () {pending_.reset ();}
    // The application, not the operator, has moved or discovered the dial: a
    // pending calibration start expects the new frequency instead of being
    // cancelled by it. Other pending retunes are left as they are.
    void follow (Radio::Frequency rx);
    bool pending () const {return pending_.has_value ();}
    std::optional<Operation> resume (quint64 acceptedIdentity, quint64 connection,
                                    OperatingFrequency::Snapshot const&);

  private:
    struct Pending
    {
      Operation operation;
      quint64 connection;
      Radio::Frequency rx;
    };
    std::optional<Pending> pending_;
  };
}

#endif
