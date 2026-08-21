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
