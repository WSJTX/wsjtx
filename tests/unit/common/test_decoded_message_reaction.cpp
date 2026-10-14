#include <QtTest>

#include "QsoReactionTestSupport.hpp"
#include "AutoRespondPeriod.hpp"

namespace
{
  using namespace QsoReactionTestSupport;

  Snapshot baseContext()
  {
    auto context = neutralStationSnapshot();
    context.mode = "FT8";
    context.tx1Enabled = true;
    context.nominalQsyAllowed = true;
    return context;
  }

  QString legacyDecode(QString const& mode, QString const& payload,
                       QString const& flags, bool lowConfidence)
  {
    auto field = payload.leftJustified(22, ' ', true);
    if (lowConfidence) field[21] = '?';

    auto line = QString {"0605 -10  0.3 0815 "} + mode + "  " + field;
    if (!flags.isEmpty()) line += " " + flags;
    return line;
  }

  bool isFastCqQsyEffect(Effect::Kind kind)
  {
    return kind == Effect::Kind::ApplyFastCqQsy
      || kind == Effect::Kind::RejectNominalQsy;
  }
}

class TestDecodedMessageReaction final
  : public QObject
{
  Q_OBJECT

private slots:
  void classifiesLegacy72Messages_data()
  {
    QTest::addColumn<QString>("mode");
    QTest::addColumn<QString>("payload");
    QTest::addColumn<QString>("flags");
    QTest::addColumn<bool>("lowConfidence");
    QTest::addColumn<bool>("expected");

    QTest::newRow("jt4-type1-prefix")
      << "$" << "1A/KA1ABC WB9XYZ" << "f" << false << true;
    QTest::newRow("jt9-type1-suffix")
      << "@" << "KA1ABC WB9XYZ/A" << "f1" << false << true;
    QTest::newRow("jt9-short-standard")
      << "@" << "A1 B1" << "" << false << true;
    QTest::newRow("jt65-ap-type1-prefix")
      << "#" << "1A/KA1ABC WB9XYZ" << "a1" << false << true;
    QTest::newRow("jt65-standard")
      << "#" << "KA1ABC WB9XYZ R-22" << "d*2" << false << true;
    QTest::newRow("jt4-type2-prefix")
      << "$" << "CQ ZL4/KA1ABC" << "f" << false << true;
    QTest::newRow("jt65-type2-suffix")
      << "#" << "CQ WB9XYZ/VE4" << "a12" << false << true;
    QTest::newRow("jt4-deep-low-confidence")
      << "$" << "KA1ABC WB9XYZ R-22" << "d1" << true << true;
    QTest::newRow("question-without-deep-flag")
      << "$" << "KA1ABC WB9XYZ R-22" << "" << true << false;
    QTest::newRow("jt65-ooo")
      << "#" << "KA1ABC WB9XYZ EN34 OOO" << "" << false << true;
    QTest::newRow("jt65-short-ooo")
      << "#" << "A1 B1 OOO" << "" << false << true;
    QTest::newRow("jt9-ooo-is-not-sideband")
      << "@" << "KA1ABC WB9XYZ EN34 OOO" << "" << false << false;
    QTest::newRow("trailing-structured-field")
      << "#" << "A1A B1B 73 X" << "d1" << false << false;
    QTest::newRow("blank")
      << "@" << "" << "" << false << false;
    QTest::newRow("single-token-call")
      << "@" << "A1AAA" << "" << false << false;
    QTest::newRow("short-token")
      << "@" << "1B" << "" << false << false;
    QTest::newRow("single-digit")
      << "@" << "0" << "" << false << false;
    QTest::newRow("leading-space-short-token")
      << "@" << " A1" << "" << false << false;
    QTest::newRow("leading-space-d-token")
      << "@" << " D12" << "" << false << false;
  }

  void classifiesLegacy72Messages()
  {
    QFETCH(QString, mode);
    QFETCH(QString, payload);
    QFETCH(QString, flags);
    QFETCH(bool, lowConfidence);
    QFETCH(bool, expected);

    DecodedText message {legacyDecode(mode, payload, flags, lowConfidence)};

    QCOMPARE(message.isStandardMessage(), expected);
  }

  void automaticProcessingDoesNotChangePartners_data()
  {
    QTest::addColumn<int>("origin");
    QTest::addColumn<bool>("manualSelection");
    QTest::addColumn<bool>("callingCq");
    QTest::addColumn<bool>("changesPartner");
    using Origin = DecodedMessageReaction::SelectionOrigin;
    QTest::newRow("automatic") << int(Origin::None) << false << false << false;
    QTest::newRow("synthetic") << int(Origin::Synthetic) << false << false << false;
    QTest::newRow("synthetic-after-double-click") << int(Origin::Synthetic) << true << false << false;
    QTest::newRow("automatic-station-selection") << int(Origin::AutomaticSelection) << true << false << true;
    QTest::newRow("manual") << int(Origin::ManualLeftPane) << true << false << true;
    QTest::newRow("cq-auto-reply") << int(Origin::Synthetic) << false << true << true;
  }

  void automaticProcessingDoesNotChangePartners()
  {
    QFETCH(int, origin);
    QFETCH(bool, manualSelection);
    QFETCH(bool, callingCq);
    QFETCH(bool, changesPartner);
    auto context = baseContext();
    context.qsoProgress = callingCq ? QsoProgress::Calling : QsoProgress::Report;
    context.selectionOrigin = static_cast<DecodedMessageReaction::SelectionOrigin>(origin);
    context.doubleClicked = manualSelection;
    context.callingCq = callingCq;
    context.autoReply = callingCq;
    auto const message = decode("K1ABC W1AWX -10");
    QVERIFY(message.isStandardMessage());
    auto const plan = DecodedMessageReaction::planProcessMessage(message, context);

    QCOMPARE(hasEffect(plan, Effect::Kind::SetDxCall), changesPartner);
    QCOMPARE(hasEffect(plan, Effect::Kind::ClearDxGrid), changesPartner);
    QCOMPARE(hasEffect(plan, Effect::Kind::GenerateStandardMessages), changesPartner);
    if (changesPartner) {
      QCOMPARE(textEffect(plan, Effect::Kind::SetDxCall), QString {"W1AWX"});
    } else {
      QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
      QVERIFY(!hasEffect(plan, Effect::Kind::SetQsoProgress));
    }
  }

  void udpDirectedRepliesRespectPartnerSelection_data()
  {
    QTest::addColumn<QString>("previousPartner");
    QTest::addColumn<bool>("euVhf");
    QTest::addColumn<bool>("holdTxFrequency");
    QTest::addColumn<bool>("autoReply");
    QTest::addColumn<int>("expectedMessage");
    QTest::newRow("eu-vhf-without-partner") << "" << true << false << false << 3;
    QTest::newRow("roger-report-without-partner") << "" << false << false << false << 4;
    QTest::newRow("eu-vhf-current-partner") << "G4ABC" << true << false << true << 3;
    QTest::newRow("roger-report-current-partner") << "G4ABC" << false << false << true << 4;
    QTest::newRow("eu-vhf-other-partner-rejected") << "W1AW" << true << false << false << -1;
    QTest::newRow("roger-report-other-partner-fallback") << "W1AW" << false << false << false << 2;
    QTest::newRow("roger-report-other-partner-auto-reply") << "W1AW" << false << false << true << -1;
    QTest::newRow("eu-vhf-hold-frequency-selects") << "W1AW" << true << true << false << 3;
    QTest::newRow("roger-report-hold-frequency-selects") << "W1AW" << false << true << true << 4;
  }

  void udpDirectedRepliesRespectPartnerSelection()
  {
    QFETCH(QString, previousPartner);
    QFETCH(bool, euVhf);
    QFETCH(bool, holdTxFrequency);
    QFETCH(bool, autoReply);
    QFETCH(int, expectedMessage);
    auto context = baseContext();
    context.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Udp;
    context.dxCall = previousPartner;
    context.hisCall = previousPartner;
    context.doubleClicked = holdTxFrequency;
    context.holdTxFrequency = holdTxFrequency;
    context.autoReply = autoReply;
    context.qsoProgress = euVhf ? QsoProgress::Calling : QsoProgress::Report;
    if (euVhf) context.specOp = SpecialOperatingActivity::EU_VHF;
    auto const message = decode(euVhf
      ? "<K1ABC> <G4ABC> 570001 IO91NP" : "K1ABC G4ABC R-10");
    QVERIFY(message.isStandardMessage());

    auto const plan = DecodedMessageReaction::planProcessMessage(message, context);

    QCOMPARE(intEffect(plan, Effect::Kind::SetTxMessage,
                       intEffect(plan, Effect::Kind::SetTxMessageIndex)), expectedMessage);
    if (expectedMessage < 0) {
      QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::NoReaction);
      QVERIFY(!hasEffect(plan, Effect::Kind::SetDxCall));
      QVERIFY(!hasEffect(plan, Effect::Kind::SetQsoProgress));
      QVERIFY(!hasEffect(plan, Effect::Kind::DisarmAutoRespondSelection));
      return;
    }
    QCOMPARE(plan.disposition, DecodedMessageReaction::ReactionDisposition::Reacted);
    QCOMPARE(textEffect(plan, Effect::Kind::SetDxCall),
             previousPartner == "G4ABC" ? QString {} : QString {"G4ABC"});
    int const progress = effectIndex(plan, Effect::Kind::SetQsoProgress);
    QVERIFY(progress >= 0);
    QCOMPARE(plan.effects.at(progress).progress, expectedMessage == 2 ? QsoProgress::Report
      : expectedMessage == 3 ? QsoProgress::RogerReport : QsoProgress::Rogers);
    QVERIFY(hasEffect(plan, Effect::Kind::DisarmAutoRespondSelection));
  }

  void rejectsDecodeWithTooFewFields()
  {
    auto context = baseContext();
    DecodedText message {"CQ K1ABC FN42"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(decision.reason, QString {"decode has too few fields"});
  }

  void acceptedExplicitSelectionsCancelAutoRespond_data()
  {
    QTest::addColumn<int>("origin");
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("mode");
    QTest::addColumn<bool>("doubleClicked");
    QTest::addColumn<bool>("blockedFastCq");
    QTest::addColumn<bool>("transmittingSignoff");
    QTest::addColumn<bool>("reacted");
    QTest::addColumn<bool>("disarms");
    using Origin = DecodedMessageReaction::SelectionOrigin;
    QString const caller {"0605 -10  0.3 1500 ~  K1ABC W9XYZ FN31"};
    QTest::newRow("left-pane-selection")
      << int(Origin::ManualLeftPane) << caller << "FT8"
      << true << false << false << true << true;
    QTest::newRow("right-pane-selection")
      << int(Origin::ManualRightPane) << caller << "FT8"
      << true << false << false << true << true;
    QTest::newRow("udp-selection")
      << int(Origin::Udp) << caller << "FT8"
      << true << false << false << true << true;
    QTest::newRow("automatic-caller-with-double-click-privileges")
      << int(Origin::AutomaticSelection) << caller << "FT8"
      << true << false << false << true << false;
    QTest::newRow("ordinary-automatic-caller")
      << int(Origin::None) << caller << "FT8"
      << false << false << false << true << false;
    QTest::newRow("transmitted-line-frequency-pick")
      << int(Origin::ManualLeftPane) << "0605  Tx      1259 ~  CQ K1ABC FN42" << "FT8"
      << true << false << false << false << false;
    QTest::newRow("malformed-line")
      << int(Origin::ManualLeftPane) << "CQ K1ABC FN42" << "FT8"
      << true << false << false << false << false;
    QTest::newRow("mismatched-mode")
      << int(Origin::ManualLeftPane) << "0605 -10  0.3 1500 @  K1ABC W9XYZ FN31" << "JT65"
      << true << false << false << false << false;
    QTest::newRow("blocked-fast-cq")
      << int(Origin::ManualLeftPane) << "060522 -10  0.3 1500 &  CQ 074 W1AW FN31" << "MSK144"
      << true << true << false << false << false;
    QTest::newRow("ignored-udp-signoff")
      << int(Origin::Udp) << "0605 -10  0.3 1500 ~  K1ABC W1AW 73" << "FT8"
      << true << false << true << false << false;
  }

  void acceptedExplicitSelectionsCancelAutoRespond()
  {
    using Origin = DecodedMessageReaction::SelectionOrigin;
    QFETCH(int, origin);
    QFETCH(QString, line);
    QFETCH(QString, mode);
    QFETCH(bool, doubleClicked);
    QFETCH(bool, blockedFastCq);
    QFETCH(bool, transmittingSignoff);
    QFETCH(bool, reacted);
    QFETCH(bool, disarms);
    auto context = baseContext();
    context.mode = mode;
    context.selectionOrigin = static_cast<DecodedMessageReaction::SelectionOrigin>(origin);
    context.doubleClicked = doubleClicked;
    context.callingCq = context.autoReply = true;
    context.qsoProgress = QsoProgress::Calling;
    context.selectedTxMessage = 6;
    context.fastMode = context.transceiverOnline = blockedFastCq;
    context.nominalQsyAllowed = !blockedFastCq;
    context.transmittingSignoff = transmittingSignoff;

    auto const plan = DecodedMessageReaction::planProcessMessage(DecodedText {line}, context);

    QCOMPARE(plan.disposition == DecodedMessageReaction::ReactionDisposition::Reacted, reacted);
    QCOMPARE(hasEffect(plan, Effect::Kind::DisarmAutoRespondSelection), disarms);
    auto const receivePeriod = QDateTime::fromMSecsSinceEpoch(15000, Qt::UTC);
    for (auto policy : {AutoRespondPolicy::First, AutoRespondPolicy::MaxSignal,
                        AutoRespondPolicy::MinSignal, AutoRespondPolicy::MaxDistance}) {
      AutoRespondPeriodState state;
      state.setEnableTx(true);
      QVERIFY(state.observePeriod(receivePeriod, true, true, policy));
      if (context.selectionOrigin == Origin::AutomaticSelection && policy == AutoRespondPolicy::First) {
        QVERIFY(state.claimFirst(receivePeriod));
      }
      for (auto const& effect : plan.effects) {
        if (effect.kind == Effect::Kind::DisarmAutoRespondSelection) state.disarm();
      }
      state.observePeriod(receivePeriod.addSecs(15), false, false, policy);
      QCOMPARE(state.accepts(receivePeriod), !disarms);
      if (policy == AutoRespondPolicy::First) {
        QCOMPARE(state.claimFirst(receivePeriod),
                 !disarms && context.selectionOrigin != Origin::AutomaticSelection);
      }
    }
  }

  void rejectsMismatchedModeMarker()
  {
    auto context = baseContext();
    context.mode = "JT65";
    DecodedText message {"0605 -10  0.3 0815 @  K1ABC W1AW -10"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(decision.reason, QString {"decode mode marker does not match current mode"});
  }

  void txDecodeOnlyAdjustsRxFrequencyByDefault()
  {
    auto context = baseContext();
    DecodedText message {"0605  Tx      1259 #  CQ K1ABC FN42"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(intEffect(decision, Effect::Kind::SetRxFrequency), 1259);
    QVERIFY(!hasEffect(decision, Effect::Kind::SetTxFrequency));
  }

  void txDecodeWithCtrlAlsoAdjustsTxFrequencyWhenNotHeld()
  {
    auto context = baseContext();
    context.modifiers.ctrl = true;
    DecodedText message {"0605  Tx      1259 #  CQ K1ABC FN42"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(intEffect(decision, Effect::Kind::SetRxFrequency), 1259);
    QCOMPARE(intEffect(decision, Effect::Kind::SetTxFrequency), 1259);
  }

  void fastCqWithListeningFrequencyRequestsQsy()
  {
    auto context = baseContext();
    context.mode = "MSK144";
    context.fastMode = true;
    context.transceiverOnline = true;
    context.nominalFrequency = 50260000;
    DecodedText message {"060522 -10  0.3 0815 &  CQ 260 W1AW FN31"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    bool foundQsy = false;
    for (auto const& action : decision.effects) {
      if (action.kind == DecodedMessageReaction::QsoReactionEffect::Kind::ApplyFastCqQsy
          && action.frequency == 50260000
          && action.text == "QSY  50.260"
          && action.boolValue
          && !action.userInitiated) foundQsy = true;
    }

    QVERIFY(foundQsy);
  }

  void blockedFastCqSuppressesOnlyNominalQsyEffects()
  {
    auto allowedContext = baseContext();
    allowedContext.mode = "MSK144";
    allowedContext.fastMode = true;
    allowedContext.transceiverOnline = true;
    allowedContext.nominalFrequency = 50260000;
    auto blockedContext = allowedContext;
    blockedContext.nominalQsyAllowed = false;
    DecodedText message {"060522 -10  0.3 0815 &  CQ 260 W1AW FN31"};

    auto const allowed = DecodedMessageReaction::planProcessMessage(message, allowedContext);
    auto const blocked = DecodedMessageReaction::planProcessMessage(message, blockedContext);

    QCOMPARE(effectCount(allowed, Effect::Kind::ApplyFastCqQsy), 1);
    QCOMPARE(effectCount(blocked, Effect::Kind::ApplyFastCqQsy), 0);
    QVERIFY(blocked.disposition == allowed.disposition);
    QCOMPARE(blocked.reason, allowed.reason);

    QVector<Effect const *> allowedUnrelated;
    for (auto const& effect : allowed.effects) {
      if (!isFastCqQsyEffect(effect.kind)) allowedUnrelated.append(&effect);
    }
    QCOMPARE(blocked.effects.size(), allowedUnrelated.size());
    for (int i = 0; i < blocked.effects.size(); ++i) {
      auto const& actual = blocked.effects.at(i);
      auto const& expected = *allowedUnrelated.at(i);
      QVERIFY(actual.kind == expected.kind);
      QCOMPARE(actual.intValue, expected.intValue);
      QCOMPARE(actual.frequency, expected.frequency);
      QCOMPARE(actual.text, expected.text);
      QCOMPARE(actual.boolValue, expected.boolValue);
      QCOMPARE(actual.userInitiated, expected.userInitiated);
      QVERIFY(actual.progress == expected.progress);
      QVERIFY(actual.contestHint == expected.contestHint);
    }
  }

  void blockedManualFastCqRejectsBeforeStartingQso()
  {
    auto context = baseContext();
    context.mode = "MSK144";
    context.fastMode = true;
    context.transceiverOnline = true;
    context.nominalFrequency = 50260000;
    context.nominalQsyAllowed = false;
    context.selectionOrigin = DecodedMessageReaction::SelectionOrigin::ManualLeftPane;
    DecodedText message {"060522 -10  0.3 0815 &  CQ 260 W1AW FN31"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QCOMPARE(decision.effects.size(), 1);
    QVERIFY(decision.effects.first().kind == Effect::Kind::RejectNominalQsy);
    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(decision.reason, QString {"fast CQ requires a blocked nominal QSY"});
  }

  void fastCqQsyPermissionDefaultsClosed()
  {
    auto context = neutralStationSnapshot();
    context.mode = "MSK144";
    context.fastMode = true;
    context.transceiverOnline = true;
    context.nominalFrequency = 50260000;
    DecodedText message {"060522 -10  0.3 0815 &  CQ 260 W1AW FN31"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QCOMPARE(effectCount(decision, Effect::Kind::ApplyFastCqQsy), 0);
  }

  void doubleClickDoesNotStartQsoWithOwnBaseCall()
  {
    auto context = baseContext();
    context.doubleClicked = true;
    DecodedText message {"0605 -10  0.3 0815 ~  W1AW K1ABC FN42"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(decision.reason, QString {"double-click would start QSO with own base call"});
  }

  void udpReplyIsIgnoredWhileTransmittingOur73()
  {
    auto context = baseContext();
    context.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Udp;
    context.transmittingSignoff = true;
    DecodedText message {"0605 -10  0.3 0815 ~  K1ABC W1AW RR73"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(decision.reason, QString {"udp reply ignored while transmitting 73"});
  }

  void udpInvalidCqReplyIsRejectedWhileTransmittingOur73()
  {
    auto context = baseContext();
    context.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Udp;
    context.transmittingSignoff = true;
    DecodedText message {"0605 -10  0.3 0815 ~  CQ N0CALL FN31"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(decision.reason, QString {"non-standard free text is not processable"});
  }

  void udpDifferentSignoffIsHonoredWhileTransmittingOur73()
  {
    auto context = baseContext();
    context.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Udp;
    context.transmittingSignoff = true;
    DecodedText message {"0605 -10  0.3 0815 ~  K1ABC N0CALL RR73"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::Reacted);
  }

  void manualDoubleClickStillHonoredWhileTransmittingOur73()
  {
    // Answering the partner's RR73 with a 73 by double-clicking it is an
    // explicit operator override and must keep working even mid-transmission.
    auto context = baseContext();
    context.doubleClicked = true;
    context.selectionOrigin = DecodedMessageReaction::SelectionOrigin::ManualLeftPane;
    context.transmittingSignoff = true;
    DecodedText message {"0605 -10  0.3 0815 ~  K1ABC W1AW RR73"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::Reacted);
  }

  void udpReplyHonoredWhenNotSigningOff()
  {
    auto context = baseContext();
    context.doubleClicked = true;
    context.selectionOrigin = DecodedMessageReaction::SelectionOrigin::Udp;
    context.transmittingSignoff = false;
    DecodedText message {"0605 -10  0.3 0815 ~  CQ W1AW FN31"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::Reacted);
  }

  void houndIgnoresOtherHoundNegativeReport()
  {
    auto context = baseContext();
    context.specOp = SpecialOperatingActivity::HOUND;
    DecodedText message {"0605 -10  0.3 0815 ~  K1JT W10AAA R-10"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(decision.reason, QString {"hound ignores other hounds"});
  }

  void houndIgnoresOtherHoundPositiveReport()
  {
    auto context = baseContext();
    context.specOp = SpecialOperatingActivity::HOUND;
    DecodedText message {"0605 -10  0.3 0815 ~  K1JT W10AAA R+03"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::NoReaction);
    QCOMPARE(decision.reason, QString {"hound ignores other hounds"});
  }

  void suffixedCallKeepsPriorQsoPartnerSnapshot()
  {
    auto context = baseContext();
    context.dxCall = "N0CALL";
    DecodedText message {"0605 -10  0.3 0815 ~  K1ABC W1AW/R -10"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::Reacted);
    int clearGrid = -1;
    int setCall = -1;
    for (int i = 0; i < decision.effects.size(); ++i) {
      if (decision.effects[i].kind == DecodedMessageReaction::QsoReactionEffect::Kind::ClearDxGrid) {
        clearGrid = i;
      }
      if (decision.effects[i].kind == DecodedMessageReaction::QsoReactionEffect::Kind::SetDxCall
          && decision.effects[i].text == "W1AW/R") {
        setCall = i;
      }
    }
    QVERIFY(clearGrid >= 0);
    QVERIFY(setCall > clearGrid);
  }

  void maxSignalSignoffKeepsSelectedCall()
  {
    auto context = baseContext();
    context.respondPolicy = AutoRespondPolicy::MaxSignal;
    DecodedText message {"0605 -10  0.3 0815 ~  K1ABC W1AW/R 73"};

    auto const decision = DecodedMessageReaction::planProcessMessage(message, context);

    QVERIFY(decision.disposition == DecodedMessageReaction::ReactionDisposition::Reacted);
    QVERIFY(!hasEffect(decision, Effect::Kind::SetDxCall));
  }
};

QTEST_MAIN(TestDecodedMessageReaction)
#include "test_decoded_message_reaction.moc"
