#ifndef HOUND_TRANSMISSION_POLICY_HPP_
#define HOUND_TRANSMISSION_POLICY_HPP_

namespace HoundTransmissionPolicy
{
  enum class Protocol
  {
    Classic,
    SuperFox
  };

  enum class ReplyState
  {
    None,
    FirstReplyPending,
    FirstReplySent
  };

  enum class FrequencyAction
  {
    Keep,
    RandomizeCalling,
    Set
  };

  enum class TxMessage
  {
    None,
    Tx3
  };

  struct State
  {
    ReplyState replyState {ReplyState::None};
    int foxFrequency {0};
  };

  struct FrequencyDecision
  {
    FrequencyAction action {FrequencyAction::Keep};
    int frequency {0};
  };

  struct ReplyInput
  {
    Protocol protocol {Protocol::Classic};
    bool tune {false};
    bool autoEnabled {false};
    int sentReport {0};
    int decodedFoxFrequency {0};
    double trPeriod {0.0};
  };

  struct ReplyPlan
  {
    bool applyReply {false};
    TxMessage txMessage {TxMessage::None};
    int report {0};
    bool enableAuto {false};
    FrequencyDecision frequencyDecision;
    int timeoutMilliseconds {0};
    State nextState;
  };

  struct ClassicTxStartInput
  {
    bool autoEnabled {false};
    bool tune {false};
    int selectedTxMessage {0};
    int currentTxFrequency {0};
  };

  struct TxStartPlan
  {
    FrequencyDecision frequencyDecision;
    State nextState;
  };

  ReplyPlan planFoxReportReply (State const&, ReplyInput const&);
  TxStartPlan planClassicTxStart (State const&, ClassicTxStartInput const&);
}

#endif
