#include <QtTest>

#include "DecodedMessageReaction.hpp"
#include "Decoder/decodedtext.h"

class TestDecodedMessageReaction final
  : public QObject
{
  Q_OBJECT

private:
  static QString legacyDecode(QString const& mode, QString const& payload,
                              QString const& flags, bool lowConfidence)
  {
    auto field = payload.leftJustified(22, ' ', true);
    if (lowConfidence) field[21] = '?';

    auto line = QString {"0605 -10  0.3 0815 "} + mode + "  " + field;
    if (!flags.isEmpty()) line += " " + flags;
    return line;
  }

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
