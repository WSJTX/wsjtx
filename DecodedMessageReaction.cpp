#include "DecodedMessageReaction.hpp"

#include "Decoder/decodedtext.h"
#include "qt_helpers.hpp"

#include <QRegularExpression>
#include <QtMath>

namespace
{
  using DecodedMessageReaction::ProcessMessageAction;
  using DecodedMessageReaction::QsoProgress;
  using SpecOp = SpecialOperatingActivity;

  bool isSupportedModeMarker(QString const& appMode, QString const& decodeMarker)
  {
    if ("JT65" == appMode) return "#" == decodeMarker;
    if ("JT9" == appMode) return "@" == decodeMarker;
    if ("MSK144" == appMode) return "&" == decodeMarker || "^" == decodeMarker;
    if ("Q65" == appMode) return ":" == decodeMarker.left(1);
    return true;
  }

  bool canContinueAfterTxFrequencyPick(QString const& mode)
  {
    return "JT4" == mode || "JT65" == mode || mode.startsWith("JT9")
      || "Q65" == mode || "FT8" == mode || "FT4" == mode || "FST4" == mode;
  }

  constexpr int euVhfType5ReportMin {52};
  constexpr int euVhfType5ReportMax {59};
  constexpr int euVhfType5SerialMin {1};
  constexpr int euVhfType5SerialMax {2047};

  bool isEuVhfType5Exchange(int exchange)
  {
    auto const report = exchange / 10000;
    auto const serial = exchange % 10000;
    return report >= euVhfType5ReportMin
      && report <= euVhfType5ReportMax
      && serial >= euVhfType5SerialMin
      && serial <= euVhfType5SerialMax;
  }

  void appendIntAction(QVector<ProcessMessageAction>& actions, ProcessMessageAction::Kind kind, int value)
  {
    ProcessMessageAction action;
    action.kind = kind;
    action.intValue = value;
    actions.append(action);
  }

  void appendBoolAction(QVector<ProcessMessageAction>& actions, ProcessMessageAction::Kind kind, bool value)
  {
    ProcessMessageAction action;
    action.kind = kind;
    action.boolValue = value;
    actions.append(action);
  }

  void appendFrequencyAction(QVector<ProcessMessageAction>& actions, ProcessMessageAction::Kind kind,
                             Radio::Frequency frequency, QString text = QString {})
  {
    ProcessMessageAction action;
    action.kind = kind;
    action.frequency = frequency;
    action.text = text;
    actions.append(action);
  }

  bool isAcceptableFreeText73(DecodedText const& message, QStringList const& messageWords,
                              DecodedMessageReaction::AutoSequenceContext const& context)
  {
    bool const is73 = messageWords.filter(QRegularExpression {"^(73|RR73)$"}).size();
    if (!is73 || context.qsoProgress < QsoProgress::RogerReport) return false;

    if (!message.isStandardMessage()) return context.mode != "MSK144";

    return messageWords.contains(context.baseCall)
      || messageWords.contains(context.myCall)
      || messageWords.contains(context.dxCall)
      || messageWords.contains(Radio::base_callsign(context.dxCall))
      || messageWords.contains("DE");
  }

  bool messageContainsCall(QStringList const& messageWords, QString const& call)
  {
    if (call.isEmpty()) return false;
    return messageWords.contains(call) || messageWords.contains(Radio::base_callsign(call));
  }

  bool isSignoffForCurrentQso(DecodedText const& message,
                              DecodedMessageReaction::ProcessMessageContext const& context)
  {
    QStringList const messageWords = message.messageWords();
    if (!messageWords.filter(QRegularExpression {"^(73|RR73)$"}).size()) return false;

    return messageContainsCall(messageWords, context.baseCall)
      && (messageContainsCall(messageWords, context.dxCall)
          || messageContainsCall(messageWords, context.hisCall));
  }
}

