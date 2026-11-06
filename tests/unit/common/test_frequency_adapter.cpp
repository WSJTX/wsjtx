#include <QtTest>
#include "FrequencyAdapter.hpp"

using namespace RigFrequency;

class TestFrequencyAdapter : public QObject
{
  Q_OBJECT

  static Request submit (FrequencyAdapter& adapter, quint64 id, Path path,
                         Radio::Frequency target, FrequencyAdapter::Projection projection = {})
  {
    Request request {adapter.generation (), id, path, target};
    if (!adapter.submit (request, projection)) return {};
    return adapter.take_dispatch (path).value_or (Request {});
  }

private:
  Q_SLOT void backendResolutionCodes ()
  {
    for (auto const sign : {-1, 1})
      {
        QCOMPARE (FrequencyAdapter::resolution_hz (sign), Radio::Frequency {10});
        QCOMPARE (FrequencyAdapter::resolution_hz (sign * 2), Radio::Frequency {20});
        QCOMPARE (FrequencyAdapter::resolution_hz (sign * 3), Radio::Frequency {100});
      }
    QCOMPARE (FrequencyAdapter::resolution_hz (0), Radio::Frequency {1});
  }

  Q_SLOT void readbackTolerance_data ()
  {
    QTest::addColumn<int> ("code");
    QTest::addColumn<int> ("delta");
    QTest::addColumn<bool> ("tx");
    QTest::addColumn<bool> ("established");
    for (auto code : {0, -1, 1, -2, 2, -3, 3})
      {
        auto const hz = code == 0 ? 1 : std::abs (code) == 1 ? 10 : std::abs (code) == 2 ? 20 : 100;
        QList<int> const deltas = code ? QList<int> {0, hz - 1, -(hz - 1), hz, -hz, hz + 1, -(hz + 1)}
                                      : QList<int> {0, 1, -1, 2, -2};
        for (auto delta : deltas)
          for (auto tx : {false, true})
            {
              auto name = QString {"code%1-%2-delta%3"}.arg (code).arg (tx ? "TX" : "RX").arg (delta);
              QTest::newRow (qPrintable (name)) << code << delta << tx << (std::abs (delta) < hz);
            }
      }
  }

  Q_SLOT void readbackTolerance ()
  {
    QFETCH (int, code);
    QFETCH (int, delta);
    QFETCH (bool, tx);
    QFETCH (bool, established);
    FrequencyAdapter adapter;
    adapter.resolution (FrequencyAdapter::resolution_hz (code));
    auto const path = tx ? Path::Tx : Path::Rx;
    auto const nominal = Radio::Frequency {144200000};
    auto const command = submit (adapter, 1, path, nominal, {0, 3., 0., true});
    auto const observed = static_cast<Radio::Frequency> (static_cast<Radio::FrequencyDelta> (command.target) + delta);
    QVERIFY (!adapter.observe ({0, 1, path, command.target + 1000, Basis::Readback, {}}));
    auto const evidence = adapter.observe ({0, 2, path, observed, Basis::Readback, command});
    QVERIFY (evidence);
    auto const expected = established ? nominal
      : static_cast<Radio::Frequency> (static_cast<Radio::FrequencyDelta> (nominal) + delta);
    QCOMPARE (evidence->value, expected);
    auto const result = adapter.complete ({command, Outcome::Established, Basis::Readback, observed});
    QVERIFY (result);
    QCOMPARE (result->outcome, established ? Outcome::Established : Outcome::Failed);
    QCOMPARE (result->applied, expected);
    QCOMPARE (result->request.target, nominal);
    QCOMPARE (bool (adapter.snapshot (path).established), established);
    if (established) QCOMPARE (adapter.decode (path, observed), nominal);
  }

  Q_SLOT void startupCalibrationDoesNotRequireAnEstablishedTarget ()
  {
    FrequencyAdapter adapter;
    adapter.default_projection ({0, 100., 0., true});
    auto const observation = adapter.observe ({0, 1, Path::Rx, 7074100, Basis::Readback, {}});
    QVERIFY (observation);
    QCOMPARE (observation->value, Radio::Frequency {7074000});
    QCOMPARE (adapter.decode (Path::Rx, 7074100), Radio::Frequency {7074000});
    QVERIFY (!adapter.snapshot (Path::Rx).established);
    auto const command = submit (adapter, 1, Path::Rx, 7074000, {0, 0.5, 0., true});
    auto const result = adapter.complete ({command, Outcome::Established, Basis::BackendOperation, command.target});
    QVERIFY (result);
    QCOMPARE (result->applied, Radio::Frequency {7074000});
  }

