#include <QtTest>

#include "ActiveStationList.hpp"

class TestActiveStationList : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void limitsAscendingRows();
  void limitsDescendingRows();
  void preservesEqualKeyOrder();
  void handlesEmptyAndZeroLimit();
};

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

QTEST_MAIN(TestActiveStationList)

#include "test_active_station_list.moc"
