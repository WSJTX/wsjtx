#include <QtTest>

#include "AutoRespondScoring.hpp"

class TestAutoRespondScoring final : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void firstDecodedSignalCandidateAlwaysWins()
  {
    AutoRespondScores scores;

    QVERIFY(scores.considerMaximumDb(-40));
    QVERIFY(scores.considerMinimumDb(40));
  }

  Q_SLOT void maximumDbKeepsStrongestCandidate()
  {
    AutoRespondScores scores;

    QVERIFY(scores.considerMaximumDb(-20));
    QVERIFY(scores.considerMaximumDb(-10));
    QVERIFY(!scores.considerMaximumDb(-10));
    QVERIFY(!scores.considerMaximumDb(-30));
  }

  Q_SLOT void minimumDbKeepsWeakestCandidate()
  {
    AutoRespondScores scores;

    QVERIFY(scores.considerMinimumDb(-10));
    QVERIFY(scores.considerMinimumDb(-20));
    QVERIFY(!scores.considerMinimumDb(-20));
    QVERIFY(!scores.considerMinimumDb(-5));
  }

  Q_SLOT void distanceRequiresPositiveImprovement()
  {
    AutoRespondScores scores;

    QVERIFY(!scores.considerDistance(0));
    QVERIFY(scores.considerDistance(1));
    QVERIFY(scores.considerDistance(100));
    QVERIFY(!scores.considerDistance(100));
  }

  Q_SLOT void resetAllowsNewCandidates()
  {
    AutoRespondScores scores;
    QVERIFY(scores.considerDistance(100));
    QVERIFY(scores.considerMaximumDb(10));
    QVERIFY(scores.considerMinimumDb(-40));

    scores.reset();

    QVERIFY(scores.considerDistance(1));
    QVERIFY(scores.considerMaximumDb(-40));
    QVERIFY(scores.considerMinimumDb(40));
  }
};

QTEST_MAIN(TestAutoRespondScoring)
#include "test_auto_respond_scoring.moc"
