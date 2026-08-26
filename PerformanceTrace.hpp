#ifndef PERFORMANCE_TRACE_HPP_
#define PERFORMANCE_TRACE_HPP_

#include <QtGlobal>
#include <QString>

namespace PerformanceTrace
{
  using RunId = quint64;

  // Measurements use one process-wide monotonic clock and are logged at INFO
  // severity. The functions are thread-safe, but an individual Phase object
  // must remain on one thread. Runs are retained until process exit so delayed
  // asynchronous milestones can still be attributed to their originating run.
  // Enable WSJT-X Diagnostic logging to capture the emitted "PERF" records.

  // A run groups related measurements on a shared monotonic timeline. The
  // category and kind are emitted verbatim, so keep them stable and free of
  // whitespace for log analysis. Records identify milestones, timed phases,
  // and run ends explicitly through the event field.
  RunId begin_run (char const * category, char const * kind);
  RunId current_run ();
  void finish_run (RunId run, char const * phase, QString const& details = {});
  qint64 elapsed_ms ();

  // Pass an explicit run from asynchronous work. current_run() may refer to a
  // newer operation by the time a worker thread or queued callback completes.
  // A finished run still accepts such explicitly attributed measurements.
  void milestone (char const * phase, QString const& details = {});
  void milestone (RunId run, char const * phase, QString const& details = {});

  // Phase records its duration when destroyed. Call finish() when details must
  // accompany the measurement; subsequent calls and destruction are no-ops.
  class Phase final
  {
  public:
    explicit Phase (char const * phase);
    Phase (RunId run, char const * phase);
    ~Phase () noexcept;

    Phase (Phase const&) = delete;
    Phase& operator= (Phase const&) = delete;

    void finish (QString const& details = {});

  private:
    RunId run_;
    QString phase_;
    qint64 started_ms_;
    bool finished_ {false};
  };
}

#endif
