#ifndef DECODEDMESSAGEREACTION_HPP
#define DECODEDMESSAGEREACTION_HPP

#include "QsoProgress.hpp"
#include "Radio.hpp"
#include "SpecialOperatingActivity.hpp"

#include <functional>
#include <QString>
#include <QVector>

class DecodedText;

namespace DecodedMessageReaction
{
  enum class ReactionDisposition
  {
    NoReaction,
    Reacted,
    IgnoreDecode
  };

  enum class WaitDecodeSource
  {
    SlowDecoder,
    Msk144FastDecoder
  };

  enum class AutoSequencePhase
  {
    StandardDecode,
    LegacyShortMessage
  };

  enum class ContestHint
  {
    EuVhf,
    FieldDay,
    Rtty
  };

  enum class SelectionOrigin
  {
    None,
    Manual,
    Synthetic,
    Udp
  };

  struct KeyboardModifiers
  {
    bool shift {false};
    bool ctrl {false};
    bool alt {false};
  };

  struct QsoReactionSnapshot
  {
    QString mode;
    SpecialOperatingActivity specOp {SpecialOperatingActivity::NONE};
    QString myCall;
    QString baseCall;
    QString dxCall;
    QString hisCall;
    QString hisGrid;
    QString respondSelection;
    double trPeriod {60.0};
    Radio::Frequency nominalFrequency {0u};
    int rxFrequency {0};
    int txFrequency {0};

    QsoProgress qsoProgress {QsoProgress::Calling};
    int selectedTxMessage {0};
    int currentMessageType {0};
    bool sentReport {false};
    bool shortMessages {false};
    bool sendRr73 {false};
    bool transmitting {false};
    bool transmittingSignoff {false};

    bool doubleClicked {false};
    bool doubleClickAfterCqFrequency {false};
    SelectionOrigin selectionOrigin {SelectionOrigin::None};
    KeyboardModifiers modifiers;

    bool fastMode {false};
    bool transceiverOnline {false};
    bool nominalQsyAllowed {false};
    bool enableVhfFeatures {false};
    bool holdTxFrequency {false};
    bool rxFrequencyEnabled {true};
    bool tx1Enabled {true};

    bool autoEnabled {false};
    bool autoButtonChecked {false};
    bool autoReply {false};
    bool callingCq {false};
    bool sentFirst73 {false};
    bool autoSequenceEnabled {false};
    bool autoSequenceChecked {false};
    bool quickCall {false};
    bool repeatTx {false};

    bool loggingEnabled {false};
    bool contestHintShown {false};
    bool ncccSprint {false};
    int tx73Count {0};

    bool waitFeaturesEnabled {false};
    bool waitAndCall {false};
    bool noWaitAndCall {false};
    bool waitAndCallControlChecked {false};
    bool fullDuplexEnabled {false};
    bool txing {false};

  };

  struct QsoReactionEffect
  {
    enum class Kind
    {
      SetRxFrequency,
      SetTxFrequency,
      ApplyFastCqQsy,
      RejectNominalQsy,
      SetTxFirst,
      // Keep setTxMsg(), raw index assignment, checked-state changes, and clicks distinct;
      // each has different synchronous signal behavior in MainWindow.
      SetTxMessage,
      SetTxMessageIndex,
      CheckTxMessage,
      ClickTxMessage,
      SetQsoProgress,
      SetDxCall,
      ClearDxGrid,
      SetDxGrid,
      SetReport,
      SetReceivedExchange,
      SetCallingCq,
      SetMaxPoints,
      SetRestart,
      SetDoubleClicked,
      SetDoubleClickAfterCqFrequency,
      SetTuMessage,
      SetNextCall,
      SetNoLogging,
      SetNoWaitAndCall,
      SetBlockRightDisplay,
      SetAutoEnabled,
      RefreshQsoPaneIfChanged,
      Lookup,
      CaptureHisGrid,
      ExtractReceivedReport,
      GenerateStandardMessages,
      RecordRr73Received,
      RequestLogQso,
      RequestLogQsoUnlessSuppressed,
      CeaseAutoTx,
      StopTx,
      ClickStopTx,
      LogStopped,
      StartTxAgainTimer,
      ResetWatchdog,
      StopWaitCallTimer,
      DisableWaitAndCallControl,
      StartWaitReplyTimer,
      StartWaitCallTimer,
      ScheduleStopTx,
      ScheduleNcccAutoReset,
      ScheduleNoLoggingReset,
      ScheduleAutoFlagOff,
      QueueContestHint,
      ProcessSyntheticMessageNow
    };

    Kind kind;
    int intValue {0};
    Radio::Frequency frequency {0u};
    QString text;
    bool boolValue {false};
    bool userInitiated {false};
    QsoProgress progress {QsoProgress::Calling};
    ContestHint contestHint {ContestHint::EuVhf};
  };

  struct QsoReactionPlan
  {
    // Plans are deterministic for their inputs, but are one-shot ordered imperative scripts.
    // Effects must never be reordered, replayed, or batched. ProcessSyntheticMessageNow takes
    // a fresh live snapshot after every preceding effect has been applied.
    ReactionDisposition disposition {ReactionDisposition::NoReaction};
    QString reason;
    QVector<QsoReactionEffect> effects;
  };

  bool shouldDeferAutoTxStopAfterRrr(QString const& mode, bool repeatTx, bool sendRr73);
  void applyAutoTxStopAfterLogging(QString const& mode, bool repeatTx, bool sendRr73,
                                   std::function<void()> stopAutoTx);

  QsoReactionPlan planProcessMessage(DecodedText const& message, QsoReactionSnapshot const& snapshot);
  QsoReactionPlan planAutoSequence(DecodedText const& message, QsoReactionSnapshot const& snapshot,
                                   AutoSequencePhase phase, unsigned startTolerance,
                                   unsigned stopTolerance);
  QsoReactionPlan planWaitReplyCall(DecodedText const& message, QsoReactionSnapshot const& snapshot,
                                    WaitDecodeSource source);
}

#endif // DECODEDMESSAGEREACTION_HPP
