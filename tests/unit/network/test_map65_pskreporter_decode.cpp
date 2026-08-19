#include <QtTest>

#include <QDateTime>

#include "map65/pskreporter_decode.h"

namespace
{
  QDateTime nowUtc()
  {
    return {QDate {2026, 8, 17}, QTime {12, 34, 56}, Qt::UTC};
  }

  QString decodeLine(QStringList const& message, QString const& time = "1234")
  {
    return (QStringList {"50313.0", "1000", "0.1", time, "-12"} + message)
      .join(' ');
  }

}

class TestMap65PSKReporterDecode final : public QObject
{
  Q_OBJECT

private slots:
  void acceptsMap65Callsigns()
  {
    const auto valid = QStringList {
      "K1ABC", "YW18FIFA", "QU1RK", "<K1ABC>", "K1ABC/P"
    };
    for (auto const& callsign : valid) {
      QVERIFY2(Map65PSKReporter::isValidMap65Callsign(callsign),
               qPrintable(callsign));
    }

    const auto invalid = QStringList {
      "KABC", "Q1ABC", "K1AB1", "K1ABCDE", "K1AB-C", "K1AB?"
    };
    for (auto const& callsign : invalid) {
      QVERIFY2(!Map65PSKReporter::isValidMap65Callsign(callsign),
               qPrintable(callsign));
    }
  }

  void parsesFourDecodeLayouts()
  {
    QStringList seen;
    auto const spots = Map65PSKReporter::parseMap65PSKReporterSpots(
      {
        decodeLine({"CQ", "K1ABC", "1200.0", "#A"}),
        decodeLine({"CQ", "K1ABC", "FN20", "1200.0", ":B"}),
        decodeLine({"CQ", "DX", "K1ABC", "1200.0", "#C"}),
        decodeLine({"CQ", "DX", "K1ABC", "FN20", "1200.0", ":D"})
      }, "N0CALL", "FN21", nowUtc(), seen);

    QCOMPARE(spots.size(), 4);
    QCOMPARE(spots.at(0).callsign, QString {"K1ABC"});
    QCOMPARE(spots.at(0).locator, QString {"--"});
    QCOMPARE(spots.at(0).mode, QString {"JT65A"});
    QCOMPARE(spots.at(1).locator, QString {"FN20"});
    QCOMPARE(spots.at(1).mode, QString {"Q65-60B"});
    QCOMPARE(spots.at(2).locator, QString {"--"});
    QCOMPARE(spots.at(2).mode, QString {"JT65C"});
    QCOMPARE(spots.at(3).locator, QString {"FN20"});
    QCOMPARE(spots.at(3).mode, QString {"Q65-60D"});
  }

  void acceptsAllReportableMessageTypes()
  {
    QStringList seen;
    QStringList lines;
    for (auto const& type : {QString {"CQ"}, QString {"QRZ"}, QString {"CQV"},
                             QString {"CQH"}, QString {"QRT"}}) {
      lines.append(decodeLine({type, "K1ABC", "1200.0", "#A"}));
    }
    lines.append(decodeLine({"DE", "K1ABC", "1200.0", "#A"}));

    auto const spots = Map65PSKReporter::parseMap65PSKReporterSpots(
      lines, "N0CALL", "FN21", nowUtc(), seen);
    QCOMPARE(spots.size(), 5);
  }

  void preservesSpotFieldsAndLegacySnr()
  {
    QStringList seen;
    auto const spots = Map65PSKReporter::parseMap65PSKReporterSpots(
      {decodeLine({"CQ", "k1abc", "1200.0", "#A"})},
      "N0CALL", "FN21", nowUtc(), seen);
    QCOMPARE(spots.size(), 1);
    auto const& spot = spots.first();
    QCOMPARE(spot.callsign, QString {"K1ABC"});
    QCOMPARE(spot.frequency, Radio::Frequency {50313000000LL});
    QCOMPARE(spot.snr, -10);
    auto const expectedTime = QDateTime {
      QDate {2026, 8, 17}, QTime {12, 34, 0}, Qt::UTC};
    QCOMPARE(spot.time, expectedTime);
  }

  void rollsLateDecodeToPreviousUtcDate()
  {
    QStringList seen;
    auto const spots = Map65PSKReporter::parseMap65PSKReporterSpots(
      {decodeLine({"CQ", "K1ABC", "235940.0", "#A"}, "235940")},
      "N0CALL", "FN21", nowUtc(), seen);
    QCOMPARE(spots.size(), 1);
    auto const expectedTime = QDateTime {
      QDate {2026, 8, 16}, QTime {23, 59, 40}, Qt::UTC};
    QCOMPARE(spots.first().time, expectedTime);
  }

  void suppressesDuplicateDecodeKeys()
  {
    auto const first = decodeLine({"CQ", "K1ABC", "1200.0", "#A"})
      + " trailing-one";
    auto const second = decodeLine({"CQ", "K1ABC", "1200.0", "#A"})
      + " trailing-two";
    QCOMPARE(first.left(53), second.left(53));

    QStringList seen;
    auto const spots = Map65PSKReporter::parseMap65PSKReporterSpots(
      {first, second}, "N0CALL", "FN21", nowUtc(), seen);
    QCOMPARE(spots.size(), 1);
    QCOMPARE(seen.size(), 1);
  }

  void rejectsMalformedInput()
  {
    const auto valid = decodeLine({"CQ", "K1ABC", "1200.0", "#A"});
    const auto malformed = QStringList {
      QString {},
      "50313.0 1000 0.1 1234 -12 CQ",
      decodeLine({"DE", "K1ABC", "1200.0", "#A"}),
      decodeLine({"CQ", "KABC", "1200.0", "#A"}),
      decodeLine({"CQ", "K1ABC", "not-a-time", "#A"}),
      decodeLine({"CQ", "K1ABC", "1200.0", "?A"})
    };

    for (auto const& line : malformed) {
      QStringList seen;
      QVERIFY(Map65PSKReporter::parseMap65PSKReporterSpots(
        {line}, "N0CALL", "FN21", nowUtc(), seen).isEmpty());
    }

    QStringList seen;
    QVERIFY(Map65PSKReporter::parseMap65PSKReporterSpots(
      {valid}, "NO", "FN21", nowUtc(), seen).isEmpty());
    QVERIFY(seen.isEmpty());
    QVERIFY(Map65PSKReporter::parseMap65PSKReporterSpots(
      {valid}, "N0CALL", "FN", nowUtc(), seen).isEmpty());
  }
};

QTEST_APPLESS_MAIN(TestMap65PSKReporterDecode)

#include "test_map65_pskreporter_decode.moc"
