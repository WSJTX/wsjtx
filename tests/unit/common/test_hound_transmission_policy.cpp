#include <QtTest/QtTest>

#include "HoundTransmissionPolicy.hpp"

class TestHoundTransmissionPolicy final : public QObject
{
  Q_OBJECT

private slots:
  void planFoxReportReply_data ();
  void planFoxReportReply ();
  void planInitialCallingFrequency_data ();
  void planInitialCallingFrequency ();
  void planReplyTransmission_data ();
  void planReplyTransmission ();
};

void TestHoundTransmissionPolicy::planFoxReportReply_data ()
{
  QTest::addColumn<int> ("protocol");
  QTest::addColumn<bool> ("tune");
  QTest::addColumn<bool> ("autoEnabled");
  QTest::addColumn<int> ("priorReplyState");
  QTest::addColumn<bool> ("applyReply");
  QTest::addColumn<bool> ("enableAuto");
  QTest::addColumn<int> ("frequencyAction");
  QTest::addColumn<int> ("nextReplyState");

  using namespace HoundTransmissionPolicy;
  QTest::newRow ("classic") << int (Protocol::Classic) << false << false
    << int (ReplyState::None) << true << true << int (FrequencyAction::Set)
    << int (ReplyState::FirstReplyPending);
  QTest::newRow ("auto already enabled") << int (Protocol::Classic) << false << true
    << int (ReplyState::None) << true << false << int (FrequencyAction::Set)
    << int (ReplyState::FirstReplyPending);
  QTest::newRow ("SuperHound") << int (Protocol::SuperFox) << false << false
    << int (ReplyState::None) << true << true << int (FrequencyAction::Keep)
    << int (ReplyState::FirstReplyPending);
  QTest::newRow ("tune") << int (Protocol::Classic) << true << false
    << int (ReplyState::FirstReplySent) << false << false << int (FrequencyAction::Keep)
    << int (ReplyState::FirstReplySent);
}

void TestHoundTransmissionPolicy::planFoxReportReply ()
{
  QFETCH (int, protocol);
  QFETCH (bool, tune);
  QFETCH (bool, autoEnabled);
  QFETCH (int, priorReplyState);
  QFETCH (bool, applyReply);
  QFETCH (bool, enableAuto);
  QFETCH (int, frequencyAction);
  QFETCH (int, nextReplyState);

  using namespace HoundTransmissionPolicy;
  State state;
  state.replyState = static_cast<ReplyState> (priorReplyState);
  state.foxFrequency = 321;
  ReplyInput input;
  input.protocol = static_cast<Protocol> (protocol);
  input.tune = tune;
  input.autoEnabled = autoEnabled;
  input.sentReport = -17;
  input.decodedFoxFrequency = 654;
  input.trPeriod = 15.0;

  auto const plan = HoundTransmissionPolicy::planFoxReportReply (state, input);
  QCOMPARE (plan.applyReply, applyReply);
  QCOMPARE (plan.enableAuto, enableAuto);
  QCOMPARE (int (plan.frequencyDecision.action), frequencyAction);
  QCOMPARE (int (plan.nextState.replyState), nextReplyState);
  QCOMPARE (plan.nextState.foxFrequency, 654);
  if (applyReply)
    {
      QCOMPARE (int (plan.txMessage), int (TxMessage::Tx3));
      QCOMPARE (plan.report, -17);
      QCOMPARE (plan.timeoutMilliseconds, 165000);
      if (FrequencyAction::Set == plan.frequencyDecision.action)
        {
          QCOMPARE (plan.frequencyDecision.frequency, 654);
        }
    }
  else
    {
      QCOMPARE (int (plan.txMessage), int (TxMessage::None));
      QCOMPARE (plan.timeoutMilliseconds, 0);
    }
}

