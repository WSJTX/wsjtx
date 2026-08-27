#include <QtTest>

#include "Ft8MtdDecodeScheduler.hpp"

class TestFt8MtdDecodeScheduler final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void eligibilityRequiresIndependentMtdFinal ();
  void idleRequestsPublishImmediately ();
  void earlyRequestsNeverQueue ();
  void finalRequestsDeferOrReplace ();
  void failedProbesBackOffExponentially ();
  void oneEarlyPassMtdConfigurationRecovers ();
  void twoEarlyPassMtdConfigurationRequiresBothEarlyPasses ();
  void cancelRestoresBoundedInitialState ();
};

using Stage = Ft8MtdDecodeScheduler::Stage;
using Action = Ft8MtdDecodeScheduler::Action;

void TestFt8MtdDecodeScheduler::eligibilityRequiresIndependentMtdFinal ()
{
  QVERIFY (!Ft8MtdDecodeScheduler::supportsBackpressure (false, false));
  QVERIFY (Ft8MtdDecodeScheduler::supportsBackpressure (true, false));
  QVERIFY (!Ft8MtdDecodeScheduler::supportsBackpressure (true, true));
}

void TestFt8MtdDecodeScheduler::idleRequestsPublishImmediately ()
{
  Ft8MtdDecodeScheduler scheduler;
  QCOMPARE (scheduler.request (Stage::EarlyOne, 10, 1, false, false).action,
            Action::Publish);
  QCOMPARE (scheduler.request (Stage::Final, 10, 1, false, false).action,
            Action::Publish);
  QVERIFY (!scheduler.degraded ());
}

void TestFt8MtdDecodeScheduler::earlyRequestsNeverQueue ()
{
  Ft8MtdDecodeScheduler scheduler;
  auto const decision = scheduler.request (Stage::EarlyOne, 20, 1, true, false);
  QCOMPARE (decision.action, Action::SkipEarly);
  QVERIFY (decision.enteredDegraded);
  QVERIFY (scheduler.degraded ());
}

void TestFt8MtdDecodeScheduler::finalRequestsDeferOrReplace ()
{
  Ft8MtdDecodeScheduler scheduler;
  QCOMPARE (scheduler.request (Stage::Final, 30, 1, true, false).action,
            Action::DeferFinal);
  QCOMPARE (scheduler.request (Stage::Final, 31, 1, true, true).action,
            Action::ReplaceFinal);
}

void TestFt8MtdDecodeScheduler::failedProbesBackOffExponentially ()
{
  Ft8MtdDecodeScheduler scheduler;
  auto result = scheduler.request (Stage::EarlyOne, 40, 1, true, false);
  QCOMPARE (result.skippedPeriods, 1);
  QCOMPARE (scheduler.nextProbePeriod (), qint64 {42});
  QCOMPARE (scheduler.request (Stage::EarlyOne, 41, 1, false, false).action,
            Action::SkipEarly);

  result = scheduler.request (Stage::EarlyOne, 42, 1, true, false);
  QCOMPARE (result.skippedPeriods, 2);
  QCOMPARE (scheduler.nextProbePeriod (), qint64 {45});
  result = scheduler.request (Stage::EarlyOne, 45, 1, true, false);
  QCOMPARE (result.skippedPeriods, 4);
  QCOMPARE (scheduler.nextProbePeriod (), qint64 {50});
  result = scheduler.request (Stage::EarlyOne, 50, 1, true, false);
  QCOMPARE (result.skippedPeriods, 8);
  QCOMPARE (scheduler.nextProbePeriod (), qint64 {59});
  result = scheduler.request (Stage::EarlyOne, 59, 1, true, false);
  QCOMPARE (result.skippedPeriods, 8);
  QCOMPARE (scheduler.nextProbePeriod (), qint64 {68});
}

void TestFt8MtdDecodeScheduler::oneEarlyPassMtdConfigurationRecovers ()
{
  Ft8MtdDecodeScheduler scheduler;
  scheduler.request (Stage::Final, 60, 1, true, false);
  QCOMPARE (scheduler.request (Stage::EarlyOne, 62, 1, false, false).action,
            Action::Publish);
  scheduler.published (Stage::EarlyOne, 62);
  scheduler.completed (Stage::EarlyOne, 62);
  QCOMPARE (scheduler.request (Stage::Final, 62, 1, false, false).action,
            Action::Publish);
  QVERIFY (scheduler.published (Stage::Final, 62));
  QVERIFY (!scheduler.degraded ());
}

void TestFt8MtdDecodeScheduler::twoEarlyPassMtdConfigurationRequiresBothEarlyPasses ()
{
  Ft8MtdDecodeScheduler complete;
  complete.request (Stage::Final, 70, 2, true, false);
  complete.request (Stage::EarlyOne, 72, 2, false, false);
  complete.published (Stage::EarlyOne, 72);
  complete.completed (Stage::EarlyOne, 72);
  QCOMPARE (complete.request (Stage::EarlyTwo, 72, 2, false, false).action,
            Action::Publish);
  complete.published (Stage::EarlyTwo, 72);
  complete.completed (Stage::EarlyTwo, 72);
  QCOMPARE (complete.request (Stage::Final, 72, 2, false, false).action,
            Action::Publish);
  QVERIFY (complete.published (Stage::Final, 72));
  QVERIFY (!complete.degraded ());

  Ft8MtdDecodeScheduler incomplete;
  incomplete.request (Stage::Final, 80, 2, true, false);
  incomplete.request (Stage::EarlyOne, 82, 2, false, false);
  incomplete.published (Stage::EarlyOne, 82);
  incomplete.completed (Stage::EarlyOne, 82);
  incomplete.request (Stage::EarlyTwo, 82, 2, false, false);
  incomplete.published (Stage::EarlyTwo, 82);
  QCOMPARE (incomplete.request (Stage::Final, 82, 2, true, false).action,
            Action::DeferFinal);
  QVERIFY (incomplete.degraded ());
}

void TestFt8MtdDecodeScheduler::cancelRestoresBoundedInitialState ()
{
  Ft8MtdDecodeScheduler scheduler;
  scheduler.request (Stage::Final, 80, 1, true, true);
  QVERIFY (scheduler.degraded ());
  scheduler.cancel ();
  QVERIFY (!scheduler.degraded ());
  QCOMPARE (scheduler.nextProbePeriod (), qint64 {0});
  QCOMPARE (scheduler.request (Stage::EarlyOne, 81, 1, false, false).action,
            Action::Publish);
}

QTEST_GUILESS_MAIN (TestFt8MtdDecodeScheduler)

#include "test_ft8_mtd_decode_scheduler.moc"
