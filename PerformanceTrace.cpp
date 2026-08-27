#include "PerformanceTrace.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include "Logger.hpp"

namespace
{
  enum class Event {milestone, phase, run_end};

  struct Run
  {
    QString category;
    QString kind;
    qint64 started_ms {0};
    bool finished {false};
  };

  struct State
  {
    QMutex mutex;
    QElapsedTimer process_timer;
    QHash<PerformanceTrace::RunId, Run> runs;
    PerformanceTrace::RunId current_run {0};
    PerformanceTrace::RunId next_run {1};
  };

  State& state ()
  {
    static State value;
    return value;
  }

  void ensure_started (State& value)
  {
    if (!value.process_timer.isValid ())
      {
        value.process_timer.start ();
      }
  }

  void ensure_current_run (State& value)
  {
    ensure_started (value);
    if (!value.current_run)
      {
        value.current_run = value.next_run++;
        value.runs.insert (value.current_run, {"performance", "implicit", 0});
      }
  }

  void record (PerformanceTrace::RunId run, Event event, QString const& phase,
               qint64 duration_ms, QString const& details)
  {
    QString category;
    QString kind;
    qint64 elapsed {0};
    qint64 run_elapsed {0};
    {
      auto& value = state ();
      QMutexLocker lock {&value.mutex};
      ensure_current_run (value);
      elapsed = value.process_timer.elapsed ();
      auto const run_info = value.runs.value (run, value.runs.value (value.current_run));
      category = run_info.category;
      kind = run_info.kind;
      run_elapsed = elapsed - run_info.started_ms;
    }

    char const * event_name = "milestone";
    if (Event::phase == event)
      {
        event_name = "phase";
      }
    else if (Event::run_end == event)
      {
        event_name = "run_end";
      }
    QString message = QString {"PERF %1 pid=%2 run=%3 kind=%4 event=%5 phase=%6 elapsed_ms=%7"}
      .arg (category).arg (QCoreApplication::applicationPid ()).arg (run)
      .arg (kind, QString::fromLatin1 (event_name), phase).arg (run_elapsed);
    if (duration_ms >= 0)
      {
        message += QString {" duration_ms=%1"}.arg (duration_ms);
      }
    if (!details.isEmpty ())
      {
        message += ' ' + details;
      }
    LOG_INFO (message.toStdString ());
  }
}

namespace PerformanceTrace
{
  RunId begin_run (char const * category, char const * kind)
  {
    RunId run;
    {
      auto& value = state ();
      QMutexLocker lock {&value.mutex};
      ensure_started (value);
      run = value.next_run++;
      value.current_run = run;
      value.runs.insert (run, {QString::fromLatin1 (category), QString::fromLatin1 (kind),
                               value.process_timer.elapsed ()});
    }
    return run;
  }

  RunId current_run ()
  {
    auto& value = state ();
    QMutexLocker lock {&value.mutex};
    ensure_current_run (value);
    return value.current_run;
  }

  void finish_run (RunId run, char const * phase, QString const& details)
  {
    bool finished {false};
    {
      auto& value = state ();
      QMutexLocker lock {&value.mutex};
      ensure_current_run (value);
      auto run_info = value.runs.find (run);
      if (run_info != value.runs.end () && !run_info->finished)
        {
          run_info->finished = true;
          finished = true;
        }
    }
    if (finished)
      {
        record (run, Event::run_end, QString::fromLatin1 (phase), -1, details);
      }
  }

  qint64 elapsed_ms ()
  {
    auto& value = state ();
    QMutexLocker lock {&value.mutex};
    ensure_started (value);
    return value.process_timer.elapsed ();
  }

  void milestone (char const * phase, QString const& details)
  {
    milestone (current_run (), phase, details);
  }

  void milestone (RunId run, char const * phase, QString const& details)
  {
    record (run, Event::milestone, QString::fromLatin1 (phase), -1, details);
  }

  Phase::Phase (char const * phase)
    : Phase {current_run (), phase}
  {
  }

  Phase::Phase (RunId run, char const * phase)
    : run_ {run}
    , phase_ {QString::fromLatin1 (phase)}
    , started_ms_ {elapsed_ms ()}
  {
  }

  Phase::~Phase () noexcept
  {
    if (!finished_)
      {
        try
          {
            finish ();
          }
        catch (...)
          {
            // Instrumentation must not terminate an already-unwinding path.
          }
      }
  }

  void Phase::finish (QString const& details)
  {
    if (!finished_)
      {
        finished_ = true;
        auto const finished_ms = elapsed_ms ();
        record (run_, Event::phase, phase_, finished_ms - started_ms_, details);
      }
  }
}
