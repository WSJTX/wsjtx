#include "Audio/TxPlaybackDiagnostics.hpp"

#include <atomic>

#include <QStringList>

namespace
{
  std::atomic<qint64> next_session_id {1};
  std::atomic<qint64> next_generation {1};

  qint64 framesFromUsecs (qint64 usecs, int sampleRateHz)
  {
    if (usecs < 0 || sampleRateHz <= 0)
      {
        return -1;
      }
    return usecs * sampleRateHz / 1000000;
  }

  QString reasonName (TxEvidence::TxStopReason reason)
  {
    switch (reason)
      {
      case TxEvidence::TxStopReason::NormalEnd: return "normal-end";
      case TxEvidence::TxStopReason::UserHalt: return "user-halt";
      case TxEvidence::TxStopReason::Watchdog: return "watchdog";
      case TxEvidence::TxStopReason::Error: return "error";
      case TxEvidence::TxStopReason::ModeChange: return "mode-change";
      }
    return "unknown";
  }

  QString tierName (TxEvidence::TxPlayoutTier tier)
  {
    switch (tier)
      {
      case TxEvidence::TxPlayoutTier::DeviceClock: return "device-clock";
      case TxEvidence::TxPlayoutTier::DeadReckoning: return "dead-reckoning";
      case TxEvidence::TxPlayoutTier::Unavailable: return "unavailable";
      }
    return "unavailable";
  }

  int tierStrength (TxEvidence::TxPlayoutTier tier)
  {
    switch (tier)
      {
      case TxEvidence::TxPlayoutTier::Unavailable: return 0;
      case TxEvidence::TxPlayoutTier::DeadReckoning: return 1;
      case TxEvidence::TxPlayoutTier::DeviceClock: return 2;
      }
    return 0;
  }

  QString combinedDiagnostic (QString const& source, QString const& playout)
  {
    if (source.isEmpty () || source == playout) return playout;
    if (playout.isEmpty ()) return source;
    return source + QStringLiteral (" | ") + playout;
  }
}

namespace TxEvidence
{
  TxPlaybackDiagnostics::TxPlaybackDiagnostics () = default;
  TxPlaybackDiagnostics::~TxPlaybackDiagnostics () = default;

  TxSessionId TxPlaybackDiagnostics::allocateSessionId ()
  {
    return TxSessionId {next_session_id.fetch_add (1, std::memory_order_relaxed)};
  }

  TxGeneration TxPlaybackDiagnostics::allocateGeneration ()
  {
    return TxGeneration {next_generation.fetch_add (1, std::memory_order_relaxed)};
  }

  TxStopDecision TxPlaybackDiagnostics::decisionFor (TxStopReason reason,
                                                      bool zeroTailMode)
  {
    TxStopDecision decision;
    decision.reason = reason;
    decision.tail_ms = zeroTailMode ? 0 : 200;
    return decision;
  }

  TxSessionId TxPlaybackDiagnostics::beginSession (QString const& mode,
                                                    int sampleRateHz,
                                                    qint64 committedEndSample,
                                                    bool targetKnown,
                                                    QString const& diagnostic)
  {
    auto const sessionId = allocateSessionId ();
    TxStartSnapshot snapshot;
    snapshot.session_id = sessionId;
    snapshot.generation = allocateGeneration ();
    snapshot.mode = mode;
    snapshot.sample_rate_hz = sampleRateHz;
    snapshot.committed_end_sample = committedEndSample;
    snapshot.target_known = targetKnown;
    snapshot.diagnostic = diagnostic;
    if (!commitStart (snapshot)) return TxSessionId::invalid ();
    findActive (sessionId, snapshot.generation)->claimed = false;
    return sessionId;
  }

  TxGeneration TxPlaybackDiagnostics::beginGeneration (TxSessionId sessionId)
  {
    for (auto& active : active_)
      {
        if (active.start.session_id == sessionId && !active.stopped)
          {
            if (!active.claimed)
              {
                active.claimed = true;
                return active.start.generation;
              }
            TxStartSnapshot next = active.start;
            next.generation = allocateGeneration ();
            commitStart (next);
            return next.generation;
          }
      }
    return TxGeneration::invalid ();
  }

  bool TxPlaybackDiagnostics::commitStart (TxStartSnapshot const& snapshot)
  {
    if (!snapshot.session_id.isValid () || !snapshot.generation.isValid () ||
        snapshot.sample_rate_hz <= 0)
      {
        return false;
      }
    if (findActive (snapshot.session_id, snapshot.generation))
      {
        return false;
      }
    ActiveSession active;
    active.start = snapshot;
    active.playout.session_id = snapshot.session_id;
    active.playout.generation = snapshot.generation;
    active_.append (active);
    return true;
  }

