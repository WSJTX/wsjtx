#include <type_traits>

#include <QtTest/QtTest>

#include "RigFrequencyChangePolicy.hpp"

Q_DECLARE_METATYPE (RigFrequencyChangePolicy::Activity)

namespace
{
  using RigFrequencyChangePolicy::Activity;
  using RigFrequencyChangePolicy::BlockReason;
  using RigFrequencyChangePolicy::ChangeKind;
  using RigFrequencyChangePolicy::Decision;

  using EvaluateFunction = Decision (*) (ChangeKind, Activity const&, bool);
  static_assert (std::is_same<decltype (&RigFrequencyChangePolicy::evaluate),
                              EvaluateFunction>::value,
                 "frequency-change policy must not depend on a frequency magnitude");

  void addActivityRows ()
  {
    QTest::newRow ("transmit-requested")
      << Activity{true, false, false, false, false, false, false, false, false};
    QTest::newRow ("transmit-command-pending")
      << Activity{false, true, false, false, false, false, false, false, false};
    QTest::newRow ("transmitting")
      << Activity{false, false, true, false, false, false, false, false, false};
    QTest::newRow ("tuning")
      << Activity{false, false, false, true, false, false, false, false, false};
    QTest::newRow ("jtty-transmitting")
      << Activity{false, false, false, false, true, false, false, false, false};
    QTest::newRow ("start-delay-active")
      << Activity{false, false, false, false, false, true, false, false, false};
    QTest::newRow ("stop-delay-active")
      << Activity{false, false, false, false, false, false, true, false, false};
    QTest::newRow ("rig-ptt")
      << Activity{false, false, false, false, false, false, false, true, false};
    QTest::newRow ("rig-tune")
      << Activity{false, false, false, false, false, false, false, false, true};
  }

  void compareDecision (Decision const& actual, bool allowed, BlockReason reason)
  {
    QCOMPARE (actual.allowed, allowed);
    QCOMPARE (static_cast<int> (actual.reason), static_cast<int> (reason));
  }
}

class TestRigFrequencyChangePolicy final
  : public QObject
{
  Q_OBJECT

private slots:
  void idleAllowsEveryChangeKind ();
  void everyActivityBlocksNominalQsy_data ();
  void everyActivityBlocksNominalQsy ();
  void transmitRequestedAloneAllowsPreKeyCorrection ();
  void activeRfCorrectionRequiresPermission_data ();
  void activeRfCorrectionRequiresPermission ();
  void combinedActivityUsesStableBlockReasons ();
  void evaluationSignatureExcludesFrequencyMagnitude ();
  void guardedApplyProtectsMutationBoundary ();
  void txPathFrequencyRequiresSplit ();
  void acceptedRequestControlsDependentCommit ();
  void rejectedRequestStopsMonitoringItStarted ();
};

void TestRigFrequencyChangePolicy::idleAllowsEveryChangeKind ()
{
  Activity const idle{};

  for (auto const correctionsAllowed : {false, true})
    {
      compareDecision (RigFrequencyChangePolicy::evaluate (
                         ChangeKind::NominalQsy, idle, correctionsAllowed),
                       true, BlockReason::None);
      compareDecision (RigFrequencyChangePolicy::evaluate (
                         ChangeKind::TxPathCorrection, idle, correctionsAllowed),
                       true, BlockReason::None);
    }
}

void TestRigFrequencyChangePolicy::everyActivityBlocksNominalQsy_data ()
{
  QTest::addColumn<Activity> ("activity");
  addActivityRows ();
}

void TestRigFrequencyChangePolicy::everyActivityBlocksNominalQsy ()
{
  QFETCH (Activity, activity);

  for (auto const correctionsAllowed : {false, true})
    {
      compareDecision (RigFrequencyChangePolicy::evaluate (
                         ChangeKind::NominalQsy, activity, correctionsAllowed),
                       false, BlockReason::NominalQsyWhileRfActive);
    }
}

void TestRigFrequencyChangePolicy::transmitRequestedAloneAllowsPreKeyCorrection ()
{
  Activity const requested{true, false, false, false, false, false, false, false, false};

  for (auto const correctionsAllowed : {false, true})
    {
      compareDecision (RigFrequencyChangePolicy::evaluate (
                         ChangeKind::TxPathCorrection, requested, correctionsAllowed),
                       true, BlockReason::None);
    }
}

void TestRigFrequencyChangePolicy::activeRfCorrectionRequiresPermission_data ()
{
  QTest::addColumn<Activity> ("activity");

  QTest::newRow ("transmit-command-pending")
    << Activity{false, true, false, false, false, false, false, false, false};
  QTest::newRow ("transmitting")
    << Activity{false, false, true, false, false, false, false, false, false};
  QTest::newRow ("tuning")
    << Activity{false, false, false, true, false, false, false, false, false};
  QTest::newRow ("jtty-transmitting")
    << Activity{false, false, false, false, true, false, false, false, false};
  QTest::newRow ("start-delay-active")
    << Activity{false, false, false, false, false, true, false, false, false};
  QTest::newRow ("stop-delay-active")
    << Activity{false, false, false, false, false, false, true, false, false};
  QTest::newRow ("rig-ptt")
    << Activity{false, false, false, false, false, false, false, true, false};
  QTest::newRow ("rig-tune")
    << Activity{false, false, false, false, false, false, false, false, true};
}

