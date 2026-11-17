#include <QtTest>
#include <QTextDocument>
#include "JttyReceiveLine.hpp"

class TestJttyReceiveLine final : public QObject
{
  Q_OBJECT

  static JttyReceiveLine::Presentation message (qint64 id, QString const& text,
                                               double seconds = 0, quint64 group = 1)
  {
    return {group, id, QDateTime {QDate {2026, 9, 11}, QTime {12, 34, 56}, Qt::UTC},
            seconds, 1500, text};
  }

private slots:
  void growingWrappedLinePreservesOtherMessagesAndTransmitText ()
  {
    QTextDocument document;
    JttyReceiveLine first, second;
    QFont const font {"Monospace", 10};
    QVERIFY (!first.render (document, message (11, "CQ TEST\nCONTINUATION"), {}, font).isNull ());
    QVERIFY (!second.render (document, message (12, "ANOTHER MESSAGE"), {}, font).isNull ());
    QTextCursor tx (&document);
    tx.movePosition (QTextCursor::End);
    tx.insertBlock ();
    QTextBlockFormat txBlock;
    txBlock.setBackground (Qt::yellow);
    tx.setBlockFormat (txBlock);
    QTextCharFormat txText;
    txText.setForeground (Qt::red);
    tx.insertText ("Tx: MY RESPONSE", txText);
    QVERIFY (!first.render (document, message (11, "CQ TEST\nCONTINUATION COMPLETE"), {}, font).isNull ());
    QCOMPARE (document.toPlainText (), QString {
      "123456 1500 -10  CQ TEST\nCONTINUATION COMPLETE\n123456 1500 -10  ANOTHER MESSAGE\nTx: MY RESPONSE"});
    JttyReceiveLine::refresh (document, {true}, font);
    QCOMPARE (document.toPlainText (), QString {
      "123456 1500 -10  cq test\ncontinuation complete\n123456 1500 -10  another message\nTx: MY RESPONSE"});
    QCOMPARE (document.lastBlock ().blockFormat (), txBlock);
    QTextCursor check (document.lastBlock ());
    check.movePosition (QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QCOMPARE (check.charFormat ().foreground (), txText.foreground ());
    QVERIFY (!second.render (document, message (12, "ANOTHER MESSAGE COMPLETE"), {true}, font).isNull ());
    QCOMPARE (document.lastBlock ().text (), QString {"Tx: MY RESPONSE"});
    QVERIFY (second.render (document, message (12, "ANOTHER MESSAGE COMPLETE"), {true}, font).isNull ());
  }

  void completedRecordsRefreshAndRestoreCanonicalCase ()
  {
    QTextDocument document;
    QFont const font {"Monospace", 10};
    {
      JttyReceiveLine completed;
      QVERIFY (!completed.render (document, message (1, "CQ MiXeD CASE"), {}, font).isNull ());
    }
    QFont const larger {"Monospace", 14};
    JttyReceiveLine::refresh (document, {true}, larger);
    QCOMPARE (document.toPlainText (), QString {"123456 1500 -10  cq mixed case"});
    QTextCursor check (document.begin ());
    check.movePosition (QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QCOMPARE (check.charFormat ().font ().pointSize (), 14);
    JttyReceiveLine::refresh (document, {false}, font);
    QCOMPARE (document.toPlainText (), QString {"123456 1500 -10  CQ MiXeD CASE"});
  }

  void lateEarlierMessagesAndEqualTimeIdsAreOrdered ()
  {
    QTextDocument document;
    QFont const font;
    {
      JttyReceiveLine completed;
      QVERIFY (!completed.render (document, message (30, "LATE", 30), {}, font, true).isNull ());
    }
    JttyReceiveLine earlier, equal, middle;
    QVERIFY (!earlier.render (document, message (20, "EARLIER", 10), {}, font, true).isNull ());
    QVERIFY (!equal.render (document, message (10, "EQUAL TIME LOWER ID", 10), {}, font, true).isNull ());
    QVERIFY (!middle.render (document, message (25, "MIDDLE", 20), {}, font, true).isNull ());
    QCOMPARE (document.toPlainText (), QString {
      "123456 1500 -10  EQUAL TIME LOWER ID\n123456 1500 -10  EARLIER\n123456 1500 -10  MIDDLE\n123456 1500 -10  LATE"});
    JttyReceiveLine::refresh (document, {true}, font);
    QCOMPARE (document.toPlainText (), QString {
      "123456 1500 -10  equal time lower id\n123456 1500 -10  earlier\n123456 1500 -10  middle\n123456 1500 -10  late"});
    QVERIFY (!earlier.render (document, message (20, "EARLIER COMPLETE", 10), {true}, font, true).isNull ());
    QCOMPARE (document.findBlockByNumber (1).text (), QString {"123456 1500 -10  earlier complete"});
  }

  void chronologyStaysWithinItsGroupAndQsoAppends ()
  {
    QTextDocument document;
    QFont const font;
    JttyReceiveLine first, second, late, early, qso;
    QVERIFY (!first.render (document, message (1, "GROUP ONE", 20, 1), {}, font, true).isNull ());
    QTextCursor heading (&document);
    heading.movePosition (QTextCursor::End);
    heading.insertBlock ();
    QTextBlockFormat headingFormat;
    headingFormat.setAlignment (Qt::AlignRight);
    heading.setBlockFormat (headingFormat);
    heading.insertText ("FILE HEADING");
    QVERIFY (!second.render (document, message (1, "GROUP TWO", 5, 2), {}, font, true).isNull ());
    QVERIFY (!late.render (document, message (2, "GROUP ONE LATER", 30, 1), {}, font, true).isNull ());
    QVERIFY (!early.render (document, message (0, "GROUP TWO EARLIER", 1, 2), {}, font, true).isNull ());
    QVERIFY (!qso.render (document, message (0, "QSO APPENDED", 0, 1), {}, font).isNull ());
    QCOMPARE (document.toPlainText (), QString {
      "123456 1500 -10  GROUP ONE\n123456 1500 -10  GROUP ONE LATER\nFILE HEADING\n123456 1500 -10  GROUP TWO EARLIER\n123456 1500 -10  GROUP TWO\n123456 1500 -10  QSO APPENDED"});
    QCOMPARE (document.findBlockByNumber (2).blockFormat (), headingFormat);
  }

  void eraseAndReplacementCannotReuseOldOwnership ()
  {
    QTextDocument document;
    JttyReceiveLine old, replacement;
    QFont const font;
    QVERIFY (!old.render (document, message (1, "OLD PARTIAL"), {}, font).isNull ());
    document.clear ();
    QVERIFY (!replacement.render (document, message (2, "NEW MESSAGE"), {}, font).isNull ());
    QVERIFY (old.render (document, message (1, "OLD PARTIAL COMPLETED"), {}, font).isNull ());
    JttyReceiveLine::refresh (document, {true}, font);
    QCOMPARE (document.toPlainText (), QString {"123456 1500 -10  new message"});
    QVERIFY (!replacement.render (document, message (2, "NEW MESSAGE COMPLETED"), {}, font).isNull ());
    QVERIFY (old.render (document, message (1, "OLD PARTIAL COMPLETED"), {}, font).isNull ());
    QCOMPARE (document.toPlainText (), QString {"123456 1500 -10  NEW MESSAGE COMPLETED"});
  }

  void prunedBlocksCannotEraseReusedBlocksOrReappear ()
  {
    QTextDocument document;
    document.setMaximumBlockCount (3);
    JttyReceiveLine old;
    QFont const font;
    QVERIFY (!old.render (document, message (1, "OLD PARTIAL\nOLD CONTINUATION"), {}, font).isNull ());
    for (int i = 0; i < 20; ++i) {
      JttyReceiveLine archived;
      QVERIFY (!archived.render (document, message (i + 2, QString::number (i)), {}, font).isNull ());
    }
    auto const retained = document.toPlainText ();
    QVERIFY (old.render (document, message (1, "OLD PARTIAL COMPLETE"), {}, font).isNull ());
    QCOMPARE (document.toPlainText (), retained);
    QCOMPARE (document.blockCount (), 3);
  }

  void partiallyPrunedWrappedRecordIsNotRefreshed ()
  {
    QTextDocument document;
    document.setMaximumBlockCount (3);
    QFont const font;
    JttyReceiveLine wrapped;
    QString const longMessage (90, QLatin1Char {'A'});
    QVERIFY (!wrapped.render (document, message (1, longMessage), {}, font).isNull ());
    QCOMPARE (document.blockCount (), 3);
    {
      JttyReceiveLine completed;
      QVERIFY (!completed.render (document, message (2, "RETAINED"), {}, font).isNull ());
    }
    auto const first = document.begin ().text ();
    auto const second = document.begin ().next ().text ();
    JttyReceiveLine::refresh (document, {true}, font);
    QCOMPARE (document.begin ().text (), first);
    QCOMPARE (document.begin ().next ().text (), second);
    QCOMPARE (document.lastBlock ().text (), QString {"123456 1500 -10  retained"});
    QVERIFY (wrapped.render (document, message (1, longMessage + " COMPLETE"), {}, font).isNull ());
    document.clear ();
    JttyReceiveLine::refresh (document, {}, font);
    QVERIFY (document.toPlainText ().isEmpty ());
  }

  void earlierInsertAtHistoryLimitCannotTakeExistingOwnership ()
  {
    QTextDocument document;
    document.setMaximumBlockCount (1);
    QFont const font;
    JttyReceiveLine retained, earlier;
    QVERIFY (!retained.render (document, message (2, "RETAINED", 20), {}, font, true).isNull ());
    QVERIFY (earlier.render (document, message (1, "EARLIER", 10), {}, font, true).isNull ());
    JttyReceiveLine::refresh (document, {true}, font);
    QCOMPARE (document.toPlainText (), QString {"123456 1500 -10  retained"});
    QVERIFY (!retained.render (document, message (2, "RETAINED COMPLETE", 20), {}, font, true).isNull ());
    QVERIFY (earlier.render (document, message (1, "EARLIER COMPLETE", 10), {}, font, true).isNull ());
    QCOMPARE (document.toPlainText (), QString {"123456 1500 -10  RETAINED COMPLETE"});
  }

  void foreignTextInsideOwnedRangeIsNeverDeleted ()
  {
    QTextDocument document;
    JttyReceiveLine old;
    QFont const font;
    QVERIFY (!old.render (document, message (1, "PARTIAL"), {}, font).isNull ());
    QTextCursor external (&document);
    external.movePosition (QTextCursor::End);
    external.insertText (" UNRELATED TEXT");
    JttyReceiveLine::refresh (document, {true}, font);
    QVERIFY (old.render (document, message (1, "PARTIAL COMPLETED"), {}, font).isNull ());
    QCOMPARE (document.toPlainText (), QString {"123456 1500 -10  PARTIAL UNRELATED TEXT"});
  }

  void movedActiveHandleStillOwnsItsMessage ()
  {
    QTextDocument document;
    JttyReceiveLine line;
    QFont const font;
    QVERIFY (!line.render (document, message (9, "PARTIAL"), {}, font).isNull ());
    auto moved = std::move (line);
    QVERIFY (!moved.render (document, message (9, "PARTIAL COMPLETE"), {}, font).isNull ());
    QCOMPARE (document.toPlainText (), QString {"123456 1500 -10  PARTIAL COMPLETE"});
  }
};

QTEST_MAIN (TestJttyReceiveLine)
#include "test_jtty_receive_line.moc"
