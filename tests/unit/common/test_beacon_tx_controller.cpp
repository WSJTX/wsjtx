#include <QtTest>

#include "BeaconTxController.hpp"

using namespace BeaconTx;

namespace
{
  int count (Controller::Actions const& actions, ActionKind kind)
  {
    int result = 0;
    for (auto const& action : actions) if (action.kind == kind) ++result;
    return result;
  }

  Action const * first (Controller::Actions const& actions, ActionKind kind)
  {
    for (auto const& action : actions) if (action.kind == kind) return &action;
    return nullptr;
  }

  ScheduleProposal proposal (Disposition disposition)
  {
    ScheduleProposal result;
    result.disposition = disposition;
    return result;
  }

  ScheduleProposal hoppingProposal (Disposition disposition, bool tune = false)
  {
    auto result = proposal (disposition);
    result.source = PlanSource::BandHop;
    result.hasHoppingProposal = true;
    result.hopping.frequenciesIndex = 3;
    result.hopping.tuneRequired = tune;
    return result;
  }

  struct Harness
  {
    Controller controller;

    void initialize (RoundRobinPolicy policy = RoundRobinPolicy::random ())
    {
      controller.enterMode (1000, policy);
      controller.setAutoEnabled (true);
      auto const actions = controller.observeUtc (100);
      QCOMPARE (count (actions, ActionKind::SetTransmitWindow), 1);
      QVERIFY (!actions.front ().enabled);
    }

    PlanId prepare (Disposition disposition = Disposition::Transmit)
    {
      auto const actions = controller.receiveCompleted ();
      auto const request = first (actions, ActionKind::RequestScheduleProposal);
      if (!request) return {};
      auto const id = request->planId;
      controller.proposalDelivered (id, proposal (disposition));
      return id;
    }

    PlanId enterPreparedTransmit ()
    {
      auto const id = prepare ();
      controller.observeUtc (1100);
      return id;
    }
  };

  struct ActionRecorder
  {
    std::vector<Action> actions;

    void record (Controller::Actions const& newActions)
    {
      actions.insert (actions.end (), newActions.begin (), newActions.end ());
    }

    int countFor (PlanId id, ActionKind kind) const
    {
      int result = 0;
      for (auto const& action : actions)
        {
          if (action.planId == id && action.kind == kind) ++result;
        }
      return result;
    }
  };

}

class TestBeaconTxController final : public QObject
{
  Q_OBJECT

private slots:
  void roundRobinPolicyCodec ();
  void delayedAdjacentPeriodAndDuplicateObservation ();
  void resyncReceivesPartialSlotAndPreparesAtCompletion ();
  void receiveCompletionPreparesOnce ();
  void successfulTxLifecycleRecordsAndPreparesOnce ();
  void failedTxStartRecordsNothingAndRecovers ();
  void modeExitDuringPttTailDoesNotPrepare ();
  void manualTuneAcrossBoundaryRestoresAutoAndPrepares ();
  void automaticTuneCompletesExistingPlanWithoutRedraw ();
  void staleAutomaticTuneEventsAreIgnored ();
  void expiredAutomaticTunePreparesOnceAfterTail ();
  void txNextSurvivesTuneAndResync ();
  void txNextClearsOnlyWhenMessageStarts ();
  void txNextCancellationReusesUnderlyingPlan ();
  void suppressedTxNextCancellationDoesNotDraw ();
  void fixedRoundRobinUsesUtcBoundaryWithoutProposal ();
  void txNextPrecedesFixedRoundRobin ();
  void policyChangeDuringTxDrawsOnceAtCompletion ();
  void bandChangeFailureConvertsSamePlanToReceive ();
  void staleAndDuplicateBandOutcomesAreIgnored ();
  void proposalRequestDoesNotActuate ();
  void startedSessionCrossesBoundaryAndPreparesOnStop ();
  void oldStopCannotPrepareReenteredEpoch ();
  void txNextWithdrawalAfterBoundaryUsesUnderlyingReceive ();
  void txNextWithdrawalDuringStartWaitsForStop ();
  void acceptedStartIgnoresConflictingDuplicateResult ();
  void acceptedStartWithoutMessageRecordsNothingAndRecovers ();
  void earlyTxStopClosesWindowAndPrepares ();
  void autoDisabledForcesReceiveWithoutRedraw ();
  void backwardClockStepResynchronizesToReceive ();
  void oneProposalRequestAndApplicationPerPlanId ();
};

