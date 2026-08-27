#include <QMap>
#include <QQueue>
#include <QTest>

#include "FoxOperatorActions.hpp"

namespace
{
  QString queueLine (QString const& call, QString const& details)
  {
    return (call + QString (12, ' ')).left (12) + details;
  }

  struct Qso
  {
    int ncall;
    int sentinel;
  };
}

class TestFoxOperatorActions final
  : public QObject
{
  Q_OBJECT

private slots:
  void moveToFrontUsesLiveQueueLine ()
  {
    auto const first = queueLine ("W1AAA", "-02 FN31");
    auto const live_target = queueLine ("K1ABC", "+04 FN42");
    QQueue<QString> queue;
    queue.enqueue (first);
    queue.enqueue (live_target);

    QVERIFY (FoxOperatorActions::moveQueuedHoundToFront (
        queue, queueLine ("K1ABC", "+01 stale snapshot")));

    QCOMPARE (queue.size (), 2);
    QCOMPARE (queue.at (0), live_target);
    QCOMPARE (queue.at (1), first);
  }

  void moveToFrontRejectsMissingHound ()
  {
    auto const live_line = queueLine ("W1AAA", "-02 FN31");
    QQueue<QString> queue;
    queue.enqueue (live_line);

    QVERIFY (!FoxOperatorActions::moveQueuedHoundToFront (
        queue, queueLine ("K1ABC", "+01 stale snapshot")));

    QCOMPARE (queue.size (), 1);
    QCOMPARE (queue.at (0), live_line);
  }

  void removeUsesLiveQueueCallsign ()
  {
    auto const retained = queueLine ("W1AAA", "-02 FN31");
    QQueue<QString> queue;
    queue.enqueue (retained);
    queue.enqueue (queueLine ("K1ABC", "+04 FN42"));
    queue.enqueue (queueLine ("K1ABC", "+02 FN41"));

    QVERIFY (FoxOperatorActions::removeQueuedHound (
        queue, queueLine ("K1ABC", "+01 stale snapshot")));

    QCOMPARE (queue.size (), 1);
    QCOMPARE (queue.at (0), retained);
  }

  void removeRejectsMissingHound ()
  {
    auto const live_line = queueLine ("W1AAA", "-02 FN31");
    QQueue<QString> queue;
    queue.enqueue (live_line);

    QVERIFY (!FoxOperatorActions::removeQueuedHound (
        queue, queueLine ("K1ABC", "+01 stale snapshot")));

    QCOMPARE (queue.size (), 1);
    QCOMPARE (queue.at (0), live_line);
  }

  void timeoutRequiresActiveCallAndQso ()
  {
    QQueue<QString> in_progress;
    in_progress.enqueue ("K1ABC");
    QMap<QString, Qso> qsos {{"K1ABC", {2, 73}}};

    QVERIFY (FoxOperatorActions::timeoutIfActive (
        in_progress, qsos, queueLine ("K1ABC", "3 (rx)"), 6));

    QCOMPARE (qsos.value ("K1ABC").ncall, 6);
    QCOMPARE (qsos.value ("K1ABC").sentinel, 73);
  }

  void timeoutRejectsCallNoLongerInProgress ()
  {
    QQueue<QString> in_progress;
    QMap<QString, Qso> qsos {{"K1ABC", {2, 73}}};

    QVERIFY (!FoxOperatorActions::timeoutIfActive (
        in_progress, qsos, queueLine ("K1ABC", "3 (rx)"), 6));

    QCOMPARE (qsos.value ("K1ABC").ncall, 2);
    QCOMPARE (qsos.value ("K1ABC").sentinel, 73);
  }

  void timeoutDoesNotCreateMissingQso ()
  {
    QQueue<QString> in_progress;
    in_progress.enqueue ("K1ABC");
    QMap<QString, Qso> qsos;

    QVERIFY (!FoxOperatorActions::timeoutIfActive (
        in_progress, qsos, queueLine ("K1ABC", "3 (rx)"), 6));

    QVERIFY (qsos.isEmpty ());
  }
};

QTEST_MAIN (TestFoxOperatorActions)

#include "test_fox_operator_actions.moc"
