#ifndef DECODEDMESSAGEREACTION_HPP
#define DECODEDMESSAGEREACTION_HPP

#include "Radio.hpp"
#include "SpecialOperatingActivity.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

class DecodedText;

namespace DecodedMessageReaction
{
  enum class QsoProgress
  {
    Calling,
    Replying,
    Report,
    RogerReport,
    Rogers,
    Signoff
  };

  struct KeyboardModifiers
  {
    bool shift {false};
    bool ctrl {false};
    bool alt {false};
  };

  struct ProcessMessageContext
  {
    QString mode;
    SpecialOperatingActivity specOp {SpecialOperatingActivity::NONE};
    QString myCall;
    QString baseCall;
    QString dxCall;
    QString hisCall;
    double trPeriod {60.0};
    Radio::Frequency nominalFrequency {0u};
    int rxFrequency {0};
    int txFrequency {0};
    bool fastMode {false};
    bool transceiverOnline {false};
    bool enableVhfFeatures {false};
    bool holdTxFrequency {false};
    bool rxFrequencyEnabled {true};
    bool txFirst {false};
    bool txFirstVisible {true};
    bool txFirstEnabled {true};
    bool doubleClicked {false};
    bool fromUdpReply {false};
    bool transmittingSignoff {false};
    bool autoReply {false};
    bool autoEnabled {false};
    bool tx1Enabled {true};
    int currentMessageType {0};
    QsoProgress qsoProgress {QsoProgress::Calling};
    KeyboardModifiers modifiers;
  };

  struct ProcessMessageAction
  {
    enum class Kind
    {
      SetRxFrequency,
      SetTxFrequency,
      SetRigFrequency,
      DisplayQsy,
      SetMsk144BaseFrequency,
      SetTxFirst,
      SetDxCall
    };

    Kind kind;
    int intValue {0};
    Radio::Frequency frequency {0u};
    QString text;
    bool boolValue {false};
  };

  struct ProcessMessageDecision
  {
    bool continueProcessing {false};
    QString reason;
    QStringList messageWords;
    QStringList payloadWords;
    QString firstCall;
    QString hisCall;
    QString hisGrid;
    QString effectiveDxCall;
    QString qsoPartnerBaseCall;
    QString hisBaseCall;
    bool is73 {false};
    QVector<ProcessMessageAction> actions;
  };

  struct AutoSequenceContext
  {
    QString mode;
    SpecialOperatingActivity specOp {SpecialOperatingActivity::NONE};
    QString myCall;
    QString baseCall;
    QString dxCall;
    QString hisCall;
    int rxFrequency {0};
    int txFrequency {0};
    bool autoEnabled {false};
    bool autoSequenceEnabled {false};
    bool callingCQ {false};
    bool autoReply {false};
    bool sentFirst73 {false};
    bool tx1Enabled {true};
    QsoProgress qsoProgress {QsoProgress::Calling};
  };

  struct AutoSequenceDecision
  {
    enum class Action
    {
      None,
      StopToAvoidQrm,
      ProcessMessage
    };

    Action action {Action::None};
    QString reason;
    QString receivedExchange;
  };

  ProcessMessageDecision decideProcessMessageEntry(DecodedText const& message, ProcessMessageContext const& context);
  AutoSequenceDecision decideAutoSequence(DecodedText const& message, AutoSequenceContext const& context,
                                          unsigned startTolerance, unsigned stopTolerance);
}

#endif // DECODEDMESSAGEREACTION_HPP