void TestBeaconTxController::roundRobinPolicyCodec ()
{
  auto policy = parseRoundRobinPolicy ("2/4");
  QCOMPARE (policy.kind, RoundRobinPolicy::Kind::Fixed);
  QCOMPARE (policy.selectedSlot, 1);
  QCOMPARE (policy.slotCount, 4);
  QVERIFY (formatRoundRobinPolicy (policy) == "2/4");

  policy = parseRoundRobinPolicy ("7/12");
  QCOMPARE (policy.kind, RoundRobinPolicy::Kind::Fixed);
  QCOMPARE (policy.selectedSlot, 6);
  QCOMPARE (policy.slotCount, 12);

  QVERIFY (formatRoundRobinPolicy (parseRoundRobinPolicy ("  +2 / 4 \t")) == "2/4");
  for (auto const& text : {"random", "Random", "randomized", "0/4", "5/4", "2/4x",
                           "2/4/6", "2/", "/4", "2/99999999999999999999", "-1/4"})
    {
      QCOMPARE (parseRoundRobinPolicy (text).kind, RoundRobinPolicy::Kind::Random);
    }
  QVERIFY (formatRoundRobinPolicy (RoundRobinPolicy::random ()) == "random");
}

void TestBeaconTxController::delayedAdjacentPeriodAndDuplicateObservation ()
{
  Harness h;
  h.initialize ();
  auto const request = h.controller.receiveCompleted ();
  auto const id = first (request, ActionKind::RequestScheduleProposal)->planId;

  auto const boundary = h.controller.observeUtc (1101);
  QVERIFY (!boundary.front ().enabled);
  QVERIFY (h.controller.proposalDelivered (id, proposal (Disposition::Transmit)).empty ());
  QVERIFY (h.controller.observeUtc (1199).empty ());
}

void TestBeaconTxController::resyncReceivesPartialSlotAndPreparesAtCompletion ()
{
  Harness h;
  h.initialize ();
  h.prepare ();

  auto const resync = h.controller.observeUtc (3100);
  QCOMPARE (count (resync, ActionKind::SetTransmitWindow), 1);
  QVERIFY (!resync.front ().enabled);
  auto const completion = h.controller.receiveCompleted ();
  QCOMPARE (count (completion, ActionKind::RequestScheduleProposal), 1);
  QVERIFY (h.controller.receiveCompleted ().empty ());
}

void TestBeaconTxController::receiveCompletionPreparesOnce ()
{
  Harness h;
  h.initialize ();
  QCOMPARE (count (h.controller.receiveCompleted (), ActionKind::RequestScheduleProposal), 1);
  QVERIFY (h.controller.receiveCompleted ().empty ());
}

void TestBeaconTxController::successfulTxLifecycleRecordsAndPreparesOnce ()
{
  Harness h;
  h.initialize ();
  auto const id = h.enterPreparedTransmit ();
  h.controller.txStartRequested ();
  QVERIFY (h.controller.txStartResult (id, true).empty ());
  auto const started = h.controller.messageStarted (id);
  QCOMPARE (count (started, ActionKind::RecordBeaconTransmission), 1);

  h.controller.transmitWindowEnded ();
  auto const stopped = h.controller.txStopped (id);
  QCOMPARE (count (stopped, ActionKind::RequestScheduleProposal), 1);
  QVERIFY (h.controller.txStopped (id).empty ());
}

void TestBeaconTxController::failedTxStartRecordsNothingAndRecovers ()
{
  Harness h;
  h.initialize ();
  auto const id = h.enterPreparedTransmit ();
  h.controller.txStartRequested ();
  auto const failed = h.controller.txStartResult (id, false);
  QCOMPARE (count (failed, ActionKind::RecordBeaconTransmission), 0);
  QCOMPARE (count (failed, ActionKind::RequestScheduleProposal), 0);
  QVERIFY (h.controller.txStartResult (id, false).empty ());
  QCOMPARE (count (h.controller.txStopped (id), ActionKind::RequestScheduleProposal), 1);
}

