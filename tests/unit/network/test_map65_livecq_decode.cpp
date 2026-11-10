#include <QtTest>

#include <QDateTime>
#include <QUrlQuery>

#include "map65/livecq_decode.hpp"

namespace
{
  QDateTime nowUtc()
  {
    return {QDate {2026, 8, 17}, QTime {12, 34, 56}, Qt::UTC};
  }

  QString decodeLine(QStringList const& message, QString const& time = "1234",
                     QString const& polarization = "0.1")
  {
    return (QStringList {"50313.0", "1000", polarization, time, "-12"} + message)
      .join(' ');
  }
}

class TestMap65LiveCQDecode final : public QObject
{
  Q_OBJECT

private slots:
  void parsesSupportedLayouts()
  {
    QStringList seen;
    auto const spots = Map65LiveCQ::parseSpots(
      {
        decodeLine({"CQ", "K1ABC", "1200.0", "#A"}),
        decodeLine({"CQ", "K1ABC", "FN20", "1200.0", ":B"}),
        decodeLine({"CQ", "DX", "K1ABC", "1200.0", "#C"}),
        decodeLine({"CQ", "DX", "K1ABC", "FN20", "1200.0", ":D"})
      }, "N0CALL", "FN21", false, nowUtc(), seen);

    QCOMPARE(spots.size(), 4);
    QCOMPARE(spots.at(0).callsign, QString {"K1ABC"});
    QCOMPARE(spots.at(0).grid, QString {"--"});
    QCOMPARE(spots.at(0).mode, QString {"JT65A"});
    QCOMPARE(spots.at(0).deltaTime, QString {"1200.0"});
    QCOMPARE(spots.at(1).callsign, QString {"K1ABC"});
    QCOMPARE(spots.at(1).grid, QString {"FN20"});
    QCOMPARE(spots.at(1).mode, QString {"Q65-60B"});
    QCOMPARE(spots.at(2).callsign, QString {"K1ABC"});
    QCOMPARE(spots.at(2).grid, QString {"--"});
    QCOMPARE(spots.at(2).mode, QString {"JT65C"});
    QCOMPARE(spots.at(3).callsign, QString {"K1ABC"});
    QCOMPARE(spots.at(3).grid, QString {"FN20"});
    QCOMPARE(spots.at(3).mode, QString {"Q65-60D"});
  }

  void rejectsShortOrMalformedLines()
  {
    const auto malformed = QStringList {
      QString {},
      "50313.0 1000 0.1",
      "50313.0 1000 0.1 1234 -12",
      "50313.0 1000 0.1 1234 -12 CQ",
      "50313.0 1000 0.1 1234 -12 CQ K1ABC",
      "50313.0 1000 0.1 1234 -12 CQ K1ABC 1200.0",
      decodeLine({"CQ", "K1ABC", "not-a-time", "#A"}),
      decodeLine({"CQ", "K1ABC", "1200.0", "?A"})
    };

    for (auto const& line : malformed) {
      QStringList seen;
      QVERIFY2(Map65LiveCQ::parseSpots(
        {line}, "N0CALL", "FN21", false, nowUtc(), seen).isEmpty(),
        qPrintable(line));
    }
  }

  void rejectsNonReportableMessageTypes()
  {
    for (auto const& type : {QString {"DE"}, QString {"K1ABC"}, QString {"TU"}}) {
      QStringList seen;
      QVERIFY(Map65LiveCQ::parseSpots(
        {decodeLine({type, "K1ABC", "1200.0", "#A"})},
        "N0CALL", "FN21", false, nowUtc(), seen).isEmpty());
    }
  }

  void rejectsInvalidCallsigns()
  {
    for (auto const& callsign : {QString {"KABC"}, QString {"N5"}, QString {"Q1ABC"}}) {
      QStringList seen;
      QVERIFY2(Map65LiveCQ::parseSpots(
        {decodeLine({"CQ", callsign, "1200.0", "#A"})},
        "N0CALL", "FN21", false, nowUtc(), seen).isEmpty(),
        qPrintable(callsign));
    }
  }

  void requiresReceiverCallAndGrid()
  {
    auto const line = decodeLine({"CQ", "K1ABC", "1200.0", "#A"});
    QStringList seen;
    QVERIFY(Map65LiveCQ::parseSpots(
      {line}, "NO", "FN21", false, nowUtc(), seen).isEmpty());
    QVERIFY(Map65LiveCQ::parseSpots(
      {line}, "N0CALL", "FN", false, nowUtc(), seen).isEmpty());
    QVERIFY(seen.isEmpty());
  }

  void defaultsTransmitPolarizationWhenXpolDisabled()
  {
    QStringList seen;
    auto const spots = Map65LiveCQ::parseSpots(
      {decodeLine({"CQ", "K1ABC", "FN20", "1200.0", ":B", "V"})},
      "N0CALL", "FN21", false, nowUtc(), seen);
    QCOMPARE(spots.size(), 1);
    QCOMPARE(spots.first().polarization, QString {"--"});
    QCOMPARE(spots.first().transmitPolarization, QString {"--"});
  }