  Q_SLOT void projectionRoundTrips ()
  {
    FrequencyAdapter adapter;
    FrequencyAdapter::Projection projection {-116000000, 12., 2., true};
    auto const command = submit (adapter, 1, Path::Rx, 144174000, projection);
    QCOMPARE (command.target, Radio::Frequency {28174068});
    auto const result = adapter.complete ({command, Outcome::Established, Basis::Readback, command.target});
    QVERIFY (result);
    QCOMPARE (result->request.target, Radio::Frequency {144174000});
    QCOMPARE (result->applied, Radio::Frequency {144174000});
    auto observation = adapter.observe ({0, 1, Path::Rx, command.target + 100, Basis::Readback, {}});
    QVERIFY (observation);
    QCOMPARE (observation->value, Radio::Frequency {144174100});
  }

  Q_SLOT void projectionChangesWaitForEvidence ()
  {
    FrequencyAdapter adapter;
    auto const old = submit (adapter, 1, Path::Rx, 144174000, {-116000000, 0., 0., true});
    QVERIFY (adapter.complete ({old, Outcome::Established, Basis::Readback, old.target}));
    auto const next = submit (adapter, 2, Path::Rx, 144174000, {-116000000, 100., 0., true});
    QCOMPARE (adapter.decode (Path::Rx, old.target), Radio::Frequency {144174000});
    QVERIFY (adapter.complete ({next, Outcome::Deferred, Basis::Retained, old.target}));
    QCOMPARE (adapter.decode (Path::Rx, old.target), Radio::Frequency {144174000});
    QVERIFY (adapter.submit ({0, 3, Path::Rx, 144174000}, {-116000000, 100., 0., true}));
    QVERIFY (!adapter.take_dispatch (Path::Rx));
    adapter.resume_deferred ();
    auto const retry = *adapter.take_dispatch (Path::Rx);
    QVERIFY (adapter.complete ({retry, Outcome::Established, Basis::Readback, retry.target}));
    QCOMPARE (adapter.decode (Path::Rx, retry.target), Radio::Frequency {144174000});
    QVERIFY (!adapter.complete ({old, Outcome::Established, Basis::Readback, old.target}));
  }

  Q_SLOT void partialResultsAreIndependent ()
  {
    FrequencyAdapter adapter;
    auto const rx = submit (adapter, 1, Path::Rx, 7074000);
    auto const tx = submit (adapter, 2, Path::Tx, 7075500);
    QVERIFY (adapter.complete ({rx, Outcome::Established, Basis::BackendOperation, rx.target}));
    QVERIFY (adapter.complete ({tx, Outcome::Deferred, Basis::Retained, 0}));
    QVERIFY (adapter.snapshot (Path::Rx).established);
    QCOMPARE (adapter.snapshot (Path::Rx).established->basis, Basis::BackendOperation);
    QVERIFY (!adapter.snapshot (Path::Tx).established);
    QVERIFY (adapter.submit ({0, 3, Path::Tx, 7075500}, {}));
    adapter.resume_deferred ();
    auto const tx_retry = *adapter.take_dispatch (Path::Tx);
    QVERIFY (adapter.complete ({tx_retry, Outcome::Established, Basis::Emulated, tx_retry.target}));
    QCOMPARE (adapter.snapshot (Path::Tx).established->basis, Basis::Emulated);
    QCOMPARE (adapter.snapshot (Path::Rx).established->request.id, quint64 {1});
  }

  Q_SLOT void newestQueuedIntentSurvivesDelayedResults ()
  {
    FrequencyAdapter adapter;
    auto const first = submit (adapter, 1, Path::Rx, 7074000);
    for (quint64 id = 2; id != 100; ++id)
      QVERIFY (adapter.submit ({0, id, Path::Rx, 7074000 + id}, {}));
    QVERIFY (!adapter.take_dispatch (Path::Rx));
    QCOMPARE (adapter.snapshot (Path::Rx).queued->request.id, quint64 {99});
    QVERIFY (adapter.complete ({first, Outcome::Established, Basis::Readback, first.target}));
    auto const newest = adapter.take_dispatch (Path::Rx);
    QVERIFY (newest);
    QCOMPARE (newest->target, Radio::Frequency {7074099});
    QVERIFY (!adapter.complete ({first, Outcome::Established, Basis::Readback, first.target}));
    QVERIFY (adapter.complete ({*newest, Outcome::Established, Basis::Readback, newest->target}));
    QCOMPARE (adapter.snapshot (Path::Rx).established->request.id, quint64 {99});
  }

