#include <QtTest>
#include "OperatingFrequency.hpp"

namespace
{
  using Owner = OperatingFrequency;
  constexpr Radio::Frequency rf = 10368200000ULL;
  constexpr Radio::Frequency intermediate = 144200000;
  Owner::Observation offline {false, false, false, 0, 0};
  Owner::Observation initial {true, false, false, intermediate, 0};
  Owner::Context automatic {false, true, true, true, false, 0, 0};
  struct Recorder
  {
    QVector<Radio::Frequency> commands;
    bool accepted = true;
    bool operator() (Radio::Frequency value)
    {
      commands.append (value);
      return accepted;
    }
  };
}

class TestOperatingFrequency : public QObject
{
  Q_OBJECT
private slots:
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
    Recorder requests;
    auto context = automatic;
    context.rxCorrection = initialCorrection;
    context.txCorrection = -50;
    auto const transition = frequency.reconcile (offline, initial, context, std::ref (requests));
    QVERIFY (transition.restorationAccepted);
    QVERIFY (transition.monitor && *transition.monitor);
    QCOMPARE (transition.before.rx, 0ULL);
    QCOMPARE (transition.after.rx, rf);
    QCOMPARE (transition.after.tx, rf);
    QCOMPARE (transition.after.remembered, rf);
    QCOMPARE (requests.commands, QVector<Radio::Frequency> {rf + initialCorrection});
    context.monitoring = true;
    Owner::Observation response {true, false, false, rf + initialCorrection, 0};
    if (responseFirst)
      frequency.reconcile (initial, response, context, std::ref (requests));
    context.rxCorrection = 175;
    requests (frequency.correctedRx (context.rxCorrection));
    QCOMPARE (requests.commands.last (), rf + 175);
    // Configuration forwards RF plus correction and filters superseded responses.
    Owner::Observation corrected {true, false, false, rf + 175, 0};
    frequency.reconcile (responseFirst ? response : initial, corrected, context, std::ref (requests));
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.tx (), rf);
    QCOMPARE (frequency.remembered (), rf);
    auto const count = requests.commands.size ();
    frequency.reconcile (corrected, corrected, context, std::ref (requests));
    QCOMPARE (requests.commands.size (), count);
    corrected.rx += 1000;
    frequency.reconcile (response, corrected, context, std::ref (requests));
    QCOMPARE (frequency.rx (), rf + 1000);
    QCOMPARE (frequency.remembered (), rf + 1000);
  }

  void manualMonitorPreservesSavedTarget ()
  {
    Owner frequency {rf};
    Recorder requests;
    auto context = automatic;
    context.monitorAtStartup = false;
    auto transition = frequency.reconcile (offline, initial, context, std::ref (requests));
    QVERIFY (transition.monitor && !*transition.monitor);
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.tx (), intermediate);
    QCOMPARE (frequency.remembered (), rf);
    auto tuned = initial;
    tuned.rx += 2000;
    frequency.reconcile (initial, tuned, context, std::ref (requests));
    QCOMPARE (frequency.rx (), tuned.rx);
    QCOMPARE (frequency.remembered (), rf);
    transition = frequency.enableMonitor (context, std::ref (requests));
    QVERIFY (transition.restorationAccepted);
    QCOMPARE (requests.commands, QVector<Radio::Frequency> {rf});
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
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
    Recorder requests;
    auto context = automatic;
    context.restore = restore;
    context.echo = echo;
    context.monitorAllowed = allowed;
    auto transition = frequency.reconcile (offline, initial, context, std::ref (requests));
    QVERIFY (!transition.restorationAccepted);
    QVERIFY (requests.commands.isEmpty ());
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.remembered (), !restore && allowed && !echo ? intermediate : rf);
    frequency.enableMonitor (context, std::ref (requests));
    QVERIFY (requests.commands.isEmpty ());
  }

  void rejectionAndReconnect ()
  {
    Owner frequency {rf};
    Recorder requests;
    requests.accepted = false;
    auto context = automatic;
    auto transition = frequency.reconcile (offline, initial, context, std::ref (requests));
    QVERIFY (!transition.restorationAccepted);
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.tx (), intermediate);
    QCOMPARE (frequency.remembered (), rf);
    context.monitoring = true;
    frequency.reconcile (initial, initial, context, std::ref (requests));
    frequency.reconcile (initial, offline, context, std::ref (requests));
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.remembered (), rf);
    Owner::Observation zero {true, false, false, 0, 0};
    frequency.reconcile (initial, zero, context, std::ref (requests));
    QCOMPARE (frequency.remembered (), rf);
    // A reconnect while already monitoring is not a new Monitor activation.
    frequency.reconcile (offline, initial, context, std::ref (requests));
    QCOMPARE (requests.commands.size (), 1);
    QCOMPARE (frequency.remembered (), rf);
    context.monitoring = false;
    requests.accepted = true;
    transition = frequency.reconcile (offline, initial, context, std::ref (requests));
    QVERIFY (transition.restorationAccepted);
    QCOMPARE (requests.commands.size (), 2);
    QCOMPARE (frequency.rx (), rf);
    QCOMPARE (frequency.remembered (), rf);
  }

  void acceptedRequestsAndTxReports ()
  {
    Owner frequency {rf};
    Recorder requests;
    auto const* stableRx = &frequency.rx ();
    QVERIFY (frequency.requestNominal (intermediate, 125, std::ref (requests)));
    QCOMPARE (requests.commands.last (), intermediate + 125);
    QCOMPARE (*stableRx, intermediate);
    QCOMPARE (frequency.tx (), intermediate);
    requests.accepted = false;
    QVERIFY (!frequency.requestNominal (432200000, 0, std::ref (requests)));
    QCOMPARE (frequency.rx (), intermediate);
    QCOMPARE (frequency.tx (), intermediate);
    QCOMPARE (frequency.remembered (), rf);
    QVERIFY (!frequency.requestNominal (0, 0, std::ref (requests)));
    auto context = automatic;
    context.monitoring = true;
    context.rxCorrection = 125;
    context.txCorrection = 50;
    Owner::Observation rx {true, false, false, 432200125, 0};
    frequency.reconcile (initial, rx, context, std::ref (requests));
    QCOMPARE (frequency.rx (), 432200000ULL);
    QCOMPARE (frequency.remembered (), 432200000ULL);
    Owner::Observation split {true, true, true, 432200125, 432201050};
    frequency.reconcile (rx, split, context, std::ref (requests));
    QCOMPARE (frequency.tx (), 432201000ULL);
    QCOMPARE (frequency.rx (), 432200000ULL);
    auto changedTx = split;
    changedTx.tx += 500;
    frequency.reconcile (split, changedTx, context, std::ref (requests));
    QCOMPARE (frequency.tx (), 432201500ULL);
    Owner::Observation simplex {true, true, false, 432202000, 0};
    frequency.reconcile (split, simplex, context, std::ref (requests));
    QCOMPARE (frequency.tx (), 432202000ULL);
    auto adjustTx = [&] (Radio::Frequency nominal) {
      if (!requests (nominal + context.txCorrection)) return false;
      frequency.commitAcceptedTx (nominal);
      return true;
    };
    QVERIFY (!adjustTx (432203000));
    QCOMPARE (frequency.tx (), 432202000ULL);
    requests.accepted = true;
    QVERIFY (adjustTx (432203000));
    QCOMPARE (frequency.correctedTx (-25), 432202975ULL);
    QCOMPARE (frequency.rx (), 432200000ULL);
  }
};

QTEST_GUILESS_MAIN (TestOperatingFrequency)
#include "test_operating_frequency.moc"
