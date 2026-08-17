#include <QtTest>

#include <memory>
#include <vector>

#include "Ft8MtdDecodeCoordinator.hpp"

namespace
{
  using Stage = Ft8MtdDecodeCoordinator::Stage;
  using Action = Ft8MtdDecodeCoordinator::Action;
  using DrainResult = Ft8MtdDecodeCoordinator::DrainResult;
  using PendingPublishResult = Ft8MtdDecodeCoordinator::PendingPublishResult;

  std::unique_ptr<Ft8MtdDecodeCoordinator::PendingMtdDecode> pendingDecode (
      qint64 period, int nutc, short firstSample)
  {
    auto pending = std::make_unique<Ft8MtdDecodeCoordinator::PendingMtdDecode> ();
    pending->period = period;
    pending->payload.params.nmode = 8;
    pending->payload.params.lmultift8 = true;
    pending->payload.params.nutc = nutc;
    pending->payload.samples[0] = firstSample;
    return pending;
  }

  class FakeBackend
  {
  public:
    void startEarly (Stage stage, qint64 period, int earlyStageCount)
    {
      auto const decision = coordinator.request (
        stage, period, earlyStageCount, active);
      QCOMPARE (decision.action, Action::Publish);
      active = true;
      activeStage = stage;
      activePeriod = period;
      coordinator.published (stage, period);
      publishedPeriods.push_back (period);
    }

    Action submitFinal (qint64 period, int nutc, short firstSample,
                        int earlyStageCount = 1)
    {
      auto const decision = coordinator.request (
        Stage::Final, period, earlyStageCount,
        active || coordinator.hasPending ());
      if (Action::Publish == decision.action)
        {
          active = true;
          activeStage = Stage::Final;
          activePeriod = period;
          coordinator.published (Stage::Final, period);
          publishedPeriods.push_back (period);
          publishedNutcs.push_back (nutc);
          return decision.action;
        }

      return coordinator.deferFinal (
        pendingDecode (period, nutc, firstSample));
    }

    void completeAndDrain ()
    {
      QVERIFY (active);
      coordinator.completed (activeStage, activePeriod);
      active = false;
      activeStage = Stage::None;
      activePeriod = -1;
      if (!coordinator.hasPending ()) return;

      auto const outcome = coordinator.drainPending (
        [] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {return true;},
        [this] (Ft8MtdDecodeCoordinator::PendingMtdDecode const& pending) {
          active = true;
          activeStage = Stage::Final;
          activePeriod = pending.period;
          publishedPeriods.push_back (pending.period);
          publishedNutcs.push_back (pending.payload.params.nutc);
          publishedFirstSamples.push_back (pending.payload.samples[0]);
          return PendingPublishResult::Published;
        });
      QCOMPARE (outcome.result, DrainResult::Published);
    }

    bool externallyBusy () const
    {
      return active || coordinator.hasPending ();
    }

    Ft8MtdDecodeCoordinator coordinator;
    bool active {false};
    Stage activeStage {Stage::None};
    qint64 activePeriod {-1};
    std::vector<qint64> publishedPeriods;
    std::vector<int> publishedNutcs;
    std::vector<short> publishedFirstSamples;
  };
}

class TestFt8MtdDecodeCoordinator final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void slowEarlyDecodeRetainsAndPublishesFinalOnce ();
  void multiPeriodOverrunPublishesOnlyNewestFinal ();
  void pendingSnapshotIsImmutable ();
  void pendingCountsAsBusyUntilDrain ();
  void temporaryBackendFailurePreservesPending ();
  void fatalBackendFailureDropsPending ();
  void changedContextDropsPendingWithoutPublishing ();
  void cancellationDropsPendingAndSchedulingState ();
};

void TestFt8MtdDecodeCoordinator::slowEarlyDecodeRetainsAndPublishesFinalOnce ()
{
  FakeBackend backend;
  backend.startEarly (Stage::EarlyOne, 100, 1);

  QCOMPARE (backend.submitFinal (100, 120000, 17), Action::DeferFinal);
  QVERIFY (backend.externallyBusy ());
  backend.completeAndDrain ();

  QVERIFY (backend.publishedPeriods == std::vector<qint64> ({100, 100}));
  QVERIFY (backend.publishedNutcs == std::vector<int> ({120000}));
  QVERIFY (backend.active);
  QVERIFY (!backend.coordinator.hasPending ());
}

void TestFt8MtdDecodeCoordinator::multiPeriodOverrunPublishesOnlyNewestFinal ()
{
  FakeBackend backend;
  backend.startEarly (Stage::EarlyOne, 200, 1);
  QCOMPARE (backend.submitFinal (200, 130000, 20), Action::DeferFinal);
  QCOMPARE (backend.submitFinal (201, 130015, 21), Action::ReplaceFinal);
  QCOMPARE (backend.submitFinal (202, 130030, 22), Action::ReplaceFinal);

  backend.completeAndDrain ();

  QVERIFY (backend.publishedPeriods == std::vector<qint64> ({200, 202}));
  QVERIFY (backend.publishedNutcs == std::vector<int> ({130030}));
  QVERIFY (backend.publishedFirstSamples == std::vector<short> ({22}));
}

