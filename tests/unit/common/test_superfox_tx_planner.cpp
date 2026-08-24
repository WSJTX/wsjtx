#include "SuperFoxTxPlanner.h"

#include <QTest>

class TestSuperFoxTxPlanner : public QObject
{
  Q_OBJECT

private slots:
  void freeTextCapsQueuedReportsAtFour();
  void noFreeTextAllowsNineRecords();
  void freeTextRR73UsesSharedRecordCapacity();
  void parseQueuedHoundLine();

private:
  static SuperFoxTxPlanner::QsoState qso(QString const& call,
                                         QString const& report = "-10",
                                         int ncall = 0,
                                         int tFoxRrpt = -1,
                                         int tFoxTxRR73 = -1);
  static QString queueLine(QString const& call, QString const& report,
                           QString const& grid);
};

SuperFoxTxPlanner::QsoState TestSuperFoxTxPlanner::qso(QString const& call,
                                                       QString const& report,
                                                       int ncall,
                                                       int tFoxRrpt,
                                                       int tFoxTxRR73)
{
  SuperFoxTxPlanner::QsoState result;
  result.call = call;
  result.grid = "FN20";
  result.sent = report;
  result.rcvd = "-05";
  result.ncall = ncall;
  result.tFoxRrpt = tFoxRrpt;
  result.tFoxTxRR73 = tFoxTxRR73;
  return result;
}

QString TestSuperFoxTxPlanner::queueLine(QString const& call, QString const& report,
                                         QString const& grid)
{
  return (call + "            ").left(12) + report + " " + grid;
}

void TestSuperFoxTxPlanner::freeTextCapsQueuedReportsAtFour()
{
  QVector<SuperFoxTxPlanner::QsoState> qsos;
  QQueue<QString> inProgress;
  QVector<SuperFoxTxPlanner::QueuedHound> queue;
  for (auto const& call : {"K1AAA", "K1AAB", "K1AAC", "K1AAD", "K1AAE"})
    {
      queue.push_back(SuperFoxTxPlanner::parseQueuedHound(queueLine(call, "-12", "FN31")));
    }

  auto const plan = SuperFoxTxPlanner::plan(qsos, inProgress, queue, 3, true);

  QCOMPARE(plan.records.size(), 4);
  QCOMPARE(plan.reportCount(), 4);
  QCOMPARE(plan.queuedReportCount(), 4);
  QCOMPARE(plan.records.at(0).call, QString {"K1AAA"});
  QCOMPARE(plan.records.at(3).call, QString {"K1AAD"});
}

void TestSuperFoxTxPlanner::noFreeTextAllowsNineRecords()
{
  QVector<SuperFoxTxPlanner::QsoState> qsos;
  for (auto const& call : {"K1RR1", "K1RR2", "K1RR3", "K1RR4", "K1RR5"})
    {
      qsos.push_back(qso(call, "-10", 0, 10, -1));
    }

  QQueue<QString> inProgress;
  QVector<SuperFoxTxPlanner::QueuedHound> queue;
  for (auto const& call : {"K1RP1", "K1RP2", "K1RP3", "K1RP4", "K1RP5"})
    {
      queue.push_back(SuperFoxTxPlanner::parseQueuedHound(queueLine(call, "-07", "FN32")));
    }

  auto const plan = SuperFoxTxPlanner::plan(qsos, inProgress, queue, 3, false);

  QCOMPARE(plan.records.size(), 9);
  QCOMPARE(plan.rr73Count(), 5);
  QCOMPARE(plan.reportCount(), 4);
  QCOMPARE(plan.queuedReportCount(), 4);
}

void TestSuperFoxTxPlanner::freeTextRR73UsesSharedRecordCapacity()
{
  QVector<SuperFoxTxPlanner::QsoState> qsos;
  for (auto const& call : {"K1RR1", "K1RR2", "K1RR3"})
    {
      qsos.push_back(qso(call, "-10", 0, 10, -1));
    }

  QQueue<QString> inProgress;
  QVector<SuperFoxTxPlanner::QueuedHound> queue;
  for (auto const& call : {"K1RP1", "K1RP2"})
    {
      queue.push_back(SuperFoxTxPlanner::parseQueuedHound(queueLine(call, "-07", "FN32")));
    }

  auto const plan = SuperFoxTxPlanner::plan(qsos, inProgress, queue, 3, true);

  QCOMPARE(plan.records.size(), 4);
  QCOMPARE(plan.rr73Count(), 3);
  QCOMPARE(plan.reportCount(), 1);
  QCOMPARE(plan.queuedReportCount(), 1);
  QCOMPARE(plan.records.last().call, QString {"K1RP1"});
}

void TestSuperFoxTxPlanner::parseQueuedHoundLine()
{
  auto const hound = SuperFoxTxPlanner::parseQueuedHound(queueLine("K1ABC", "+03", "FN42"));

  QCOMPARE(hound.call, QString {"K1ABC"});
  QCOMPARE(hound.report, QString {"+03"});
  QCOMPARE(hound.grid, QString {"FN42"});
}

QTEST_MAIN(TestSuperFoxTxPlanner)

#include "test_superfox_tx_planner.moc"
