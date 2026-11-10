#include <QtTest>
#include "OperatingFrequency.hpp"
#include "FrequencyAdapter.hpp"

namespace
{
  using Owner = OperatingFrequency;
  using Path = RigFrequency::Path;
  using Outcome = RigFrequency::Outcome;
  using Basis = RigFrequency::Basis;
  constexpr Radio::Frequency rf = 10368200000ULL;
  constexpr Radio::Frequency intermediate = 144200000;
  Owner::Observation offline {false, false, false, 0, 0};
  Owner::Observation initial {true, false, false, intermediate, 0};
  Owner::Context automatic {false, true, true, true, false};
  RigFrequencyChangePolicy::Activity idle;

  struct IndependentRig
  {
    QVector<RigFrequency::Request> commands;
    Radio::Frequency appliedRx = intermediate;
    Radio::Frequency appliedTx = 0;
    quint64 serial = 0;
    bool accepted = true;

    bool operator() (RigFrequency::Request const& request)
    {
      commands.append (request);
      return accepted;
    }
    void apply (int index)
    {
      auto const& request = commands.at (index);
      (request.path == Path::Rx ? appliedRx : appliedTx) = request.target;
    }
    void result (Owner& owner, int index, Outcome outcome = Outcome::Established,
                 Basis basis = Basis::Readback)
    {
      auto const& request = commands.at (index);
      owner.handleResult ({request, outcome, basis,
                           request.path == Path::Rx ? appliedRx : appliedTx});
    }
    void establish (Owner& owner, int index)
    {
      apply (index);
      result (owner, index);
    }
    RigFrequency::Evidence evidence (Path path, RigFrequency::Request operation = {})
    {
      return {1, ++serial, path, path == Path::Rx ? appliedRx : appliedTx,
              Basis::Readback, operation};
    }
    Owner::Observation report (bool ptt = false, bool split = false) const
    { return {true, ptt, split, appliedRx, appliedTx}; }
  };

  void connectOwner (Owner& owner) { owner.setConnectionGeneration (1); }
  Owner::Context monitoring ()
  {
    auto context = automatic;
    context.monitoring = true;
    return context;
  }
}

class TestOperatingFrequency : public QObject
{
  Q_OBJECT
private slots:
  void cfomCorrectionDeferredDuringStopTail_data ()
  {
    QTest::addColumn<qint64> ("initialCorrection");
    QTest::addColumn<qint64> ("step");
    QTest::newRow ("positive-fine") << qint64 (125) << qint64 (7);
    QTest::newRow ("negative-fine") << qint64 (-125) << qint64 (-7);
    QTest::newRow ("cross-zero-ten-hertz") << qint64 (-20) << qint64 (10);
    QTest::newRow ("twenty-hertz") << qint64 (40) << qint64 (-20);
    QTest::newRow ("hundred-hertz") << qint64 (300) << qint64 (100);
    QTest::newRow ("unchanged") << qint64 (125) << qint64 (0);
  }

  void cfomCorrectionDeferredDuringStopTail ()
  {
    QFETCH (qint64, initialCorrection);
    QFETCH (qint64, step);
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    frequency.setDesiredCorrection (initialCorrection, -initialCorrection);
    QVERIFY (frequency.requestNominal (rf, std::ref (rig)));
    rig.establish (frequency, 0);
    QVERIFY (frequency.requestTx (rf, std::ref (rig)));
    rig.establish (frequency, 1);
    auto const context = monitoring ();
    frequency.observe (rig.evidence (Path::Rx), context);
    auto established = initialCorrection;
    for (int cycle = 1; cycle <= 8; ++cycle)
      {
        auto receiving = rig.report ();
        auto transmitting = rig.report (true, true);
        frequency.reconcile (receiving, transmitting, context, std::ref (rig));
        auto const desired = initialCorrection + cycle * step;
        frequency.setDesiredCorrection (desired, -desired);
        RigFrequencyChangePolicy::Activity tail;
        tail.stopDelayActive = true;
        tail.rigPtt = true;
        auto const count = rig.commands.size ();
        QVERIFY (!frequency.applyCorrection (Path::Rx, tail, false, std::ref (rig)));
        QCOMPARE (rig.commands.size (), count);
        frequency.reconcile (transmitting, receiving, context, std::ref (rig));
        frequency.observe (rig.evidence (Path::Rx), context);
        QCOMPARE (frequency.rx (), rf);
        QCOMPARE (frequency.tx (), rf);
        QCOMPARE (frequency.remembered (), rf);
        QCOMPARE (frequency.correctedRx (), rf + established);
        QCOMPARE (rig.appliedRx, rf + established);
        QVERIFY (frequency.applyCorrection (Path::Rx, idle, false, std::ref (rig)));
        if (desired != established) rig.establish (frequency, rig.commands.size () - 1);
        QVERIFY (frequency.applyCorrection (Path::Tx, idle, false, std::ref (rig)));
        if (desired != established) rig.establish (frequency, rig.commands.size () - 1);
        QCOMPARE (rig.appliedRx, rf + desired);
        QCOMPARE (rig.appliedTx, rf - desired);
        frequency.observe (rig.evidence (Path::Rx), context);
        frequency.observe (rig.evidence (Path::Tx), context);
        QCOMPARE (frequency.rx (), rf);
        QCOMPARE (frequency.tx (), rf);
        QCOMPARE (frequency.remembered (), rf);
        established = desired;
      }
  }

