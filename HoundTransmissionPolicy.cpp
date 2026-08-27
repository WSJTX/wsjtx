#include "HoundTransmissionPolicy.hpp"

namespace HoundTransmissionPolicy
{
  ReplyPlan planFoxReportReply (State const& state, ReplyInput const& input)
  {
    ReplyPlan plan;
    plan.nextState = state;
    plan.nextState.foxFrequency = input.decodedFoxFrequency;

    if (input.tune)
      {
        return plan;
      }

    plan.applyReply = true;
    plan.txMessage = TxMessage::Tx3;
    plan.report = input.sentReport;
    plan.enableAuto = !input.autoEnabled;
    plan.timeoutMilliseconds = static_cast<int> (11000.0 * input.trPeriod);
    plan.nextState.replyState = ReplyState::FirstReplyPending;
    if (Protocol::Classic == input.protocol)
      {
        plan.frequencyDecision.action = FrequencyAction::Set;
        plan.frequencyDecision.frequency = input.decodedFoxFrequency;
      }
    return plan;
  }

  TxStartPlan planClassicTxStart (State const& state, ClassicTxStartInput const& input)
  {
    TxStartPlan plan;
    plan.nextState = state;

    if (input.autoEnabled && !input.tune && input.currentTxFrequency < 999
        && 3 != input.selectedTxMessage)
      {
        plan.frequencyDecision.action = FrequencyAction::RandomizeCalling;
      }

    if (ReplyState::FirstReplySent == state.replyState && 3 == input.selectedTxMessage)
      {
        plan.frequencyDecision.action = FrequencyAction::Set;
        plan.frequencyDecision.frequency = state.foxFrequency <= 600
          ? state.foxFrequency + 300 : state.foxFrequency - 300;
      }

    if (ReplyState::FirstReplyPending == state.replyState)
      {
        plan.nextState.replyState = ReplyState::FirstReplySent;
      }
    return plan;
  }
}
