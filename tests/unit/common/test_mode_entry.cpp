#include <QtTest>
#include "ModeEntry.hpp"

namespace
{
  OperatingFrequency::Snapshot receiving (Radio::Frequency frequency = 14074000)
  {
    OperatingFrequency::Snapshot state {};
    state.rx = state.tx = state.correctedRx = state.correctedTx = frequency;
    return state;
  }
}

class TestModeEntry final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void target_policy_data ();
  void target_policy ();
  void reentrant_requests_wait_for_current_operation ();
  void queued_request_replaces_only_after_acceptance ();
  void rejected_request_preserves_installed_mode ();
  void stale_acceptance_cannot_replace_current_mode ();
  void queued_request_preserves_its_policy ();
  void stale_completion_cannot_release_replacement ();
  void retune_waits_for_both_frequency_paths ();
  void startup_retune_uses_first_available_radio_state ();
  void changed_retune_context_is_discarded_data ();
  void changed_retune_context_is_discarded ();
  void corrections_do_not_supersede_retune ();
  void calibration_start_follows_dial_moved_by_the_application ();
  void follow_leaves_other_retunes_alone ();
  void explicit_cancellation_survives_return_to_old_frequency ();
  void replacement_retune_retains_its_own_context ();
};

void TestModeEntry::target_policy_data ()
{
  using Retune = ModeEntry::RetunePolicy;
  using Target = ModeEntry::TargetKind;
  QTest::addColumn<QString> ("mode");
  QTest::addColumn<int> ("retune");
  QTest::addColumn<bool> ("hound");
  QTest::addColumn<bool> ("requestedEffects");
  QTest::addColumn<int> ("target");
  QTest::addColumn<bool> ("selectionEffects");
  auto const row = [] (char const * name, QString const& mode, Retune retune, bool hound,
                      bool requestedEffects, Target target, bool selectionEffects) {
    QTest::newRow (name) << mode << int (retune) << hound << requestedEffects
                        << int (target) << selectionEffects;
  };
  row ("working", "FT8", Retune::Working, false, true, Target::Working, true);
  row ("preserved", "FT8", Retune::Preserve, false, true, Target::None, true);
  row ("explicit", "FT8", Retune::Explicit, false, true, Target::Explicit, true);
  row ("hound-working", "FT8", Retune::Working, true, true, Target::None, true);
  row ("hound-explicit", "FT8", Retune::Explicit, true, true, Target::Explicit, true);
  row ("hound-other-mode", "Q65", Retune::Working, true, true, Target::Working, true);
  row ("calibration-start", "FreqCal", Retune::Working, false, true, Target::CalibrationStart, false);
  row ("calibration-preserved", "FreqCal", Retune::Preserve, false, true, Target::None, false);
  row ("calibration-explicit", "FreqCal", Retune::Explicit, false, true, Target::Explicit, false);
  row ("effects-disabled", "FT8", Retune::Working, false, false, Target::Working, false);
}

void TestModeEntry::target_policy ()
{
  QFETCH (QString, mode);
  QFETCH (int, retune);
  QFETCH (bool, hound);
  QFETCH (bool, requestedEffects);
  QFETCH (int, target);
  QFETCH (bool, selectionEffects);
  ModeEntry::Request request {mode, ModeEntry::RetunePolicy (retune), Radio::Frequency {7074000}, requestedEffects};
  auto const policy = ModeEntry::policyFor (request, hound);
  QCOMPARE (int (policy.target), target);
  QCOMPARE (policy.selectionEffects, selectionEffects);
}

void TestModeEntry::reentrant_requests_wait_for_current_operation ()
{
  ModeEntry::Queue queue;
  auto const first = queue.submit ({"FT8"});
  auto active = queue.takeNext ();
  QVERIFY (active);
  QCOMPARE (active->identity, first);
  auto const second = queue.submit ({"Echo"});
  auto const third = queue.submit ({"Q65"});
  QVERIFY (!queue.takeNext ());
  QCOMPARE (queue.activeIdentity (), first);
  queue.finish (first);
  active = queue.takeNext ();
  QVERIFY (active);
  QCOMPARE (active->identity, second);
  QCOMPARE (active->request.mode, QString {"Echo"});
  queue.finish (second);
  active = queue.takeNext ();
  QVERIFY (active);
  QCOMPARE (active->identity, third);
  queue.finish (third);
  QVERIFY (!queue.takeNext ());
}