namespace DecodedMessageReaction
{
  ProcessMessageDecision decideProcessMessageEntry(DecodedText const& message, ProcessMessageContext const& context)
  {
    ProcessMessageDecision decision;

    if (context.fromUdpReply && context.transmittingSignoff && isSignoffForCurrentQso(message, context)) {
      decision.reason = "udp reply ignored while transmitting 73";
      return decision;
    }

    QStringList const parts = message.clean_string().split(' ', SkipEmptyParts);
    if (parts.size() < 5) {
      decision.reason = "decode has too few fields";
      return decision;
    }

    QString const modeMarker = parts.at(4).left(1);
    if (!isSupportedModeMarker(context.mode, modeMarker)) {
      decision.reason = "decode mode marker does not match current mode";
      return decision;
    }

    int const frequency = message.frequencyOffset();
    if (message.isTX()) {
      if (!context.enableVhfFeatures) {
        if (!context.modifiers.shift) appendIntAction(decision.actions, ProcessMessageAction::Kind::SetRxFrequency, frequency);
        if ((context.modifiers.ctrl || context.modifiers.shift) && !context.holdTxFrequency) {
          appendIntAction(decision.actions, ProcessMessageAction::Kind::SetTxFrequency, frequency);
        }
      }
      decision.reason = "transmitted decode only adjusts frequencies";
      return decision;
    }

    if (parts.size() >= 7 && context.fastMode && "CQ" == parts[5] && context.transceiverOnline) {
      bool ok = false;
      auto const kHz = parts[6].toUInt(&ok);
      if (ok && kHz >= 10 && 3 == parts[6].size()) {
        auto const dialFrequency = context.nominalFrequency / 1000000 * 1000000 + 1000 * kHz;
        appendFrequencyAction(decision.actions, ProcessMessageAction::Kind::SetRigFrequency, dialFrequency);
        appendFrequencyAction(decision.actions, ProcessMessageAction::Kind::DisplayQsy, context.nominalFrequency,
                              QString {"QSY %1"}.arg(context.nominalFrequency / 1e6, 7, 'f', 3));
        if ("MSK144" == context.mode) {
          appendFrequencyAction(decision.actions, ProcessMessageAction::Kind::SetMsk144BaseFrequency, dialFrequency);
        }
      }
    }

    int const nmod = fmod(double(message.timeInSeconds()), 2.0 * context.trPeriod);
    bool txFirst = nmod != 0;
    if (SpecOp::HOUND == context.specOp) txFirst = false;
    if (SpecOp::FOX == context.specOp) txFirst = true;
    appendBoolAction(decision.actions, ProcessMessageAction::Kind::SetTxFirst, txFirst);

    decision.messageWords = message.messageWords();
    if (decision.messageWords.size() < 3) {
      decision.reason = "message has too few message words";
      return decision;
    }

    message.deCallAndGrid(decision.hisCall, decision.hisGrid);
    decision.effectiveDxCall = context.dxCall;

    if (context.doubleClicked && decision.hisCall == context.baseCall) {
      decision.reason = "double-click would start QSO with own base call";
      return decision;
    }

    QString const cleanMessage = message.clean_string().remove("<").remove(">");
    if (context.doubleClicked
        && cleanMessage.contains(" " + context.baseCall + " ")
        && cleanMessage.contains(" " + decision.hisCall + " ")
        && message.clean_string().mid(22).contains(" 73")) {
      decision.reason = "double-click on final 73 from current QSO";
      return decision;
    }

    // deCallAndGrid() already preserves rover/portable suffixes; keep that
    // suffixed call as the active QSO partner for later base-call checks.
    if (decision.hisCall.endsWith("/R") || decision.hisCall.endsWith("/P")) {
      decision.effectiveDxCall = decision.hisCall;
    }

    decision.payloadWords = message.clean_string().mid(22).remove("<").remove(">").split(" ", SkipEmptyParts);
    if (decision.payloadWords.size() >= 4) {
      if (decision.messageWords.size() < 4) {
        decision.reason = "payload has contest shape but message words are incomplete";
        return decision;
      }
      int const serial = decision.payloadWords.at(decision.payloadWords.size() - 2).toInt();
      if (isEuVhfType5Exchange(serial)) {
        decision.hisCall = decision.payloadWords.at(1);
        decision.hisGrid = decision.payloadWords.at(decision.payloadWords.size() - 1);
      }
    }

    decision.is73 = decision.messageWords.filter(QRegularExpression {"^(73|RR73)$"}).size();
    if (!decision.is73 && !message.isStandardMessage() && !message.clean_string().contains("<")) {
      decision.reason = "non-standard free text is not processable";
      return decision;
    }

    if ((message.isJT9() && context.mode != "JT9" && context.mode != "JT4")
        || (message.isJT65() && context.mode != "JT65" && context.mode != "JT4")) {
      decision.reason = "JT9/JT65 decode not allowed in current mode";
      return decision;
    }

    if (SpecOp::HOUND == context.specOp
        && message.messageWords().indexOf(QRegularExpression {R"(R[-+][0-9]+)"}) >= 1) {
      decision.reason = "hound ignores other hounds";
      return decision;
    }

    decision.firstCall = message.call();
    if (decision.firstCall.length() >= 4 && decision.firstCall.mid(0, 3) == "CQ ") decision.firstCall = "CQ";
    if (!context.fastMode && (!context.enableVhfFeatures || context.mode == "FT8" || context.mode == "FT4" || context.mode == "FST4")) {
      bool const callOrCq = (Radio::is_callsign(decision.firstCall)
                             && decision.firstCall != context.myCall
                             && decision.firstCall != context.baseCall
                             && decision.firstCall != "DE")
        || "CQ" == decision.firstCall || "QRZ" == decision.firstCall
        || context.modifiers.ctrl || context.modifiers.shift;
      if (callOrCq) {
        if (((SpecOp::HOUND != context.specOp) || context.mode != "FT8")
            && (!context.holdTxFrequency || context.modifiers.shift || context.modifiers.ctrl)) {
          appendIntAction(decision.actions, ProcessMessageAction::Kind::SetTxFrequency, frequency);
        }
        if (!canContinueAfterTxFrequencyPick(context.mode)) {
          decision.reason = "frequency-pick-only mode";
          return decision;
        }
      }
    }

    decision.qsoPartnerBaseCall = Radio::base_callsign(context.dxCall);
    decision.hisBaseCall = Radio::base_callsign(decision.hisCall);
    decision.continueProcessing = true;
    return decision;
  }

