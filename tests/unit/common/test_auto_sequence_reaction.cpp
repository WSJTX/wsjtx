#include <QtTest>

#include "DecodedMessageReaction.hpp"
#include "Decoder/decodedtext.h"

class TestAutoSequenceReaction final
  : public QObject
{
  Q_OBJECT

private:
  DecodedMessageReaction::AutoSequenceContext baseContext() const
  {
    DecodedMessageReaction::AutoSequenceContext context;
    context.mode = "FT8";
    context.myCall = "K1ABC";
    context.baseCall = "K1ABC";
    context.dxCall = "W1AW";
    context.hisCall = "W1AW";
    context.rxFrequency = 1500;
    context.txFrequency = 1500;
    context.autoEnabled = true;
    context.autoSequenceEnabled = true;
    context.tx1Enabled = true;
    context.qsoProgress = DecodedMessageReaction::QsoProgress::Calling;
    return context;
  }

private slots:
  void stopsToAvoidQrmWhenPartnerRepliesNearOurTxFrequencyToAnotherCaller()
  {
    auto context = baseContext();
    context.qsoProgress = DecodedMessageReaction::QsoProgress::Replying;
    context.txFrequency = 815;
    DecodedText message {"0605 -10  0.3 0815 ~  K9XYZ W1AW -10"};

    auto const decision = DecodedMessageReaction::decideAutoSequence(message, context, 25, 50);

    QCOMPARE(decision.action, DecodedMessageReaction::AutoSequenceDecision::Action::StopToAvoidQrm);
  }

  void processesDirectedReplyToOurCall()
  {
    auto context = baseContext();
    DecodedText message {"0605 -10  0.3 1500 ~  K1ABC W1AW -10"};

    auto const decision = DecodedMessageReaction::decideAutoSequence(message, context, 25, 50);

    QCOMPARE(decision.action, DecodedMessageReaction::AutoSequenceDecision::Action::ProcessMessage);
  }

  void processesTypeTwoDeReplyWithinTolerance()
  {
    auto context = baseContext();
    context.callingCQ = true;
    context.autoReply = true;
    DecodedText message {"0605 -10  0.3 1500 ~  DE W1AW -10"};

    auto const decision = DecodedMessageReaction::decideAutoSequence(message, context, 25, 50);

    QCOMPARE(decision.action, DecodedMessageReaction::AutoSequenceDecision::Action::ProcessMessage);
  }

  void ignoresProcessActionInFoxMode()
  {
    auto context = baseContext();
    context.specOp = SpecialOperatingActivity::FOX;
    DecodedText message {"0605 -10  0.3 1500 ~  K1ABC W1AW -10"};

    auto const decision = DecodedMessageReaction::decideAutoSequence(message, context, 25, 50);

    QCOMPARE(decision.action, DecodedMessageReaction::AutoSequenceDecision::Action::None);
  }

  void freeText73DoesNotAdvanceMsk144AutoSequence()
  {
    auto context = baseContext();
    context.mode = "MSK144";
    context.qsoProgress = DecodedMessageReaction::QsoProgress::RogerReport;
    DecodedText message {"060522 -10  0.3 1500 &  RR73"};

    auto const decision = DecodedMessageReaction::decideAutoSequence(message, context, 25, 50);

    QCOMPARE(decision.action, DecodedMessageReaction::AutoSequenceDecision::Action::None);
  }

  void storesEuVhfExchangeForAutoSequenceCandidate()
  {
    auto context = baseContext();
    context.mode = "MSK144";
    DecodedText message {"060522 -10  0.3 1500 &  <K1ABC> W1AW 520001 FN31 W1AW R X"};

    auto const decision = DecodedMessageReaction::decideAutoSequence(message, context, 25, 50);

    QCOMPARE(decision.receivedExchange, message.clean_string().trimmed().right(13));
  }

  void ignoresEuVhfExchangeForRejectedDecode()
  {
    auto context = baseContext();
    DecodedText message {"0605 -10  0.3 1500 ~  RANDOM <K1ABC> 520001 FN31"};

    auto const decision = DecodedMessageReaction::decideAutoSequence(message, context, 25, 50);

    QVERIFY(decision.receivedExchange.isEmpty());
  }
};

QTEST_MAIN(TestAutoSequenceReaction)
#include "test_auto_sequence_reaction.moc"