void TestModeEntry::queued_request_replaces_only_after_acceptance ()
{
  ModeEntry::Queue queue;
  auto const old = queue.submit ({"FT8"});
  QVERIFY (queue.takeNext ());
  QVERIFY (!queue.current (old));
  queue.accept (old);
  QVERIFY (queue.current (old));
  auto const middle = queue.submit ({"Q65"});
  auto const replacement = queue.submit ({"FT8"});
  QVERIFY (queue.current (old));
  QVERIFY (!queue.current (replacement));
  QCOMPARE (queue.activeIdentity (), old);
  queue.finish (old);
  QVERIFY (queue.takeNext ());
  QVERIFY (queue.current (old));
  queue.accept (middle);
  QVERIFY (!queue.current (old));
  QVERIFY (queue.current (middle));
  queue.finish (middle);
  QVERIFY (queue.takeNext ());
  queue.accept (replacement);
  QVERIFY (queue.current (replacement));
  QVERIFY (!queue.current (old));
}

void TestModeEntry::rejected_request_preserves_installed_mode ()
{
  ModeEntry::Queue queue;
  auto const installed = queue.submit ({"FT8"});
  auto const operation = queue.takeNext ();
  QVERIFY (operation);
  queue.accept (installed);
  queue.finish (installed);
  ModeEntry::PendingRetune retune;
  retune.defer (*operation, 7, 14074000);

  auto const rejected = queue.submit ({"MSK144"});
  QVERIFY (queue.takeNext ());
  queue.finish (rejected);
  QVERIFY (queue.current (installed));
  QVERIFY (!queue.current (rejected));
  auto const resumed = retune.resume (queue.acceptedIdentity (), 7, receiving ());
  QVERIFY (resumed);
  QCOMPARE (resumed->identity, installed);
}

void TestModeEntry::stale_acceptance_cannot_replace_current_mode ()
{
  ModeEntry::Queue queue;
  auto const old = queue.submit ({"FT8"});
  QVERIFY (queue.takeNext ());
  queue.accept (old);
  queue.finish (old);
  auto const current = queue.submit ({"Q65"});
  QVERIFY (queue.takeNext ());
  queue.accept (current);
  queue.accept (old);
  QCOMPARE (queue.acceptedIdentity (), current);
  queue.finish (current);
  queue.accept (old);
  QCOMPARE (queue.acceptedIdentity (), current);
}

void TestModeEntry::queued_request_preserves_its_policy ()
{
  ModeEntry::Queue queue;
  auto const first = queue.submit ({"JT65"});
  QVERIFY (queue.takeNext ());
  queue.submit ({"FT8", ModeEntry::RetunePolicy::Explicit,
                Radio::Frequency {7074000}, false, true});
  queue.finish (first);
  auto const next = queue.takeNext ();
  QVERIFY (next);
  QCOMPARE (next->request.retune, ModeEntry::RetunePolicy::Explicit);
  QVERIFY (next->request.frequency);
  QCOMPARE (*next->request.frequency, Radio::Frequency {7074000});
  QVERIFY (!next->request.selectionEffects);
  QVERIFY (next->request.userInitiated);
  QVERIFY (queue.activeRequest ());
  QCOMPARE (*queue.activeRequest ()->frequency, Radio::Frequency {7074000});
}

void TestModeEntry::stale_completion_cannot_release_replacement ()
{
  ModeEntry::Queue queue;
  auto const first = queue.submit ({"FT8"});
  QVERIFY (queue.takeNext ());
  queue.finish (first);
  auto const second = queue.submit ({"Q65"});
  QVERIFY (queue.takeNext ());
  queue.submit ({"Echo"});
  queue.finish (first);
  QCOMPARE (queue.activeIdentity (), second);
  QVERIFY (!queue.takeNext ());
}

void TestModeEntry::retune_waits_for_both_frequency_paths ()
{
  ModeEntry::PendingRetune retune;
  retune.defer ({1, {"Q65"}}, 7, 14074000);
  auto state = receiving ();
  state.pendingRx = true;
  QVERIFY (!retune.resume (1, 7, state));
  QVERIFY (retune.pending ());
  state.pendingRx = false;
  state.pendingTx = true;
  QVERIFY (!retune.resume (1, 7, state));
  QVERIFY (retune.pending ());
  state.pendingTx = false;
  auto const resumed = retune.resume (1, 7, state);
  QVERIFY (resumed);
  QCOMPARE (resumed->request.mode, QString {"Q65"});
  QVERIFY (!retune.pending ());
  QVERIFY (!retune.resume (1, 7, state));
}

void TestModeEntry::startup_retune_uses_first_available_radio_state ()
{
  ModeEntry::PendingRetune retune;
  retune.defer ({1, {"Echo"}}, 0, 0);
  QVERIFY (!retune.resume (1, 0, receiving (0)));
  QVERIFY (!retune.resume (1, 9, receiving (0)));
  QVERIFY (retune.pending ());
  auto const resumed = retune.resume (1, 9, receiving (144174000));
  QVERIFY (resumed);
  QCOMPARE (resumed->request.mode, QString {"Echo"});
  QVERIFY (!retune.pending ());
}

