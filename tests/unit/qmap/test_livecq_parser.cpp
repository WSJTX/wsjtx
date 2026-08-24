#include <QtTest>

#include "qmap/livecq_parser.h"

class TestQMapLiveCQParser final : public QObject
{
  Q_OBJECT

private slots:
  void parsesSupportedLayouts()
  {
    QMapLiveCQ::Record record;
    QVERIFY(QMapLiveCQ::parse(
      {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "N5L", "1200.0"},
      100, record));
    QCOMPARE(record.callsign, QString {"N5L"});
    QCOMPARE(record.grid, QString {"--"});
    QCOMPARE(record.message, QString {"CQ N5L"});
    QCOMPARE(record.receiveFrequency, 100);
    QCOMPARE(record.scheduledFrequency, 1200);

    QVERIFY(QMapLiveCQ::parse(
      {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "N5L", "FN20", "1200.0"},
      100, record));
    QCOMPARE(record.grid, QString {"FN20"});
    QCOMPARE(record.message, QString {"CQ N5L FN20"});

    QVERIFY(QMapLiveCQ::parse(
      {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "DX", "N5L", "1200.0"},
      100, record));
    QCOMPARE(record.callsign, QString {"N5L"});
    QCOMPARE(record.grid, QString {"--"});
    QCOMPARE(record.message, QString {"CQ N5L"});

    QVERIFY(QMapLiveCQ::parse(
      {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "DX", "N5L", "FN20", "1200.0"},
      100, record));
    QCOMPARE(record.grid, QString {"FN20"});
    QCOMPARE(record.message, QString {"CQ N5L FN20"});
  }

  void parsesHighFrequencyFraction()
  {
    QMapLiveCQ::Record record;
    QVERIFY(QMapLiveCQ::parse(
      {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "N5L", "1200.750"},
      100, record));
    QCOMPARE(record.receiveFrequency, -150);
    QCOMPARE(record.scheduledFrequency, 1201);
  }

  void rejectsMalformedRecordsBeforeIndexing()
  {
    QMapLiveCQ::Record record;
    for (auto const& tokens : {
           QStringList {},
           QStringList {"123456", "-10", "0.1", "1234", "-12", "60", "CQ"},
           QStringList {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "N5L"},
           QStringList {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "N5L", "FN20", "1200.0", "extra", "too-much"}}) {
      QVERIFY(!QMapLiveCQ::parse(tokens, 100, record));
    }
  }

  void rejectsMalformedFrequencyAndCalls()
  {
    QMapLiveCQ::Record record;
    for (auto const& frequency : {QString {"1200"}, QString {"1200."},
                                  QString {"1200.x"}, QString {"1200.0.1"}}) {
      QVERIFY(!QMapLiveCQ::parse(
        {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "N5L", frequency},
        100, record));
    }
    QVERIFY(!QMapLiveCQ::parse(
      {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "N5", "1200.0"},
      100, record));
    QVERIFY(!QMapLiveCQ::parse(
      {"123456", "-10", "0.1", "1234", "-12", "60", "CQ", "DX", "N5", "1200.0"},
      100, record));
  }
};

QTEST_APPLESS_MAIN(TestQMapLiveCQParser)

#include "test_livecq_parser.moc"
