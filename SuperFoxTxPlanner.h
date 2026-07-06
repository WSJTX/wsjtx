#ifndef SUPERFOXTXPLANNER_H
#define SUPERFOXTXPLANNER_H

#include <QQueue>
#include <QSet>
#include <QString>
#include <QVector>

class SuperFoxTxPlanner
{
public:
  enum class RecordKind
  {
    RR73,
    Report
  };

  struct QsoState
  {
    QString call;
    QString grid;
    QString sent;
    QString rcvd;
    int ncall {0};
    int nRR73 {0};
    int tFoxRrpt {-1};
    int tFoxTxRR73 {-1};
  };

  struct QueuedHound
  {
    QString call;
    QString grid;
    QString report;
  };

  struct Record
  {
    RecordKind kind {RecordKind::Report};
    QString call;
    QString grid;
    QString report;
    bool fromQueue {false};

    Record() = default;
    Record(RecordKind kind, QString const& call, QString const& grid,
           QString const& report, bool fromQueue);
  };

  struct Capacity
  {
    int maxRecords {9};
    int maxRR73 {5};
    int maxReports {4};

    Capacity() = default;
    Capacity(int maxRecords, int maxRR73, int maxReports);
  };

  struct Plan
  {
    Capacity capacity;
    QVector<Record> records;

    int rr73Count() const;
    int reportCount() const;
    int queuedReportCount() const;
  };

  static Capacity capacity(bool sendFreeText);
  static Plan plan(QVector<QsoState> const& qsos,
                   QQueue<QString> const& inProgress,
                   QVector<QueuedHound> const& queuedHounds,
                   int maxStrikes,
                   bool sendFreeText);
  static QueuedHound parseQueuedHound(QString const& line);

private:
  static void addRR73Candidates(QVector<QsoState> const& qsos,
                                QSet<QString>& selectedCalls,
                                Plan& plan,
                                bool (*predicate)(QsoState const&));
  static bool canAdd(Plan const& plan, RecordKind kind);
};

#endif