void TestBeaconTxController::modeExitDuringPttTailDoesNotPrepare ()
{
  Harness h;
  h.initialize ();
  auto const id = h.enterPreparedTransmit ();
  h.controller.txStartRequested ();
  h.controller.txStartResult (id, true);
  QCOMPARE (count (h.controller.messageStarted (id), ActionKind::RecordBeaconTransmission), 1);
  h.controller.transmitWindowEnded ();
  h.controller.exitMode ();
  QCOMPARE (count (h.controller.txStopped (id), ActionKind::RequestScheduleProposal), 0);
  QCOMPARE (h.controller.txLifecycle (), TxLifecycle::None);
}

void TestBeaconTxController::manualTuneAcrossBoundaryRestoresAutoAndPrepares ()
{
  Harness h;
  h.initialize ();
  h.controller.tuneStarted (TuneKind::Manual);
  auto const boundary = h.controller.observeUtc (1100);
  QVERIFY (!boundary.front ().enabled);
  QVERIFY (h.controller.receiveCompleted ().empty ());

  auto const completed = h.controller.tuneCompleted (h.controller.tunePlanId ());
  QCOMPARE (count (completed, ActionKind::RestoreAuto), 1);
  QCOMPARE (count (completed, ActionKind::RequestScheduleProposal), 1);
}

void TestBeaconTxController::automaticTuneCompletesExistingPlanWithoutRedraw ()
{
  Harness h;
  h.initialize ();
  auto const request = h.controller.receiveCompleted ();
  auto const id = first (request, ActionKind::RequestScheduleProposal)->planId;
  auto const apply = h.controller.proposalDelivered (id, hoppingProposal (Disposition::Transmit, true));
  QCOMPARE (count (apply, ActionKind::ApplyBandChange), 1);
  auto const tune = h.controller.bandChangeOutcome (id, true);
  QCOMPARE (count (tune, ActionKind::StartAutomaticTune), 1);
  h.controller.tuneStarted (TuneKind::Automatic, id);

  auto const completed = h.controller.tuneCompleted (id);
  QCOMPARE (count (completed, ActionKind::RestoreAuto), 1);
  QCOMPARE (count (completed, ActionKind::RequestScheduleProposal), 0);
  QVERIFY (h.controller.observeUtc (1100).front ().enabled);
}

void TestBeaconTxController::staleAutomaticTuneEventsAreIgnored ()
{
  Harness h;
  h.initialize ();
  auto const request = h.controller.receiveCompleted ();
  auto const id = first (request, ActionKind::RequestScheduleProposal)->planId;
  h.controller.proposalDelivered (id, hoppingProposal (Disposition::Transmit, true));
  h.controller.bandChangeOutcome (id, true);
  auto stale = id;
  ++stale.epoch;

  QVERIFY (h.controller.tuneStarted (TuneKind::Automatic, stale).empty ());
  QCOMPARE (h.controller.tuneKind (), TuneKind::None);
  h.controller.tuneStarted (TuneKind::Automatic, id);
  QVERIFY (h.controller.tuneCompleted (stale).empty ());
  QCOMPARE (h.controller.tuneKind (), TuneKind::Automatic);
  QCOMPARE (count (h.controller.tuneCompleted (id), ActionKind::RestoreAuto), 1);
}

void TestBeaconTxController::expiredAutomaticTunePreparesOnceAfterTail ()
{
  Harness h;
  h.initialize ();
  auto const request = h.controller.receiveCompleted ();
  auto const id = first (request, ActionKind::RequestScheduleProposal)->planId;
  h.controller.proposalDelivered (id, hoppingProposal (Disposition::Transmit, true));
  h.controller.bandChangeOutcome (id, true);
  h.controller.tuneStarted (TuneKind::Automatic, id);
  QVERIFY (!h.controller.observeUtc (1100).front ().enabled);

  auto const completed = h.controller.tuneCompleted (id);
  QCOMPARE (count (completed, ActionKind::RequestScheduleProposal), 1);
  QVERIFY (h.controller.tuneCompleted (id).empty ());
}