void TestHoundTransmissionPolicy::planInitialCallingFrequency_data ()
{
  QTest::addColumn<bool> ("autoEnabled");
  QTest::addColumn<bool> ("tune");
  QTest::addColumn<int> ("selectedTxMessage");
  QTest::addColumn<int> ("currentTxFrequency");
  QTest::addColumn<bool> ("randomize");

  QTest::newRow ("eligible at 998 Hz") << true << false << 1 << 998 << true;
  QTest::newRow ("Auto off") << false << false << 1 << 998 << false;
  QTest::newRow ("Tune") << true << true << 1 << 998 << false;
  QTest::newRow ("Tx3") << true << false << 3 << 998 << false;
  QTest::newRow ("999 Hz") << true << false << 1 << 999 << false;
}

void TestHoundTransmissionPolicy::planInitialCallingFrequency ()
{
  QFETCH (bool, autoEnabled);
  QFETCH (bool, tune);
  QFETCH (int, selectedTxMessage);
  QFETCH (int, currentTxFrequency);
  QFETCH (bool, randomize);

  using namespace HoundTransmissionPolicy;
  ClassicTxStartInput input;
  input.autoEnabled = autoEnabled;
  input.tune = tune;
  input.selectedTxMessage = selectedTxMessage;
  input.currentTxFrequency = currentTxFrequency;
  auto const plan = planClassicTxStart (State {}, input);
  QCOMPARE (plan.frequencyDecision.action == FrequencyAction::RandomizeCalling, randomize);
}

void TestHoundTransmissionPolicy::planReplyTransmission_data ()
{
  QTest::addColumn<int> ("replyState");
  QTest::addColumn<int> ("foxFrequency");
  QTest::addColumn<int> ("selectedTxMessage");
  QTest::addColumn<bool> ("tune");
  QTest::addColumn<int> ("frequencyAction");
  QTest::addColumn<int> ("frequency");
  QTest::addColumn<int> ("nextReplyState");

  using namespace HoundTransmissionPolicy;
  QTest::newRow ("first reply") << int (ReplyState::FirstReplyPending) << 600 << 3 << false
    << int (FrequencyAction::Keep) << 0 << int (ReplyState::FirstReplySent);
  QTest::newRow ("retry at 600 Hz") << int (ReplyState::FirstReplySent) << 600 << 3 << false
    << int (FrequencyAction::Set) << 900 << int (ReplyState::FirstReplySent);
  QTest::newRow ("retry at 601 Hz") << int (ReplyState::FirstReplySent) << 601 << 3 << false
    << int (FrequencyAction::Set) << 301 << int (ReplyState::FirstReplySent);
  QTest::newRow ("non-Tx3 retry") << int (ReplyState::FirstReplySent) << 600 << 2 << false
    << int (FrequencyAction::Keep) << 0 << int (ReplyState::FirstReplySent);
  QTest::newRow ("Tune retry") << int (ReplyState::FirstReplySent) << 600 << 3 << true
    << int (FrequencyAction::Set) << 900 << int (ReplyState::FirstReplySent);
  QTest::newRow ("non-Tx3 advances pending") << int (ReplyState::FirstReplyPending) << 600 << 2 << true
    << int (FrequencyAction::Keep) << 0 << int (ReplyState::FirstReplySent);
}

void TestHoundTransmissionPolicy::planReplyTransmission ()
{
  QFETCH (int, replyState);
  QFETCH (int, foxFrequency);
  QFETCH (int, selectedTxMessage);
  QFETCH (bool, tune);
  QFETCH (int, frequencyAction);
  QFETCH (int, frequency);
  QFETCH (int, nextReplyState);

  using namespace HoundTransmissionPolicy;
  State state;
  state.replyState = static_cast<ReplyState> (replyState);
  state.foxFrequency = foxFrequency;
  ClassicTxStartInput input;
  input.autoEnabled = true;
  input.tune = tune;
  input.selectedTxMessage = selectedTxMessage;
  input.currentTxFrequency = 1500;

  auto const plan = planClassicTxStart (state, input);
  QCOMPARE (int (plan.frequencyDecision.action), frequencyAction);
  QCOMPARE (plan.frequencyDecision.frequency, frequency);
  QCOMPARE (int (plan.nextState.replyState), nextReplyState);
  QCOMPARE (plan.nextState.foxFrequency, foxFrequency);
}

QTEST_GUILESS_MAIN (TestHoundTransmissionPolicy)

#include "test_hound_transmission_policy.moc"