  bool TxPlaybackDiagnostics::commitTarget (TxSessionId sessionId,
                                             TxGeneration generation,
                                             qint64 committedEndSample,
                                             bool targetKnown,
                                             QString const& diagnostic)
  {
    auto * active = findActive (sessionId, generation);
    if (!active || committedEndSample < -1 ||
        (active->start.committed_end_sample >= 0 &&
         committedEndSample >= 0 &&
         committedEndSample < active->start.committed_end_sample))
      {
        return false;
      }
    active->start.committed_end_sample = committedEndSample;
    active->start.target_known = targetKnown;
    if (!diagnostic.isEmpty ()) active->start.diagnostic = diagnostic;
    active->playout = normalize (active->start, active->raw);
    if (active->stopped)
      {
        for (auto& item : terminal_)
          {
            if (item.session_id == sessionId && item.generation == generation)
              {
                item = terminal (*active);
                break;
              }
          }
      }
    return true;
  }

  bool TxPlaybackDiagnostics::observeSourceProgress (TxSessionId sessionId,
                                                       TxGeneration generation,
                                                       qint64 servedSamples,
                                                       qint64 totalSamples,
                                                       QString const& diagnostic)
  {
    auto * active = findActive (sessionId, generation);
    if (!active || servedSamples < 0 || totalSamples < servedSamples ||
        (active->source_served_frames >= 0 &&
         servedSamples < active->source_served_frames) ||
        (active->source_total_frames >= 0 &&
         totalSamples < active->source_total_frames))
      {
        return false;
      }
    active->source_served_frames = servedSamples;
    active->source_total_frames = totalSamples;
    if (!diagnostic.isEmpty ()) active->source_progress_diagnostic = diagnostic;
    if (active->stopped)
      {
        for (auto& item : terminal_)
          {
            if (item.session_id == sessionId && item.generation == generation)
              {
                item = terminal (*active);
                break;
              }
          }
      }
    return true;
  }

  bool TxPlaybackDiagnostics::observe (TxSessionId sessionId,
                                        TxGeneration generation,
                                        TxRawPlayoutSnapshot const& raw)
  {
    auto * active = findActive (sessionId, generation);
    if (!active)
      {
        return false;
      }
    if (raw.start_event)
      {
        if (raw.backend_start_sequence <= 0 ||
            (active->bound && raw.backend_start_sequence <= active->backend_start_sequence))
          {
            return false;
          }
        active->bound = true;
        active->backend_start_sequence = raw.backend_start_sequence;
      }
    else if (!active->bound || raw.backend_start_sequence != active->backend_start_sequence)
      {
        return false;
      }
    if ((active->raw.processed_usecs >= 0 && raw.processed_usecs >= 0 &&
         raw.processed_usecs < active->raw.processed_usecs) ||
        (active->raw.successfully_written_frames >= 0 &&
         raw.successfully_written_frames >= 0 &&
         raw.successfully_written_frames < active->raw.successfully_written_frames))
      {
        return false;
      }
    if (!raw.start_event && tierStrength (raw.tier) < tierStrength (active->raw.tier))
      {
        return false;
      }
    if (raw.source_served_frames >= 0 &&
        raw.source_total_frames >= raw.source_served_frames &&
        (active->source_served_frames < 0 ||
         raw.source_served_frames >= active->source_served_frames) &&
        (active->source_total_frames < 0 ||
         raw.source_total_frames >= active->source_total_frames))
      {
        active->source_served_frames = raw.source_served_frames;
        active->source_total_frames = raw.source_total_frames;
      }
    active->raw = raw;
    active->underrun = active->underrun || raw.error == TxBackendError::Underrun;
    active->playout = normalize (active->start, raw);
    if (active->stopped)
      {
        for (auto& item : terminal_)
          {
            if (item.session_id == sessionId && item.generation == generation)
              {
                item = terminal (*active);
                break;
              }
          }
      }
    return true;
  }

  TxStopDecision TxPlaybackDiagnostics::stop (TxSessionId sessionId,
                                               TxGeneration generation,
                                               TxStopReason reason,
                                               int legacyTailMs)
  {
    TxStopDecision decision;
    decision.reason = reason;
    decision.tail_ms = legacyTailMs;
    auto * active = findActive (sessionId, generation);
    if (!active)
      {
        return decision;
      }
    active->stopped = true;
    active->stop = decision;
    terminal_.append (terminal (*active));
    if (terminal_.size () > 16)
      {
        int const removeCount = terminal_.size () - 16;
        for (int index = 0; index != removeCount; ++index)
          {
            auto const expired = terminal_.at (index);
            for (int activeIndex = 0; activeIndex != active_.size (); ++activeIndex)
              {
                if (active_[activeIndex].start.session_id == expired.session_id &&
                    active_[activeIndex].start.generation == expired.generation)
                  {
                    active_.remove (activeIndex);
                    break;
                  }
              }
          }
        terminal_.remove (0, removeCount);
      }
    return decision;
  }

  QVector<TxTerminalSession> TxPlaybackDiagnostics::terminalSessions () const
  {
    return terminal_;
  }

