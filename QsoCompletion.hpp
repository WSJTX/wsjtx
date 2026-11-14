#ifndef QSOCOMPLETION_HPP
#define QSOCOMPLETION_HPP

#include <memory>
#include <QString>
#include "QsoCompletionPhase.hpp"
#include "QsoLogData.hpp"

struct QsoCompletion
{
  using Phase = QsoCompletionPhase;
  enum class LogState {None, Pending, Accepted, Declined};

  QString call;
  QString mode;
  QString band;
  double trPeriod {0};
  Phase phase {Phase::Ordinary};
  LogState logState {LogState::None};
  bool clearDxPending {false};
  bool clearGridPending {false};
  QDateTime confirmedAt;

  bool waiting () const
  {
    return phase == Phase::AwaitingFinal73 || phase == Phase::FinalReplyPending;
  }

  bool defersClear () const {return waiting () || phase == Phase::TimedOut;}

  bool matches (QString const& otherCall, QString const& otherMode,
                QString const& otherBand, double otherPeriod) const
  {
    return call == otherCall && mode == otherMode && band == otherBand
      && trPeriod == otherPeriod;
  }

  void cancel ()
  {
    phase = Phase::Cancelled;
    clearDxPending = false;
    clearGridPending = false;
  }
};

struct QsoLogRequest
{
  enum class Stage {Queued, ReadyToDisplay};
  QsoLogData data;
  std::shared_ptr<QsoCompletion> context;
  bool automatic {true};
  Stage stage {Stage::Queued};
};

enum class QsoRepeatEnd
{
  FinalReply,
  Deadline,
  OperatorStop,
  ActivityChanged,
  AutoDisabled,
  Fault,
  Superseded
};

#endif