  AutoSequenceDecision decideAutoSequence(DecodedText const& message, AutoSequenceContext const& context,
                                          unsigned startTolerance, unsigned stopTolerance)
  {
    AutoSequenceDecision decision;
    QStringList const messageWords = message.messageWords();
    bool const is73 = messageWords.filter(QRegularExpression {"^(73|RR73)$"}).size();
    QString msgNoHash = message.clean_string().mid(22).remove("<").remove(">");
    bool isOk = false;
    if (context.mode == "MSK144" && msgNoHash.indexOf(context.dxCall + " R ") > 0) isOk = true;

    if (messageWords.size() <= 3 || !(message.isStandardMessage() || is73 || isOk)) {
      decision.reason = "message is not an auto-sequence candidate";
      return decision;
    }

    int const df = message.frequencyOffset();
    bool const withinTolerance = qAbs(context.rxFrequency - df) <= int(startTolerance)
      || qAbs(context.txFrequency - df) <= int(startTolerance);

    QStringList const words = msgNoHash.split(" ", SkipEmptyParts);
    QString thirdPayloadWord;
    int reportOrSerial = 0;
    if (words.size() > 2) {
      thirdPayloadWord = words.at(2);
      if (words.size() > 3) {
        reportOrSerial = thirdPayloadWord.toInt();
        if (thirdPayloadWord == "R") reportOrSerial = words.at(3).toInt();
      }
    }
    bool const euVhfSerial = isEuVhfType5Exchange(reportOrSerial);
    if (euVhfSerial && message.clean_string().contains("<" + context.myCall + "> ")) {
      decision.receivedExchange = message.clean_string().trimmed().right(13);
    }

    if (context.autoEnabled
        && (context.qsoProgress == QsoProgress::Replying
            || (!context.tx1Enabled && context.qsoProgress == QsoProgress::Report))
        && SpecOp::HOUND != context.specOp
        && qAbs(context.txFrequency - df) <= int(stopTolerance)
        && messageWords.at(2) != "DE"
        && !messageWords.at(2).contains(QRegularExpression {"(^(CQ|QRZ))|" + context.baseCall})
        && messageWords.at(3).contains(Radio::base_callsign(context.dxCall))) {
      decision.action = AutoSequenceDecision::Action::StopToAvoidQrm;
      decision.reason = "reply from QSO partner appears directed to another caller";
      return decision;
    }

    bool const acceptable73 = isAcceptableFreeText73(message, messageWords, context);
    bool const directlyForUs = messageWords.at(2).contains(context.baseCall)
      && (messageWords.at(3).contains(Radio::base_callsign(context.dxCall)) || euVhfSerial);
    bool const rr73Style = messageWords.at(1) == context.baseCall;
    bool const type2Or73 = withinTolerance
      && (acceptable73 || ("DE" == messageWords.at(2) && thirdPayloadWord.contains(Radio::base_callsign(context.hisCall))));
    bool const autoReplyForUs = context.callingCQ && context.autoReply
      && ((withinTolerance && "DE" == messageWords.at(2)) || messageWords.at(2).contains(context.baseCall));

    if (SpecOp::FOX != context.specOp
        && context.autoEnabled
        && context.autoSequenceEnabled
        && ((!context.callingCQ && !context.sentFirst73 && (directlyForUs || rr73Style || type2Or73))
            || autoReplyForUs)) {
      decision.action = AutoSequenceDecision::Action::ProcessMessage;
      decision.reason = "message advances auto sequence";
    }
    return decision;
  }
}