  QString TxPlaybackDiagnostics::diagnosticDump () const
  {
    QStringList entries;
    for (auto const& active : active_)
      {
        if (active.stopped)
          {
            continue;
          }
        auto entry = QString ("active session=%1 generation=%2 mode=%3 tier=%4 "
                                 "target_known=%5 committed_end=%6 consumed=%7 "
                                 "served=%8 underrun=%9 sequence=%10")
                        .arg (active.start.session_id.value ())
                        .arg (active.start.generation.value ())
                        .arg (active.start.mode)
                        .arg (tierName (active.raw.tier))
                        .arg (active.start.target_known ? "true" : "false")
                        .arg (active.start.committed_end_sample)
                        .arg (active.playout.consumed_samples)
                        .arg (active.source_served_frames)
                        .arg (active.underrun ? "true" : "false")
                        .arg (active.backend_start_sequence);
        entry += QString (" source_diagnostic=\"%1\" playout_diagnostic=\"%2\"")
          .arg (combinedDiagnostic (active.start.diagnostic,
                                    active.source_progress_diagnostic),
                active.raw.diagnostic);
        entries.append (entry);
      }
    for (auto const& item : terminal_)
      {
        auto entry = QString ("terminal session=%1 generation=%2 mode=%3 tier=%4 "
                                 "reason=%5 tail_ms=%6 target_known=%7 committed_end=%8 "
                                 "consumed=%9 served=%10 underrun=%11 truncated=%12")
                        .arg (item.session_id.value ())
                        .arg (item.generation.value ())
                        .arg (item.mode)
                        .arg (tierName (item.tier))
                        .arg (reasonName (item.stop.reason))
                        .arg (item.stop.tail_ms)
                        .arg (item.target_known ? "true" : "false")
                        .arg (item.committed_end_sample)
                        .arg (item.consumed_samples)
                        .arg (item.source_served_samples)
                        .arg (item.underrun ? "true" : "false")
                        .arg (item.truncated_known ? (item.truncated ? "true" : "false") : "unknown");
        entry += QString (" diagnostic=\"%1\"").arg (item.diagnostic);
        entries.append (entry);
      }
    return entries.join ('\n');
  }

  TxPlaybackDiagnostics::ActiveSession * TxPlaybackDiagnostics::findActive (
    TxSessionId sessionId, TxGeneration generation)
  {
    for (auto& active : active_)
      {
        if (active.start.session_id == sessionId && active.start.generation == generation)
          {
            return &active;
          }
      }
    return nullptr;
  }

  TxPlaybackDiagnostics::ActiveSession const * TxPlaybackDiagnostics::findActive (
    TxSessionId sessionId, TxGeneration generation) const
  {
    for (auto const& active : active_)
      {
        if (active.start.session_id == sessionId && active.start.generation == generation)
          {
            return &active;
          }
      }
    return nullptr;
  }

  TxPlayoutSnapshot TxPlaybackDiagnostics::normalize (TxStartSnapshot const& start,
                                                       TxRawPlayoutSnapshot const& raw)
  {
    TxPlayoutSnapshot result;
    result.session_id = start.session_id;
    result.generation = start.generation;
    result.raw = raw;
    int const rate = raw.sample_rate_hz > 0 ? raw.sample_rate_hz : start.sample_rate_hz;
    if (raw.tier == TxPlayoutTier::DeviceClock)
      {
        result.consumed_samples = framesFromUsecs (raw.processed_usecs, rate);
      }
    else if (raw.tier == TxPlayoutTier::DeadReckoning)
      {
        result.consumed_samples = raw.successfully_written_frames;
      }
    if (result.consumed_samples >= 0 && raw.report_interval_ms > 0 && rate > 0)
      {
        result.uncertainty_samples = qint64 (raw.report_interval_ms) * rate / 1000;
      }
    return result;
  }

  TxTerminalSession TxPlaybackDiagnostics::terminal (ActiveSession const& active)
  {
    TxTerminalSession result;
    result.session_id = active.start.session_id;
    result.generation = active.start.generation;
    result.mode = active.start.mode;
    result.tier = active.raw.tier;
    result.target_known = active.start.target_known;
    result.committed_end_sample = active.start.committed_end_sample;
    result.source_served_samples = active.source_served_frames;
    result.consumed_samples = active.playout.consumed_samples;
    result.uncertainty_samples = active.playout.uncertainty_samples;
    result.underrun = active.underrun;
    qint64 const observedSamples = result.consumed_samples >= 0
      ? result.consumed_samples : result.source_served_samples;
    result.truncated_known = result.target_known && observedSamples >= 0;
    result.truncated = result.truncated_known &&
      observedSamples <= result.committed_end_sample;
    result.terminal_playout_observed = active.bound && active.raw.available &&
      active.raw.tier != TxPlayoutTier::Unavailable;
    result.backend_start_sequence = active.backend_start_sequence;
    result.stop = active.stop;
    result.diagnostic = combinedDiagnostic (
      combinedDiagnostic (active.start.diagnostic,
                          active.source_progress_diagnostic),
      active.raw.diagnostic);
    return result;
  }
}