void TestFt8MtdDecodeCoordinator::pendingSnapshotIsImmutable ()
{
  FakeBackend backend;
  backend.startEarly (Stage::EarlyOne, 300, 1);
  int sourceNutC {140000};
  short sourceSample {31};
  backend.submitFinal (300, sourceNutC, sourceSample);
  sourceNutC = 140015;
  sourceSample = 32;

  backend.completeAndDrain ();

  QCOMPARE (backend.publishedNutcs.back (), 140000);
  QCOMPARE (backend.publishedFirstSamples.back (), short {31});
}

void TestFt8MtdDecodeCoordinator::pendingCountsAsBusyUntilDrain ()
{
  FakeBackend backend;
  backend.startEarly (Stage::EarlyOne, 400, 1);
  backend.submitFinal (400, 150000, 41);

  backend.active = false;
  QVERIFY (backend.coordinator.hasPending ());
  QVERIFY (backend.externallyBusy ());
  backend.active = true;
  backend.completeAndDrain ();
  QVERIFY (backend.externallyBusy ());
}

void TestFt8MtdDecodeCoordinator::temporaryBackendFailurePreservesPending ()
{
  Ft8MtdDecodeCoordinator coordinator;
  coordinator.deferFinal (pendingDecode (500, 160000, 51));

  auto const unavailable = coordinator.drainPending (
    [] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {return true;},
    [] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {
      return PendingPublishResult::Unavailable;
    });
  QCOMPARE (unavailable.result, DrainResult::Retained);
  QVERIFY (coordinator.hasPending ());

  auto shared = std::make_unique<shared_dec_data_t> ();
  DecoderIpc::initialize (*shared);
  int publishCalls {0};
  auto const published = coordinator.drainPending (
    [] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {return true;},
    [&] (Ft8MtdDecodeCoordinator::PendingMtdDecode const& pending) {
      ++publishCalls;
      return DecoderIpc::publishFt8Mtd (*shared, pending.payload, 1)
        ? PendingPublishResult::Published : PendingPublishResult::Failed;
    });

  QCOMPARE (published.result, DrainResult::Published);
  QCOMPARE (publishCalls, 1);
  QVERIFY (!coordinator.hasPending ());
  QCOMPARE (shared->payload.params.nutc, 160000);
  QCOMPARE (shared->payload.d2[0], short {51});

  auto const empty = coordinator.drainPending (
    [] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {return true;},
    [&] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {
      ++publishCalls;
      return PendingPublishResult::Published;
    });
  QCOMPARE (empty.result, DrainResult::NoPending);
  QCOMPARE (publishCalls, 1);
}

void TestFt8MtdDecodeCoordinator::fatalBackendFailureDropsPending ()
{
  Ft8MtdDecodeCoordinator coordinator;
  coordinator.deferFinal (pendingDecode (510, 160015, 52));

  auto const outcome = coordinator.drainPending (
    [] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {return true;},
    [] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {
      return PendingPublishResult::Failed;
    });

  QCOMPARE (outcome.result, DrainResult::Failed);
  QVERIFY (!coordinator.hasPending ());
}

void TestFt8MtdDecodeCoordinator::changedContextDropsPendingWithoutPublishing ()
{
  Ft8MtdDecodeCoordinator coordinator;
  coordinator.request (Stage::Final, 520, 1, true);
  coordinator.deferFinal (pendingDecode (520, 160030, 53));
  int publishCalls {0};

  auto const outcome = coordinator.drainPending (
    [] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {return false;},
    [&] (Ft8MtdDecodeCoordinator::PendingMtdDecode const&) {
      ++publishCalls;
      return PendingPublishResult::Published;
    });

  QCOMPARE (outcome.result, DrainResult::Obsolete);
  QCOMPARE (publishCalls, 0);
  QVERIFY (!coordinator.hasPending ());
  QVERIFY (!coordinator.degraded ());
}

void TestFt8MtdDecodeCoordinator::cancellationDropsPendingAndSchedulingState ()
{
  Ft8MtdDecodeCoordinator coordinator;
  coordinator.request (Stage::Final, 600, 1, true);
  coordinator.deferFinal (pendingDecode (600, 170000, 61));
  QVERIFY (coordinator.degraded ());

  coordinator.cancel ();

  QVERIFY (!coordinator.hasPending ());
  QVERIFY (!coordinator.degraded ());
  QCOMPARE (coordinator.nextProbePeriod (), qint64 {0});
}

QTEST_GUILESS_MAIN (TestFt8MtdDecodeCoordinator)

#include "test_ft8_mtd_decode_coordinator.moc"
