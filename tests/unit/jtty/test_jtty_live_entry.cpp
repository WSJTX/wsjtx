#include <QtTest>

#include "widgets/JttyLiveEntry.hpp"

class TestJttyLiveEntry final
  : public QObject
{
  Q_OBJECT

private slots:

  void emptyTextCommitsNothing ()
  {
    auto const plan = Jtty::planIncrementalCommit (QString {});
    QVERIFY (plan.text.isEmpty ());
    QCOMPARE (plan.length, 0);
  }

  void fewerThanHoldbackWordsCommitsNothing ()
  {
    // Exactly maxCompactAtomWords complete words: still all held back.
    auto const plan = Jtty::planIncrementalCommit (QStringLiteral ("CQ K1ABC CQ "));
    QVERIFY (plan.text.isEmpty ());
    QCOMPARE (plan.length, 0);
  }

  void callSplitAcrossHoldbackNeverCommitsPartway ()
  {
    // The motivating case: TEXT5's fixed 5-char windows would slice "CQ K1ABC" mid-callsign if released early.
    for (auto const& text : {QStringLiteral ("CQ "), QStringLiteral ("CQ K1ABC ")}) {
      auto const plan = Jtty::planIncrementalCommit (text);
      QVERIFY2 (plan.text.isEmpty (), qPrintable (text));
    }
  }

  void wordBeyondHoldbackReleasesOnlyTheEarlierWords ()
  {
    // A 4th word pushes the first word out past the 3-word holdback.
    auto const plan = Jtty::planIncrementalCommit (
      QStringLiteral ("CQ K1ABC CQ DE "));
    QCOMPARE (plan.text, QStringLiteral ("CQ "));
    QCOMPARE (plan.length, 3);
  }

  void releasedPrefixExactlyMatchesOriginalText ()
  {
    QString const text = QStringLiteral ("FIRST SECOND THIRD FOURTH FIFTH ");
    auto const plan = Jtty::planIncrementalCommit (text);
    QCOMPARE (text.left (plan.length), plan.text);
    // 5 complete words, holdback 3: releases the first 2.
    QCOMPARE (plan.text, QStringLiteral ("FIRST SECOND "));
  }

  void trailingPartialWordIsNeverReleased ()
  {
    // Same words as the previous case, but the 5th is still mid-typing (no trailing space), so only 4 are complete.
    auto const plan = Jtty::planIncrementalCommit (
      QStringLiteral ("FIRST SECOND THIRD FOURTH FIF"));
    QCOMPARE (plan.text, QStringLiteral ("FIRST "));
  }

  void forceFlushReleasesEverythingRegardlessOfHoldback ()
  {
    QString const text = QStringLiteral ("CQ K1ABC");
    auto const plan = Jtty::planIncrementalCommit (text, true);
    QCOMPARE (plan.text, text);
    QCOMPARE (plan.length, text.size ());
  }

  void forceFlushOnEmptyTextCommitsNothing ()
  {
    auto const plan = Jtty::planIncrementalCommit (QString {}, true);
    QVERIFY (plan.text.isEmpty ());
    QCOMPARE (plan.length, 0);
  }

  void customHoldbackIsHonoured ()
  {
    auto const plan = Jtty::planIncrementalCommit (
      QStringLiteral ("ONE TWO THREE "), false, 1);
    QCOMPARE (plan.text, QStringLiteral ("ONE TWO "));
  }

  // Queue-low commits use holdback 0, but must not split a half-typed word.
  void zeroHoldbackReleasesCompleteWordsButNotAPartialOne ()
  {
    auto const plan = Jtty::planIncrementalCommit (
      QStringLiteral ("CQ CQ DE K1A"), false, 0);
    QCOMPARE (plan.text, QStringLiteral ("CQ CQ DE "));
    QCOMPARE (plan.length, 9);
  }

  void zeroHoldbackOnAPartialWordAloneCommitsNothing ()
  {
    // Nothing complete yet: the trailing word waits for the idle flush.
    auto const plan = Jtty::planIncrementalCommit (
      QStringLiteral ("K1A"), false, 0);
    QVERIFY (plan.text.isEmpty ());
    QCOMPARE (plan.length, 0);
  }

  void commitTriggersOnlyOnceTheBacklogFallsBelowAFrame ()
  {
    QVERIFY (!Jtty::shouldCommitForQueueDepth (Jtty::samplesPerFrame * 3));
    // Exactly one frame still queued: that frame is playing, so hold.
    QVERIFY (!Jtty::shouldCommitForQueueDepth (Jtty::samplesPerFrame));
    QVERIFY (Jtty::shouldCommitForQueueDepth (Jtty::samplesPerFrame - 1));
    QVERIFY (Jtty::shouldCommitForQueueDepth (0));
  }

  void frameLengthMatchesTheEncoderAndIsAboutTwoSeconds ()
  {
    // Guards the constant against drifting from the encoder's frame size.
    QCOMPARE (Jtty::samplesPerFrame, 59LL * 1536);
    QCOMPARE (Jtty::samplesPerFrame / 48000.0, 1.888);
  }
};

QTEST_GUILESS_MAIN (TestJttyLiveEntry)
#include "test_jtty_live_entry.moc"