  Q_SLOT void cancellationRestoresDialObservationWithoutReplayingOldTargets_data ()
  {
    QTest::addColumn<bool> ("deferred");
    QTest::newRow ("in-flight") << false;
    QTest::newRow ("deferred") << true;
  }

  Q_SLOT void cancellationRestoresDialObservationWithoutReplayingOldTargets ()
  {
    QFETCH (bool, deferred);
    FrequencyAdapter adapter;
    auto const initial = submit (adapter, 1, Path::Rx, 144174000,
                                 {-116000000, 100., 0., true});
    QVERIFY (adapter.complete ({initial, Outcome::Established, Basis::Readback, initial.target}));
    auto const pendingRx = submit (adapter, 2, Path::Rx, 144174100,
                                   {-116000000, 200., 0., true});
    if (deferred)
      QVERIFY (adapter.complete ({pendingRx, Outcome::Deferred, Basis::Retained, initial.target}));
    Request const queued {adapter.generation (), 3, Path::Rx, 144174200};
    QVERIFY (adapter.submit (queued, {-116000000, 300., 0., true}));
    auto const pendingTx = submit (adapter, 4, Path::Tx, 144175000);
    auto const generation = adapter.generation ();

    auto const cancelled = adapter.cancel_pending ();
    QCOMPARE (int (cancelled.size ()), 3);
    auto cancelledRequest = [&] (quint64 id, Radio::Frequency target) {
      for (auto const& request : cancelled)
        if (request.id == id && request.target == target && request.generation == generation)
          return true;
      return false;
    };
    QVERIFY (cancelledRequest (2, 144174100));
    QVERIFY (cancelledRequest (3, 144174200));
    QVERIFY (cancelledRequest (4, 144175000));
    QCOMPARE (adapter.generation (), generation);
    for (auto path : {Path::Rx, Path::Tx})
      {
        QVERIFY (!adapter.snapshot (path).pending);
        QVERIFY (!adapter.snapshot (path).queued);
      }
    QCOMPARE (adapter.snapshot (Path::Rx).established->request.id, quint64 {1});
    QCOMPARE (adapter.decode (Path::Rx, initial.target), Radio::Frequency {144174000});
    adapter.resume_deferred ();
    QVERIFY (!adapter.take_dispatch (Path::Rx));
    QVERIFY (!adapter.take_dispatch (Path::Tx));

    auto const dial = adapter.observe ({generation, 1, Path::Rx, initial.target + 500,
                                       Basis::Readback, {}});
    QVERIFY (dial);
    QCOMPARE (dial->value, Radio::Frequency {144174500});
    QVERIFY (!dial->operation);
    QVERIFY (!adapter.complete ({pendingRx, Outcome::Failed, Basis::Retained, 0}));
    QVERIFY (!adapter.complete ({pendingTx, Outcome::Established, Basis::Readback, pendingTx.target}));
    QVERIFY (!adapter.observe ({generation, 2, Path::Rx, pendingRx.target,
                                Basis::Readback, pendingRx}));
    QVERIFY (adapter.cancel_pending ().empty ());

    auto const current = submit (adapter, 5, Path::Rx, 144174500,
                                 {-116000000, 100., 0., true});
    QVERIFY (current);
    QVERIFY (adapter.complete ({current, Outcome::Established, Basis::Readback, current.target}));
    QCOMPARE (adapter.decode (Path::Rx, current.target), Radio::Frequency {144174500});
  }

  Q_SLOT void staleSessionsAndRetainedReportsCannotChangeMappings ()
  {
    FrequencyAdapter adapter;
    auto const stale = submit (adapter, 1, Path::Rx, 7074000);
    adapter.new_connection ();
    auto const current = submit (adapter, 2, Path::Rx, 14074000);
    QVERIFY (!adapter.complete ({stale, Outcome::Established, Basis::Readback, stale.target}));
    QVERIFY (!adapter.observe ({0, 10, Path::Rx, stale.target, Basis::Readback, stale}));
    QVERIFY (!adapter.observe ({1, 11, Path::Rx, current.target, Basis::Retained, {}}));
    QVERIFY (adapter.complete ({current, Outcome::Established, Basis::Readback, current.target}));
    QVERIFY (adapter.observe ({1, 12, Path::Rx, current.target + 100, Basis::Readback, {}}));
    QVERIFY (!adapter.observe ({1, 11, Path::Rx, current.target - 100, Basis::Readback, {}}));
    QVERIFY (!adapter.observe ({1, 12, Path::Rx, current.target, Basis::Readback, {}}));
  }

