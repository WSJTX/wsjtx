#include <QtTest>

#include "Network/DecodedTime.hpp"

namespace
{
  QDateTime nowUtc()
  {
    return {QDate {2026, 8, 17}, QTime {12, 34, 56}, Qt::UTC};
  }
}

class TestDecodedTime final : public QObject
{
  Q_OBJECT

private slots:
  void acceptsFourAndSixDigitTimes()
  {
    auto const fourDigitExpected = QDateTime {QDate {2026, 8, 17}, QTime {12, 34}, Qt::UTC};
    QCOMPARE(DecodedTime::spotTime("1234", nowUtc(), 60), fourDigitExpected);
    auto const sixDigitExpected = nowUtc();
    QCOMPARE(DecodedTime::spotTime("123456", nowUtc(), 60), sixDigitExpected);
  }

  void preservesMidnightConvention()
  {
    auto const firstExpected = QDateTime {QDate {2026, 8, 16}, QTime {23, 59}, Qt::UTC};
    auto const secondExpected = QDateTime {QDate {2026, 8, 16}, QTime {23, 59, 45}, Qt::UTC};
    auto const thirdExpected = QDateTime {QDate {2026, 8, 17}, QTime {23, 59, 30}, Qt::UTC};
    QCOMPARE(DecodedTime::spotTime("2359", nowUtc(), 60), firstExpected);
    QCOMPARE(DecodedTime::spotTime("235945", nowUtc(), 15), secondExpected);
    QCOMPARE(DecodedTime::spotTime("235930", nowUtc(), 15), thirdExpected);
  }

  void datesMidnightPeriodFromCurrentUtcClock_data()
  {
    QTest::addColumn<QString>("encodedTime");
    QTest::addColumn<QTime>("clock");
    QTest::addColumn<double>("period");
    QTest::addColumn<int>("dayOffset");
    QTest::newRow("before-midnight") << QString {"2359"} << QTime {23, 59, 55} << 60.0 << 0;
    QTest::newRow("equal-clock") << QString {"2359"} << QTime {23, 59} << 60.0 << 0;
    QTest::newRow("after-midnight") << QString {"2359"} << QTime {0, 0, 5} << 60.0 << -1;
    QTest::newRow("six-digit-before") << QString {"235945"} << QTime {23, 59, 59} << 15.0 << 0;
    QTest::newRow("six-digit-after") << QString {"235945"} << QTime {0, 0, 1} << 15.0 << -1;
    QTest::newRow("fractional-before") << QString {"235952"} << QTime {23, 59, 59} << 7.5 << 0;
  }

  void datesMidnightPeriodFromCurrentUtcClock()
  {
    QFETCH(QString, encodedTime);
    QFETCH(QTime, clock);
    QFETCH(double, period);
    QFETCH(int, dayOffset);
    QDate const date {2026, 8, 17};
    QDateTime const now {date, clock, Qt::UTC};
    auto const result = DecodedTime::spotTime(encodedTime, now, period);
    QCOMPARE(result.date(), date.addDays(dayOffset));
    QCOMPARE(DecodedTime::spotTime(encodedTime, now.toOffsetFromUtc(7200), period), result);
  }

  void roundsFractionalPeriodForMidnight()
  {
    auto const expected = QDateTime {QDate {2026, 8, 16}, QTime {23, 59, 52}, Qt::UTC};
    QCOMPARE(DecodedTime::spotTime("235952", nowUtc(), 7.5), expected);
  }

  void rejectsMalformedTimes()
  {
    for (auto const& encodedTime : {QString {}, QString {"123"}, QString {"1234567"},
                                    QString {"2460"}, QString {"1260"},
                                    QString {"126099"}, QString {"12x4"}}) {
      QVERIFY(!DecodedTime::spotTime(encodedTime, nowUtc(), 60).isValid());
    }
  }
};

QTEST_APPLESS_MAIN(TestDecodedTime)

#include "test_decoded_time.moc"
