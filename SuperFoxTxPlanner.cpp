#include "SuperFoxTxPlanner.h"

#include <QMap>

namespace
{
bool rr73RepeatDue(SuperFoxTxPlanner::QsoState const& qso)
{
  return qso.tFoxRrpt >= 0 && qso.tFoxRrpt - qso.tFoxTxRR73 > 3;
}

bool rr73NeverSent(SuperFoxTxPlanner::QsoState const& qso)
{
  return qso.tFoxRrpt >= 0 && qso.tFoxTxRR73 < 0;
}

bool rr73ReplyNewerThanTx(SuperFoxTxPlanner::QsoState const& qso)
{
  return qso.tFoxRrpt >= 0 && qso.tFoxTxRR73 <= qso.tFoxRrpt;
}
}

SuperFoxTxPlanner::Record::Record(RecordKind kind, QString const& call,
                                  QString const& grid, QString const& report,
                                  bool fromQueue)
  : kind {kind}
  , call {call}
  , grid {grid}
  , report {report}
  , fromQueue {fromQueue}
{
}

SuperFoxTxPlanner::Capacity::Capacity(int maxRecords, int maxRR73, int maxReports)
  : maxRecords {maxRecords}
  , maxRR73 {maxRR73}
  , maxReports {maxReports}
{
}

int SuperFoxTxPlanner::Plan::rr73Count() const
{
  int count = 0;
  for (auto const& record : records)
    {
      if (record.kind == RecordKind::RR73) ++count;
    }
  return count;
}

int SuperFoxTxPlanner::Plan::reportCount() const
{
  int count = 0;
  for (auto const& record : records)
    {
      if (record.kind == RecordKind::Report) ++count;
    }
  return count;
}

int SuperFoxTxPlanner::Plan::queuedReportCount() const
{
  int count = 0;
  for (auto const& record : records)
    {
      if (record.kind == RecordKind::Report && record.fromQueue) ++count;
    }
  return count;
}

SuperFoxTxPlanner::Capacity SuperFoxTxPlanner::capacity(bool sendFreeText)
{
  if (sendFreeText)
    {
      return Capacity {4, 4, 4};
    }

  return Capacity {9, 5, 4};
}

SuperFoxTxPlanner::Plan SuperFoxTxPlanner::plan(QVector<QsoState> const& qsos,
                                                QQueue<QString> const& inProgress,
                                                QVector<QueuedHound> const& queuedHounds,
                                                int maxStrikes,
                                                bool sendFreeText)
{
  Plan result;
  result.capacity = capacity(sendFreeText);

  QSet<QString> selectedRR73;
  addRR73Candidates(qsos, selectedRR73, result, rr73RepeatDue);
  addRR73Candidates(qsos, selectedRR73, result, rr73NeverSent);
  addRR73Candidates(qsos, selectedRR73, result, rr73ReplyNewerThanTx);

  QMap<QString, QsoState> qsoByCall;
  for (auto const& qso : qsos)
    {
      qsoByCall.insert(qso.call, qso);
    }

  for (auto const& call : inProgress)
    {
      if (!canAdd(result, RecordKind::Report)) break;
      auto const it = qsoByCall.constFind(call);
      if (it == qsoByCall.constEnd()) continue;
      auto const& qso = it.value();
      if (qso.tFoxRrpt < 0 && qso.ncall < maxStrikes)
        {
          result.records.push_back(Record {RecordKind::Report, call, qso.grid,
                                           qso.sent, false});
        }
    }

  for (auto const& queued : queuedHounds)
    {
      if (!canAdd(result, RecordKind::Report)) break;
      result.records.push_back(Record {RecordKind::Report, queued.call, queued.grid,
                                       queued.report, true});
    }

  return result;
}

SuperFoxTxPlanner::QueuedHound SuperFoxTxPlanner::parseQueuedHound(QString const& line)
{
  QueuedHound hound;
  auto const i0 = line.indexOf(" ");
  hound.call = i0 >= 0 ? line.mid(0, i0).trimmed() : line.trimmed();
  hound.report = line.mid(12, 3).trimmed();
  hound.grid = line.mid(16, 4).trimmed();
  return hound;
}

void SuperFoxTxPlanner::addRR73Candidates(QVector<QsoState> const& qsos,
                                          QSet<QString>& selectedCalls,
                                          Plan& plan,
                                          bool (*predicate)(QsoState const&))
{
  for (auto const& qso : qsos)
    {
      if (!canAdd(plan, RecordKind::RR73)) break;
      if (selectedCalls.contains(qso.call)) continue;
      if (!predicate(qso)) continue;
      selectedCalls.insert(qso.call);
      plan.records.push_back(Record {RecordKind::RR73, qso.call, qso.grid, {}, false});
    }
}

bool SuperFoxTxPlanner::canAdd(Plan const& plan, RecordKind kind)
{
  if (plan.records.size() >= plan.capacity.maxRecords) return false;
  if (kind == RecordKind::RR73) return plan.rr73Count() < plan.capacity.maxRR73;
  return plan.reportCount() < plan.capacity.maxReports;
}