void TestBeaconTxController::txNextSurvivesTuneAndResync ()
{
  Harness h;
  h.initialize ();
  h.controller.setTxNext (true);
  QVERIFY (h.controller.receiveCompleted ().empty ());
  h.controller.tuneStarted (TuneKind::Manual);
  h.controller.observeUtc (3100);
  h.controller.tuneCompleted (h.controller.tunePlanId ());
  QVERIFY (h.controller.txNextPending ());
  QVERIFY (h.controller.receiveCompleted ().empty ());
  QVERIFY (h.controller.observeUtc (4100).front ().enabled);
}

void TestBeaconTxController::txNextClearsOnlyWhenMessageStarts ()
{
  Harness h;
  h.initialize ();
  h.controller.setTxNext (true);
  h.controller.receiveCompleted ();
  auto const id = h.controller.observeUtc (1100).front ().planId;
  h.controller.txStartRequested ();
  h.controller.txStartResult (id, true);
  QVERIFY (h.controller.txNextPending ());
  auto const started = h.controller.messageStarted (id);
  QCOMPARE (count (started, ActionKind::ClearTxNextUi), 1);
  QCOMPARE (count (started, ActionKind::RecordBeaconTransmission), 1);
  QVERIFY (!h.controller.txNextPending ());
}

void TestBeaconTxController::txNextCancellationReusesUnderlyingPlan ()
{
  Harness h;
  h.initialize ();
  h.prepare (Disposition::Transmit);
  h.controller.setTxNext (true);
  h.controller.setTxNext (false);
  QVERIFY (h.controller.observeUtc (1100).front ().enabled);
}

void TestBeaconTxController::suppressedTxNextCancellationDoesNotDraw ()
{
  Harness h;
  h.initialize ();
  h.controller.setTxNext (true);
  QVERIFY (h.controller.receiveCompleted ().empty ());
  h.controller.setTxNext (false);
  QVERIFY (!h.controller.observeUtc (1100).front ().enabled);
}

void TestBeaconTxController::fixedRoundRobinUsesUtcBoundaryWithoutProposal ()
{
  Harness h;
  h.initialize (RoundRobinPolicy::fixed (1, 2));
  QVERIFY (h.controller.receiveCompleted ().empty ());
  QVERIFY (h.controller.observeUtc (1100).front ().enabled);
  auto const receiveBoundary = h.controller.observeUtc (2100);
  QCOMPARE (count (receiveBoundary, ActionKind::SetTransmitWindow), 1);
  QVERIFY (!receiveBoundary.front ().enabled);
  QVERIFY (!h.controller.observeUtc (2199).size ());
}

void TestBeaconTxController::txNextPrecedesFixedRoundRobin ()
{
  Harness h;
  h.initialize (RoundRobinPolicy::fixed (0, 2));
  h.controller.setTxNext (true);
  h.controller.receiveCompleted ();
  QVERIFY (h.controller.observeUtc (1100).front ().enabled);
}

void TestBeaconTxController::policyChangeDuringTxDrawsOnceAtCompletion ()
{
  Harness h;
  h.initialize (RoundRobinPolicy::fixed (1, 2));
  auto const id = h.controller.observeUtc (1100).front ().planId;
  h.controller.txStartRequested ();
  h.controller.txStartResult (id, true);
  h.controller.messageStarted (id);
  h.controller.setRoundRobinPolicy (RoundRobinPolicy::random ());
  h.controller.transmitWindowEnded ();
  QCOMPARE (count (h.controller.txStopped (id), ActionKind::RequestScheduleProposal), 1);
  QVERIFY (h.controller.txStopped (id).empty ());
}

void TestBeaconTxController::bandChangeFailureConvertsSamePlanToReceive ()
{
  Harness h;
  h.initialize ();
  auto const request = h.controller.receiveCompleted ();
  auto const id = first (request, ActionKind::RequestScheduleProposal)->planId;
  auto const apply = h.controller.proposalDelivered (id, hoppingProposal (Disposition::Transmit));
  QCOMPARE (count (apply, ActionKind::ApplyBandChange), 1);
  QVERIFY (h.controller.bandChangeOutcome (id, false).empty ());
  QVERIFY (!h.controller.observeUtc (1100).front ().enabled);
}

