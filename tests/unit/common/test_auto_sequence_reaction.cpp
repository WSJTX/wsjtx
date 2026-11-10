#include <QtTest>

#include <limits>

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
  void supersededRepeatCannotStopNewSelection()
  {
    for (auto const& mode : {QString {"MSK144"}, QString {"Q65"}}) {
      auto snapshot = baseSnapshot();
      snapshot.mode = mode;
      snapshot.repeatTx = true;
      snapshot.sendRr73 = true;
      snapshot.qsoProgress = QsoProgress::Rogers;
      snapshot.selectedTxMessage = 6;
      snapshot.completion.phase = QsoCompletionPhase::Cancelled;
      snapshot.completion.logRequested = true;
      DecodedText message {QString {"060500 -10  0.3 1500 %1  K1ABC W1AW 73"}
        .arg(mode == "MSK144" ? "&" : ":")};
      auto const plan = DecodedMessageReaction::planAutoSequence(
        message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
      QVERIFY(plan.effects.isEmpty());
    }
  }

  void cancelledRepeatDoesNotSuppressAnotherContact()
  {
    for (auto const& mode : {QString {"MSK144"}, QString {"Q65"}}) {
      auto snapshot = baseSnapshot();
      snapshot.mode = mode;
      snapshot.qsoProgress = QsoProgress::Rogers;
      snapshot.completion = {QsoCompletionPhase::Cancelled, true, "W2XYZ"};
      auto const plan = DecodedMessageReaction::planAutoSequence(
        decode("K1ABC W1AW 73", mode == "MSK144" ? "&" : ":"), snapshot,
        DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
      QVERIFY(hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow));
      QVERIFY(!hasEffect(plan, Effect::Kind::FinishRepeat));

      auto const lateReply = DecodedMessageReaction::planAutoSequence(
        decode("<K1ABC> <W2XYZ/P> 73 ?", mode == "MSK144" ? "&" : ":"), snapshot,
        DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
      QVERIFY(lateReply.effects.isEmpty());
    }
  }

  void timedOutRepeatAcceptsLateConfirmation()
  {
    for (auto const& mode : {QString {"MSK144"}, QString {"Q65"}}) {
      for (bool logRequested : {false, true}) {
        auto snapshot = baseSnapshot();
        snapshot.mode = mode;
        snapshot.autoEnabled = false;
        snapshot.autoSequenceEnabled = false;
        snapshot.repeatTx = true;
        snapshot.sendRr73 = true;
        snapshot.loggingEnabled = true;
        snapshot.completion = {QsoCompletionPhase::TimedOut, logRequested, "W1AW"};
        auto const plan = DecodedMessageReaction::planAutoSequence(
          decode("K1ABC W1AW 73", mode == "MSK144" ? "&" : ":"), snapshot,
          DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
        QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
        QCOMPARE(effectIndex(plan, Effect::Kind::RecordFinalConfirmation), 0);
        QCOMPARE(effectCount(plan, Effect::Kind::RequestLogQso), int(!logRequested));
        QCOMPARE(effectIndex(plan, Effect::Kind::FinishRepeat), logRequested ? 1 : 2);
        QVERIFY(!hasEffect(plan, Effect::Kind::ScheduleRepeatFinish));
      }
    }
  }

  void confirmedContactIgnoresRepeated73AfterSelectingCq()
  {
    for (auto const& mode : {QString {"MSK144"}, QString {"Q65"}}) {
      auto snapshot = baseSnapshot();
      snapshot.mode = mode;
      snapshot.qsoProgress = QsoProgress::Calling;
      snapshot.selectedTxMessage = 6;
      snapshot.callingCq = true;
      snapshot.autoReply = true;
      snapshot.completion = {QsoCompletionPhase::Finished, true, "W1AW", true};
      auto const plan = DecodedMessageReaction::planAutoSequence(
        decode("K1ABC W1AW 73", mode == "MSK144" ? "&" : ":"), snapshot,
        DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
      QVERIFY(plan.effects.isEmpty());
    }
  }

  void repeatFinal73Admission_data()
  {
    QTest::addColumn<QString>("mode");
    QTest::addColumn<QString>("payload");
    QTest::addColumn<bool>("matches");
    for (auto const& mode : {QString {"MSK144"}, QString {"Q65"}}) {
      auto const prefix = mode.toLatin1() + '-';
      QTest::newRow((prefix + "direct").constData()) << mode << "K1ABC W1AW 73" << true;
      QTest::newRow((prefix + "confidence").constData()) << mode << "K1ABC W1AW 73 ?" << true;
      QTest::newRow((prefix + "ap-a1").constData()) << mode << "K1ABC W1AW 73 a1" << true;
      QTest::newRow((prefix + "ap-q3").constData()) << mode << "K1ABC W1AW 73 q3" << true;
      QTest::newRow((prefix + "confidence-ap").constData()) << mode << "K1ABC W1AW 73 ? q3" << true;
      QTest::newRow((prefix + "bracketed").constData()) << mode << "<K1ABC> <W1AW> 73" << true;
      QTest::newRow((prefix + "portable").constData()) << mode << "K1ABC/P W1AW/P 73" << true;
      QTest::newRow((prefix + "wrong-recipient").constData()) << mode << "K2ABC W1AW 73" << false;
      QTest::newRow((prefix + "wrong-sender").constData()) << mode << "K1ABC W2XYZ 73" << false;
      QTest::newRow((prefix + "recipient-substring").constData()) << mode << "K1ABCD W1AW 73" << false;
      QTest::newRow((prefix + "sender-substring").constData()) << mode << "K1ABC XW1AW 73" << false;
      QTest::newRow((prefix + "reversed").constData()) << mode << "W1AW K1ABC 73" << false;
      QTest::newRow((prefix + "unresolved-recipient").constData()) << mode << "<...> W1AW 73" << false;
      QTest::newRow((prefix + "unresolved-sender").constData()) << mode << "K1ABC <...> 73" << false;
      QTest::newRow((prefix + "rr73").constData()) << mode << "K1ABC W1AW RR73" << false;
      QTest::newRow((prefix + "bare73").constData()) << mode << "73" << false;
      QTest::newRow((prefix + "free-text").constData()) << mode << "K1ABC W1AW 73 GL" << false;
    }
  }

  void repeatFinal73Admission()
  {
    QFETCH(QString, mode);
    QFETCH(QString, payload);
    QFETCH(bool, matches);
    auto snapshot = baseSnapshot();
    snapshot.mode = mode;
    snapshot.qsoProgress = QsoProgress::Rogers;
    snapshot.autoSequenceEnabled = false;
    snapshot.sentFirst73 = true;
    snapshot.repeatTx = true;
    snapshot.sendRr73 = true;
    snapshot.loggingEnabled = true;
    snapshot.completion = {QsoCompletionPhase::AwaitingFinal73, true};
    auto const message = decode(payload, mode == "MSK144" ? "&" : ":");
    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QCOMPARE(plan.disposition == DecodedMessageReaction::ReactionDisposition::Reacted, matches);
    QCOMPARE(hasEffect(plan, Effect::Kind::ScheduleRepeatFinish), matches && mode == "MSK144");
    QCOMPARE(hasEffect(plan, Effect::Kind::FinishRepeat), matches && mode == "Q65");
    QVERIFY(!hasEffect(plan, Effect::Kind::RequestLogQso));
    QVERIFY(!hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow));

    if (payload.split(' ').contains("73")) {
      snapshot.autoSequenceEnabled = true;
      snapshot.sentFirst73 = false;
      auto const admission = DecodedMessageReaction::planAutoSequence(
        message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
      auto const reaction = DecodedMessageReaction::planProcessMessage(message, snapshot);
      for (auto const& finalReplyPlan : {admission, reaction}) {
        QCOMPARE(finalReplyPlan.disposition == DecodedMessageReaction::ReactionDisposition::Reacted,
                 matches);
        QCOMPARE(hasEffect(finalReplyPlan, Effect::Kind::ScheduleRepeatFinish),
                 matches && mode == "MSK144");
        QCOMPARE(hasEffect(finalReplyPlan, Effect::Kind::FinishRepeat), matches && mode == "Q65");
        QVERIFY(!hasEffect(finalReplyPlan, Effect::Kind::RequestLogQso));
        QVERIFY(!hasEffect(finalReplyPlan, Effect::Kind::ProcessSyntheticMessageNow));
        QVERIFY(!hasEffect(finalReplyPlan, Effect::Kind::SetTxMessageIndex));
      }
    }
  }

  void repeatFinal73RejectsOtherModeDecode()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "MSK144";
    snapshot.qsoProgress = QsoProgress::Rogers;
    snapshot.repeatTx = true;
    snapshot.sendRr73 = true;
    snapshot.completion = {QsoCompletionPhase::AwaitingFinal73, true};
    auto const message = decode("K1ABC W1AW 73", ":");
    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
    QVERIFY(plan.effects.isEmpty());
  }

  void repeatFinal73RequiresCurrentRepeat_data()
  {
    QTest::addColumn<QString>("mode");
    QTest::addColumn<bool>("autoEnabled");
    QTest::addColumn<bool>("repeatTx");
    QTest::addColumn<bool>("sendRr73");
    QTest::newRow("auto-off") << QString {"MSK144"} << false << true << true;
    QTest::newRow("repeat-off") << QString {"MSK144"} << true << false << true;
    QTest::newRow("rrr") << QString {"MSK144"} << true << true << false;
    QTest::newRow("other-mode") << QString {"FT8"} << true << true << true;
    QTest::newRow("Q65 auto-off") << QString {"Q65"} << false << true << true;
  }

  void repeatFinal73RequiresCurrentRepeat()
  {
    QFETCH(QString, mode);
    QFETCH(bool, autoEnabled);
    QFETCH(bool, repeatTx);
    QFETCH(bool, sendRr73);
    auto snapshot = baseSnapshot();
    snapshot.mode = mode;
    snapshot.autoEnabled = autoEnabled;
    snapshot.repeatTx = repeatTx;
    snapshot.sendRr73 = sendRr73;
    snapshot.autoSequenceEnabled = false;
    snapshot.sentFirst73 = true;
    snapshot.completion = {QsoCompletionPhase::AwaitingFinal73, true};
    auto const message = decode("K1ABC W1AW 73", mode == "MSK144" ? "&" : mode == "Q65" ? ":" : "~");
    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QVERIFY(!hasEffect(plan, Effect::Kind::ScheduleRepeatFinish));
    QVERIFY(!hasEffect(plan, Effect::Kind::FinishRepeat));
  }

  void q65RrrFinal73Logging_data()
  {
    QTest::addColumn<bool>("autoSequence");
    QTest::addColumn<bool>("logRequested");
    QTest::addColumn<bool>("tuning");
    for (bool sequencing : {false, true}) {
      for (bool requested : {false, true}) {
        for (bool tuning : {false, true}) {
          auto const name = QString {"sequence%1-requested%2-tuning%3"}
            .arg(sequencing).arg(requested).arg(tuning).toLatin1();
          QTest::newRow(name.constData()) << sequencing << requested << tuning;
        }
      }
    }
  }

  void q65RrrFinal73Logging()
  {
    QFETCH(bool, autoSequence);
    QFETCH(bool, logRequested);
    QFETCH(bool, tuning);
    auto snapshot = baseSnapshot();
    snapshot.mode = "Q65";
    snapshot.qsoProgress = QsoProgress::Rogers;
    snapshot.autoSequenceEnabled = autoSequence;
    snapshot.repeatTx = true;
    snapshot.sendRr73 = false;
    snapshot.selectedTxMessage = 4;
    snapshot.loggingEnabled = true;
    snapshot.tuning = tuning;
    snapshot.completion.logRequested = logRequested;
    auto const plan = DecodedMessageReaction::planAutoSequence(
      decode("K1ABC W1AW 73", ":"), snapshot,
      DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    if (tuning) {
      QVERIFY(plan.effects.isEmpty());
      return;
    }
    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
    QCOMPARE(effectCount(plan, Effect::Kind::RequestLogQso), int(!logRequested && !tuning));
    QCOMPARE(effectIndex(plan, Effect::Kind::RecordFinalConfirmation), 0);
    QCOMPARE(effectIndex(plan, Effect::Kind::FinishRepeat), logRequested ? 1 : 2);
    QVERIFY(!hasEffect(plan, Effect::Kind::CeaseAutoTx));
    QVERIFY(!hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow));
    QVERIFY(!hasEffect(plan, Effect::Kind::ScheduleAutoFlagOff));
    QVERIFY(!hasEffect(plan, Effect::Kind::ScheduleRepeatFinish));
  }

  void q65RrrFinal73DoesNotFinishOtherActivity()
  {
    for (int changed : {0, 1, 2}) {
      auto snapshot = baseSnapshot();
      snapshot.mode = "Q65";
      snapshot.repeatTx = true;
      snapshot.sendRr73 = false;
      snapshot.selectedTxMessage = changed == 0 ? 6 : 4;
      snapshot.autoEnabled = changed != 1;
      snapshot.qsoProgress = changed == 2 ? QsoProgress::Report : QsoProgress::Rogers;
      auto const plan = DecodedMessageReaction::planAutoSequence(
        decode("K1ABC W1AW 73", ":"), snapshot,
        DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
      QVERIFY(!hasEffect(plan, Effect::Kind::RecordFinalConfirmation));
      QVERIFY(!hasEffect(plan, Effect::Kind::FinishRepeat));
    }
  }

  void unresolvedCallsCannotAdvanceExchange_data()
  {
    QTest::addColumn<QString>("payload");
    QTest::addColumn<QString>("partner");
    QTest::addColumn<int>("progress");
    for (auto progress : {QsoProgress::RogerReport, QsoProgress::Rogers}) {
      auto const prefix = QByteArray::number(int(progress)) + '-';
      QTest::newRow((prefix + "namibia").constData())
        << "<...> V5/HB9SHD RR73" << "V5/HB9SHD" << int(progress);
      QTest::newRow((prefix + "unresolved-recipient").constData())
        << "<...> W1AW 73" << "W1AW" << int(progress);
      QTest::newRow((prefix + "unresolved-sender").constData())
        << "K1ABC <...> RR73" << "W1AW" << int(progress);
      QTest::newRow((prefix + "both-unresolved").constData())
        << "<...> <...> RR73" << "W1AW" << int(progress);
    }
  }

  void unresolvedCallsCannotAdvanceExchange()
  {
    QFETCH(QString, payload);
    QFETCH(QString, partner);
    QFETCH(int, progress);
    auto snapshot = baseSnapshot();
    snapshot.dxCall = snapshot.hisCall = partner;
    snapshot.qsoProgress = static_cast<QsoProgress>(progress);
    snapshot.loggingEnabled = true;
    auto const message = decode(payload);
    QVERIFY(message.isStandardMessage());

    auto const admission = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QVERIFY(!hasEffect(admission, Effect::Kind::ProcessSyntheticMessageNow));

    snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Synthetic;
    auto const reaction = DecodedMessageReaction::planProcessMessage(message, snapshot);
    QCOMPARE(reaction.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
    QVERIFY(!hasEffect(reaction, Effect::Kind::SetTxMessageIndex));
    QVERIFY(!hasEffect(reaction, Effect::Kind::SetQsoProgress));
    QVERIFY(!hasEffect(reaction, Effect::Kind::RequestLogQso));
    QVERIFY(!hasEffect(reaction, Effect::Kind::SetDxCall));
  }

  void explicitDualReplyWithUnresolvedSenderStillAdmitted()
  {
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = QsoProgress::Rogers;
    snapshot.loggingEnabled = true;
    auto const message = decode("K1ABC RR73; W2XX <...> -10");
    QVERIFY(message.isStandardMessage());

    auto const admission = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QVERIFY(hasEffect(admission, Effect::Kind::ProcessSyntheticMessageNow));

    snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Synthetic;
    auto const reaction = DecodedMessageReaction::planProcessMessage(message, snapshot);
    QVERIFY(!hasEffect(reaction, Effect::Kind::SetDxCall));
    QVERIFY(!hasEffect(reaction, Effect::Kind::RequestLogQso));
  }

  void explicitDualReportIgnoresUnresolvedSignoffRecipient()
  {
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = QsoProgress::Report;
    snapshot.loggingEnabled = true;
    auto const message = decode("<...> RR73; K1ABC <W1AW> -10");
    QVERIFY(message.isStandardMessage());

    auto const admission = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QVERIFY(hasEffect(admission, Effect::Kind::ProcessSyntheticMessageNow));

    snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Synthetic;
    auto const reaction = DecodedMessageReaction::planProcessMessage(message, snapshot);
    QCOMPARE(reaction.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
    QCOMPARE(intEffect(reaction, Effect::Kind::SetTxMessage), 3);
    auto const progress = effectIndex(reaction, Effect::Kind::SetQsoProgress);
    QVERIFY(progress >= 0);
    QCOMPARE(reaction.effects.at(progress).progress, QsoProgress::RogerReport);
    QVERIFY(!hasEffect(reaction, Effect::Kind::SetDxCall));
    QVERIFY(!hasEffect(reaction, Effect::Kind::RequestLogQso));
    QVERIFY(!hasEffect(reaction, Effect::Kind::CeaseAutoTx));
  }

  void stopsToAvoidQrmWhenPartnerRepliesNearOurTxFrequencyToAnotherCaller_data()
  {
    QTest::addColumn<QString>("payload");
    QTest::addColumn<int>("progress");
    QTest::addColumn<bool>("stops");
    for (auto progress : {QsoProgress::Replying, QsoProgress::Report}) {
      auto const prefix = QByteArray::number(int(progress)) + '-';
      QTest::newRow((prefix + "known-recipient").constData())
        << "K9XYZ W1AW -10" << int(progress) << true;
      QTest::newRow((prefix + "unresolved-recipient").constData())
        << "<...> W1AW RR73" << int(progress) << true;
      QTest::newRow((prefix + "unresolved-sender").constData())
        << "K9XYZ <...> RR73" << int(progress) << false;
      QTest::newRow((prefix + "both-unresolved").constData())
        << "<...> <...> RR73" << int(progress) << false;
    }
  }

  void stopsToAvoidQrmWhenPartnerRepliesNearOurTxFrequencyToAnotherCaller()
  {
    QFETCH(QString, payload);
    QFETCH(int, progress);
    QFETCH(bool, stops);
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = static_cast<QsoProgress>(progress);
    snapshot.tx1Enabled = snapshot.qsoProgress != QsoProgress::Report;
    snapshot.txFrequency = 815;
    snapshot.loggingEnabled = true;
    auto const message = decode(payload, "~", "0605", 815);
    QVERIFY(message.isStandardMessage());

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QCOMPARE(plan.disposition == DecodedMessageReaction::ReactionDisposition::Reacted, stops);
    QCOMPARE(hasEffect(plan, Effect::Kind::ClickStopTx), stops);
    QCOMPARE(hasEffect(plan, Effect::Kind::LogStopped), stops);
    QVERIFY(!hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow));
    QVERIFY(!hasEffect(plan, Effect::Kind::RequestLogQso));
  }

  void msk144StopTolerance_data()
  {
    QTest::addColumn<unsigned>("stopTolerance");
    QTest::addColumn<int>("frequency");
    QTest::addColumn<int>("progress");
    QTest::addColumn<bool>("tx1Enabled");
    QTest::addColumn<bool>("hound");
    QTest::addColumn<bool>("stops");

    unsigned const unrestricted = std::numeric_limits<unsigned>::max();
    QTest::newRow("unrestricted-zero") << unrestricted << 1500
      << int(QsoProgress::Replying) << true << false << true;
    QTest::newRow("unrestricted-separated") << unrestricted << 2400
      << int(QsoProgress::Replying) << true << false << true;
    QTest::newRow("finite-minus-boundary") << 50u << 1450
      << int(QsoProgress::Replying) << true << false << true;
    QTest::newRow("finite-plus-boundary") << 50u << 1550
      << int(QsoProgress::Replying) << true << false << true;
    QTest::newRow("finite-outside") << 50u << 1551
      << int(QsoProgress::Replying) << true << false << false;
    QTest::newRow("zero-tolerance") << 0u << 1500
      << int(QsoProgress::Replying) << true << false << true;
    QTest::newRow("zero-outside") << 0u << 1501
      << int(QsoProgress::Replying) << true << false << false;
    QTest::newRow("report-without-tx1") << unrestricted << 1500
      << int(QsoProgress::Report) << false << false << true;
    QTest::newRow("report-with-tx1") << unrestricted << 1500
      << int(QsoProgress::Report) << true << false << false;
    QTest::newRow("hound") << unrestricted << 1500
      << int(QsoProgress::Replying) << true << true << false;
  }

  void msk144StopTolerance()
  {
    QFETCH(unsigned, stopTolerance);
    QFETCH(int, frequency);
    QFETCH(int, progress);
    QFETCH(bool, tx1Enabled);
    QFETCH(bool, hound);
    QFETCH(bool, stops);
    auto snapshot = baseSnapshot();
    snapshot.mode = "MSK144";
    snapshot.qsoProgress = static_cast<QsoProgress>(progress);
    snapshot.tx1Enabled = tx1Enabled;
    if (hound) snapshot.specOp = SpecialOperatingActivity::HOUND;
    auto const message = decode("K9XYZ W1AW -10", "&", "060522", frequency);
    QVERIFY(message.isStandardMessage());
    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode,
      25, stopTolerance);
    QCOMPARE(hasEffect(plan, Effect::Kind::ClickStopTx), stops);
    QCOMPARE(hasEffect(plan, Effect::Kind::LogStopped), stops);
    QVERIFY(!hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow));
  }

  void processesDirectedReplyToOurCall()
  {
    auto snapshot = baseSnapshot();
    DecodedText message {"0605 -10  0.3 1500 ~  K1ABC W1AW -10"};

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QVERIFY(hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow));
  }

  void directedRepliesMatchWholeStationIdentities_data()
  {
    QTest::addColumn<QString>("localCall");
    QTest::addColumn<QString>("partner");
    QTest::addColumn<QString>("payload");
    QTest::addColumn<bool>("advances");

    QTest::newRow("exact") << "K1ABC" << "W1AW" << "K1ABC W1AW -10" << true;
    QTest::newRow("sender-extension") << "K1ABC" << "W1AW" << "K1ABC W1AWX -10" << false;
    QTest::newRow("sender-prefix") << "W1AW" << "K1ABC" << "W1AW AK1ABC -10" << false;
    QTest::newRow("destination-extension") << "K1AB" << "W1AW" << "K1ABC W1AW -10" << false;
    QTest::newRow("portable-sender") << "K1ABC" << "W1AW" << "K1ABC W1AW/P -10" << true;
    QTest::newRow("selected-compound") << "K1ABC" << "EA8/W1AW" << "K1ABC W1AW -10" << true;
    QTest::newRow("resolved-hash") << "PJ4/K1ABC" << "W1AW"
      << "<PJ4/K1ABC> W1AW -10" << true;
    QTest::newRow("nonstandard-partner") << "K1ABC" << "ABCDEFGH"
      << "K1ABC <ABCDEFGH> -10" << true;
    QTest::newRow("nonstandard-local-call") << "ABCDEFGH" << "W1AW"
      << "<ABCDEFGH> W1AW -10" << true;
    QTest::newRow("different-nonstandard-partner") << "K1ABC" << "ABCDEFGH"
      << "K1ABC <ABCDEFGX> -10" << false;
    QTest::newRow("nonstandard-partner-substring") << "K1ABC" << "ABCDEF"
      << "K1ABC <ABCDEFGH> -10" << false;
    QTest::newRow("empty-partner") << "K1ABC" << "" << "K1ABC W1AW -10" << false;
    QTest::newRow("unresolved-partner") << "K1ABC" << "<...>" << "K1ABC W1AW -10" << false;
  }

  void directedRepliesMatchWholeStationIdentities()
  {
    QFETCH(QString, localCall);
    QFETCH(QString, partner);
    QFETCH(QString, payload);
    QFETCH(bool, advances);
    auto snapshot = baseSnapshot();
    snapshot.myCall = localCall;
    snapshot.baseCall = Radio::base_callsign(localCall);
    snapshot.dxCall = partner;
    snapshot.hisCall = partner;
    snapshot.qsoProgress = QsoProgress::Report;
    auto const message = decode(payload);
    QVERIFY(message.isStandardMessage());

    auto const admission = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QCOMPARE(hasEffect(admission, Effect::Kind::ProcessSyntheticMessageNow), advances);
    if (advances) {
      snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Synthetic;
      auto const reaction = DecodedMessageReaction::planProcessMessage(message, snapshot);
      QCOMPARE(intEffect(reaction, Effect::Kind::SetTxMessage), 3);
      QVERIFY(!hasEffect(reaction, Effect::Kind::ClearDxGrid));
      if (hasEffect(reaction, Effect::Kind::SetDxCall)) {
        QCOMPARE(Radio::base_callsign(textEffect(reaction, Effect::Kind::SetDxCall)),
                 Radio::base_callsign(partner));
      }
    }
  }

  void stopRuleMatchesWholeStationIdentities_data()
  {
    QTest::addColumn<QString>("payload");
    QTest::addColumn<bool>("stops");
    QTest::newRow("partner") << "K9XYZ W1AW -10" << true;
    QTest::newRow("different-sender") << "K9XYZ W1AWX -10" << false;
    QTest::newRow("different-recipient") << "AK1ABC W1AW -10" << true;
    QTest::newRow("our-portable-call") << "K1ABC/P W1AW -10" << false;
    QTest::newRow("cq") << "CQ W1AW FN31" << false;
  }

  void stopRuleMatchesWholeStationIdentities()
  {
    QFETCH(QString, payload);
    QFETCH(bool, stops);
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = QsoProgress::Replying;
    auto const message = decode(payload);
    QVERIFY(message.isStandardMessage());
    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QCOMPARE(hasEffect(plan, Effect::Kind::ClickStopTx), stops);
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

  void establishedTypeTwoDeReplies_data()
  {
    QTest::addColumn<QString>("mode");
    QTest::addColumn<QString>("marker");
    QTest::addColumn<QString>("partner");
    QTest::addColumn<int>("frequency");
    QTest::addColumn<bool>("advances");

    for (QString const& mode : {QString {"JT65"}, QString {"JT9"}}) {
      QString const marker = mode == "JT65" ? "#" : "@";
      auto const prefix = mode.toLatin1() + '-';
      QTest::newRow((prefix + "selected-compound").constData())
        << mode << marker << "W4/G0XYZ" << 1500 << true;
      QTest::newRow((prefix + "selected-base").constData())
        << mode << marker << "G0XYZ" << 1500 << true;
      QTest::newRow((prefix + "different-partner").constData())
        << mode << marker << "W4/K1ABC" << 1500 << false;
      QTest::newRow((prefix + "substring-partner").constData())
        << mode << marker << "G0XY" << 1500 << false;
      QTest::newRow((prefix + "empty-partner").constData())
        << mode << marker << "" << 1500 << false;
      for (int frequency : {1474, 1475, 1525, 1526, 1675, 1725, 1726}) {
        QTest::newRow((prefix + QByteArray::number(frequency)).constData())
          << mode << marker << "W4/G0XYZ" << frequency
          << (frequency == 1475 || frequency == 1525 || frequency == 1675 || frequency == 1725);
      }
    }
  }

  void establishedTypeTwoDeReplies()
  {
    QFETCH(QString, mode);
    QFETCH(QString, marker);
    QFETCH(QString, partner);
    QFETCH(int, frequency);
    QFETCH(bool, advances);
    auto snapshot = baseSnapshot();
    snapshot.mode = mode;
    snapshot.dxCall = partner;
    snapshot.hisCall = partner;
    snapshot.qsoProgress = QsoProgress::Report;
    snapshot.txFrequency = 1700;
    snapshot.callingCq = false;
    snapshot.autoReply = false;
    auto const message = decode("DE W4/G0XYZ R-22", marker, "0605", frequency);
    QVERIFY(message.isStandardMessage());
    QCOMPARE(message.messageWords().at(2), QString {"DE"});
    QCOMPARE(message.messageWords().at(3), QString {"W4/G0XYZ"});
    QCOMPARE(message.messageWords().at(4), QString {"R-22"});

    auto const admission = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QCOMPARE(hasEffect(admission, Effect::Kind::ProcessSyntheticMessageNow), advances);
    if (advances) {
      snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Synthetic;
      auto const reaction = DecodedMessageReaction::planProcessMessage(message, snapshot);
      QCOMPARE(intEffect(reaction, Effect::Kind::SetTxMessageIndex), 4);
      QCOMPARE(intEffect(reaction, Effect::Kind::CheckTxMessage), 4);
      int const progressIndex = effectIndex(reaction, Effect::Kind::SetQsoProgress);
      QVERIFY(progressIndex >= 0);
      QCOMPARE(reaction.effects.at(progressIndex).progress, QsoProgress::Rogers);
      QVERIFY(!hasEffect(reaction, Effect::Kind::ClearDxGrid));
      QCOMPARE(textEffect(reaction, Effect::Kind::SetDxCall), QString {"W4/G0XYZ"});
    }
  }

  void processesTypeTwoDeReplyWithinTolerance()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "JT65";
    snapshot.callingCq = true;
    snapshot.autoReply = true;
    auto const message = decode("DE W4/G0XYZ R-22", "#");
    QVERIFY(message.isStandardMessage());

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);

    QVERIFY(hasEffect(plan, Effect::Kind::ProcessSyntheticMessageNow));
  }

  void startTolerancePreservesUnsignedRange_data()
  {
    QTest::addColumn<int>("phase");
    QTest::addColumn<unsigned>("tolerance");
    QTest::addColumn<int>("frequency");
    QTest::addColumn<bool>("advances");
    using Phase = DecodedMessageReaction::AutoSequencePhase;
    for (auto phase : {Phase::LegacyShortMessage, Phase::StandardDecode}) {
      QByteArray const prefix = phase == Phase::LegacyShortMessage ? "short-" : "standard-";
      auto row = [phase, &prefix](char const* name, unsigned tolerance, int frequency, bool advances) {
        QTest::newRow((prefix + name).constData())
          << int(phase) << tolerance << frequency << advances;
      };
      row("unrestricted", std::numeric_limits<unsigned>::max(), 2400, true);
      row("signed-maximum", unsigned(std::numeric_limits<int>::max()), 2400, true);
      row("beyond-signed-maximum", unsigned(std::numeric_limits<int>::max()) + 1u, 2400, true);
      row("zero", 0u, 1500, true);
      row("zero-outside", 0u, 1501, false);
      row("lower-boundary", 25u, 1475, true);
      row("upper-boundary", 25u, 1525, true);
      row("below-boundary", 25u, 1474, false);
      row("above-boundary", 25u, 1526, false);
      row("tx-frequency", 0u, 1700, phase == Phase::StandardDecode);
      row("tx-boundary", 25u, 1725, phase == Phase::StandardDecode);
    }
  }

  void startTolerancePreservesUnsignedRange()
  {
    QFETCH(int, phase);
    QFETCH(unsigned, tolerance);
    QFETCH(int, frequency);
    QFETCH(bool, advances);
    auto snapshot = baseSnapshot();
    snapshot.mode = "JT65";
    snapshot.dxCall = snapshot.hisCall = "W4/G0XYZ";
    snapshot.qsoProgress = QsoProgress::Report;
    snapshot.txFrequency = 1700;
    auto const sequencePhase = static_cast<DecodedMessageReaction::AutoSequencePhase>(phase);
    bool const shortMessage = sequencePhase == DecodedMessageReaction::AutoSequencePhase::LegacyShortMessage;
    auto const message = decode(shortMessage ? "K1ABC W1AW FN31 OOO" : "DE W4/G0XYZ R-22",
                                "#", "0605", frequency);
    QVERIFY(message.isStandardMessage());

    auto const plan = DecodedMessageReaction::planAutoSequence(
      message, snapshot, sequencePhase, tolerance, 50);

    QCOMPARE(plan.disposition == DecodedMessageReaction::ReactionDisposition::Reacted, advances);
    QCOMPARE(hasEffect(plan, shortMessage ? Effect::Kind::SetTxMessage
                                         : Effect::Kind::ProcessSyntheticMessageNow), advances);
  }

  void freeTextSignoffsPreserveSelectedPartner_data()
  {
    QTest::addColumn<QString>("payload");
    QTest::addColumn<int>("frequency");
    QTest::addColumn<bool>("autoReply");
    QTest::addColumn<bool>("advances");
    QTest::newRow("generic") << "TNX QSO 73" << 1500 << false << true;
    QTest::newRow("our-call-as-second-word") << "TNX K1ABC 73" << 1500 << false << true;
    QTest::newRow("selected-call-as-second-word") << "TNX W1AW 73" << 1500 << false << true;
    QTest::newRow("other-call-as-second-word") << "TNX K9XYZ 73" << 1500 << false << true;
    QTest::newRow("after-cq-auto-reply") << "TNX K1ABC 73" << 1500 << true << true;
    QTest::newRow("outside-tolerance") << "TNX K1ABC 73" << 1526 << false << false;
  }

  void freeTextSignoffsPreserveSelectedPartner()
  {
    QFETCH(QString, payload);
    QFETCH(int, frequency);
    QFETCH(bool, autoReply);
    QFETCH(bool, advances);
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = QsoProgress::RogerReport;
    snapshot.autoReply = autoReply;
    auto const message = decode(payload, "~", "0605", frequency);
    QVERIFY(!message.isStandardMessage());
    auto const admission = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QCOMPARE(hasEffect(admission, Effect::Kind::ProcessSyntheticMessageNow), advances);
    if (!advances) return;

    snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Synthetic;
    auto const reaction = DecodedMessageReaction::planProcessMessage(message, snapshot);

    QCOMPARE(reaction.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
    QCOMPARE(intEffect(reaction, Effect::Kind::SetTxMessageIndex), 5);
    QCOMPARE(intEffect(reaction, Effect::Kind::CheckTxMessage), 5);
    auto const progress = effectIndex(reaction, Effect::Kind::SetQsoProgress);
    QVERIFY(progress >= 0);
    QCOMPARE(reaction.effects.at(progress).progress, QsoProgress::Signoff);
    QVERIFY(!hasEffect(reaction, Effect::Kind::SetDxCall));
    QVERIFY(!hasEffect(reaction, Effect::Kind::ClearDxGrid));
    QVERIFY(!hasEffect(reaction, Effect::Kind::SetDxGrid));
    QVERIFY(!hasEffect(reaction, Effect::Kind::DisarmAutoRespondSelection));
  }

  void automaticSignoffCannotInheritEarlierSelectionPrivileges()
  {
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = QsoProgress::Rogers;
    snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::ManualLeftPane;
    snapshot.doubleClicked = true;
    auto const frequencyPick = DecodedMessageReaction::planProcessMessage(
      DecodedText {"0605  Tx      1259 ~  CQ K1ABC FN42"}, snapshot);
    QCOMPARE(frequencyPick.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);

    auto const message = decode("K1ABC K9XYZ RR73");
    QVERIFY(message.isStandardMessage());
    auto const admission = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QVERIFY(hasEffect(admission, Effect::Kind::ProcessSyntheticMessageNow));

    snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Synthetic;
    auto const reaction = DecodedMessageReaction::planProcessMessage(message, snapshot);
    QCOMPARE(reaction.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
    QVERIFY(!hasEffect(reaction, Effect::Kind::SetDxCall));
    QVERIFY(!hasEffect(reaction, Effect::Kind::ClearDxGrid));
    QVERIFY(!hasEffect(reaction, Effect::Kind::SetQsoProgress));
    QVERIFY(!hasEffect(reaction, Effect::Kind::GenerateStandardMessages));
  }

  void dualRepliesOnlyFinishSelectedPartners_data()
  {
    QTest::addColumn<QString>("partner");
    QTest::addColumn<QString>("sender");
    QTest::addColumn<bool>("finishes");
    QTest::newRow("standard-partner") << "W1AW" << "W1AW" << true;
    QTest::newRow("other-nonstandard-station") << "W1AW" << "TM100PARIS" << false;
    QTest::newRow("letter-only-partner") << "ABCDEFGH" << "ABCDEFGH" << true;
    QTest::newRow("other-letter-only-station") << "W1AW" << "ABCDEFGH" << false;
    QTest::newRow("letter-only-substring") << "ABCDEF" << "ABCDEFGH" << false;
  }

  void dualRepliesOnlyFinishSelectedPartners()
  {
    QFETCH(QString, partner);
    QFETCH(QString, sender);
    QFETCH(bool, finishes);
    auto snapshot = baseSnapshot();
    snapshot.dxCall = partner;
    snapshot.hisCall = partner;
    snapshot.qsoProgress = QsoProgress::Rogers;
    snapshot.loggingEnabled = true;
    auto const message = decode(QString {"K1ABC RR73; W2XX <%1> -10"}.arg(sender));
    QVERIFY(message.isStandardMessage());
    auto const admission = DecodedMessageReaction::planAutoSequence(
      message, snapshot, DecodedMessageReaction::AutoSequencePhase::StandardDecode, 25, 50);
    QVERIFY(hasEffect(admission, Effect::Kind::ProcessSyntheticMessageNow));

    snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Synthetic;
    auto const reaction = DecodedMessageReaction::planProcessMessage(message, snapshot);

    QCOMPARE(hasEffect(reaction, Effect::Kind::RequestLogQso), finishes);
    QCOMPARE(intEffect(reaction, Effect::Kind::SetTxMessageIndex), finishes ? 6 : -1);
    if (!finishes) {
      QCOMPARE(reaction.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
      QVERIFY(!hasEffect(reaction, Effect::Kind::CeaseAutoTx));
      QVERIFY(!hasEffect(reaction, Effect::Kind::SetDxCall));
      QVERIFY(!hasEffect(reaction, Effect::Kind::SetQsoProgress));
      QVERIFY(!hasEffect(reaction, Effect::Kind::GenerateStandardMessages));
    }
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
