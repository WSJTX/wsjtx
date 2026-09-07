#include <QtTest>
#include "OperatingFrequency.hpp"

class TestOperatingFrequency : public QObject
{
  Q_OBJECT
private slots:
  void acceptedRequests ()
  {
    OperatingFrequency frequency {10368200000ULL};
    Radio::Frequency command = 0;
    auto accept = [&] (Radio::Frequency value) { command = value; return true; };
    QVERIFY (frequency.requestNominal (144200000, 125, accept));
    QCOMPARE (command, 144200125ULL);
    QCOMPARE (frequency.rx (), 144200000ULL);
    QCOMPARE (frequency.tx (), 144200000ULL);
    QVERIFY (!frequency.requestNominal (432200000, 0, [] (auto) { return false; }));
    QCOMPARE (frequency.rx (), 144200000ULL);
    QCOMPARE (frequency.remembered (), 10368200000ULL);
    QVERIFY (frequency.enableMonitor (false, true, false, -50, accept));
    QCOMPARE (command, 10368199950ULL);
    QCOMPARE (frequency.correctedRx (75), 10368200075ULL);
    QVERIFY (!frequency.enableMonitor (true, true, false, 0, accept));
    QVERIFY (!frequency.enableMonitor (false, true, true, 0, accept));
    QVERIFY (!frequency.enableMonitor (false, false, false, 0, accept));
  }

  void receiveAndTransmitReports ()
  {
    OperatingFrequency frequency {144200000};
    frequency.observe ({true, false, false, 432200125, 0}, true, 125, 50, 0);
    QCOMPARE (frequency.rx (), 432200000ULL);
    QCOMPARE (frequency.tx (), 432200000ULL);
    QCOMPARE (frequency.remembered (), 432200000ULL);
    frequency.observe ({true, true, true, 432200125, 432201050}, true, 125, 50, frequency.rx ());
    QCOMPARE (frequency.tx (), 432201000ULL);
    QCOMPARE (frequency.rx (), 432200000ULL);
    frequency.observe ({true, true, false, 432202000, 0}, true, 125, 50, frequency.rx ());
    QCOMPARE (frequency.tx (), 432202000ULL);
    frequency.commitAcceptedTx (432203000);
    QCOMPARE (frequency.correctedTx (-25), 432202975ULL);
  }
};

QTEST_GUILESS_MAIN (TestOperatingFrequency)
#include "test_operating_frequency.moc"
