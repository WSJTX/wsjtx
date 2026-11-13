#include <QtTest>

#include "widgets/JttyTransmitEcho.hpp"
#include "widgets/JttyTransmitHistory.hpp"
#include "widgets/JttyTransmitQueue.hpp"

class TestJttyTransmitQueue final
  : public QObject
{
  Q_OBJECT

  static Jtty::TransmitSegment segment (QString const& text, int toneCount = 2)
  {
    return {text, QVector<int> (toneCount, 1), 0};
  }

private slots:
  void fifoPreservesFullSegmentsAndLaterRequests ()
  {
    Jtty::TransmitQueue queue;
    auto const full = QString (79, 'A') + 'Z';
    queue.append (11, {segment (full), segment ("TAIL")});
    QCOMPARE (queue.nextRequestId (), qint64 {11});
    QCOMPARE (queue.nextSegment ()->text, full);
    QVERIFY (queue.commitNext (3072));

    queue.append (12, {segment ("NEW MESSAGE")});
    QCOMPARE (queue.nextRequestId (), qint64 {11});
    QCOMPARE (queue.nextSegment ()->text, QString {"TAIL"});
    QVERIFY (!queue.commitNext (6144));
    QCOMPARE (queue.nextRequestId (), qint64 {12});
    QCOMPARE (queue.nextSegment ()->text, QString {"NEW MESSAGE"});
    QVERIFY (queue.commitNext (9216));
    QVERIFY (!queue.hasUncommitted ());
    QVERIFY (!queue.nextSegment ());
    QCOMPARE (queue.nextRequestId (), qint64 {0});
    QCOMPARE (queue.requests ().at (0).segments.at (0).text, full);
  }

  void progressDoesNotCompleteRequests ()
  {
    Jtty::TransmitQueue queue;
    queue.append (21, {segment ("FIRST"), segment ("LAST", 3)});
    QCOMPARE (queue.segmentCount (), 2);
    QCOMPARE (queue.nextSegment ()->sampleCount (), qint64 {3072});
    QCOMPARE (queue.remainingSegments (0), 2);
    QCOMPARE (queue.pendingText (0), QString {"FIRST\nLAST"});
    queue.commitNext (3072);
    QCOMPARE (queue.remainingSegments (3071), 2);
    QCOMPARE (queue.remainingSegments (3072), 1);
    QCOMPARE (queue.pendingText (3072), QString {"LAST"});
    QVERIFY (queue.complete (3072).isEmpty ());
    QCOMPARE (queue.nextSegment ()->sampleCount (), qint64 {4608});
    queue.commitNext (7680);
    QCOMPARE (queue.remainingSegments (7680), 0);
    QVERIFY (queue.pendingText (7680).isEmpty ());
    QVERIFY (!queue.empty ());
    QCOMPARE (queue.segmentCount (), 2);
    QVERIFY (queue.complete (7679).isEmpty ());
    QCOMPARE (queue.complete (7680), QVector<qint64> {21});
    QVERIFY (queue.empty ());
    QVERIFY (queue.complete (7680).isEmpty ());
  }

  void drainCompletesOnlyCommittedPrefix ()
  {
    Jtty::TransmitQueue queue;
    queue.append (31, {segment ("FIRST")});
    queue.append (32, {segment ("SECOND"), segment ("THIRD")});
    queue.append (33, {segment ("FOURTH")});
    queue.commitNext (3072);
    queue.commitNext (6144);
    QCOMPARE (queue.complete (6144), QVector<qint64> {31});
    QCOMPARE (queue.requests ().size (), 2);
    QCOMPARE (queue.nextSegment ()->text, QString {"THIRD"});
    queue.commitNext (9216);
    queue.commitNext (12288);
    QCOMPARE (queue.complete (9216), QVector<qint64> {32});
    QCOMPARE (queue.complete (12288), QVector<qint64> {33});
    QVERIFY (queue.empty ());
  }

  void cancellationReportsAcceptedAndUnsubmittedOnce ()
  {
    Jtty::TransmitQueue queue;
    queue.append (41, {segment ("ACCEPTED")});
    queue.append (42, {segment ("PENDING"), segment ("UNSUBMITTED")});
    queue.append (43, {segment ("LATER")});
    queue.commitNext (3072);
    queue.commitNext (6144);
    auto const cancelled = queue.cancel ();
    QCOMPARE (cancelled, (QVector<qint64> {41, 42, 43}));
    QVERIFY (queue.empty ());
    QVERIFY (!queue.hasUncommitted ());
    QCOMPARE (queue.segmentCount (), 0);
    QCOMPARE (queue.remainingSegments (0), 0);
    QVERIFY (queue.pendingText (0).isEmpty ());
    QVERIFY (queue.cancel ().isEmpty ());
    QVERIFY (queue.complete (6144).isEmpty ());
    queue.append (44, {segment ("FRESH")});
    QCOMPARE (queue.nextSegment ()->text, QString {"FRESH"});
    QVERIFY (queue.commitNext (3072));
    QCOMPARE (queue.complete (3072), QVector<qint64> {44});
  }

  // Three frames; frame starts at characters 0, 5 and 11.
  static Jtty::TransmitSegment framed (QString const& text, qint64 endSample,
                                       QVector<int> starts = {0, 5, 11})
  {
    return {text, QVector<int> (59 * 3, 1), endSample, 1500, starts};
  }

  void sentCharsStepsOneFrameAtATime ()
  {
    qint64 const frame = Jtty::transmitFrameSamples;
    auto const s = framed (QStringLiteral ("CQ K1ABC CQ TEST"), 10 * frame);
    qint64 const start = 7 * frame;
    QCOMPARE (Jtty::sentChars (s, start), 0);
    QCOMPARE (Jtty::sentChars (s, start + frame - 1), 0);
    QCOMPARE (Jtty::sentChars (s, start + frame), 5);
    QCOMPARE (Jtty::sentChars (s, start + 2 * frame), 11);
    QCOMPARE (Jtty::sentChars (s, start + 3 * frame), s.text.size ());
    QCOMPARE (Jtty::sentChars (s, 99 * frame), s.text.size ());

    auto unframed = s;
    unframed.frameCharStarts.clear ();
    QCOMPARE (Jtty::sentChars (unframed, start + 2 * frame), 0);
    QCOMPARE (Jtty::sentChars (unframed, start + 3 * frame), s.text.size ());

    auto uncommitted = s;
    uncommitted.endSample = 0;
    QCOMPARE (Jtty::sentChars (uncommitted, 99 * frame), 0);
  }

  void echoGrowsOneOverAndSkipsDiddle ()
  {
    qint64 const frame = Jtty::transmitFrameSamples;
    auto const isFiller = [] (QString const& text) { return text == QStringLiteral ("<<<"); };
    Jtty::TransmitQueue::Request first {7, {framed (QStringLiteral ("CQ K1ABC CQ TEST"), 3 * frame)}, 1};
    Jtty::TransmitQueue::Request diddle {8, {framed (QStringLiteral ("<<<"), 6 * frame, {0, 1, 2})}, 1};
    Jtty::TransmitQueue::Request second {9, {framed (QStringLiteral ("DE K1ABC K"), 9 * frame, {0, 3, 6})}, 1};
    QVector<Jtty::TransmitQueue::Request> const requests {first, diddle, second};

    Jtty::TransmitEcho echo;
    auto update = echo.update (requests, 0, isFiller);
    QVERIFY (!update.grew);
    QVERIFY (echo.isEmpty ());

    update = echo.update (requests, frame, isFiller);
    QVERIFY (update.grew);
    QCOMPARE (echo.text (), QStringLiteral ("CQ K1"));
    QCOMPARE (echo.firstRequestId (), qint64 {7});
    QCOMPARE (echo.frequency (), 1500);
    QVERIFY (update.completedSegments.isEmpty ());

    QVERIFY (!echo.update (requests, frame + 10, isFiller).grew);

    update = echo.update (requests, 6 * frame, isFiller);
    QCOMPARE (echo.text (), QStringLiteral ("CQ K1ABC CQ TEST"));
    QCOMPARE (update.completedSegments, QStringList {QStringLiteral ("CQ K1ABC CQ TEST")});

    // A separately committed piece gets the space between words back.
    echo.update (requests, 7 * frame, isFiller);
    QCOMPARE (echo.text (), QStringLiteral ("CQ K1ABC CQ TEST DE"));
    update = echo.update (requests, 9 * frame, isFiller);
    QCOMPARE (echo.text (), QStringLiteral ("CQ K1ABC CQ TEST DE K1ABC K"));
    QCOMPARE (update.completedSegments, QStringList {QStringLiteral ("DE K1ABC K")});

    echo.reset ();
    QVERIFY (echo.isEmpty ());
  }

  void historyRecallsLikeAShell ()
  {
    Jtty::TransmitHistory history;
    QString text {QStringLiteral ("unchanged")};
    QVERIFY (!history.recall (-1, {}, &text));
    QCOMPARE (text, QStringLiteral ("unchanged"));

    history.add (QStringLiteral ("  CQ  K1ABC "));
    history.add (QStringLiteral ("CQ K1ABC"));   // repeat of the newest: not added
    history.add (QStringLiteral ("W2ZQ 599 MA"));
    history.add (QString {});
    QCOMPARE (history.overs (), (QStringList {QStringLiteral ("CQ K1ABC"), QStringLiteral ("W2ZQ 599 MA")}));

    QVERIFY (history.recall (-1, QStringLiteral ("draft"), &text));
    QCOMPARE (text, QStringLiteral ("W2ZQ 599 MA"));
    QVERIFY (history.recall (-1, text, &text));
    QCOMPARE (text, QStringLiteral ("CQ K1ABC"));
    QVERIFY (!history.recall (-1, text, &text));
    QVERIFY (history.recall (1, text, &text));
    QCOMPARE (text, QStringLiteral ("W2ZQ 599 MA"));
    QVERIFY (history.recall (1, text, &text));
    QCOMPARE (text, QStringLiteral ("draft"));
    QVERIFY (!history.recall (1, text, &text));

    // An edit ends recall: the next Ctrl+Up starts from the newest again.
    QVERIFY (history.recall (-1, QStringLiteral ("x"), &text));
    QVERIFY (history.recall (-1, text, &text));
    history.endRecall ();
    QVERIFY (history.recall (-1, QStringLiteral ("y"), &text));
    QCOMPARE (text, QStringLiteral ("W2ZQ 599 MA"));

    for (int i = 0; i < 30; ++i) history.add (QString::number (i));
    QCOMPARE (history.overs ().size (), Jtty::TransmitHistory::capacity);
    QCOMPARE (history.overs ().constLast (), QStringLiteral ("29"));
  }

  // Halting cancels every queued request; only those still silent may have
  // their text reopened for editing, or the next over repeats what was heard.
  void requestsOnAirCoversStartedButUnfinishedSegments ()
  {
    Jtty::TransmitQueue queue;
    queue.append (41, {segment ("SENT")});         // 0..3072
    queue.append (42, {segment ("PLAYING")});      // 3072..6144
    queue.append (43, {segment ("SILENT")});       // 6144..9216
    QVERIFY (queue.commitNext (3072));
    QVERIFY (queue.commitNext (6144));
    QVERIFY (queue.commitNext (9216));

    // Nothing played yet.
    QCOMPARE (queue.requestsOnAir (0), QSet<qint64> {});
    // 41 fully played; 42 part-way; 43 untouched.
    QCOMPARE (queue.requestsOnAir (4000), (QSet<qint64> {41, 42}));
    // Exactly at a boundary: 42 has finished, 43 has not begun.
    QCOMPARE (queue.requestsOnAir (6144), (QSet<qint64> {41, 42}));
    QCOMPARE (queue.requestsOnAir (9216), (QSet<qint64> {41, 42, 43}));
  }

  // An uncommitted segment has no endSample, so it cannot have been heard.
  void requestsOnAirIgnoresUncommittedSegments ()
  {
    Jtty::TransmitQueue queue;
    queue.append (51, {segment ("QUEUED")});
    QCOMPARE (queue.requestsOnAir (1 << 20), QSet<qint64> {});
  }
};

QTEST_GUILESS_MAIN (TestJttyTransmitQueue)

#include "test_jtty_transmit_queue.moc"