  Q_SLOT void reconnectPreservesProjectionWithoutOldOperationIdentity ()
  {
    FrequencyAdapter adapter;
    auto const corrected_target = Radio::Frequency {144174120};
    auto const old = submit (adapter, 7, Path::Rx, corrected_target, {-116000000, 100., 0., true});
    QVERIFY (adapter.complete ({old, Outcome::Established, Basis::Readback, old.target}));
    adapter.new_connection ();
    adapter.default_projection ({0, 200., 0., true});
    QVERIFY (!adapter.snapshot (Path::Rx).established);
    QVERIFY (!adapter.observe ({0, 2, Path::Rx, old.target, Basis::Readback, old}));
    auto const discovery = adapter.observe ({1, 1, Path::Rx, old.target, Basis::Readback, {}});
    QVERIFY (discovery);
    QCOMPARE (discovery->value, corrected_target);
    QVERIFY (!discovery->operation);
    QCOMPARE (adapter.decode (Path::Rx, old.target), corrected_target);
    auto const next = submit (adapter, 1, Path::Rx, 432174000, {-404000000, 200., 0., true});
    QCOMPARE (adapter.decode (Path::Rx, old.target), corrected_target);
    QVERIFY (adapter.complete ({next, Outcome::Established, Basis::Readback, next.target}));
    QCOMPARE (adapter.decode (Path::Rx, next.target), Radio::Frequency {432174000});
  }

  Q_SLOT void unchangedTargetsStillRequireEvidence ()
  {
    FrequencyAdapter adapter;
    auto const first = submit (adapter, 1, Path::Rx, 7074000);
    QVERIFY (adapter.complete ({first, Outcome::Established, Basis::Readback, first.target}));
    auto const same = submit (adapter, 2, Path::Rx, 7074000);
    QCOMPARE (adapter.snapshot (Path::Rx).established->request.id, quint64 {1});
    QVERIFY (adapter.complete ({same, Outcome::Established, Basis::BackendOperation, same.target}));
    QCOMPARE (adapter.snapshot (Path::Rx).established->request.id, quint64 {2});
    auto const off = submit (adapter, 3, Path::Tx, 0, {-116000000, 100., 2., true});
    QCOMPARE (off.target, Radio::Frequency {0});
    QVERIFY (adapter.complete ({off, Outcome::Established, Basis::Emulated, 0}));
    QCOMPARE (adapter.decode (Path::Tx, 0), Radio::Frequency {0});
  }

  Q_SLOT void unresolvedAndMismatchedApplicationRequireNewConnection ()
  {
    FrequencyAdapter adapter;
    auto const first = submit (adapter, 1, Path::Rx, 7074000);
    QVERIFY (!adapter.observe ({0, 1, Path::Rx, 14074000, Basis::Readback, {}}));
    auto const result = adapter.complete ({first, Outcome::Established, Basis::Readback, first.target});
    QVERIFY (result);
    QCOMPARE (result->outcome, Outcome::Failed);
    QVERIFY (!adapter.snapshot (Path::Rx).established);
    QVERIFY (!adapter.submit ({0, 2, Path::Rx, 7074100}, {}));
    adapter.new_connection ();
    auto const retry = submit (adapter, 3, Path::Rx, 7074000);
    auto const mismatch = adapter.complete ({retry, Outcome::Established, Basis::Readback, retry.target + 10});
    QVERIFY (mismatch);
    QCOMPARE (mismatch->outcome, Outcome::Failed);
    adapter.new_connection ();
    auto const retained = submit (adapter, 4, Path::Rx, 7074000);
    auto const unproven = adapter.complete ({retained, Outcome::Established, Basis::Retained, retained.target});
    QVERIFY (unproven);
    QCOMPARE (unproven->outcome, Outcome::Failed);
    QVERIFY (!adapter.snapshot (Path::Rx).established);
    adapter.new_connection ();
    auto const rejected = submit (adapter, 5, Path::Tx, 7075500);
    auto const rejection = adapter.complete ({rejected, Outcome::Rejected, Basis::Retained, 0});
    QVERIFY (rejection);
    QCOMPARE (rejection->outcome, Outcome::Rejected);
    QVERIFY (!adapter.submit ({adapter.generation (), 6, Path::Tx, 7075600}, {}));
  }

