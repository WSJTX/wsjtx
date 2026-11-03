#include <QtTest>

#include "ActiveStationList.hpp"

namespace
{
  // A UTC QDateTime at the given time-of-day on an arbitrary fixed date.
  QDateTime utc(int h, int m, int s = 0, int dayOffset = 0)
  {
    return QDateTime {QDate {2026, 1, 1}.addDays(dayOffset), QTime {h, m, s}, Qt::UTC};
  }
}

class TestActiveStationList : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void limitsAscendingRows();
  void limitsDescendingRows();
  void preservesEqualKeyOrder();
  void handlesEmptyAndZeroLimit();
  void formatsQ65Row();
  void filtersQ65Stations_data();
  void filtersQ65Stations();
  void agesQ65StationsAcrossMidnight();
};

namespace
{
  QStringList q65Calls(QVector<ActiveStationListItem> const& rows)
  {
    QStringList calls;
    for(auto const& row : rows) calls << row.text.mid(25, 8).trimmed();
    return calls;
  }
}

void TestActiveStationList::limitsAscendingRows()
{
  QVector<ActiveStationListItem> rows;
  for(int i=59; i>=0; --i) rows.append({float(i), QString::number(i)});

  auto const limited=sorted_limited_active_station_items(rows, 50, false);

  QCOMPARE(limited.size(), 50);
  QCOMPARE(limited.front().text, QString {"0"});
  QCOMPARE(limited.back().text, QString {"49"});
}

void TestActiveStationList::limitsDescendingRows()
{
  QVector<ActiveStationListItem> rows;
  for(int i=0; i<60; ++i) rows.append({float(i), QString::number(i)});

  auto const limited=sorted_limited_active_station_items(rows, 50, true);

  QCOMPARE(limited.size(), 50);
  QCOMPARE(limited.front().text, QString {"59"});
  QCOMPARE(limited.back().text, QString {"10"});
}

void TestActiveStationList::preservesEqualKeyOrder()
{
  QVector<ActiveStationListItem> rows {
    {2.0f, "first"},
    {1.0f, "low"},
    {2.0f, "second"},
    {2.0f, "third"}
  };

  auto const limited=sorted_limited_active_station_items(rows, 4, true);

  QCOMPARE(limited[0].text, QString {"first"});
  QCOMPARE(limited[1].text, QString {"second"});
  QCOMPARE(limited[2].text, QString {"third"});
  QCOMPARE(limited[3].text, QString {"low"});
}

void TestActiveStationList::handlesEmptyAndZeroLimit()
{
  QVERIFY(sorted_limited_active_station_items({}, 50, false).isEmpty());

  QVector<ActiveStationListItem> rows {{1.0f, "row"}};
  QVERIFY(sorted_limited_active_station_items(rows, 0, false).isEmpty());
}

void TestActiveStationList::formatsQ65Row()
{
  Q65ActiveStation const station {"K1ABC", "FN42", 140.5, 139.9, -21,
                                  utc(12, 4), "60A", true, false};

  auto const rows=q65_active_station_items({station}, {utc(12, 6), 10, false, false});

  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows[0].sort_key, 139.9f);
  QCOMPARE(rows[0].text,
           QString {"140.500 139.9  -21  60A  K1ABC    FN42   1   2 #*\n"});
}

void TestActiveStationList::filtersQ65Stations_data()
{
  QTest::addColumn<bool>("wantedOnly");
  QTest::addColumn<bool>("readyOnly");
  QTest::addColumn<QStringList>("calls");

  QTest::newRow("all") << false << false << QStringList {"W1AAA", "W2BBB", "W3CCC"};
  QTest::newRow("wanted") << true << false << QStringList {"W2BBB", "W3CCC"};
  QTest::newRow("ready") << false << true << QStringList {"W1AAA", "W2BBB"};
  QTest::newRow("both") << true << true << QStringList {"W2BBB"};
}

void TestActiveStationList::filtersQ65Stations()
{
  QFETCH(bool, wantedOnly);
  QFETCH(bool, readyOnly);
  QFETCH(QStringList, calls);
  QDateTime const now=utc(18, 30);
  QVector<Q65ActiveStation> const stations {
    {"W1AAA", "FN31", 100.0, 100.0, -20, now.addSecs(-60), "60A", true, true},
    {"W2BBB", "FN20", 110.0, 110.0, -22, now, "60A", true, false},
    {"W3CCC", "FM19", 120.0, 120.0, -24, now.addSecs(-5*60), "60A", false, false},
    {"W4DDD", "EM73", 130.0, 130.0, -19, now.addSecs(-11*60), "60A", true, false}
  };

  auto const rows=q65_active_station_items(stations, {now, 10, wantedOnly, readyOnly});

  QCOMPARE(q65Calls(rows), calls);
}

void TestActiveStationList::agesQ65StationsAcrossMidnight()
{
  Q65ActiveStation const station {"K1ABC", "FN42", 140.5, 139.9, -21,
                                  utc(23, 59), "60A", false, false};

  // "now" is 2 minutes later, just past UTC midnight the next day.
  auto const rows=q65_active_station_items({station}, {utc(0, 1, 0, 1), 10, false, false});

  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows[0].text.mid(42, 4).toInt(), 2);
}

QTEST_MAIN(TestActiveStationList)

#include "test_active_station_list.moc"
