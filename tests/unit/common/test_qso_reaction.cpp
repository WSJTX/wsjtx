#include <QtTest>

#include "QsoReactionTestSupport.hpp"

namespace
{
  using namespace QsoReactionTestSupport;

  Snapshot baseSnapshot()
  {
    auto snapshot = neutralStationSnapshot();
    snapshot.mode = "FT8";
    snapshot.tx1Enabled = true;
    return snapshot;
  }

  struct FinalState
  {
    int txMessage;
    QsoProgress progress;
  };

  FinalState finalState(Plan const& plan, Snapshot const& snapshot)
  {
    FinalState state {snapshot.selectedTxMessage, snapshot.qsoProgress};
    for (auto const& effect : plan.effects) {
      if (effect.kind == Effect::Kind::SetTxMessage
          || effect.kind == Effect::Kind::SetTxMessageIndex
          || effect.kind == Effect::Kind::CheckTxMessage
          || effect.kind == Effect::Kind::ClickTxMessage) {
        state.txMessage = effect.intValue;
      } else if (effect.kind == Effect::Kind::SetQsoProgress) {
        state.progress = effect.progress;
      }
    }
    return state;
  }
}

class TestQsoReaction final
  : public QObject
{
  Q_OBJECT

private slots:
  void qsoProgressValuesMatchDecoderContract()
  {
    QCOMPARE(static_cast<int>(QsoProgress::Calling), 0);
    QCOMPARE(static_cast<int>(QsoProgress::Replying), 1);
    QCOMPARE(static_cast<int>(QsoProgress::Report), 2);
    QCOMPARE(static_cast<int>(QsoProgress::RogerReport), 3);
    QCOMPARE(static_cast<int>(QsoProgress::Rogers), 4);
    QCOMPARE(static_cast<int>(QsoProgress::Signoff), 5);
  }

  void responseTransitions_data()
  {
    QTest::addColumn<int>("initialProgress");
    QTest::addColumn<QString>("response");
    QTest::addColumn<int>("expectedTxMessage");
    QTest::addColumn<int>("expectedProgress");
    QTest::addColumn<bool>("expectedReaction");

    QStringList const responses {"FN31", "-10", "R", "R-10", "RRR", "RR73", "73"};
    for (int initial = 0; initial <= 5; ++initial) {
      for (auto const& response : responses) {
        int tx = -1;
        int progress = initial;
        bool reacts = true;
        if (response == "FN31") {
          tx = 2;
          progress = 2;
        } else if (response == "-10") {
          tx = 3;
          progress = 3;
        } else if (response == "R") {
          tx = initial == 3 ? 5 : 4;
          if (initial == 2) progress = 4;
          else if (initial >= 3) progress = 5;
        } else if (response == "R-10") {
          tx = 4;
          progress = 4;
        } else if (response == "RRR" || response == "RR73") {
          tx = 5;
          if (initial >= 3) progress = 5;
        } else if (initial == 4) {
          tx = 6;
          progress = 5;
        } else {
          tx = 0;
        }
        QByteArray const name = QByteArray::number(initial) + '-' + response.toLatin1();
        QTest::newRow(name.constData()) << initial << response << tx << progress << reacts;
      }
    }
  }

  void responseTransitions()
  {
    QFETCH(int, initialProgress);
    QFETCH(QString, response);
    QFETCH(int, expectedTxMessage);
    QFETCH(int, expectedProgress);
    QFETCH(bool, expectedReaction);

    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = static_cast<QsoProgress>(initialProgress);
    QString const payload = response == "R"
      ? "<K1ABC> W1AW R" : "K1ABC W1AW " + response;
    auto const plan = DecodedMessageReaction::planProcessMessage(decode(payload), snapshot);

    QCOMPARE(plan.disposition == DecodedMessageReaction::ReactionDisposition::Reacted,
             expectedReaction);
    if (expectedReaction) {
      auto const state = finalState(plan, snapshot);
      QCOMPARE(state.txMessage, expectedTxMessage);
      QCOMPARE(static_cast<int>(state.progress), expectedProgress);
    }
  }

  void tx1AvailabilityControlsFallback_data()
  {
    QTest::addColumn<bool>("tx1Enabled");
    QTest::addColumn<int>("expectedTxMessage");
    QTest::addColumn<int>("expectedProgress");
    QTest::newRow("tx1") << true << 1 << 1;
    QTest::newRow("tx2-fallback") << false << 2 << 2;
  }

  void tx1AvailabilityControlsFallback()
  {
    QFETCH(bool, tx1Enabled);
    QFETCH(int, expectedTxMessage);
    QFETCH(int, expectedProgress);
    auto snapshot = baseSnapshot();
    snapshot.tx1Enabled = tx1Enabled;
    snapshot.dxCall.clear();
    snapshot.hisCall.clear();

    auto const plan = DecodedMessageReaction::planProcessMessage(decode("CQ W1AW FN31"), snapshot);
    auto const state = finalState(plan, snapshot);
    QCOMPARE(state.txMessage, expectedTxMessage);
    QCOMPARE(static_cast<int>(state.progress), expectedProgress);
  }

  void signoffLogsOrCeasesAuto_data()
  {
    QTest::addColumn<bool>("loggingEnabled");
    QTest::addColumn<int>("expectedEffect");
    QTest::newRow("log") << true << static_cast<int>(Effect::Kind::RequestLogQso);
    QTest::newRow("cease") << false << static_cast<int>(Effect::Kind::CeaseAutoTx);
  }

  void signoffLogsOrCeasesAuto()
  {
    QFETCH(bool, loggingEnabled);
    QFETCH(int, expectedEffect);
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = QsoProgress::Rogers;
    snapshot.loggingEnabled = loggingEnabled;

    auto const plan = DecodedMessageReaction::planProcessMessage(decode("K1ABC W1AW 73"), snapshot);
    QVERIFY(effectIndex(plan, static_cast<Effect::Kind>(expectedEffect)) >= 0);
  }

  void sharedTailRequestsRestartQuickCallAndRegeneration()
  {
    auto snapshot = baseSnapshot();
    snapshot.doubleClicked = true;
    snapshot.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Manual;
    snapshot.quickCall = true;
    snapshot.transmitting = true;

    auto const plan = DecodedMessageReaction::planProcessMessage(decode("K1ABC W1AW -10"), snapshot);

    QVERIFY(effectIndex(plan, Effect::Kind::SetReport) >= 0);
    QVERIFY(effectIndex(plan, Effect::Kind::GenerateStandardMessages) >= 0);
    QVERIFY(effectIndex(plan, Effect::Kind::SetRestart) >= 0);
    QVERIFY(effectIndex(plan, Effect::Kind::SetAutoEnabled) >= 0);
  }

  void jt65OooSelectsRogerReport()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "JT65";
    auto const plan = DecodedMessageReaction::planProcessMessage(
      decode("K1ABC W1AW FN31 OOO", "#"), snapshot);
    auto const state = finalState(plan, snapshot);
    QCOMPARE(state.txMessage, 3);
    QCOMPARE(state.progress, QsoProgress::RogerReport);
  }

  void dualFoxMessagePreservesCurrentDisposition()
  {
    auto snapshot = baseSnapshot();
    snapshot.loggingEnabled = true;
    auto const message = decode("K1ABC RR73; W2XX W1AW -10");
    auto const plan = DecodedMessageReaction::planProcessMessage(message, snapshot);
    QCOMPARE(message.messageWords().size(), 7);
    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
    QVERIFY(effectIndex(plan, Effect::Kind::RequestLogQso) < 0);
  }

  void freeTextSignoffSelectsTx5()
  {
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = QsoProgress::RogerReport;
    auto const plan = DecodedMessageReaction::planProcessMessage(decode("TNX QSO 73"), snapshot);
    auto const state = finalState(plan, snapshot);
    QCOMPARE(state.txMessage, 5);
    QCOMPARE(state.progress, QsoProgress::Signoff);
    int const select = effectIndex(plan, Effect::Kind::SetTxMessageIndex);
    int const check = effectIndex(plan, Effect::Kind::CheckTxMessage, select + 1);
    int const progress = effectIndex(plan, Effect::Kind::SetQsoProgress, check + 1);
    QVERIFY(select >= 0 && check > select && progress > check);
  }

  void repeatTxUsesClickableTx5AndLiveAutoTimer()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "Q65";
    snapshot.qsoProgress = QsoProgress::Rogers;
    snapshot.repeatTx = true;
    auto const plan = DecodedMessageReaction::planProcessMessage(
      decode("K1ABC W1AW 73", ":"), snapshot);
    QCOMPARE(intEffect(plan, Effect::Kind::ClickTxMessage), 5);
    QCOMPARE(intEffect(plan, Effect::Kind::ScheduleAutoFlagOff), 15000);
  }

  void contestGridPolicies_data()
  {
    QTest::addColumn<int>("specOp");
    QTest::addColumn<QString>("mode");
    QTest::addColumn<QString>("marker");
    QTest::addColumn<int>("expectedTx");
    QTest::addColumn<int>("expectedProgress");
    QTest::newRow("normal") << int(SpecialOperatingActivity::NONE) << "FT8" << "~" << 2 << 2;
    QTest::newRow("na-vhf") << int(SpecialOperatingActivity::NA_VHF) << "FT8" << "~" << 3 << 3;
    QTest::newRow("ww-digi") << int(SpecialOperatingActivity::WW_DIGI) << "FT8" << "~" << 3 << 3;
    QTest::newRow("arrl-digi") << int(SpecialOperatingActivity::ARRL_DIGI) << "FT8" << "~" << 3 << 3;
    QTest::newRow("q65-pileup") << int(SpecialOperatingActivity::Q65_PILEUP) << "Q65" << ":" << 3 << 3;
  }

  void contestGridPolicies()
  {
    QFETCH(int, specOp);
    QFETCH(QString, mode);
    QFETCH(QString, marker);
    QFETCH(int, expectedTx);
    QFETCH(int, expectedProgress);
    auto snapshot = baseSnapshot();
    snapshot.specOp = static_cast<SpecialOperatingActivity>(specOp);
    snapshot.mode = mode;
    auto const state = finalState(
      DecodedMessageReaction::planProcessMessage(decode("K1ABC W1AW FN31", marker), snapshot),
      snapshot);
    QCOMPARE(state.txMessage, expectedTx);
    QCOMPARE(static_cast<int>(state.progress), expectedProgress);
  }

  void contestExchangePolicies()
  {
    auto eu = baseSnapshot();
    eu.specOp = SpecialOperatingActivity::EU_VHF;
    auto euPlan = DecodedMessageReaction::planProcessMessage(
      decode("<K1ABC> W1AW 520001 FN31"), eu);
    QCOMPARE(finalState(euPlan, eu).progress, QsoProgress::RogerReport);

    auto rtty = baseSnapshot();
    rtty.specOp = SpecialOperatingActivity::RTTY;
    auto rttyPlan = DecodedMessageReaction::planProcessMessage(
      decode("K1ABC W1AW 599 0001"), rtty);
    QCOMPARE(finalState(rttyPlan, rtty).progress, QsoProgress::RogerReport);
    QVERIFY(effectIndex(rttyPlan, Effect::Kind::SetReceivedExchange) >= 0);

    auto fieldDay = baseSnapshot();
    fieldDay.specOp = SpecialOperatingActivity::FIELD_DAY;
    auto fieldDayPlan = DecodedMessageReaction::planProcessMessage(
      decode("K1ABC W1AW R 1D EMA"), fieldDay);
    QCOMPARE(finalState(fieldDayPlan, fieldDay).progress, QsoProgress::Rogers);
  }

  void euVhfReportBoundaries_data()
  {
    QTest::addColumn<int>("exchange");
    QTest::addColumn<bool>("valid");
    QTest::newRow("below-report") << 510001 << false;
    QTest::newRow("lower") << 520001 << true;
    QTest::newRow("upper") << 592047 << true;
    QTest::newRow("above-serial") << 592048 << false;
  }

  void euVhfReportBoundaries()
  {
    QFETCH(int, exchange);
    QFETCH(bool, valid);
    auto snapshot = baseSnapshot();
    snapshot.dxCall.clear();
    snapshot.hisCall.clear();
    auto const plan = DecodedMessageReaction::planProcessMessage(
      decode(QString {"<K1ABC> W1AW %1 FN31"}.arg(exchange)), snapshot);
    QCOMPARE(effectIndex(plan, Effect::Kind::QueueContestHint) >= 0, valid);
  }

  void contestHints_data()
  {
    QTest::addColumn<QString>("payload");
    QTest::addColumn<int>("expectedHint");

    QTest::newRow("eu-vhf")
      << "<K1ABC> W1AW 520001 FN31" << int(DecodedMessageReaction::ContestHint::EuVhf);
    QTest::newRow("field-day")
      << "K1ABC W1AW R 1D EMA" << int(DecodedMessageReaction::ContestHint::FieldDay);
    QTest::newRow("rtty")
      << "K1ABC W1AW 599 0001" << int(DecodedMessageReaction::ContestHint::Rtty);
  }

  void contestHints()
  {
    QFETCH(QString, payload);
    QFETCH(int, expectedHint);
    auto snapshot = baseSnapshot();

    auto const plan = DecodedMessageReaction::planProcessMessage(decode(payload), snapshot);

    QCOMPARE(effectCount(plan, Effect::Kind::QueueContestHint), 1);
    int const hint = effectIndex(plan, Effect::Kind::QueueContestHint);
    QCOMPARE(int(plan.effects[hint].contestHint), expectedHint);
  }

  void shownContestHintSuppressesFurtherHints()
  {
    QStringList const payloads {
      "<K1ABC> W1AW 520001 FN31",
      "K1ABC W1AW R 1D EMA",
      "K1ABC W1AW 599 0001"
    };
    auto snapshot = baseSnapshot();
    snapshot.contestHintShown = true;

    for (auto const& payload : payloads) {
      auto const plan = DecodedMessageReaction::planProcessMessage(decode(payload), snapshot);
      QCOMPARE(effectCount(plan, Effect::Kind::QueueContestHint), 0);
    }
  }

  void fieldDayExchangePrecedesContestHint()
  {
    auto snapshot = baseSnapshot();

    auto const plan = DecodedMessageReaction::planProcessMessage(
      decode("K1ABC W1AW R 1D EMA"), snapshot);

    int const exchange = effectIndex(plan, Effect::Kind::SetReceivedExchange);
    int const hint = effectIndex(plan, Effect::Kind::QueueContestHint);
    QVERIFY(exchange >= 0 && hint > exchange);
  }

  void msk144ReportNormalization_data()
  {
    QTest::addColumn<int>("input");
    QTest::addColumn<int>("expected");
    QTest::newRow("negative") << -2 << -3;
    QTest::newRow("zero-low") << -1 << 0;
    QTest::newRow("zero-high") << 1 << 0;
    QTest::newRow("three-low") << 2 << 3;
    QTest::newRow("three-high") << 4 << 3;
    QTest::newRow("six-low") << 5 << 6;
    QTest::newRow("six-high") << 7 << 6;
    QTest::newRow("ten-low") << 8 << 10;
    QTest::newRow("ten-high") << 11 << 10;
    QTest::newRow("thirteen-low") << 12 << 13;
    QTest::newRow("thirteen-high") << 14 << 13;
    QTest::newRow("sixteen") << 15 << 16;
  }

  void msk144ReportNormalization()
  {
    QFETCH(int, input);
    QFETCH(int, expected);
    auto snapshot = baseSnapshot();
    snapshot.mode = "MSK144";
    snapshot.shortMessages = true;
    DecodedText const message {
      QString {"060522 %1  0.3 1500 &  K1ABC W1AW -10"}.arg(input, 3)};
    auto const plan = DecodedMessageReaction::planProcessMessage(
      message, snapshot);
    QVERIFY2(plan.disposition == DecodedMessageReaction::ReactionDisposition::Reacted,
             qPrintable(plan.reason));
    QCOMPARE(intEffect(plan, Effect::Kind::SetReport), expected);
  }

  void selectionOrigins_data()
  {
    QTest::addColumn<int>("origin");
    QTest::addColumn<bool>("transmittingSignoff");
    QTest::addColumn<bool>("reacts");
    QTest::newRow("manual") << int(DecodedMessageReaction::SelectionOrigin::Manual) << true << true;
    QTest::newRow("synthetic") << int(DecodedMessageReaction::SelectionOrigin::Synthetic) << true << true;
    QTest::newRow("udp-guard") << int(DecodedMessageReaction::SelectionOrigin::Udp) << true << false;
  }

  void selectionOrigins()
  {
    QFETCH(int, origin);
    QFETCH(bool, transmittingSignoff);
    QFETCH(bool, reacts);
    auto snapshot = baseSnapshot();
    snapshot.doubleClicked = true;
    snapshot.selectionOrigin = static_cast<DecodedMessageReaction::SelectionOrigin>(origin);
    snapshot.transmittingSignoff = transmittingSignoff;
    auto const plan = DecodedMessageReaction::planProcessMessage(
      decode("K1ABC W1AW RR73"), snapshot);
    QCOMPARE(plan.disposition == DecodedMessageReaction::ReactionDisposition::Reacted, reacts);
  }

  void slowWaitReplyOrderingAndTimeout()
  {
    auto snapshot = baseSnapshot();
    snapshot.waitFeaturesEnabled = true;
    auto const plan = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW -10"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::SlowDecoder);
    int const watchdog = effectIndex(plan, Effect::Kind::ResetWatchdog);
    int const selection = effectIndex(plan, Effect::Kind::SetDoubleClicked, watchdog + 1);
    int const process = effectIndex(plan, Effect::Kind::ProcessSyntheticMessageNow, selection + 1);
    int const enableAuto = effectIndex(plan, Effect::Kind::SetAutoEnabled, process + 1);
    int const timer = effectIndex(plan, Effect::Kind::StartWaitReplyTimer, enableAuto + 1);
    QVERIFY(watchdog >= 0 && selection > watchdog && process > selection
            && enableAuto > process && timer > enableAuto);
    QCOMPARE(plan.effects[timer].intValue, 120000);
  }

  void slowWaitReplyAndCallCanBothFire()
  {
    auto snapshot = baseSnapshot();
    snapshot.waitFeaturesEnabled = true;
    snapshot.waitAndCall = true;
    snapshot.autoSequenceChecked = true;
    auto const plan = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW CQ W1AW"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::SlowDecoder);
    QCOMPARE(effectCount(plan, Effect::Kind::ProcessSyntheticMessageNow), 2);
    int const replySelection = effectIndex(plan, Effect::Kind::SetDoubleClicked);
    int const replyProcess = effectIndex(
      plan, Effect::Kind::ProcessSyntheticMessageNow, replySelection + 1);
    int const replyAuto = effectIndex(plan, Effect::Kind::SetAutoEnabled, replyProcess + 1);
    int const callSelection = effectIndex(plan, Effect::Kind::SetDoubleClicked, replyAuto + 1);
    int const callProcess = effectIndex(
      plan, Effect::Kind::ProcessSyntheticMessageNow, callSelection + 1);
    int const callAuto = effectIndex(plan, Effect::Kind::SetAutoEnabled, callProcess + 1);
    QCOMPARE(replyProcess, replySelection + 1);
    QCOMPARE(replyAuto, replyProcess + 1);
    QCOMPARE(callProcess, callSelection + 1);
    QCOMPARE(callAuto, callProcess + 1);
    QCOMPARE(intEffect(plan, Effect::Kind::StartWaitCallTimer), 93000);
  }

  void slowHoundWrongSlotIgnoresDecode()
  {
    auto snapshot = baseSnapshot();
    snapshot.specOp = SpecialOperatingActivity::HOUND;
    snapshot.waitFeaturesEnabled = true;
    auto const plan = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW -10", "~", "060515"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::SlowDecoder);
    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::IgnoreDecode);
  }

  void msk144WaitPolicies()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "MSK144";
    snapshot.waitFeaturesEnabled = true;
    auto reply = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW -10", "&", "060522"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
    QCOMPARE(intEffect(reply, Effect::Kind::StartWaitReplyTimer), 180000);

    snapshot.specOp = SpecialOperatingActivity::HOUND;
    auto houndReply = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW -10", "&", "060515"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
    QCOMPARE(houndReply.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
    snapshot.specOp = SpecialOperatingActivity::NONE;

    snapshot.fullDuplexEnabled = true;
    snapshot.txing = true;
    auto blocked = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW -10", "&", "060522"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
    QVERIFY(effectIndex(blocked, Effect::Kind::ProcessSyntheticMessageNow) < 0);

    snapshot.fullDuplexEnabled = false;
    snapshot.txing = false;
    snapshot.waitAndCall = true;
    snapshot.waitAndCallControlChecked = true;
    snapshot.autoSequenceChecked = true;
    auto cancelled = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW CQ W1AW", "&", "060522"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
    QVERIFY(effectIndex(cancelled, Effect::Kind::StopWaitCallTimer) >= 0);
    QVERIFY(effectIndex(cancelled, Effect::Kind::DisableWaitAndCallControl) >= 0);
    QVERIFY(effectIndex(cancelled, Effect::Kind::StartWaitCallTimer) < 0);
  }

  void msk144WaitPolicyRequiresFastDecoderSource()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "MSK144";
    snapshot.waitFeaturesEnabled = true;
    snapshot.waitAndCall = true;
    snapshot.waitAndCallControlChecked = true;
    snapshot.autoSequenceChecked = true;
    auto const message = decode("K1ABC W1AW CQ W1AW", "&", "060522");

    auto const slowPlan = DecodedMessageReaction::planWaitReplyCall(
      message, snapshot, DecodedMessageReaction::WaitDecodeSource::SlowDecoder);
    QCOMPARE(slowPlan.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
    QVERIFY(!hasEffect(slowPlan, Effect::Kind::StopWaitCallTimer));
    QVERIFY(!hasEffect(slowPlan, Effect::Kind::DisableWaitAndCallControl));
    QVERIFY(!hasEffect(slowPlan, Effect::Kind::ProcessSyntheticMessageNow));
    QVERIFY(!hasEffect(slowPlan, Effect::Kind::SetAutoEnabled));
    QVERIFY(!hasEffect(slowPlan, Effect::Kind::StartWaitReplyTimer));
    QVERIFY(!hasEffect(slowPlan, Effect::Kind::StartWaitCallTimer));

    auto const fastPlan = DecodedMessageReaction::planWaitReplyCall(
      message, snapshot, DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
    QVERIFY(hasEffect(fastPlan, Effect::Kind::StopWaitCallTimer));
    QVERIFY(hasEffect(fastPlan, Effect::Kind::DisableWaitAndCallControl));
    QVERIFY(hasEffect(fastPlan, Effect::Kind::ProcessSyntheticMessageNow));
    QVERIFY(hasEffect(fastPlan, Effect::Kind::SetAutoEnabled));
    QCOMPARE(intEffect(fastPlan, Effect::Kind::StartWaitReplyTimer), 180000);
    QVERIFY(!hasEffect(fastPlan, Effect::Kind::StartWaitCallTimer));
  }

  void nonDirectWaitAndCallBlocksOnlySlowRightDisplay()
  {
    auto slowSnapshot = baseSnapshot();
    slowSnapshot.waitFeaturesEnabled = true;
    slowSnapshot.waitAndCall = true;
    slowSnapshot.autoSequenceChecked = true;
    auto const slowPlan = DecodedMessageReaction::planWaitReplyCall(
      decode("CQ W1AW FN31"), slowSnapshot,
      DecodedMessageReaction::WaitDecodeSource::SlowDecoder);
    QVERIFY(hasEffect(slowPlan, Effect::Kind::SetBlockRightDisplay));

    auto fastSnapshot = slowSnapshot;
    fastSnapshot.mode = "MSK144";
    auto const fastPlan = DecodedMessageReaction::planWaitReplyCall(
      decode("CQ W1AW FN31", "&", "060522"), fastSnapshot,
      DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
    QVERIFY(hasEffect(fastPlan, Effect::Kind::ProcessSyntheticMessageNow));
    QVERIFY(!hasEffect(fastPlan, Effect::Kind::SetBlockRightDisplay));
    QCOMPARE(intEffect(fastPlan, Effect::Kind::StartWaitCallTimer), 93000);
  }

  void ncccSprintAvoidsDuplicateLogging()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "FT4";
    snapshot.specOp = SpecialOperatingActivity::NA_VHF;
    snapshot.ncccSprint = true;
    snapshot.loggingEnabled = true;
    auto const plan = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW R FN31", "+"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::SlowDecoder);
    QCOMPARE(effectCount(plan, Effect::Kind::RequestLogQso), 1);
    QCOMPARE(effectCount(plan, Effect::Kind::RequestLogQsoUnlessSuppressed), 1);
    QCOMPARE(intEffect(plan, Effect::Kind::ScheduleStopTx), 500);
    QVERIFY(effectIndex(plan, Effect::Kind::ProcessSyntheticMessageNow) >= 0);
  }

  void ncccReplyDefersLoggingSuppressionCheck()
  {
    auto snapshot = baseSnapshot();
    snapshot.mode = "FT4";
    snapshot.specOp = SpecialOperatingActivity::NA_VHF;
    snapshot.ncccSprint = true;
    snapshot.loggingEnabled = true;
    auto const plan = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW -10", "+"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::SlowDecoder);

    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
    QCOMPARE(effectCount(plan, Effect::Kind::RequestLogQso), 0);
    QCOMPARE(effectCount(plan, Effect::Kind::RequestLogQsoUnlessSuppressed), 1);
    QVERIFY(effectIndex(plan, Effect::Kind::ProcessSyntheticMessageNow)
            < effectIndex(plan, Effect::Kind::RequestLogQsoUnlessSuppressed));

    snapshot.loggingEnabled = false;
    auto const loggingDisabled = DecodedMessageReaction::planWaitReplyCall(
      decode("K1ABC W1AW -10", "+"), snapshot,
      DecodedMessageReaction::WaitDecodeSource::SlowDecoder);
    QCOMPARE(effectCount(loggingDisabled, Effect::Kind::RequestLogQsoUnlessSuppressed), 1);
  }

  void representativeProcessEffectOrdering()
  {
    auto snapshot = baseSnapshot();
    snapshot.qsoProgress = QsoProgress::Rogers;
    snapshot.loggingEnabled = true;
    auto const plan = DecodedMessageReaction::planProcessMessage(
      decode("K1ABC W1AW 73"), snapshot);
    int const log = effectIndex(plan, Effect::Kind::RequestLogQso);
    int const select = effectIndex(plan, Effect::Kind::SetTxMessageIndex, log + 1);
    int const check = effectIndex(plan, Effect::Kind::CheckTxMessage, select + 1);
    int const progress = effectIndex(plan, Effect::Kind::SetQsoProgress, check + 1);
    int const lookup = effectIndex(plan, Effect::Kind::Lookup, progress + 1);
    int const generate = effectIndex(plan, Effect::Kind::GenerateStandardMessages, lookup + 1);
    QVERIFY(log >= 0 && select > log && check > select && progress > check
            && lookup > progress && generate > lookup);
    QCOMPARE(effectCount(plan, Effect::Kind::RefreshQsoPaneIfChanged), 1);
  }

  void earlyTxDecodeEffectsPrecedeRejection()
  {
    auto snapshot = baseSnapshot();
    DecodedText message {"0605  Tx      1259 #  CQ K1ABC FN42"};
    auto const plan = DecodedMessageReaction::planProcessMessage(message, snapshot);
    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(plan.effects.size(), 1);
    QCOMPARE(plan.effects.first().kind, Effect::Kind::SetRxFrequency);
  }
};

QTEST_MAIN(TestQsoReaction)
#include "test_qso_reaction.moc"
