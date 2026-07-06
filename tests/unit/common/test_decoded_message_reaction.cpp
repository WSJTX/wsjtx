#include <QtTest>

#include "DecodedMessageReaction.hpp"
#include "Decoder/decodedtext.h"

class TestDecodedMessageReaction final
  : public QObject
{
  Q_OBJECT

private:
  DecodedMessageReaction::ProcessMessageContext baseContext() const
  {
    DecodedMessageReaction::ProcessMessageContext context;
    context.mode = "FT8";
    context.myCall = "K1ABC";
    context.baseCall = "K1ABC";
    context.dxCall = "W1AW";
    context.hisCall = "W1AW";
    context.trPeriod = 15.0;
    context.nominalFrequency = 14074000;
    context.rxFrequency = 1500;
    context.txFrequency = 1500;
    context.tx1Enabled = true;
    return context;
  }

  static bool hasIntAction(QVector<DecodedMessageReaction::ProcessMessageAction> const& actions,
                           DecodedMessageReaction::ProcessMessageAction::Kind kind, int value)
  {
    for (auto const& action : actions) {
      if (action.kind == kind && action.intValue == value) return true;
    }
    return false;
  }

private slots:
  void rejectsDecodeWithTooFewFields()
  {
    auto context = baseContext();
    DecodedText message {"CQ K1ABC FN42"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(!decision.continueProcessing);
    QCOMPARE(decision.reason, QString {"decode has too few fields"});
  }

  void rejectsMismatchedModeMarker()
  {
    auto context = baseContext();
    context.mode = "JT65";
    DecodedText message {"0605 -10  0.3 0815 @  K1ABC W1AW -10"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(!decision.continueProcessing);
    QCOMPARE(decision.reason, QString {"decode mode marker does not match current mode"});
  }

  void txDecodeOnlyAdjustsRxFrequencyByDefault()
  {
    auto context = baseContext();
    DecodedText message {"0605  Tx      1259 #  CQ K1ABC FN42"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(!decision.continueProcessing);
    QVERIFY(hasIntAction(decision.actions,
                         DecodedMessageReaction::ProcessMessageAction::Kind::SetRxFrequency, 1259));
    QVERIFY(!hasIntAction(decision.actions,
                          DecodedMessageReaction::ProcessMessageAction::Kind::SetTxFrequency, 1259));
  }

  void txDecodeWithCtrlAlsoAdjustsTxFrequencyWhenNotHeld()
  {
    auto context = baseContext();
    context.modifiers.ctrl = true;
    DecodedText message {"0605  Tx      1259 #  CQ K1ABC FN42"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(!decision.continueProcessing);
    QVERIFY(hasIntAction(decision.actions,
                         DecodedMessageReaction::ProcessMessageAction::Kind::SetRxFrequency, 1259));
    QVERIFY(hasIntAction(decision.actions,
                         DecodedMessageReaction::ProcessMessageAction::Kind::SetTxFrequency, 1259));
  }

  void fastCqWithListeningFrequencyRequestsQsy()
  {
    auto context = baseContext();
    context.mode = "MSK144";
    context.fastMode = true;
    context.transceiverOnline = true;
    context.nominalFrequency = 50260000;
    DecodedText message {"060522 -10  0.3 0815 &  CQ 260 W1AW FN31"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    bool foundRigFrequency = false;
    bool foundDisplayQsy = false;
    bool foundMskBaseFrequency = false;
    for (auto const& action : decision.actions) {
      if (action.kind == DecodedMessageReaction::ProcessMessageAction::Kind::SetRigFrequency
          && action.frequency == 50260000) foundRigFrequency = true;
      if (action.kind == DecodedMessageReaction::ProcessMessageAction::Kind::DisplayQsy
          && action.text == "QSY  50.260") foundDisplayQsy = true;
      if (action.kind == DecodedMessageReaction::ProcessMessageAction::Kind::SetMsk144BaseFrequency
          && action.frequency == 50260000) foundMskBaseFrequency = true;
    }

    QVERIFY(foundRigFrequency);
    QVERIFY(foundDisplayQsy);
    QVERIFY(foundMskBaseFrequency);
  }

  void doubleClickDoesNotStartQsoWithOwnBaseCall()
  {
    auto context = baseContext();
    context.doubleClicked = true;
    DecodedText message {"0605 -10  0.3 0815 ~  W1AW K1ABC FN42"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(!decision.continueProcessing);
    QCOMPARE(decision.reason, QString {"double-click would start QSO with own base call"});
  }

  void udpReplyIsIgnoredWhileTransmittingOur73()
  {
    auto context = baseContext();
    context.fromUdpReply = true;
    context.transmittingSignoff = true;
    DecodedText message {"0605 -10  0.3 0815 ~  K1ABC W1AW RR73"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(!decision.continueProcessing);
    QCOMPARE(decision.reason, QString {"udp reply ignored while transmitting 73"});
  }

  void udpInvalidCqReplyIsRejectedWhileTransmittingOur73()
  {
    auto context = baseContext();
    context.fromUdpReply = true;
    context.transmittingSignoff = true;
    DecodedText message {"0605 -10  0.3 0815 ~  CQ N0CALL FN31"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(!decision.continueProcessing);
    QCOMPARE(decision.reason, QString {"non-standard free text is not processable"});
  }

  void udpDifferentSignoffIsHonoredWhileTransmittingOur73()
  {
    auto context = baseContext();
    context.fromUdpReply = true;
    context.transmittingSignoff = true;
    DecodedText message {"0605 -10  0.3 0815 ~  K1ABC N0CALL RR73"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(decision.continueProcessing);
  }

  void manualDoubleClickStillHonoredWhileTransmittingOur73()
  {
    // Answering the partner's RR73 with a 73 by double-clicking it is an
    // explicit operator override and must keep working even mid-transmission.
    auto context = baseContext();
    context.doubleClicked = true;
    context.fromUdpReply = false;
    context.transmittingSignoff = true;
    DecodedText message {"0605 -10  0.3 0815 ~  K1ABC W1AW RR73"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(decision.continueProcessing);
  }

  void udpReplyHonoredWhenNotSigningOff()
  {
    auto context = baseContext();
    context.doubleClicked = true;
    context.fromUdpReply = true;
    context.transmittingSignoff = false;
    DecodedText message {"0605 -10  0.3 0815 ~  CQ W1AW FN31"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(decision.continueProcessing);
  }

  void houndIgnoresOtherHoundNegativeReport()
  {
    auto context = baseContext();
    context.specOp = SpecialOperatingActivity::HOUND;
    DecodedText message {"0605 -10  0.3 0815 ~  K1JT W10AAA R-10"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(!decision.continueProcessing);
    QCOMPARE(decision.reason, QString {"hound ignores other hounds"});
  }

  void houndIgnoresOtherHoundPositiveReport()
  {
    auto context = baseContext();
    context.specOp = SpecialOperatingActivity::HOUND;
    DecodedText message {"0605 -10  0.3 0815 ~  K1JT W10AAA R+03"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(!decision.continueProcessing);
    QCOMPARE(decision.reason, QString {"hound ignores other hounds"});
  }

  void suffixedCallKeepsPriorQsoPartnerSnapshot()
  {
    auto context = baseContext();
    context.dxCall = "N0CALL";
    DecodedText message {"0605 -10  0.3 0815 ~  K1ABC W1AW/R -10"};

    auto const decision = DecodedMessageReaction::decideProcessMessageEntry(message, context);

    QVERIFY(decision.continueProcessing);
    QCOMPARE(decision.effectiveDxCall, QString {"W1AW/R"});
    QCOMPARE(decision.qsoPartnerBaseCall, QString {"N0CALL"});
  }
};

QTEST_MAIN(TestDecodedMessageReaction)
#include "test_decoded_message_reaction.moc"
