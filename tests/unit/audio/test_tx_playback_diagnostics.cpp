#include <QtTest>

#include <type_traits>

#include "Audio/TxPlaybackDiagnostics.hpp"

namespace
{
  TxEvidence::TxRawPlayoutSnapshot deviceClockStart (qint64 sequence)
  {
    TxEvidence::TxRawPlayoutSnapshot raw;
    raw.tier = TxEvidence::TxPlayoutTier::DeviceClock;
    raw.backend_start_sequence = sequence;
    raw.start_event = true;
    raw.available = true;
    raw.state = TxEvidence::TxBackendState::Active;
    raw.sample_rate_hz = 12000;
    raw.processed_usecs = 250000;
    raw.successfully_written_frames = 3000;
    raw.report_interval_ms = 25;
    return raw;
  }
}

class TestTxPlaybackDiagnostics : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void identities_are_trivial_and_strong ()
  {
    static_assert (std::is_trivially_copyable<TxEvidence::TxSessionId>::value,
                   "session identities must remain trivially copyable");
    static_assert (std::is_trivially_copyable<TxEvidence::TxGeneration>::value,
                   "generation identities must remain trivially copyable");
    static_assert (!std::is_convertible<qint64, TxEvidence::TxSessionId>::value,
                   "session identity conversion must be explicit");
    QCOMPARE (TxEvidence::TxSessionId {}.value (), qint64 (0));
    QCOMPARE (TxEvidence::TxSessionId::invalid (), TxEvidence::TxSessionId {});
    QCOMPARE (TxEvidence::TxGeneration::invalid (), TxEvidence::TxGeneration {});
    QVERIFY (!TxEvidence::TxGeneration {}.isValid ());
    QVERIFY (TxEvidence::TxSessionId {4} < TxEvidence::TxSessionId {5});
    QVERIFY (TxEvidence::TxGeneration {4} < TxEvidence::TxGeneration {5});
    QVERIFY (qMetaTypeId<TxEvidence::TxSessionId> () != QMetaType::UnknownType);
    QVERIFY (qMetaTypeId<TxEvidence::TxGeneration> () != QMetaType::UnknownType);
    auto const generation1 = TxEvidence::TxPlaybackDiagnostics::allocateGeneration ();
    auto const generation2 = TxEvidence::TxPlaybackDiagnostics::allocateGeneration ();
    QVERIFY (generation1 < generation2);
  }

  Q_SLOT void preserves_every_stop_reason_and_tail ()
  {
    TxEvidence::TxPlaybackDiagnostics recorder;
    TxEvidence::TxStopReason const reasons[] = {
      TxEvidence::TxStopReason::NormalEnd,
      TxEvidence::TxStopReason::UserHalt,
      TxEvidence::TxStopReason::Watchdog,
      TxEvidence::TxStopReason::Error,
      TxEvidence::TxStopReason::ModeChange
    };
    for (int index = 0; index != 5; ++index)
      {
        auto const session = recorder.beginSession ("FT8", 12000);
        auto const generation = recorder.beginGeneration (session);
        QVERIFY (generation.isValid ());
        auto const decision = recorder.stop (session, generation, reasons[index], index + 1);
        QCOMPARE (decision.reason, reasons[index]);
        QCOMPARE (decision.tail_ms, index + 1);
      }
    QCOMPARE (recorder.terminalSessions ().size (), 5);
  }

  Q_SLOT void constructs_bounded_commit_extents ()
  {
    QCOMPARE (TxEvidence::boundedCommittedEndSample (0, 0, 47999), qint64 (47999));
    QCOMPARE (TxEvidence::boundedCommittedEndSample (4800, 0, 47999), qint64 (52799));
    QCOMPARE (TxEvidence::boundedCommittedEndSample (0, 12000, 47999), qint64 (35999));
    QCOMPARE (TxEvidence::boundedCommittedEndSample (0, 48000, 47999), qint64 (-1));
    QCOMPARE (TxEvidence::interleavedFrameCount (2000, 2), qint64 (1000));
    QCOMPARE (TxEvidence::interleavedFrameCount (1999, 2), qint64 (-1));
  }

  Q_SLOT void rejects_invalid_session_start ()
  {
    TxEvidence::TxPlaybackDiagnostics recorder;
    QCOMPARE (recorder.beginSession ("FT8", 0), TxEvidence::TxSessionId::invalid ());
  }

  Q_SLOT void maps_legacy_tails_for_every_stop_reason ()
  {
    TxEvidence::TxStopReason const reasons[] = {
      TxEvidence::TxStopReason::NormalEnd,
      TxEvidence::TxStopReason::UserHalt,
      TxEvidence::TxStopReason::Watchdog,
      TxEvidence::TxStopReason::Error,
      TxEvidence::TxStopReason::ModeChange
    };
    for (auto reason : reasons)
      {
        QCOMPARE (TxEvidence::TxPlaybackDiagnostics::decisionFor (reason, false).tail_ms, 200);
        QCOMPARE (TxEvidence::TxPlaybackDiagnostics::decisionFor (reason, true).tail_ms, 0);
      }
  }

  Q_SLOT void captures_lifecycle_and_growing_target ()
  {
    TxEvidence::TxPlaybackDiagnostics recorder;
    auto const session = recorder.beginSession ("JTTY", 12000, 100, true);
    auto const generation = recorder.beginGeneration (session);
    QVERIFY (recorder.commitTarget (session, generation, 300));
    QVERIFY (!recorder.commitTarget (session, generation, 299));
    QVERIFY (recorder.observe (session, generation, deviceClockStart (7)));
    QVERIFY (recorder.observeSourceProgress (session, generation, 250, 301,
                                             "JTTY FIFO progress"));
    QVERIFY (!recorder.observeSourceProgress (session, generation, 249, 301));
    recorder.stop (session, generation, TxEvidence::TxStopReason::NormalEnd, 42);
    auto later = deviceClockStart (7);
    later.start_event = false;
    later.processed_usecs = 300000;
    QVERIFY (recorder.observe (session, generation, later));

    auto terminal = recorder.terminalSessions ().last ();
    QCOMPARE (terminal.committed_end_sample, qint64 (300));
    QCOMPARE (terminal.consumed_samples, qint64 (3600));
    QCOMPARE (terminal.source_served_samples, qint64 (250));
    QCOMPARE (terminal.stop.tail_ms, 42);
    QVERIFY (terminal.terminal_playout_observed);
    QVERIFY (terminal.diagnostic.contains ("JTTY FIFO progress"));

    QVERIFY (recorder.commitTarget (session, generation, 400, true,
                                    "Final JTTY source commitment"));
    terminal = recorder.terminalSessions ().last ();
    QCOMPARE (terminal.committed_end_sample, qint64 (400));
    QVERIFY (terminal.diagnostic.contains ("Final JTTY source commitment"));
    QVERIFY (terminal.diagnostic.contains ("JTTY FIFO progress"));
    QVERIFY (recorder.diagnosticDump ().contains ("Final JTTY source commitment"));
  }

  Q_SLOT void normalizes_evidence_tiers_and_underrun ()
  {
    TxEvidence::TxPlaybackDiagnostics recorder;
    auto const session = recorder.beginSession ("FT4", 12000, 7999, true);
    auto const generation = recorder.beginGeneration (session);

    auto raw = deviceClockStart (10);
    raw.processed_usecs = 500000;
    raw.source_served_frames = 5000;
    raw.error = TxEvidence::TxBackendError::Underrun;
    QVERIFY (recorder.observe (session, generation, raw));
    raw.start_event = false;
    raw.processed_usecs = 600000;
    raw.error = TxEvidence::TxBackendError::None;
    QVERIFY (recorder.observe (session, generation, raw));
    recorder.stop (session, generation, TxEvidence::TxStopReason::Error);
    auto terminal = recorder.terminalSessions ().last ();
    QCOMPARE (terminal.consumed_samples, qint64 (7200));
    QVERIFY (terminal.underrun);
    QVERIFY (terminal.truncated_known);
    QVERIFY (terminal.truncated);
    QVERIFY (recorder.diagnosticDump ().contains ("underrun=true"));

    auto const deadSession = recorder.beginSession ("FT8", 12000);
    auto const deadGeneration = recorder.beginGeneration (deadSession);
    raw = deviceClockStart (11);
    raw.tier = TxEvidence::TxPlayoutTier::DeadReckoning;
    raw.successfully_written_frames = 123;
    QVERIFY (recorder.observe (deadSession, deadGeneration, raw));
    recorder.stop (deadSession, deadGeneration, TxEvidence::TxStopReason::NormalEnd);
    QCOMPARE (recorder.terminalSessions ().last ().consumed_samples, qint64 (123));

    auto const unavailableSession = recorder.beginSession ("FT8", 12000);
    auto const unavailableGeneration = recorder.beginGeneration (unavailableSession);
    raw = deviceClockStart (12);
    raw.tier = TxEvidence::TxPlayoutTier::Unavailable;
    QVERIFY (recorder.observe (unavailableSession, unavailableGeneration, raw));
    recorder.stop (unavailableSession, unavailableGeneration,
                   TxEvidence::TxStopReason::NormalEnd);
    QCOMPARE (recorder.terminalSessions ().last ().consumed_samples, qint64 (-1));
    QVERIFY (!recorder.terminalSessions ().last ().terminal_playout_observed);
  }

  Q_SLOT void unavailable_start_is_retained_and_obvious ()
  {
    TxEvidence::TxPlaybackDiagnostics recorder;
    auto const session = recorder.beginSession ("FT8", 48000, 47999, true);
    auto const generation = recorder.beginGeneration (session);
    TxEvidence::TxRawPlayoutSnapshot raw;
    raw.start_event = true;
    raw.backend_start_sequence = 20;
    raw.tier = TxEvidence::TxPlayoutTier::Unavailable;
    raw.state = TxEvidence::TxBackendState::Unavailable;
    raw.diagnostic = "No audio output device configured";
    QVERIFY (recorder.observe (session, generation, raw));
    recorder.stop (session, generation, TxEvidence::TxStopReason::Error, 200);
    auto const terminal = recorder.terminalSessions ().last ();
    QCOMPARE (terminal.tier, TxEvidence::TxPlayoutTier::Unavailable);
    QVERIFY (!terminal.terminal_playout_observed);
    QVERIFY (recorder.diagnosticDump ().contains ("tier=unavailable"));
  }

  Q_SLOT void weaker_snapshot_does_not_replace_device_clock_evidence ()
  {
    TxEvidence::TxPlaybackDiagnostics recorder;
    auto const session = recorder.beginSession ("JTTY", 12000, 2999, true);
    auto const generation = recorder.beginGeneration (session);
    auto raw = deviceClockStart (21);
    QVERIFY (recorder.observe (session, generation, raw));
    recorder.stop (session, generation, TxEvidence::TxStopReason::NormalEnd);

    TxEvidence::TxRawPlayoutSnapshot unavailable;
    unavailable.backend_start_sequence = 21;
    unavailable.diagnostic = "No audio output stream is active";
    QVERIFY (!recorder.observe (session, generation, unavailable));

    auto const terminal = recorder.terminalSessions ().last ();
    QCOMPARE (terminal.tier, TxEvidence::TxPlayoutTier::DeviceClock);
    QCOMPARE (terminal.consumed_samples, qint64 (3000));
    QVERIFY (terminal.terminal_playout_observed);
  }

  Q_SLOT void rejects_stale_and_regressing_events ()
  {
    TxEvidence::TxPlaybackDiagnostics recorder;
    auto const session = recorder.beginSession ("FT8", 12000);
    auto const generation = recorder.beginGeneration (session);
    auto raw = deviceClockStart (17);
    QVERIFY (recorder.observe (session, generation, raw));
    QVERIFY (!recorder.observe (session, generation, raw));
    QVERIFY (!recorder.observe (session, TxEvidence::TxGeneration {generation.value () - 1}, raw));
    raw.start_event = false;
    raw.backend_start_sequence = 18;
    QVERIFY (!recorder.observe (session, generation, raw));
    raw.backend_start_sequence = 17;
    raw.processed_usecs = 1;
    QVERIFY (!recorder.observe (session, generation, raw));

    recorder.stop (session, generation, TxEvidence::TxStopReason::NormalEnd);
    raw.processed_usecs = 750000;
    QVERIFY (recorder.observe (session, generation, raw));
    QCOMPARE (recorder.terminalSessions ().last ().consumed_samples, qint64 (9000));
  }

  Q_SLOT void retains_the_last_sixteen_terminal_sessions ()
  {
    TxEvidence::TxPlaybackDiagnostics recorder;
    for (int index = 0; index != 20; ++index)
      {
        auto const session = recorder.beginSession ("FT8", 12000);
        auto const generation = recorder.beginGeneration (session);
        recorder.stop (session, generation, TxEvidence::TxStopReason::NormalEnd);
      }
    auto const terminals = recorder.terminalSessions ();
    QCOMPARE (terminals.size (), 16);
    QVERIFY (terminals.first ().session_id.value () < terminals.last ().session_id.value ());
    QVERIFY (recorder.diagnosticDump ().contains ("terminal"));
  }
};

QTEST_GUILESS_MAIN (TestTxPlaybackDiagnostics)

#include "test_tx_playback_diagnostics.moc"