void TestModeEntry::changed_retune_context_is_discarded_data ()
{
  QTest::addColumn<quint64> ("latest");
  QTest::addColumn<quint64> ("connection");
  QTest::addColumn<quint64> ("frequency");
  QTest::newRow ("new mode accepted") << quint64 {2} << quint64 {7} << quint64 {14074000};
  QTest::newRow ("connection replaced") << quint64 {1} << quint64 {8} << quint64 {14074000};
  QTest::newRow ("external VFO change") << quint64 {1} << quint64 {7} << quint64 {7074000};
}

void TestModeEntry::changed_retune_context_is_discarded ()
{
  QFETCH (quint64, latest);
  QFETCH (quint64, connection);
  QFETCH (quint64, frequency);
  ModeEntry::PendingRetune retune;
  retune.defer ({1, {"Q65"}}, 7, 14074000);
  auto state = receiving (frequency);
  state.pendingRx = true;
  QVERIFY (!retune.resume (latest, connection, state));
  QVERIFY (!retune.pending ());
  QVERIFY (!retune.resume (1, 7, receiving ()));
}

void TestModeEntry::corrections_do_not_supersede_retune ()
{
  ModeEntry::PendingRetune retune;
  retune.defer ({1, {"Q65"}}, 7, 14074000);
  auto state = receiving ();
  state.correctedRx += 100;
  state.correctedTx -= 100;
  state.audioOffset = 500;
  state.pendingTx = true;
  QVERIFY (!retune.resume (1, 7, state));
  QVERIFY (retune.pending ());
  state.pendingTx = false;
  QVERIFY (retune.resume (1, 7, state));
}

void TestModeEntry::calibration_start_follows_dial_moved_by_the_application ()
{
  ModeEntry::PendingRetune retune;
  retune.defer ({1, {"FreqCal"}}, 7, 134500);
  retune.follow (14020000);
  QVERIFY (!retune.resume (1, 7, receiving (134500)));
  QVERIFY (!retune.pending ());

  retune.defer ({1, {"FreqCal"}}, 7, 134500);
  retune.follow (14020000);
  auto state = receiving (14020000);
  state.pendingRx = true;
  QVERIFY (!retune.resume (1, 7, state));
  QVERIFY (retune.pending ());
  QVERIFY (retune.resume (1, 7, receiving (14020000)));
  QVERIFY (!retune.pending ());

  retune.defer ({1, {"FreqCal"}}, 7, 134500);
  retune.follow (14020000);
  retune.follow (7074000);
  QVERIFY (retune.resume (1, 7, receiving (7074000)));

  retune.defer ({1, {"FreqCal"}}, 7, 134500);
  retune.follow (14020000);
  QVERIFY (!retune.resume (1, 7, receiving (3573000)));
  QVERIFY (!retune.pending ());

  retune.defer ({1, {"FreqCal"}}, 7, 134500);
  retune.follow (14020000);
  QVERIFY (!retune.resume (1, 8, receiving (14020000)));
  QVERIFY (!retune.pending ());
}

void TestModeEntry::follow_leaves_other_retunes_alone ()
{
  ModeEntry::PendingRetune retune;
  retune.follow (14020000);
  QVERIFY (!retune.pending ());

  retune.defer ({1, {"Q65"}}, 7, 14074000);
  retune.follow (7074000);
  QVERIFY (!retune.resume (1, 7, receiving (7074000)));
  QVERIFY (!retune.pending ());

  retune.defer ({1, {"FreqCal"}}, 7, 0);
  retune.follow (14020000);
  QVERIFY (!retune.resume (1, 7, receiving (0)));
  QVERIFY (retune.pending ());
  QVERIFY (retune.resume (1, 7, receiving (3573000)));
}

void TestModeEntry::explicit_cancellation_survives_return_to_old_frequency ()
{
  ModeEntry::PendingRetune retune;
  retune.defer ({1, {"Q65"}}, 7, 14074000);
  retune.cancel ();
  QVERIFY (!retune.resume (1, 7, receiving (7074000)));
  QVERIFY (!retune.resume (1, 7, receiving ()));
  QVERIFY (!retune.pending ());
}

void TestModeEntry::replacement_retune_retains_its_own_context ()
{
  ModeEntry::PendingRetune retune;
  retune.defer ({1, {"Q65"}}, 7, 14074000);
  retune.defer ({2, {"FT8", ModeEntry::RetunePolicy::Explicit,
                    Radio::Frequency {7074000}, false, true}}, 7, 14074000);
  auto const resumed = retune.resume (2, 7, receiving ());
  QVERIFY (resumed);
  QCOMPARE (resumed->identity, quint64 {2});
  QCOMPARE (*resumed->request.frequency, Radio::Frequency {7074000});
  QVERIFY (!resumed->request.selectionEffects);
  QVERIFY (resumed->request.userInitiated);
}

QTEST_GUILESS_MAIN (TestModeEntry)
#include "test_mode_entry.moc"