void TestBeaconTxController::staleAndDuplicateBandOutcomesAreIgnored ()
{
  Harness h;
  h.initialize ();
  auto const request = h.controller.receiveCompleted ();
  auto const id = first (request, ActionKind::RequestScheduleProposal)->planId;
  auto const apply = h.controller.proposalDelivered (id, hoppingProposal (Disposition::Transmit, true));
  QCOMPARE (count (apply, ActionKind::ApplyBandChange), 1);
  auto stale = id;
  ++stale.epoch;
  QVERIFY (h.controller.bandChangeOutcome (stale, true).empty ());
  QCOMPARE (count (h.controller.bandChangeOutcome (id, true), ActionKind::StartAutomaticTune), 1);
  QVERIFY (h.controller.bandChangeOutcome (id, true).empty ());
  QVERIFY (h.controller.proposalDelivered (id, hoppingProposal (Disposition::Transmit)).empty ());
}

void TestBeaconTxController::proposalRequestDoesNotActuate ()
{
  Harness h;
  h.initialize ();
  auto const actions = h.controller.receiveCompleted ();
  QCOMPARE (count (actions, ActionKind::RequestScheduleProposal), 1);
  QCOMPARE (count (actions, ActionKind::ApplyBandChange), 0);
  QCOMPARE (count (actions, ActionKind::StartAutomaticTune), 0);
}

void TestBeaconTxController::startedSessionCrossesBoundaryAndPreparesOnStop ()
{
  Harness h;
  h.initialize ();
  auto const id = h.enterPreparedTransmit ();
  h.controller.txStartRequested ();
  h.controller.txStartResult (id, true);
  h.controller.messageStarted (id);

  auto const boundary = h.controller.observeUtc (2100);
  QVERIFY (!boundary.front ().enabled);
  QCOMPARE (h.controller.txLifecycle (), TxLifecycle::MessageStarted);
  QCOMPARE (count (h.controller.txStopped (id), ActionKind::RequestScheduleProposal), 1);
}

void TestBeaconTxController::oldStopCannotPrepareReenteredEpoch ()
{
  Harness h;
  h.initialize ();
  auto const oldId = h.enterPreparedTransmit ();
  h.controller.txStartRequested ();
  h.controller.txStartResult (oldId, true);
  h.controller.messageStarted (oldId);
  h.controller.exitMode ();
  h.controller.enterMode (1000);
  h.controller.observeUtc (2100);

  QCOMPARE (count (h.controller.txStopped (oldId), ActionKind::RequestScheduleProposal), 0);
  QVERIFY (!h.controller.futurePlan ());
  QCOMPARE (h.controller.txLifecycle (), TxLifecycle::None);
  QVERIFY (!h.controller.txPlanId ().isValid ());
  QVERIFY (h.controller.txStopped (oldId).empty ());

  auto const completion = h.controller.receiveCompleted ();
  QCOMPARE (count (completion, ActionKind::RequestScheduleProposal), 1);
  auto const nextId = first (completion, ActionKind::RequestScheduleProposal)->planId;
  QVERIFY (h.controller.receiveCompleted ().empty ());
  QVERIFY (h.controller.txStopped (oldId).empty ());

  h.controller.proposalDelivered (nextId, proposal (Disposition::Transmit));
  auto const boundary = h.controller.observeUtc (3100);
  QCOMPARE (count (boundary, ActionKind::SetTransmitWindow), 1);
  QVERIFY (first (boundary, ActionKind::SetTransmitWindow)->enabled);
  QVERIFY (h.controller.currentPlanId () == nextId);
}

void TestBeaconTxController::txNextWithdrawalAfterBoundaryUsesUnderlyingReceive ()
{
  Harness h;
  h.initialize ();
  h.prepare (Disposition::Receive);
  h.controller.setTxNext (true);
  QVERIFY (h.controller.observeUtc (1100).front ().enabled);

  auto const withdrawn = h.controller.setTxNext (false);
  QCOMPARE (count (withdrawn, ActionKind::SetTransmitWindow), 1);
  QVERIFY (!withdrawn.front ().enabled);
  QCOMPARE (h.controller.txLifecycle (), TxLifecycle::None);
}