  void startupAndDoppler_data ()
  {
    QTest::addColumn<bool> ("responseFirst");
    QTest::addColumn<qint64> ("initialCorrection");
    QTest::newRow ("correction-before-response") << false << qint64 (0);
    QTest::newRow ("response-before-correction") << true << qint64 (0);
    QTest::newRow ("nonzero-correction-before-response") << false << qint64 (125);
    QTest::newRow ("nonzero-response-before-correction") << true << qint64 (125);
  }

  void startupAndDoppler ()
  {
    QFETCH (bool, responseFirst);
    QFETCH (qint64, initialCorrection);
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    frequency.setDesiredCorrection (initialCorrection, -50);
    auto context = automatic;
    auto const transition = frequency.reconcile (offline, initial, context, std::ref (rig));
    QVERIFY (transition.restorationAccepted);
    QVERIFY (transition.monitor && *transition.monitor);
    QCOMPARE (transition.before.rx, 0ULL);
    QCOMPARE (transition.after.rx, rf);
    QCOMPARE (transition.after.tx, rf);
    QCOMPARE (transition.after.remembered, rf);
    QCOMPARE (rig.commands.size (), 1);
    QCOMPARE (rig.commands.first ().target, rf + initialCorrection);
    context.monitoring = true;
    if (responseFirst) rig.establish (frequency, 0);
    frequency.setDesiredCorrection (175, -50);
    if (!responseFirst)
      {
        QVERIFY (!frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
        rig.establish (frequency, 0);
      }
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    QCOMPARE (rig.commands.last ().target, rf + 175);
    rig.establish (frequency, 1);
    frequency.observe (rig.evidence (Path::Rx), context);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.tx (), rf);
    QCOMPARE (frequency.remembered (), rf);
    auto const count = rig.commands.size ();
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    QCOMPARE (rig.commands.size (), count);
    auto duplicate = rig.evidence (Path::Rx);
    frequency.observe (duplicate, context);
    frequency.observe (duplicate, context);
    rig.appliedRx += 1000;
    frequency.observe (rig.evidence (Path::Rx), context);
    QCOMPARE (frequency.rx (), rf + 1000);
    QCOMPARE (frequency.remembered (), rf + 1000);
  }