  void mapsPolarizationWhenXpolEnabled()
  {
    QStringList seen;
    auto const spots = Map65LiveCQ::parseSpots(
      {decodeLine({"CQ", "K1ABC", "FN20", "1200.0", ":B", "V"}, "1234", "0.7")},
      "N0CALL", "FN21", true, nowUtc(), seen);
    QCOMPARE(spots.size(), 1);
    QCOMPARE(spots.first().polarization, QString {"0.7"});
    QCOMPARE(spots.first().transmitPolarization, QString {"V"});
  }

  void mapsPolarizationWhenXpolEnabledForNoGridLayout()
  {
    QStringList seen;
    auto const spots = Map65LiveCQ::parseSpots(
      {decodeLine({"CQ", "K1ABC", "1200.0", "#A"}, "1234", "0.7")},
      "N0CALL", "FN21", true, nowUtc(), seen);
    QCOMPARE(spots.size(), 1);
    QCOMPARE(spots.first().polarization, QString {"0.7"});
    QCOMPARE(spots.first().transmitPolarization, QString {"--"});
  }

  void timestampIsUtc()
  {
    QStringList seen;
    auto const spots = Map65LiveCQ::parseSpots(
      {decodeLine({"CQ", "K1ABC", "1200.0", "#A"})},
      "N0CALL", "FN21", false, nowUtc(), seen);
    QCOMPARE(spots.size(), 1);
    auto const expected = QDateTime {QDate {2026, 8, 17}, QTime {12, 34, 0}, Qt::UTC};
    QCOMPARE(spots.first().utcDateTime, expected);
  }

  void datesLatePeriodBeforeAndAfterMidnight()
  {
    QDate const date {2026, 8, 17};
    for (auto const clock : {QTime {23, 59, 55}, QTime {0, 0, 5}}) {
      QStringList seen;
      auto const spots = Map65LiveCQ::parseSpots(
        {decodeLine({"CQ", "K1ABC", "1200.0", "#A"}, "2359")},
        "N0CALL", "FN21", false, QDateTime {date, clock, Qt::UTC}, seen);
      QCOMPARE(spots.size(), 1);
      auto const expectedDate = clock.hour() == 23 ? date : date.addDays(-1);
      QCOMPARE(spots.first().utcDateTime, QDateTime(expectedDate, QTime(23, 59), Qt::UTC));
    }
  }

  void buildsExpectedQueryFields()
  {
    QStringList seen;
    auto const spots = Map65LiveCQ::parseSpots(
      {decodeLine({"CQ", "K1ABC", "FN20", "1200.0", "#A"})},
      "N0CALL", "FN21", false, nowUtc(), seen);
    QCOMPARE(spots.size(), 1);

    auto const query = Map65LiveCQ::spotQuery(spots.first(), "N0CALL", "FN21");
    auto const item = [&query](QString const& key) {
      return query.queryItemValue(key, QUrl::FullyDecoded);
    };
    QCOMPARE(item("skedfreq"), QString {"50313.0"});
    QCOMPARE(item("rxfreq"), QString {"1000"});
    QCOMPARE(item("rpol"), QString {"--"});
    QCOMPARE(item("dt"), QString {"1200.0"});
    QCOMPARE(item("dB"), QString {"-12"});
    QCOMPARE(item("msgtype"), QString {"CQ"});
    QCOMPARE(item("callsign"), QString {"K1ABC"});
    QCOMPARE(item("grid"), QString {"FN20"});
    QCOMPARE(item("mode"), QString {"JT65A"});
    QCOMPARE(item("utcdatetime"), QString {"2026-08-17T12:34:00Z"});
    QCOMPARE(item("spotter"), QString {"N0CALL"});
    QCOMPARE(item("spottergrid"), QString {"FN21"});
    QCOMPARE(item("txpol"), QString {"--"});
    QCOMPARE(item("apptype"), QString {"MAP65"});
  }

  void deduplicatesSeenDecodes()
  {
    auto const first = decodeLine({"CQ", "K1ABC", "1200.0", "#A"}) + " trailing-one";
    auto const second = decodeLine({"CQ", "K1ABC", "1200.0", "#A"}) + " trailing-two";
    QCOMPARE(first.left(53), second.left(53));

    QStringList seen;
    auto const spots = Map65LiveCQ::parseSpots(
      {first, second}, "N0CALL", "FN21", false, nowUtc(), seen);
    QCOMPARE(spots.size(), 1);
    QCOMPARE(seen.size(), 1);
  }
};

QTEST_APPLESS_MAIN(TestMap65LiveCQDecode)

#include "test_map65_livecq_decode.moc"