void TestBeaconTxController::txNextWithdrawalDuringStartWaitsForStop ()
{
  Harness h;
  h.initialize ();
  h.prepare (Disposition::Receive);
  h.controller.setTxNext (true);
  auto const id = h.controller.observeUtc (1100).front ().planId;
  h.controller.txStartRequested ();

  auto const withdrawn = h.controller.setTxNext (false);
  QVERIFY (!first (withdrawn, ActionKind::SetTransmitWindow)->enabled);
  QCOMPARE (h.controller.txLifecycle (), TxLifecycle::StartRequested);
  QCOMPARE (count (withdrawn, ActionKind::RequestScheduleProposal), 0);
  QCOMPARE (count (h.controller.txStopped (id), ActionKind::RequestScheduleProposal), 1);
}

void TestBeaconTxController::acceptedStartIgnoresConflictingDuplicateResult ()
{
  Harness h;
  h.initialize ();
  auto const id = h.enterPreparedTransmit ();
  h.controller.txStartRequested ();
  h.controller.txStartResult (id, true);

  QVERIFY (h.controller.txStartResult (id, false).empty ());
  QCOMPARE (h.controller.txLifecycle (), TxLifecycle::StartRequested);
  QCOMPARE (count (h.controller.messageStarted (id), ActionKind::RecordBeaconTransmission), 1);
}

void TestBeaconTxController::acceptedStartWithoutMessageRecordsNothingAndRecovers ()
{
  Harness h;
  h.initialize ();
  auto const id = h.enterPreparedTransmit ();
  h.controller.txStartRequested ();
  h.controller.txStartResult (id, true);

  auto const ended = h.controller.transmitWindowEnded ();
  QCOMPARE (count (ended, ActionKind::RecordBeaconTransmission), 0);
  QCOMPARE (count (ended, ActionKind::RequestScheduleProposal), 0);
  QCOMPARE (count (h.controller.txStopped (id), ActionKind::RequestScheduleProposal), 1);
}

void TestBeaconTxController::earlyTxStopClosesWindowAndPrepares ()
{
  Harness h;
  h.initialize ();
  auto const id = h.enterPreparedTransmit ();
  h.controller.txStartRequested ();
  h.controller.txStartResult (id, true);
  h.controller.messageStarted (id);

  auto const stopped = h.controller.txStopped (id);
  QCOMPARE (count (stopped, ActionKind::SetTransmitWindow), 1);
  QVERIFY (!first (stopped, ActionKind::SetTransmitWindow)->enabled);
  QCOMPARE (count (stopped, ActionKind::RequestScheduleProposal), 1);
}

void TestBeaconTxController::autoDisabledForcesReceiveWithoutRedraw ()
{
  Harness h;
  h.initialize ();
  h.prepare (Disposition::Transmit);
  h.controller.setAutoEnabled (false);

  auto const boundary = h.controller.observeUtc (1100);
  QVERIFY (!boundary.front ().enabled);
  QCOMPARE (count (boundary, ActionKind::RequestScheduleProposal), 0);
}

void TestBeaconTxController::backwardClockStepResynchronizesToReceive ()
{
  Harness h;
  h.initialize ();
  h.prepare (Disposition::Transmit);
  auto const epoch = h.controller.currentPlanId ().epoch;

  auto const resync = h.controller.observeUtc (50);
  QCOMPARE (count (resync, ActionKind::SetTransmitWindow), 1);
  QVERIFY (!resync.front ().enabled);
  QVERIFY (h.controller.currentPlanId ().epoch != epoch);
  QVERIFY (!h.controller.futurePlan ());
}

void TestBeaconTxController::oneProposalRequestAndApplicationPerPlanId ()
{
  Harness h;
  h.initialize ();
  ActionRecorder recorder;
  auto const requested = h.controller.receiveCompleted ();
  recorder.record (requested);
  auto const id = first (requested, ActionKind::RequestScheduleProposal)->planId;
  auto const drawn = hoppingProposal (Disposition::Transmit);
  auto const apply = h.controller.proposalDelivered (id, drawn);
  recorder.record (apply);
  h.controller.bandChangeOutcome (id, true);

  recorder.record (h.controller.receiveCompleted ());
  recorder.record (h.controller.proposalDelivered (id, drawn));
  recorder.record (h.controller.bandChangeOutcome (id, false));
  QCOMPARE (recorder.countFor (id, ActionKind::RequestScheduleProposal), 1);
  QCOMPARE (recorder.countFor (id, ActionKind::ApplyBandChange), 1);
}

QTEST_GUILESS_MAIN (TestBeaconTxController)

#include "test_beacon_tx_controller.moc"