  Q_SLOT void matchedReadbackResolvesIntermediateAmbiguity ()
  {
    FrequencyAdapter adapter;
    adapter.resolution (10);
    auto const request = submit (adapter, 1, Path::Rx, 7074004);
    QVERIFY (!adapter.observe ({0, 1, Path::Rx, 7070000, Basis::Readback, {}}));
    QVERIFY (adapter.observe ({0, 2, Path::Rx, 7074000, Basis::Readback, request}));
    auto const result = adapter.complete ({request, Outcome::Established, Basis::Readback, 7074000});
    QVERIFY (result);
    QCOMPARE (result->outcome, Outcome::Established);
  }

  Q_SLOT void deferredCalibrationWaitsForResumeAndAppliesNewestProjection ()
  {
    FrequencyAdapter adapter;
    auto const initial = submit (adapter, 1, Path::Rx, 144174000, {-116000000, 0., 0., true});
    QVERIFY (adapter.complete ({initial, Outcome::Established, Basis::Readback, initial.target}));
    auto const calibration = submit (adapter, 2, Path::Rx, 144174000, {-116000000, 100., 0., true});
    QVERIFY (adapter.complete ({calibration, Outcome::Deferred, Basis::Retained, initial.target}));
    for (auto i = 0; i != 5; ++i) QVERIFY (!adapter.take_dispatch (Path::Rx));
    QCOMPARE (adapter.decode (Path::Rx, initial.target), Radio::Frequency {144174000});
    QVERIFY (adapter.submit ({0, 3, Path::Rx, 144174000}, {-116000000, 200., 0., true}));
    QVERIFY (adapter.submit ({0, 4, Path::Rx, 144174000}, {-116000000, 300., 0., true}));
    adapter.resume_deferred ();
    auto const newest = adapter.take_dispatch (Path::Rx);
    QVERIFY (newest);
    QCOMPARE (newest->id, quint64 {4});
    QCOMPARE (newest->target, Radio::Frequency {28174300});
    QVERIFY (!adapter.complete ({calibration, Outcome::Established, Basis::Readback, calibration.target}));
    QVERIFY (adapter.complete ({*newest, Outcome::Established, Basis::Readback, newest->target}));
    QCOMPARE (adapter.decode (Path::Rx, newest->target), Radio::Frequency {144174000});
    QVERIFY (!adapter.snapshot (Path::Rx).queued);
  }

  Q_SLOT void physicalReplayFailureOfCurrentEmulatedMappingRequiresRecovery ()
  {
    FrequencyAdapter adapter;
    auto const emulated = submit (adapter, 1, Path::Tx, 7075500);
    QVERIFY (adapter.complete ({emulated, Outcome::Established, Basis::Emulated, emulated.target}));
    auto const failed = adapter.complete ({emulated, Outcome::Failed, Basis::Retained, 7074000});
    QVERIFY (failed);
    QCOMPARE (failed->outcome, Outcome::Failed);
    QVERIFY (!adapter.submit ({0, 2, Path::Tx, 7075600}, {}));
    adapter.new_connection ();
    auto const current = submit (adapter, 2, Path::Tx, 7075500);
    QVERIFY (adapter.complete ({current, Outcome::Established, Basis::Emulated, current.target}));
    auto const next = submit (adapter, 3, Path::Tx, 7075600);
    QVERIFY (adapter.submit ({1, 4, Path::Tx, 7075700}, {}));
    QVERIFY (!adapter.complete ({emulated, Outcome::Failed, Basis::Retained, 7074000}));
    QVERIFY (adapter.complete ({current, Outcome::Failed, Basis::Retained, 7074000}));
    QCOMPARE (adapter.snapshot (Path::Tx).pending->request.id, next.id);
    QCOMPARE (adapter.snapshot (Path::Tx).queued->request.id, quint64 {4});
    QVERIFY (!adapter.take_dispatch (Path::Tx));
  }
};

QTEST_APPLESS_MAIN (TestFrequencyAdapter)
#include "test_frequency_adapter.moc"