void TestRigFrequencyChangePolicy::activeRfCorrectionRequiresPermission ()
{
  QFETCH (Activity, activity);

  compareDecision (RigFrequencyChangePolicy::evaluate (
                     ChangeKind::TxPathCorrection, activity, false),
                   false, BlockReason::TxCorrectionDisabled);
  compareDecision (RigFrequencyChangePolicy::evaluate (
                     ChangeKind::TxPathCorrection, activity, true),
                   true, BlockReason::None);
}

void TestRigFrequencyChangePolicy::combinedActivityUsesStableBlockReasons ()
{
  Activity const allActive{true, true, true, true, true, true, true, true, true};

  compareDecision (RigFrequencyChangePolicy::evaluate (
                     ChangeKind::NominalQsy, allActive, true),
                   false, BlockReason::NominalQsyWhileRfActive);
  compareDecision (RigFrequencyChangePolicy::evaluate (
                     ChangeKind::TxPathCorrection, allActive, false),
                   false, BlockReason::TxCorrectionDisabled);
}

void TestRigFrequencyChangePolicy::evaluationSignatureExcludesFrequencyMagnitude ()
{
  EvaluateFunction const evaluator = &RigFrequencyChangePolicy::evaluate;
  QVERIFY (evaluator);
}

void TestRigFrequencyChangePolicy::guardedApplyProtectsMutationBoundary ()
{
  Activity active;
  active.rigPtt = true;
  bool mutated = false;

  QVERIFY (!RigFrequencyChangePolicy::applyIfAllowed (
    ChangeKind::NominalQsy, active, true, [&] {mutated = true;}));
  QVERIFY (!mutated);

  QVERIFY (RigFrequencyChangePolicy::applyIfAllowed (
    ChangeKind::TxPathCorrection, active, true, [&] {mutated = true;}));
  QVERIFY (mutated);
}

void TestRigFrequencyChangePolicy::txPathFrequencyRequiresSplit ()
{
  QVERIFY (RigFrequencyChangePolicy::tx_path_frequency_supported (false, false));
  QVERIFY (RigFrequencyChangePolicy::tx_path_frequency_supported (false, true));
  QVERIFY (!RigFrequencyChangePolicy::tx_path_frequency_supported (true, false));
  QVERIFY (RigFrequencyChangePolicy::tx_path_frequency_supported (true, true));
}

void TestRigFrequencyChangePolicy::acceptedRequestControlsDependentCommit ()
{
  bool committed = false;
  QVERIFY (!RigFrequencyChangePolicy::commitIfAccepted (
    [] {return false;}, [&] {committed = true;}));
  QVERIFY (!committed);

  QVERIFY (RigFrequencyChangePolicy::commitIfAccepted (
    [] {return true;}, [&] {committed = true;}));
  QVERIFY (committed);
}

void TestRigFrequencyChangePolicy::rejectedRequestStopsMonitoringItStarted ()
{
  bool monitoring = false;
  QVector<bool> transitions;
  auto const setMonitoring = [&] (bool state) {
    monitoring = state;
    transitions.append (state);
  };
  bool request_observed_monitoring = false;

  QVERIFY (!RigFrequencyChangePolicy::requestWhileMonitoring (
    monitoring, setMonitoring, [&] {
      request_observed_monitoring = monitoring;
      return false;
    }));
  QVERIFY (request_observed_monitoring);
  QCOMPARE (transitions, QVector<bool> ({true, false}));
  QVERIFY (!monitoring);

  transitions.clear ();
  request_observed_monitoring = false;
  QVERIFY (RigFrequencyChangePolicy::requestWhileMonitoring (
    monitoring, setMonitoring, [&] {
      request_observed_monitoring = monitoring;
      return true;
    }));
  QVERIFY (request_observed_monitoring);
  QCOMPARE (transitions, QVector<bool> ({true}));
  QVERIFY (monitoring);

  transitions.clear ();
  request_observed_monitoring = false;
  QVERIFY (!RigFrequencyChangePolicy::requestWhileMonitoring (
    monitoring, setMonitoring, [&] {
      request_observed_monitoring = monitoring;
      return false;
    }));
  QVERIFY (request_observed_monitoring);
  QVERIFY (transitions.isEmpty ());
  QVERIFY (monitoring);
}

QTEST_GUILESS_MAIN (TestRigFrequencyChangePolicy)

#include "test_rig_frequency_change_policy.moc"
