#include <QtTest>

#include "QsoReactionTestSupport.hpp"

namespace
{
  using namespace QsoReactionTestSupport;

  Snapshot baseSnapshot()
  {
    auto snapshot = neutralStationSnapshot();
    snapshot.mode = "FT8";
    snapshot.autoEnabled = true;
    snapshot.autoSequenceEnabled = true;
    snapshot.tx1Enabled = true;
    snapshot.qsoProgress = QsoProgress::Calling;
    return snapshot;
  }
}

class TestAutoSequenceReaction final
  : public QObject
{
  Q_OBJECT

private slots:
  void stopsToAvoidQrmWhenPartnerRepliesNearOurTxFrequencyToAnotherCaller()
  {
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = QsoProgress::Replying;
    snapshot.txFrequency = 815;
    DecodedText message {"0605 -10  0.3 0815 ~  K9XYZ W1AW -10"};

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
    QVERIFY(hasEffect(plan, Effect::Kind::ClickStopTx));
  }

  void processesDirectedReplyToOurCall()
  {
    auto snapshot = baseSnapshot();
    DecodedText message {"0605 -10  0.3 1500 ~  K1ABC W1AW -10"};

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QVERIFY(hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow));
  }

  void standard73DoesNotTreatEmptyDxCallAsMatch_data()
  {
    QTest::addColumn<QString>("dxCall");
    QTest::addColumn<bool>("advances");

    QTest::newRow("selected-partner") << "W1AW" << true;
    QTest::newRow("empty-partner") << "" << false;
  }

  void standard73DoesNotTreatEmptyDxCallAsMatch()
  {
    QFETCH(QString, dxCall);
    QFETCH(bool, advances);
    auto snapshot = baseSnapshot();
    snapshot.dxCall = dxCall;
    snapshot.hisCall = dxCall;
    snapshot.qsoProgress = QsoProgress::RogerReport;
    DecodedText message {"0605 -10  0.3 1500 ~  K9XYZ W1AW 73"};
    QVERIFY(message.isStandardMessage());

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QCOMPARE(hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow), advances);
  }

  void processesTypeTwoDeReplyWithinTolerance()
  {
    auto snapshot = baseSnapshot();
    snapshot.callingCq = true;
    snapshot.autoReply = true;
    DecodedText message {"0605 -10  0.3 1500 ~  DE W1AW -10"};

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QVERIFY(hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow));
  }

  void ignoresProcessActionInFoxMode()
  {
    auto snapshot = baseSnapshot();
    snapshot.specOp = SpecialOperatingActivity::FOX;
    DecodedText message {"0605 -10  0.3 1500 ~  K1ABC W1AW -10"};

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
  }

  void freeText73DoesNotAdvanceMsk144AutoSequence()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "MSK144";
    snapshot.qsoProgress = QsoProgress::RogerReport;
    DecodedText message {"060522 -10  0.3 1500 &  RR73"};

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
  }

  void storesEuVhfExchangeForAutoSequenceCandidate()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "MSK144";
    DecodedText message {"060522 -10  0.3 1500 &  <K1ABC> W1AW 520001 FN31 W1AW R X"};

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QCOMPARE(textEffect(plan, Effect::Kind::SetReceivedExchange),
             message.clean_string().trimmed().right(13));
  }

  void ignoresEuVhfExchangeForRejectedDecode()
  {
    auto snapshot = baseSnapshot();
    DecodedText message {"0605 -10  0.3 1500 ~  RANDOM <K1ABC> 520001 FN31"};

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QVERIFY(textEffect(plan, Effect::Kind::SetReceivedExchange).isEmpty());
  }

  void legacyShortMessages_data()
  {
    QTest::addColumn<QString>("payload");
    QTest::addColumn<int>("expectedTxMessage");

    QTest::newRow("ooo") << "K1ABC W1AW OOO" << 3;
    QTest::newRow("ro") << "RO" << 4;
    QTest::newRow("rrr") << "RRR" << 5;
    QTest::newRow("73") << "73" << 5;
  }

  void legacyShortMessages()
  {
    QFETCH(QString, payload);
    QFETCH(int, expectedTxMessage);
    auto snapshot = neutralStationSnapshot();

    auto const plan = DecodedMessageReaction::planAutoSequence(
      decode(payload, "#"), snapshot,
      DecodedMessageReaction::AutoSequencePhase::LegacyShortMessage, 15, 15);

    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
    QCOMPARE(intEffect(plan, Effect::Kind::SetTxMessage), expectedTxMessage);
  }

  void legacyShortMessageTolerance_data()
  {
    QTest::addColumn<int>("frequency");
    QTest::addColumn<bool>("reacts");

    QTest::newRow("minus-sixteen") << 1484 << false;
    QTest::newRow("minus-fifteen") << 1485 << true;
    QTest::newRow("plus-fifteen") << 1515 << true;
    QTest::newRow("plus-sixteen") << 1516 << false;
  }

  void legacyShortMessageTolerance()
  {
    QFETCH(int, frequency);
    QFETCH(bool, reacts);
    auto snapshot = neutralStationSnapshot();

    auto const plan = DecodedMessageReaction::planAutoSequence(
      decode("RRR", "#", "0605", frequency), snapshot,
      DecodedMessageReaction::AutoSequencePhase::LegacyShortMessage, 15, 15);

    QCOMPARE(plan.disposition == DecodedMessageReaction::ReactionDisposition::Reacted, reacts);
  }

  void legacyShortMessageHonorsStartToleranceArgument()
  {
    auto snapshot = neutralStationSnapshot();

    auto const plan = DecodedMessageReaction::planAutoSequence(
      decode("RRR", "#", "0605", 1516), snapshot,
      DecodedMessageReaction::AutoSequencePhase::LegacyShortMessage, 16, 15);

    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
    QCOMPARE(intEffect(plan, Effect::Kind::SetTxMessage), 5);
  }
};

QTEST_MAIN(TestAutoSequenceReaction)
#include "test_auto_sequence_reaction.moc"
