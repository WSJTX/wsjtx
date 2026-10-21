#include <QtTest>

#include <memory>
#include <vector>

#include "DecoderIpc.hpp"
#include "Ft8MtdDecodeCoordinator.hpp"

namespace
{
  using Stage = Ft8MtdDecodeCoordinator::Stage;
  using Action = Ft8MtdDecodeCoordinator::Action;
  using DrainResult = Ft8MtdDecodeCoordinator::DrainResult;
  using PendingPublishResult = Ft8MtdDecodeCoordinator::PendingPublishResult;

  class Ft8Capture final
    : public CapturedDecode
  {
  public:
    decoder_params_t const& settings () const override {return payload.params;}
    DecoderIpc::Ft8MtdPayload payload;
  };

  DecoderIpc::Ft8MtdPayload const& payloadOf (
      Ft8MtdDecodeCoordinator::PendingMtdDecode const& pending)
  {
    return static_cast<Ft8Capture const&> (*pending.request).payload;
  }

  std::unique_ptr<Ft8MtdDecodeCoordinator::PendingMtdDecode> pendingDecode (
      qint64 period, int nutc, short firstSample)
  {
    auto pending = std::make_unique<Ft8MtdDecodeCoordinator::PendingMtdDecode> ();
    auto capture = std::make_unique<Ft8Capture> ();
    pending->period = period;
    capture->payload.params.nmode = 8;
    capture->payload.params.lmultift8 = true;
    capture->payload.params.nutc = nutc;
    capture->payload.metadata = {period, period + 1000, 2, 170000};
    capture->payload.samples[0] = firstSample;
    pending->context.mode = "FT8";
    pending->context.periodFrequency = 14074000;
    pending->context.band = "20m";
    pending->context.multithreadFt8 = true;
    pending->context.ft8ThreadCount = 4;
    pending->context.decodeDepth = 3;
    pending->request = std::move (capture);
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

      auto pending = pendingDecode (period, nutc, firstSample);
      currentContext = pending->context;
      return coordinator.deferFinal (std::move (pending));
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
        [this] (Ft8MtdDecodeCoordinator::PendingMtdDecode const& pending) {
          return pending.context.hasSameFt8PendingIdentity (currentContext);
        },
        [this] (Ft8MtdDecodeCoordinator::PendingMtdDecode const& pending) {
          active = true;
          activeStage = Stage::Final;
          activePeriod = pending.period;
          publishedPeriods.push_back (pending.period);
          publishedNutcs.push_back (payloadOf (pending).params.nutc);
          publishedFirstSamples.push_back (payloadOf (pending).samples[0]);
          publishedContext = pending.context;
          publishedMetadata = payloadOf (pending).metadata;
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
    DecodeOperatingContext publishedContext;
    DecodeOperatingContext currentContext;
    decoder_input_metadata_t publishedMetadata {};
  };
}

class TestFt8MtdDecodeCoordinator final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void slowEarlyDecodeRetainsAndPublishesFinalOnce ();
  void receiveRolloverRetainsPendingFinal ();
  void unrelatedCompletionDoesNotPublishPendingFinal ();
  void multiPeriodOverrunPublishesOnlyNewestFinal ();
  void pendingSnapshotIsImmutable ();
  void pendingContextReachesFinalPublication ();
  void pendingCountsAsBusyUntilDrain ();
  void temporaryBackendFailurePreservesPending ();
  void fatalBackendFailureDropsPending ();
  void publicationFailureLeavesPendingForRestart ();
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
  QCOMPARE (backend.activeStage, Stage::Final);
  QCOMPARE (backend.activePeriod, qint64 {100});
  QVERIFY (backend.active);
  QVERIFY (!backend.coordinator.hasPending ());
}

void TestFt8MtdDecodeCoordinator::receiveRolloverRetainsPendingFinal ()
{
  FakeBackend backend;
  backend.startEarly (Stage::EarlyOne, 100, 1);
  QCOMPARE (backend.submitFinal (100, 120000, 17), Action::DeferFinal);
  backend.currentContext.sequenceStart = QDateTime::fromMSecsSinceEpoch (1515000);

  backend.completeAndDrain ();

  QVERIFY (backend.publishedPeriods == std::vector<qint64> ({100, 100}));
  QCOMPARE (backend.publishedNutcs.back (), 120000);
  QCOMPARE (backend.publishedFirstSamples.back (), short {17});
  QCOMPARE (backend.publishedMetadata.input_id, int64_t {100});
  QCOMPARE (backend.publishedMetadata.analysis_id, int64_t {1100});
  QCOMPARE (backend.publishedMetadata.attempt_no, int32_t {2});
  QCOMPARE (backend.publishedMetadata.valid_samples, int32_t {170000});
  QVERIFY (!backend.coordinator.hasPending ());
}

void TestFt8MtdDecodeCoordinator::unrelatedCompletionDoesNotPublishPendingFinal ()
{
  FakeBackend backend;
  backend.startEarly (Stage::EarlyOne, 150, 1);
  backend.submitFinal (150, 120015, 18);

  backend.coordinator.completed (Stage::EarlyOne, 149);

  QVERIFY (backend.coordinator.hasPending ());
  QVERIFY (backend.publishedPeriods == std::vector<qint64> ({150}));

  backend.completeAndDrain ();
  QVERIFY (backend.publishedPeriods == std::vector<qint64> ({150, 150}));
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

void TestFt8MtdDecodeCoordinator::pendingContextReachesFinalPublication ()
{
  FakeBackend backend;
  backend.startEarly (Stage::EarlyOne, 350, 1);
  backend.submitFinal (350, 140015, 35);

  backend.completeAndDrain ();

  auto const& context = backend.publishedContext;
  QCOMPARE (context.mode, QStringLiteral ("FT8"));
  QCOMPARE (context.periodFrequency, Radio::Frequency {14074000});
  QCOMPARE (context.band, QStringLiteral ("20m"));
  QVERIFY (context.multithreadFt8);
  QCOMPARE (context.ft8ThreadCount, 4);
  QCOMPARE (context.decodeDepth, 3);
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
      return DecoderIpc::publishFt8Mtd (*shared, payloadOf (pending), 1)
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

void TestFt8MtdDecodeCoordinator::publicationFailureLeavesPendingForRestart ()
{
  FakeBackend backend;
  backend.startEarly (Stage::EarlyOne, 515, 1);
  backend.submitFinal (515, 160020, 52);

  backend.coordinator.publicationFailed (Stage::EarlyOne, 515);
  QVERIFY (backend.coordinator.hasPending ());

  backend.completeAndDrain ();

  QVERIFY (backend.publishedPeriods == std::vector<qint64> ({515, 515}));
  QVERIFY (!backend.coordinator.hasPending ());
}

void TestFt8MtdDecodeCoordinator::changedContextDropsPendingWithoutPublishing ()
{
  Ft8MtdDecodeCoordinator coordinator;
  coordinator.request (Stage::Final, 520, 1, true);
  auto pending = pendingDecode (520, 160030, 53);
  auto current = pending->context;
  ++current.ft8ThreadCount;
  coordinator.deferFinal (std::move (pending));
  int publishCalls {0};

  auto const outcome = coordinator.drainPending (
    [&current] (Ft8MtdDecodeCoordinator::PendingMtdDecode const& pending) {
      return pending.context.hasSameFt8PendingIdentity (current);
    },
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
