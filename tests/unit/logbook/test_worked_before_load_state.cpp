#include <QtTest>

#include "logbook/WorkedBeforeLoadState.hpp"

class TestWorkedBeforeLoadState final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void starts_first_reload ();
  void coalesces_repeated_reload_requests ();
  void keeps_completion_active_for_reentrant_reload ();
  void starts_a_fresh_reload_after_idle ();
};

void TestWorkedBeforeLoadState::starts_first_reload ()
{
  WorkedBeforeLoadState state;

  QCOMPARE (state.request_reload (), WorkedBeforeLoadState::ReloadResult::Start);
  QVERIFY (state.active ());
}

void TestWorkedBeforeLoadState::coalesces_repeated_reload_requests ()
{
  WorkedBeforeLoadState state;

  QCOMPARE (state.request_reload (), WorkedBeforeLoadState::ReloadResult::Start);
  QCOMPARE (state.request_reload (), WorkedBeforeLoadState::ReloadResult::Coalesced);
  QCOMPARE (state.request_reload (), WorkedBeforeLoadState::ReloadResult::Coalesced);

  state.begin_completion ();
  QCOMPARE (state.finish_completion (), WorkedBeforeLoadState::CompletionResult::Restart);
  QVERIFY (state.active ());

  state.begin_completion ();
  QCOMPARE (state.finish_completion (), WorkedBeforeLoadState::CompletionResult::Idle);
  QVERIFY (!state.active ());
}

void TestWorkedBeforeLoadState::keeps_completion_active_for_reentrant_reload ()
{
  WorkedBeforeLoadState state;

  QCOMPARE (state.request_reload (), WorkedBeforeLoadState::ReloadResult::Start);
  state.begin_completion ();
  QVERIFY (state.active ());

  QCOMPARE (state.request_reload (), WorkedBeforeLoadState::ReloadResult::Coalesced);
  QCOMPARE (state.finish_completion (), WorkedBeforeLoadState::CompletionResult::Restart);
  QVERIFY (state.active ());
}

void TestWorkedBeforeLoadState::starts_a_fresh_reload_after_idle ()
{
  WorkedBeforeLoadState state;

  QCOMPARE (state.request_reload (), WorkedBeforeLoadState::ReloadResult::Start);
  state.begin_completion ();
  QCOMPARE (state.finish_completion (), WorkedBeforeLoadState::CompletionResult::Idle);
  QVERIFY (!state.active ());

  QCOMPARE (state.request_reload (), WorkedBeforeLoadState::ReloadResult::Start);
  QVERIFY (state.active ());
}

QTEST_GUILESS_MAIN (TestWorkedBeforeLoadState)

#include "test_worked_before_load_state.moc"
