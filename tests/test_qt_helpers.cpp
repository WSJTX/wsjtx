#include <QtTest>
#include <QDateTime>
#include <QDebug>

#include "qt_helpers.hpp"

class TestQtHelpers
  : public QObject
{
  Q_OBJECT

public:

private:
  Q_SLOT void round_15s_date_time_up ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 500}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 30)));
  }

  Q_SLOT void truncate_15s_date_time_up ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 500}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_15s_date_time_down ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 499}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void truncate_15s_date_time_down ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 499}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_15s_date_time_on ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 15}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void truncate_15s_date_time_on ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 15}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_15s_date_time_under ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 14, 999}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void truncate_15s_date_time_under ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 14, 999}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15)));
  }

  Q_SLOT void round_15s_date_time_over ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 15, 1}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void truncate_15s_date_time_over ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 15, 1}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_7p5s_date_time_up ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 26, 250}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 30)));
  }

  Q_SLOT void truncate_7p5s_date_time_up ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 26, 250}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void round_7p5s_date_time_down ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 26, 249}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void truncate_7p5s_date_time_down ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 26, 249}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void round_7p5s_date_time_on ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 500}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void truncate_7p5s_date_time_on ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 500}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void round_7p5s_date_time_under ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 499}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void truncate_7p5s_date_time_under ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 499}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_7p5s_date_time_over ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 501}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void truncate_7p5s_date_time_over ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 501}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }
};

QTEST_MAIN (TestQtHelpers);

#include "test_qt_helpers.moc"