  void manualMonitorPreservesSavedTarget ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    auto context = automatic;
    context.monitorAtStartup = false;
    auto transition = frequency.reconcile (offline, initial, context, std::ref (rig));
    QVERIFY (transition.monitor && !*transition.monitor);
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.tx (), intermediate);
    QCOMPARE (frequency.remembered (), rf);
    rig.appliedRx += 2000;
    frequency.observe (rig.evidence (Path::Rx), context);
    QCOMPARE (frequency.rx (), rig.appliedRx);
    QCOMPARE (frequency.remembered (), rf);
    transition = frequency.enableMonitor (context, std::ref (rig));
    QVERIFY (transition.restorationAccepted);
    QCOMPARE (rig.commands.size (), 1);
    QCOMPARE (rig.commands.first ().target, rf);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
    // A discovery report queued behind restoration cannot overwrite its target.
    frequency.observe (rig.evidence (Path::Rx), context);
    QCOMPARE (frequency.rx (), rf);
    rig.establish (frequency, 0);
    QCOMPARE (rig.appliedRx, rf);
  }

  void restorationEligibility_data ()
  {
    QTest::addColumn<bool> ("restore");
    QTest::addColumn<bool> ("echo");
    QTest::addColumn<bool> ("allowed");
    QTest::newRow ("disabled") << false << false << true;
    QTest::newRow ("echo") << true << true << true;
    QTest::newRow ("monitor-blocked") << true << false << false;
  }

  void restorationEligibility ()
  {
    QFETCH (bool, restore);
    QFETCH (bool, echo);
    QFETCH (bool, allowed);
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    auto context = automatic;
    context.restore = restore;
    context.echo = echo;
    context.monitorAllowed = allowed;
    auto transition = frequency.reconcile (offline, initial, context, std::ref (rig));
    QVERIFY (!transition.restorationAccepted);
    QVERIFY (rig.commands.isEmpty ());
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.remembered (), !restore && allowed && !echo ? intermediate : rf);
    frequency.enableMonitor (context, std::ref (rig));
    QVERIFY (rig.commands.isEmpty ());
  }

  void startupEvidencePrecedesState ()
  {
    for (auto const restore : {false, true})
      {
        Owner frequency {rf};
        connectOwner (frequency);
        IndependentRig rig;
        auto context = automatic;
        context.restore = restore;
        frequency.observe (rig.evidence (Path::Rx), context);
        QCOMPARE (frequency.remembered (), rf);
        auto transition = frequency.reconcile (offline, initial, context, std::ref (rig));
        QCOMPARE (frequency.rx (), restore ? rf : intermediate);
        QCOMPARE (frequency.remembered (), restore ? rf : intermediate);
        QCOMPARE (transition.restorationAccepted, restore);
      }
  }

  void rejectionAndReconnect ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    rig.accepted = false;
    auto context = automatic;
    auto transition = frequency.reconcile (offline, initial, context, std::ref (rig));
    QVERIFY (!transition.restorationAccepted);
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.tx (), intermediate);
    QCOMPARE (frequency.remembered (), rf);
    context.monitoring = true;
    frequency.reconcile (initial, initial, context, std::ref (rig));
    frequency.reconcile (initial, offline, context, std::ref (rig));
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.remembered (), rf);
    Owner::Observation zero {true, false, false, 0, 0};
    frequency.reconcile (initial, zero, context, std::ref (rig));
    QCOMPARE (frequency.remembered (), rf);
    frequency.reconcile (offline, initial, context, std::ref (rig));
    QCOMPARE (rig.commands.size (), 1);
    QCOMPARE (frequency.remembered (), rf);
    context.monitoring = false;
    rig.accepted = true;
    transition = frequency.reconcile (offline, initial, context, std::ref (rig));
    QVERIFY (transition.restorationAccepted);
    QCOMPARE (rig.commands.size (), 2);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
  }

  void acceptedRequestsAndTxReports ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    auto const* stableRx = &frequency.rx ();
    frequency.setDesiredCorrection (125, 50);
    QVERIFY (frequency.requestNominal (intermediate, std::ref (rig)));
    QCOMPARE (rig.commands.last ().target, intermediate + 125);
    QCOMPARE (*stableRx, intermediate);
    QCOMPARE (frequency.tx (), intermediate);
    rig.establish (frequency, 0);
    rig.accepted = false;
    QVERIFY (!frequency.requestNominal (432200000, std::ref (rig)));
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.tx (), intermediate);
    QCOMPARE (frequency.remembered (), rf);
    QVERIFY (!frequency.requestNominal (0, std::ref (rig)));
    auto const context = monitoring ();
    rig.appliedRx = 432200125;
    frequency.observe (rig.evidence (Path::Rx), context);
    QCOMPARE (frequency.rx (), 432200000ULL);
    QCOMPARE (frequency.remembered (), 432200000ULL);
    rig.accepted = true;
    QVERIFY (frequency.requestTx (432201000, std::ref (rig)));
    rig.establish (frequency, rig.commands.size () - 1);
    auto txContext = context;
    txContext.transmitting = txContext.split = true;
    frequency.observe (rig.evidence (Path::Tx), txContext);
    QCOMPARE (frequency.tx (), 432201000ULL);
    QCOMPARE (frequency.rx (), 432200000ULL);
    rig.appliedTx += 500;
    frequency.observe (rig.evidence (Path::Tx), txContext);
    QCOMPARE (frequency.tx (), 432201500ULL);
    frequency.setDesiredCorrection (125, 0);
    QVERIFY (frequency.applyCorrection (Path::Tx, idle, true, std::ref (rig)));
    rig.establish (frequency, rig.commands.size () - 1);
    rig.appliedTx = 432202000;
    frequency.observe (rig.evidence (Path::Tx), txContext);
    QCOMPARE (frequency.tx (), 432202000ULL);
    rig.accepted = false;
    QVERIFY (!frequency.requestTxPlan ({432203000, 500}, std::ref (rig)));
    QCOMPARE (frequency.tx (), 432202000ULL);
    QCOMPARE (frequency.audioOffset (), 0);
    rig.accepted = true;
    frequency.setDesiredCorrection (125, -25);
    QVERIFY (frequency.requestTxPlan ({432203000, 250}, std::ref (rig)));
    rig.establish (frequency, rig.commands.size () - 1);
    QCOMPARE (frequency.correctedTx (), 432202975ULL);
    QCOMPARE (frequency.rx (), 432200000ULL);
    QCOMPARE (frequency.audioOffset (), 250);
    QCOMPARE (frequency.snapshot ().audioOffset, 250);
    auto const commandCount = rig.commands.size ();
    QVERIFY (frequency.requestTxPlan ({0, 0}, std::ref (rig), true, false));
    QCOMPARE (rig.commands.size (), commandCount);
    QCOMPARE (frequency.tx (), 432203000ULL);
    QCOMPARE (frequency.audioOffset (), 0);
  }

  void retainedDuplicateAndStaleEvidence ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    frequency.setDesiredCorrection (125, 0);
    QVERIFY (frequency.requestNominal (rf, std::ref (rig)));
    rig.establish (frequency, 0);
    auto const context = monitoring ();
    auto old = rig.evidence (Path::Rx);
    frequency.observe (old, context);
    frequency.setDesiredCorrection (175, 0);
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    auto retained = rig.evidence (Path::Rx);
    retained.basis = Basis::Retained;
    retained.value += 1000;
    frequency.observe (retained, context);
    QVERIFY (frequency.snapshot ().pendingRx);
    frequency.reconcile (rig.report (true), rig.report (false), context, std::ref (rig));
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.correctedRx (), rf + 125);
    rig.establish (frequency, 1);
    auto newest = rig.evidence (Path::Rx, rig.commands.at (1));
    frequency.observe (newest, context);
    frequency.observe (newest, context);
    frequency.observe (old, context);
    rig.result (frequency, 0);
    QCOMPARE (frequency.correctedRx (), rf + 175);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
    auto wrongGeneration = newest;
    wrongGeneration.generation = 2;
    wrongGeneration.value += 5000;
    frequency.observe (wrongGeneration, context);
    QCOMPARE (frequency.rx (), rf);
  }

  void correctionRemovalAndCoalescing ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    frequency.setDesiredCorrection (125, -125);
    QVERIFY (frequency.requestNominal (rf, std::ref (rig)));
    rig.establish (frequency, 0);
    frequency.setDesiredCorrection (150, -150);
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    for (int correction = 151; correction <= 1000; ++correction)
      {
        frequency.setDesiredCorrection (correction, -correction);
        QVERIFY (!frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
      }
    QCOMPARE (rig.commands.size (), 2);
    rig.establish (frequency, 1);
    QCOMPARE (frequency.correctedRx (), rf + 150);
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    QCOMPARE (rig.commands.last ().target, rf + 1000);
    rig.result (frequency, 2, Outcome::Deferred);
    QVERIFY (!frequency.recoveryRequired ());
    QCOMPARE (frequency.correctedRx (), rf + 150);
    frequency.setDesiredCorrection (0, 0);
    RigFrequencyChangePolicy::Activity tail;
    tail.stopDelayActive = true;
    QVERIFY (!frequency.applyCorrection (Path::Rx, tail, false, std::ref (rig)));
    frequency.observe (rig.evidence (Path::Rx), monitoring ());
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.correctedRx (), rf + 150);
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, false, std::ref (rig)));
    QCOMPARE (rig.commands.last ().target, rf);
    rig.establish (frequency, rig.commands.size () - 1);
    QCOMPARE (frequency.correctedRx (), rf);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
  }

  void independentPathResults_data ()
  {
    QTest::addColumn<bool> ("rxFirst");
    QTest::addColumn<bool> ("failed");
    QTest::newRow ("rx-established-tx-deferred") << true << false;
    QTest::newRow ("tx-established-rx-deferred") << false << false;
    QTest::newRow ("rx-established-tx-failed") << true << true;
    QTest::newRow ("tx-established-rx-failed") << false << true;
  }

  void independentPathResults ()
  {
    QFETCH (bool, rxFirst);
    QFETCH (bool, failed);
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    QVERIFY (frequency.requestNominal (rf, std::ref (rig)));
    rig.establish (frequency, 0);
    QVERIFY (frequency.requestTx (rf, std::ref (rig)));
    rig.apply (1);
    rig.result (frequency, 1, Outcome::Established, Basis::BackendOperation);
    frequency.setDesiredCorrection (175, -50);
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    QVERIFY (frequency.applyCorrection (Path::Tx, idle, true, std::ref (rig)));
    rig.apply (rxFirst ? 2 : 3);
    rig.result (frequency, rxFirst ? 2 : 3, Outcome::Established, Basis::Emulated);
    rig.result (frequency, rxFirst ? 3 : 2, failed ? Outcome::Failed : Outcome::Deferred);
    QCOMPARE (frequency.correctedRx (), rf + (rxFirst ? 175 : 0));
    QCOMPARE (frequency.correctedTx (), rf - (rxFirst ? 0 : 50));
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.tx (), rf);
    QCOMPARE (frequency.remembered (), rf);
    QCOMPARE (frequency.recoveryRequired (), failed);
    QCOMPARE (frequency.snapshot ().establishedRxBasis, rxFirst ? Basis::Emulated : Basis::Readback);
    QCOMPARE (frequency.snapshot ().establishedTxBasis, rxFirst ? Basis::BackendOperation : Basis::Emulated);
    if (!failed)
      {
        QVERIFY (frequency.applyCorrection (rxFirst ? Path::Tx : Path::Rx,
                                            idle, true, std::ref (rig)));
        rig.establish (frequency, rig.commands.size () - 1);
        QCOMPARE (rig.appliedRx, rf + 175);
        QCOMPARE (rig.appliedTx, rf - 50);
      }
  }

  void unresolvedRequiresExplicitRetry ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    frequency.setDesiredCorrection (125, -50);
    QVERIFY (frequency.requestNominal (rf, std::ref (rig)));
    rig.establish (frequency, 0);
    frequency.setDesiredCorrection (175, -50);
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    rig.result (frequency, 1, Outcome::Failed);
    QVERIFY (frequency.recoveryRequired ());
    frequency.disconnect ();
    frequency.setConnectionGeneration (2);
    auto report = rig.evidence (Path::Rx);
    report.generation = 2;
    report.value += 5000;
    frequency.observe (report, monitoring ());
    frequency.reconcile (offline, rig.report (), monitoring (), std::ref (rig));
    QVERIFY (!frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    rig.result (frequency, 1);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
    QCOMPARE (frequency.correctedRx (), rf + 125);
    frequency.retry (2);
    frequency.observe (report, monitoring ());
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    QCOMPARE (rig.commands.last ().generation, 2ULL);
    QCOMPARE (rig.commands.last ().target, rf + 175);
    rig.establish (frequency, rig.commands.size () - 1);
    QCOMPARE (frequency.correctedRx (), rf + 175);
    QCOMPARE (frequency.rx (), rf);
    QVERIFY (!frequency.recoveryRequired ());
  }

  void intermediateOperationEvidenceRetainsMapping ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    frequency.setDesiredCorrection (125, 0);
    QVERIFY (frequency.requestNominal (rf, std::ref (rig)));
    rig.establish (frequency, 0);
    frequency.setDesiredCorrection (175, 0);
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    auto intermediateReport = rig.evidence (Path::Rx, rig.commands.last ());
    frequency.observe (intermediateReport, monitoring ());
    QVERIFY (!frequency.recoveryRequired ());
    QVERIFY (frequency.snapshot ().pendingRx);
    QCOMPARE (frequency.correctedRx (), rf + 125);
    QCOMPARE (frequency.rx (), rf);
    rig.apply (1);
    frequency.observe (rig.evidence (Path::Rx, rig.commands.last ()), monitoring ());
    QVERIFY (!frequency.snapshot ().pendingRx);
    QCOMPARE (frequency.correctedRx (), rf + 175);
    rig.result (frequency, 1);
    QCOMPARE (frequency.rx (), rf);
  }

  void retryReappliesUnchangedIntent ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    frequency.setDesiredCorrection (125, -50);
    QVERIFY (frequency.requestNominal (rf, std::ref (rig)));
    rig.establish (frequency, 0);
    QVERIFY (frequency.requestTx (rf, std::ref (rig)));
    rig.establish (frequency, 1);
    frequency.requireRecovery ();
    frequency.retry (1);
    QVERIFY (frequency.applyCorrection (Path::Rx, idle, true, std::ref (rig)));
    QCOMPARE (rig.commands.size (), 3);
    rig.establish (frequency, 2);
    QVERIFY (frequency.applyCorrection (Path::Tx, idle, true, std::ref (rig)));
    QCOMPARE (rig.commands.size (), 4);
    rig.establish (frequency, 3);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.tx (), rf);
    QCOMPARE (frequency.remembered (), rf);
  }

  void invalidResultCannotEstablishMapping ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    frequency.setDesiredCorrection (125, 0);
    QVERIFY (frequency.requestNominal (rf, std::ref (rig)));
    auto mismatched = rig.commands.at (0);
    ++mismatched.target;
    frequency.handleResult ({mismatched, Outcome::Established, Basis::Readback, mismatched.target});
    QVERIFY (frequency.snapshot ().pendingRx);
    QVERIFY (!frequency.recoveryRequired ());
    QCOMPARE (frequency.correctedRx (), rf);
    rig.apply (0);
    rig.result (frequency, 0, Outcome::Established, Basis::Retained);
    QVERIFY (frequency.recoveryRequired ());
    QCOMPARE (frequency.correctedRx (), rf);
    QCOMPARE (frequency.rx (), rf);
  }

  void productionAdapterStopTailSequence ()
  {
    Owner frequency {rf};
    FrequencyAdapter adapter;
    adapter.new_connection ();
    frequency.setConnectionGeneration (adapter.generation ());
    FrequencyAdapter::Projection projection;
    projection.offset = Radio::FrequencyDelta (intermediate) - Radio::FrequencyDelta (rf);
    IndependentRig rig;
    auto const submit = [&adapter, &projection] (RigFrequency::Request const& request) {
      return adapter.submit (request, projection);
    };
    auto const dispatch = [&] (Path path) {
      auto command = adapter.take_dispatch (path);
      if (command) rig.commands.append (*command);
      return bool (command);
    };
    auto const complete = [&] (int index, Outcome outcome) {
      auto const& command = rig.commands.at (index);
      auto translated = adapter.complete ({command, outcome, Basis::Readback,
        command.path == Path::Rx ? rig.appliedRx : rig.appliedTx});
      if (translated) frequency.handleResult (*translated);
      return bool (translated);
    };
    auto const observe = [&] (Path path, Basis basis) {
      auto evidence = rig.evidence (path);
      evidence.basis = basis;
      if (auto translated = adapter.observe (evidence))
        frequency.observe (*translated, monitoring ());
    };
    frequency.setDesiredCorrection (125, -125);
    QVERIFY (frequency.requestNominal (rf, submit));
    QVERIFY (dispatch (Path::Rx));
    QCOMPARE (rig.commands.at (0).target, intermediate + 125);
    rig.apply (0);
    QVERIFY (complete (0, Outcome::Established));
    QVERIFY (frequency.requestTx (rf, submit));
    QVERIFY (dispatch (Path::Tx));
    rig.apply (1);
    QVERIFY (complete (1, Outcome::Established));
    for (auto const correction : {175, -75, 0})
      {
        RigFrequencyChangePolicy::Activity tail;
        tail.stopDelayActive = tail.rigPtt = true;
        auto const before = rig.report (true, true);
        frequency.setDesiredCorrection (correction, -correction);
        QVERIFY (!frequency.applyCorrection (Path::Rx, tail, false, submit));
        frequency.reconcile (before, rig.report (false, true), monitoring (), submit);
        observe (Path::Rx, Basis::Retained);
        observe (Path::Tx, Basis::Retained);
        QCOMPARE (frequency.rx (), rf);
        QCOMPARE (frequency.tx (), rf);
        QCOMPARE (frequency.remembered (), rf);
        QVERIFY (frequency.applyCorrection (Path::Rx, idle, false, submit));
        QVERIFY (dispatch (Path::Rx));
        auto index = rig.commands.size () - 1;
        // The backend can still defer a request dispatched before PTT releases.
        QVERIFY (complete (index, Outcome::Deferred));
        QVERIFY (!frequency.recoveryRequired ());
        QVERIFY (frequency.applyCorrection (Path::Rx, idle, false, submit));
        adapter.resume_deferred ();
        QVERIFY (dispatch (Path::Rx));
        index = rig.commands.size () - 1;
        QCOMPARE (rig.commands.at (index).target, intermediate + correction);
        rig.apply (index);
        QVERIFY (complete (index, Outcome::Established));
        QVERIFY (frequency.applyCorrection (Path::Tx, idle, false, submit));
        QVERIFY (dispatch (Path::Tx));
        index = rig.commands.size () - 1;
        QCOMPARE (rig.commands.at (index).target, intermediate - correction);
        rig.apply (index);
        QVERIFY (complete (index, Outcome::Established));
        observe (Path::Rx, Basis::Readback);
        observe (Path::Tx, Basis::Readback);
        QCOMPARE (frequency.rx (), rf);
        QCOMPARE (frequency.tx (), rf);
        QCOMPARE (frequency.remembered (), rf);
        QCOMPARE (frequency.correctedRx (), rf + correction);
        QCOMPARE (frequency.correctedTx (), rf - correction);
      }
  }

  void monitorResumeReappliesEstablishedTxPlan ()
  {
    Owner frequency {rf};
    FrequencyAdapter adapter;
    adapter.new_connection ();
    frequency.setConnectionGeneration (adapter.generation ());
    FrequencyAdapter::Projection projection;
    projection.offset = Radio::FrequencyDelta (intermediate) - Radio::FrequencyDelta (rf);
    IndependentRig rig;
    frequency.setDesiredCorrection (125, -50);
    auto const submit = [&] (RigFrequency::Request const& request) {
      return adapter.submit (request, projection);
    };
    auto const apply = [&] (Path path) {
      auto command = adapter.take_dispatch (path);
      if (!command) return false;
      rig.commands.append (*command);
      rig.apply (rig.commands.size () - 1);
      auto translated = adapter.complete ({*command, Outcome::Established, Basis::Emulated,
        command->path == Path::Rx ? rig.appliedRx : rig.appliedTx});
      if (translated) frequency.handleResult (*translated);
      return bool (translated);
    };
    QVERIFY (frequency.requestNominal (rf, submit));
    QVERIFY (apply (Path::Rx));
    QVERIFY (frequency.requestTx (rf + 1000, submit));
    QVERIFY (apply (Path::Tx));
    auto const txOperation = adapter.snapshot (Path::Tx).established->request;
    QCOMPARE (rig.appliedTx, intermediate + 950);
    RigFrequency::Request monitorOff {adapter.generation (), (quint64 {1} << 63) + 1,
                                     Path::Tx, 0};
    QVERIFY (adapter.submit (monitorOff, projection));
    QVERIFY (apply (Path::Tx));
    QCOMPARE (rig.appliedTx, 0ULL);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.tx (), rf + 1000);
    QCOMPARE (frequency.remembered (), rf);
    QCOMPARE (frequency.correctedTx (), rf + 950);
    // Inactive-VFO reports while Monitor is stopped cannot replace the plan.
    auto report = rig.evidence (Path::Tx);
    if (auto translated = adapter.observe (report)) frequency.observe (*translated, automatic);
    QVERIFY (frequency.requestTx (rf + 1000, submit));
    QVERIFY (frequency.snapshot ().pendingTx);
    QVERIFY (apply (Path::Tx));
    QCOMPARE (rig.appliedTx, intermediate + 950);
    QCOMPARE (frequency.tx (), rf + 1000);
    QCOMPARE (frequency.remembered (), rf);
    // Duplicate synchronization results cannot invalidate its newer replacement.
    frequency.handleResult ({monitorOff, Outcome::Established, Basis::Emulated, 0});
    QVERIFY (frequency.requestTx (rf + 1000, submit));
    QVERIFY (!frequency.snapshot ().pendingTx);
    QVERIFY (!adapter.take_dispatch (Path::Tx));
    frequency.handleResult ({txOperation, Outcome::Failed, Basis::Emulated, txOperation.target});
    QVERIFY (!frequency.recoveryRequired ());
    auto restoredOperation = adapter.snapshot (Path::Tx).established->request;
    frequency.handleResult ({restoredOperation, Outcome::Failed, Basis::Emulated,
                            restoredOperation.target});
    QVERIFY (frequency.recoveryRequired ());
    QCOMPARE (frequency.correctedTx (), rf + 950);
    QCOMPARE (frequency.tx (), rf + 1000);
  }

  void offlineIntentAndSynchronousEmulation ()
  {
    Owner frequency {rf};
    IndependentRig rig;
    frequency.setDesiredCorrection (125, -50);
    QVERIFY (frequency.requestNominal (intermediate, std::ref (rig), false));
    QVERIFY (rig.commands.isEmpty ());
    QVERIFY (!frequency.snapshot ().pendingRx);
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.remembered (), rf);
    connectOwner (frequency);
    auto const emulate = [&frequency] (RigFrequency::Request const& request) {
      frequency.handleResult ({request, Outcome::Established, Basis::Emulated, request.target});
      return true;
    };
    QVERIFY (frequency.requestTx (intermediate + 500, emulate));
    QVERIFY (!frequency.snapshot ().pendingTx);
    QCOMPARE (frequency.correctedTx (), intermediate + 450);
    QCOMPARE (frequency.correctedRx (), intermediate);
    QCOMPARE (frequency.snapshot ().establishedTxBasis, Basis::Emulated);
    QCOMPARE (frequency.snapshot ().establishedRxBasis, Basis::Retained);
    frequency.requireRecovery ();
    frequency.setConnectionGeneration (2);
    QVERIFY (frequency.recoveryRequired ());
    QVERIFY (!frequency.requestTx (intermediate + 1000, emulate));
    QCOMPARE (frequency.tx (), intermediate + 500);
  }

  void connectedLocalIntentSurvivesUnchangedPolling ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    auto context = automatic;
    context.monitorAtStartup = false;
    frequency.reconcile (offline, initial, context, std::ref (rig));
    frequency.observe (rig.evidence (Path::Rx), context);
    QVERIFY (frequency.requestNominal (432200000, std::ref (rig), false));
    frequency.observe (rig.evidence (Path::Rx), context);
    frequency.observe (rig.evidence (Path::Rx), context);
    frequency.reconcile (initial, initial, context, std::ref (rig));
    QCOMPARE (frequency.rx (), 432200000ULL);
    QCOMPARE (frequency.tx (), 432200000ULL);
    QCOMPARE (frequency.remembered (), rf);
    rig.appliedRx += 1000;
    frequency.observe (rig.evidence (Path::Rx), context);
    QCOMPARE (frequency.rx (), intermediate + 1000);
    QCOMPARE (frequency.tx (), intermediate + 1000);
    QCOMPARE (frequency.remembered (), rf);
  }

  void readbackFollowsActiveFrequencyPath ()
  {
    Owner frequency {rf};
    connectOwner (frequency);
    IndependentRig rig;
    frequency.setDesiredCorrection (125, -50);
    QVERIFY (frequency.requestNominal (rf, std::ref (rig)));
    rig.establish (frequency, 0);
    QVERIFY (frequency.requestTx (rf + 1000, std::ref (rig)));
    rig.apply (1);
    auto commandEvidence = rig.evidence (Path::Tx, rig.commands.at (1));
    frequency.observe (commandEvidence, monitoring ());
    QCOMPARE (frequency.correctedTx (), rf + 950);
    QVERIFY (!frequency.snapshot ().pendingTx);
    rig.result (frequency, 1);
    rig.appliedTx += 5000;
    auto context = monitoring ();
    context.split = true;
    frequency.observe (rig.evidence (Path::Tx), context);
    QCOMPARE (frequency.tx (), rf + 1000);
    context.split = false;
    frequency.observe (rig.evidence (Path::Tx), context);
    QCOMPARE (frequency.tx (), rf + 1000);
    context.transmitting = context.split = true;
    frequency.observe (rig.evidence (Path::Tx), context);
    QCOMPARE (frequency.tx (), rf + 6000);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
    rig.appliedRx += 2000;
    frequency.observe (rig.evidence (Path::Rx), context);
    QCOMPARE (frequency.rx (), rf);
    context.split = false;
    frequency.observe (rig.evidence (Path::Rx), context);
    QCOMPARE (frequency.tx (), rf + 2125);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
    context.transmitting = false;
    frequency.observe (rig.evidence (Path::Rx), context);
    QCOMPARE (frequency.rx (), rf + 2000);
    QCOMPARE (frequency.tx (), rf + 2000);
    QCOMPARE (frequency.remembered (), rf + 2000);
  }
};

QTEST_GUILESS_MAIN (TestOperatingFrequency)
#include "test_operating_frequency.moc"
