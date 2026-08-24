#ifndef WORKED_BEFORE_LOAD_STATE_HPP
#define WORKED_BEFORE_LOAD_STATE_HPP

#include <QtGlobal>

class WorkedBeforeLoadState final
{
public:
  enum class ReloadResult
  {
    Start,
    Coalesced
  };

  enum class CompletionResult
  {
    Idle,
    Restart
  };

  ReloadResult request_reload ()
  {
    if (Phase::Idle == phase_)
      {
        phase_ = Phase::Loading;
        return ReloadResult::Start;
      }

    reload_pending_ = true;
    return ReloadResult::Coalesced;
  }

  void begin_completion ()
  {
    Q_ASSERT (Phase::Loading == phase_);
    phase_ = Phase::Completing;
  }

  CompletionResult finish_completion ()
  {
    Q_ASSERT (Phase::Completing == phase_);
    if (reload_pending_)
      {
        reload_pending_ = false;
        phase_ = Phase::Loading;
        return CompletionResult::Restart;
      }

    phase_ = Phase::Idle;
    return CompletionResult::Idle;
  }

  bool active () const
  {
    return Phase::Idle != phase_;
  }

private:
  enum class Phase
  {
    Idle,
    Loading,
    Completing
  };

  Phase phase_ {Phase::Idle};
  bool reload_pending_ {false};
};

#endif // WORKED_BEFORE_LOAD_STATE_HPP
