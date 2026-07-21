#include "DecodedMessageReaction.hpp"

#include "Decoder/decodedtext.h"
#include "qt_helpers.hpp"

#include <QRegularExpression>
#include <QtMath>

namespace
{
  using DecodedMessageReaction::ContestHint;
  using DecodedMessageReaction::QsoReactionEffect;
  using DecodedMessageReaction::QsoReactionPlan;
  using DecodedMessageReaction::QsoReactionSnapshot;
  using DecodedMessageReaction::ReactionDisposition;
  using SpecOp = SpecialOperatingActivity;

  constexpr int euVhfType5ReportMin {52};
  constexpr int euVhfType5ReportMax {59};
  constexpr int euVhfType5SerialMin {1};
  constexpr int euVhfType5SerialMax {2047};

  QRegularExpression const& gridExpression()
  {
    static QRegularExpression const expression {"\\A(?![Rr]{2}73)[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2}){0,1}\\z"};
    return expression;
  }

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

  bool isEuVhfType5Exchange(int exchange)
  {
    auto const report = exchange / 10000;
    auto const serial = exchange % 10000;
    return report >= euVhfType5ReportMin
      && report <= euVhfType5ReportMax
      && serial >= euVhfType5SerialMin
      && serial <= euVhfType5SerialMax;
  }

  void appendEffect(QsoReactionPlan& plan, QsoReactionEffect::Kind kind)
  {
    QsoReactionEffect effect;
    effect.kind = kind;
    plan.effects.append(effect);
  }

  void appendIntEffect(QsoReactionPlan& plan, QsoReactionEffect::Kind kind, int value)
  {
    QsoReactionEffect effect;
    effect.kind = kind;
    effect.intValue = value;
    plan.effects.append(effect);
  }

  void appendBoolEffect(QsoReactionPlan& plan, QsoReactionEffect::Kind kind, bool value)
  {
    QsoReactionEffect effect;
    effect.kind = kind;
    effect.boolValue = value;
    plan.effects.append(effect);
  }

  void appendTextEffect(QsoReactionPlan& plan, QsoReactionEffect::Kind kind, QString const& text)
  {
    QsoReactionEffect effect;
    effect.kind = kind;
    effect.text = text;
    plan.effects.append(effect);
  }

  void appendFrequencyEffect(QsoReactionPlan& plan, QsoReactionEffect::Kind kind,
                             Radio::Frequency frequency, QString const& text = QString {})
  {
    QsoReactionEffect effect;
    effect.kind = kind;
    effect.frequency = frequency;
    effect.text = text;
    plan.effects.append(effect);
  }

  void appendProgressEffect(QsoReactionPlan& plan, QsoProgress progress)
  {
    QsoReactionEffect effect;
    effect.kind = QsoReactionEffect::Kind::SetQsoProgress;
    effect.progress = progress;
    plan.effects.append(effect);
  }

  void appendContestHint(QsoReactionPlan& plan, ContestHint hint)
  {
    QsoReactionEffect effect;
    effect.kind = QsoReactionEffect::Kind::QueueContestHint;
    effect.contestHint = hint;
    plan.effects.append(effect);
  }

  void setProgress(QsoReactionPlan& plan, QsoProgress& progress, QsoProgress value)
  {
    appendProgressEffect(plan, value);
    progress = value;
  }

  void setTxMessageAndProgress(QsoReactionPlan& plan, QsoProgress& progress,
                               int message, QsoProgress value)
  {
    appendIntEffect(plan, QsoReactionEffect::Kind::SetTxMessage, message);
    setProgress(plan, progress, value);
  }

  void selectMessageWithProgressBetween(QsoReactionPlan& plan, QsoProgress& progress,
                                        int message, QsoProgress value)
  {
    appendIntEffect(plan, QsoReactionEffect::Kind::SetTxMessageIndex, message);
    setProgress(plan, progress, value);
    appendIntEffect(plan, QsoReactionEffect::Kind::CheckTxMessage, message);
  }

  void selectMessage(QsoReactionPlan& plan, int message)
  {
    appendIntEffect(plan, QsoReactionEffect::Kind::SetTxMessageIndex, message);
    appendIntEffect(plan, QsoReactionEffect::Kind::CheckTxMessage, message);
  }

  bool messageContainsCall(QStringList const& messageWords, QString const& call)
  {
    if (call.isEmpty()) return false;
    return messageWords.contains(call) || messageWords.contains(Radio::base_callsign(call));
  }

  bool isSignoffForCurrentQso(DecodedText const& message, QsoReactionSnapshot const& snapshot)
  {
    QStringList const messageWords = message.messageWords();
    if (!messageWords.filter(QRegularExpression {"^(73|RR73)$"}).size()) return false;

    return messageContainsCall(messageWords, snapshot.baseCall)
      && (messageContainsCall(messageWords, snapshot.dxCall)
          || messageContainsCall(messageWords, snapshot.hisCall));
  }

  bool isAcceptableFreeText73(DecodedText const& message, QStringList const& messageWords,
                              QsoReactionSnapshot const& snapshot)
  {
    bool const is73 = messageWords.filter(QRegularExpression {"^(73|RR73)$"}).size();
    if (!is73 || snapshot.qsoProgress < QsoProgress::RogerReport) return false;
    if (!message.isStandardMessage()) return snapshot.mode != "MSK144";

    return messageWords.contains(snapshot.baseCall)
      || messageWords.contains(snapshot.myCall)
      || messageContainsCall(messageWords, snapshot.dxCall)
      || messageWords.contains("DE");
  }

  bool isSlowReactionMode(QString const& mode)
  {
    return mode == "FT8" || mode == "FT4" || mode == "Q65" || mode == "FST4"
      || mode == "JT65" || mode == "JT9" || mode == "JT4";
  }

  struct EntryAnalysis
  {
    bool continueProcessing {false};
    QString reason;
    QStringList messageWords;
    QStringList payloadWords;
    QString firstCall;
    QString hisCall;
    QString hisGrid;
    QString qsoPartnerBaseCall;
    QString hisBaseCall;
    bool is73 {false};
  };

  EntryAnalysis analyzeEntry(DecodedText const& message, QsoReactionSnapshot const& snapshot,
                             QsoReactionPlan& plan)
  {
    EntryAnalysis analysis;
    if (snapshot.selectionOrigin == DecodedMessageReaction::SelectionOrigin::Udp
        && snapshot.transmittingSignoff && isSignoffForCurrentQso(message, snapshot)) {
      analysis.reason = "udp reply ignored while transmitting 73";
      return analysis;
    }

    QStringList const parts = message.clean_string().split(' ', SkipEmptyParts);
    if (parts.size() < 5) {
      analysis.reason = "decode has too few fields";
      return analysis;
    }

    if (!isSupportedModeMarker(snapshot.mode, parts.at(4).left(1))) {
      analysis.reason = "decode mode marker does not match current mode";
      return analysis;
    }

    int const frequency = message.frequencyOffset();
    if (message.isTX()) {
      if (!snapshot.enableVhfFeatures) {
        if (!snapshot.modifiers.shift) {
          appendIntEffect(plan, QsoReactionEffect::Kind::SetRxFrequency, frequency);
        }
        if ((snapshot.modifiers.ctrl || snapshot.modifiers.shift) && !snapshot.holdTxFrequency) {
          appendIntEffect(plan, QsoReactionEffect::Kind::SetTxFrequency, frequency);
        }
      }
      analysis.reason = "transmitted decode only adjusts frequencies";
      return analysis;
    }

    if (parts.size() >= 7 && snapshot.fastMode && parts[5] == "CQ" && snapshot.transceiverOnline) {
      bool ok = false;
      auto const kHz = parts[6].toUInt(&ok);
      if (ok && kHz >= 10 && parts[6].size() == 3) {
        auto const dialFrequency = snapshot.nominalFrequency / 1000000 * 1000000 + 1000 * kHz;
        appendFrequencyEffect(plan, QsoReactionEffect::Kind::SetRigFrequency, dialFrequency);
        appendFrequencyEffect(plan, QsoReactionEffect::Kind::DisplayQsy, snapshot.nominalFrequency,
                              QString {"QSY %1"}.arg(snapshot.nominalFrequency / 1e6, 7, 'f', 3));
        if (snapshot.mode == "MSK144") {
          appendFrequencyEffect(plan, QsoReactionEffect::Kind::SetMsk144BaseFrequency, dialFrequency);
        }
      }
    }

    int const nmod = fmod(double(message.timeInSeconds()), 2.0 * snapshot.trPeriod);
    bool txFirst = nmod != 0;
    if (snapshot.specOp == SpecOp::HOUND) txFirst = false;
    if (snapshot.specOp == SpecOp::FOX) txFirst = true;
    appendBoolEffect(plan, QsoReactionEffect::Kind::SetTxFirst, txFirst);

    analysis.messageWords = message.messageWords();
    if (analysis.messageWords.size() < 3) {
      analysis.reason = "message has too few message words";
      return analysis;
    }

    message.deCallAndGrid(analysis.hisCall, analysis.hisGrid);
    if (snapshot.doubleClicked && analysis.hisCall == snapshot.baseCall) {
      analysis.reason = "double-click would start QSO with own base call";
      return analysis;
    }

    QString const cleanMessage = message.clean_string().remove("<").remove(">");
    if (snapshot.doubleClicked
        && cleanMessage.contains(" " + snapshot.baseCall + " ")
        && cleanMessage.contains(" " + analysis.hisCall + " ")
        && message.clean_string().mid(22).contains(" 73")) {
      analysis.reason = "double-click on final 73 from current QSO";
      return analysis;
    }

    analysis.payloadWords = message.clean_string().mid(22).remove("<").remove(">").split(" ", SkipEmptyParts);
    if (analysis.payloadWords.size() >= 4) {
      if (analysis.messageWords.size() < 4) {
        analysis.reason = "payload has contest shape but message words are incomplete";
        return analysis;
      }
      int const serial = analysis.payloadWords.at(analysis.payloadWords.size() - 2).toInt();
      if (isEuVhfType5Exchange(serial)) {
        analysis.hisCall = analysis.payloadWords.at(1);
        analysis.hisGrid = analysis.payloadWords.at(analysis.payloadWords.size() - 1);
      }
    }

    analysis.is73 = analysis.messageWords.filter(QRegularExpression {"^(73|RR73)$"}).size();
    if (!analysis.is73 && !message.isStandardMessage() && !message.clean_string().contains("<")) {
      analysis.reason = "non-standard free text is not processable";
      return analysis;
    }

    if ((message.isJT9() && snapshot.mode != "JT9" && snapshot.mode != "JT4")
        || (message.isJT65() && snapshot.mode != "JT65" && snapshot.mode != "JT4")) {
      analysis.reason = "JT9/JT65 decode not allowed in current mode";
      return analysis;
    }

    if (snapshot.specOp == SpecOp::HOUND
        && message.messageWords().indexOf(QRegularExpression {R"(R[-+][0-9]+)"}) >= 1) {
      analysis.reason = "hound ignores other hounds";
      return analysis;
    }

    analysis.firstCall = message.call();
    if (analysis.firstCall.length() >= 4 && analysis.firstCall.mid(0, 3) == "CQ ") {
      analysis.firstCall = "CQ";
    }
    if (!snapshot.fastMode
        && (!snapshot.enableVhfFeatures || snapshot.mode == "FT8"
            || snapshot.mode == "FT4" || snapshot.mode == "FST4")) {
      bool const callOrCq = (Radio::is_callsign(analysis.firstCall)
                             && analysis.firstCall != snapshot.myCall
                             && analysis.firstCall != snapshot.baseCall
                             && analysis.firstCall != "DE")
        || analysis.firstCall == "CQ" || analysis.firstCall == "QRZ"
        || snapshot.modifiers.ctrl || snapshot.modifiers.shift;
      if (callOrCq) {
        if (((snapshot.specOp != SpecOp::HOUND) || snapshot.mode != "FT8")
            && (!snapshot.holdTxFrequency || snapshot.modifiers.shift || snapshot.modifiers.ctrl)) {
          appendIntEffect(plan, QsoReactionEffect::Kind::SetTxFrequency, frequency);
        }
        if (!canContinueAfterTxFrequencyPick(snapshot.mode)) {
          analysis.reason = "frequency-pick-only mode";
          return analysis;
        }
      }
    }

    analysis.qsoPartnerBaseCall = Radio::base_callsign(snapshot.dxCall);
    analysis.hisBaseCall = Radio::base_callsign(analysis.hisCall);
    analysis.continueProcessing = true;
    return analysis;
  }

  void appendReactedTail(DecodedText const& message, QsoReactionSnapshot const& snapshot,
                         EntryAnalysis const& analysis, QsoProgress progress,
                         QsoReactionPlan& plan)
  {
    if (snapshot.autoReply) {
      appendBoolEffect(plan, QsoReactionEffect::Kind::SetCallingCq, progress == QsoProgress::Calling);
    }
    appendIntEffect(plan, QsoReactionEffect::Kind::SetMaxPoints, -1);
    if (snapshot.rxFrequencyEnabled && snapshot.mode != "MSK144" && !snapshot.modifiers.shift) {
      appendIntEffect(plan, QsoReactionEffect::Kind::SetRxFrequency, message.frequencyOffset());
    }

    QString const cleanMessage = message.clean_string().trimmed();
    appendEffect(plan, QsoReactionEffect::Kind::RefreshQsoPaneIfChanged);

    if (Radio::is_callsign(analysis.hisCall)
        && (analysis.hisBaseCall != analysis.qsoPartnerBaseCall
            || analysis.hisBaseCall != analysis.hisCall)) {
      if (analysis.qsoPartnerBaseCall != analysis.hisBaseCall) {
        appendEffect(plan, QsoReactionEffect::Kind::ClearDxGrid);
      }
      bool const suppressCallChange = cleanMessage.contains(" " + snapshot.myCall + " ")
        && cleanMessage.mid(22).contains(" 73")
        && snapshot.respondSelection == "CQ: Max dB";
      if (!suppressCallChange) {
        appendTextEffect(plan, QsoReactionEffect::Kind::SetDxCall, analysis.hisCall);
      }
    }
    if (analysis.hisGrid.contains(gridExpression())) {
      appendTextEffect(plan, QsoReactionEffect::Kind::SetDxGrid, analysis.hisGrid);
    }
    appendEffect(plan, QsoReactionEffect::Kind::Lookup);
    appendEffect(plan, QsoReactionEffect::Kind::CaptureHisGrid);

    if (snapshot.doubleClicked) {
      appendEffect(plan, QsoReactionEffect::Kind::ExtractReceivedReport);
    }

    if (!snapshot.sentReport || analysis.hisBaseCall != analysis.qsoPartnerBaseCall
        || snapshot.mode == "FT8" || snapshot.mode == "FT4" || snapshot.mode == "FST4") {
      int report = message.report().toInt();
      if (snapshot.mode == "MSK144" && snapshot.shortMessages) {
        if (report <= -2) report = -3;
        if (report >= -1 && report <= 1) report = 0;
        if (report >= 2 && report <= 4) report = 3;
        if (report >= 5 && report <= 7) report = 6;
        if (report >= 8 && report <= 11) report = 10;
        if (report >= 12 && report <= 14) report = 13;
        if (report >= 15) report = 16;
      }
      appendIntEffect(plan, QsoReactionEffect::Kind::SetReport, report);
    }

    appendBoolEffect(plan, QsoReactionEffect::Kind::SetTuMessage, false);
    if (!snapshot.tx73Count) {
      appendEffect(plan, QsoReactionEffect::Kind::GenerateStandardMessages);
    }
    if (snapshot.transmitting) {
      appendBoolEffect(plan, QsoReactionEffect::Kind::SetRestart, true);
    }
    if (snapshot.autoSequenceEnabled && !snapshot.doubleClicked && snapshot.mode != "FT4") return;
    if (snapshot.quickCall && snapshot.doubleClicked) {
      appendBoolEffect(plan, QsoReactionEffect::Kind::SetAutoEnabled, true);
    }
    appendBoolEffect(plan, QsoReactionEffect::Kind::SetDoubleClicked, false);
  }
}

namespace DecodedMessageReaction
{
  QsoReactionPlan planProcessMessage(DecodedText const& message, QsoReactionSnapshot const& snapshot)
  {
    QsoReactionPlan plan;
    EntryAnalysis const analysis = analyzeEntry(message, snapshot, plan);
    if (!analysis.continueProcessing) {
      plan.reason = analysis.reason;
      return plan;
    }

    auto progress = snapshot.qsoProgress;
    bool reacted = false;
    QString dtext = " " + message.clean_string() + " ";
    dtext = dtext.remove("<").remove(">");
    bool const addressed = dtext.contains(" " + snapshot.baseCall + " ")
      || dtext.contains("<" + snapshot.baseCall + "> ")
      || dtext.contains("/" + snapshot.baseCall + " ")
      || dtext.contains(" " + snapshot.baseCall + "/")
      || analysis.firstCall == "DE";

    if (addressed) {
      QString word2;
      int const payloadSize = analysis.payloadWords.size();
      if (payloadSize >= 3) word2 = analysis.payloadWords.at(2);
      int reportOrSerial = word2.toInt();
      QString finalPayloadWord;
      if (payloadSize >= 4) {
        reportOrSerial = analysis.payloadWords.at(payloadSize - 2).toInt();
        finalPayloadWord = analysis.payloadWords.at(payloadSize - 1);
      }
      bool const rttyReport = reportOrSerial >= 529 && reportOrSerial <= 599;
      bool const euVhfExchange = isEuVhfType5Exchange(reportOrSerial);
      bool hintShown = snapshot.contestHintShown;
      if (!hintShown && euVhfExchange && snapshot.specOp != SpecOp::EU_VHF) {
        appendContestHint(plan, ContestHint::EuVhf);
        hintShown = true;
      }

      QStringList fullWords = message.clean_string().split(' ', SkipEmptyParts);
      int const fullWordCount = fullWords.size();
      if (fullWordCount < 2) {
        plan.reason = "decode has too few full-line fields";
        return plan;
      }
      QString fieldDayLead = fullWords.at(fullWordCount - 2);
      QString const fieldDayClass = fieldDayLead.right(1);
      bool fieldDayMessage = fieldDayClass >= "A" && fieldDayClass <= "F"
        && fieldDayLead.size() <= 3 && fullWordCount >= 9;
      int const fieldDayNumber = fieldDayLead.remove(fieldDayClass).toInt();
      if (fieldDayNumber < 1) fieldDayMessage = false;
      if (fieldDayMessage) {
        appendTextEffect(plan, QsoReactionEffect::Kind::SetReceivedExchange,
                         fullWords.at(fullWordCount - 2) + " " + fullWords.at(fullWordCount - 1));
        fieldDayLead = fullWords.at(fullWordCount - 3);
      }
      if (!hintShown) {
        if (fieldDayMessage && snapshot.specOp != SpecOp::FIELD_DAY) {
          appendContestHint(plan, ContestHint::FieldDay);
          hintShown = true;
        } else if (rttyReport && snapshot.specOp != SpecOp::RTTY) {
          appendContestHint(plan, ContestHint::Rtty);
          hintShown = true;
        }
      }

      if ((snapshot.specOp == SpecOp::EU_VHF || snapshot.specOp == SpecOp::RTTY
           || snapshot.specOp == SpecOp::FIELD_DAY)
          && message.string().contains("<...>")) {
        plan.reason = "contest message contains unresolved call";
        return plan;
      }
      if (snapshot.specOp == SpecOp::EU_VHF && analysis.messageWords.size() > 3
          && analysis.messageWords.at(2).contains(snapshot.baseCall)
          && !analysis.messageWords.at(3).contains(analysis.qsoPartnerBaseCall)
          && !snapshot.doubleClicked) {
        plan.reason = "EU VHF message is from a different QSO partner";
        return plan;
      }

      bool const contestCapable = snapshot.mode == "FT4" || snapshot.mode == "FT8"
        || snapshot.mode == "Q65" || snapshot.mode == "MSK144";
      if (analysis.messageWords.size() > 4
          && (analysis.messageWords.at(2).contains(snapshot.baseCall)
              || analysis.messageWords.at(2) == "DE")
          && (analysis.messageWords.at(3).contains(analysis.qsoPartnerBaseCall)
              || snapshot.doubleClicked || euVhfExchange || progress == QsoProgress::Calling)) {
        reacted = true;
        if (analysis.messageWords.at(4).contains(gridExpression()) && snapshot.specOp != SpecOp::EU_VHF) {
          if ((snapshot.specOp == SpecOp::NA_VHF || snapshot.specOp == SpecOp::WW_DIGI
               || snapshot.specOp == SpecOp::ARRL_DIGI || snapshot.specOp == SpecOp::Q65_PILEUP)
              && contestCapable) {
            setTxMessageAndProgress(plan, progress, 3, QsoProgress::RogerReport);
            if (snapshot.specOp == SpecOp::NA_VHF && snapshot.mode == "FT4" && snapshot.ncccSprint) {
              if (snapshot.autoEnabled && snapshot.loggingEnabled) {
                appendEffect(plan, QsoReactionEffect::Kind::RequestLogQso);
              }
              appendIntEffect(plan, QsoReactionEffect::Kind::ScheduleNcccAutoReset,
                              int(850.0 * snapshot.trPeriod));
              if (snapshot.loggingEnabled) {
                appendBoolEffect(plan, QsoReactionEffect::Kind::SetNoLogging, true);
                appendIntEffect(plan, QsoReactionEffect::Kind::ScheduleNoLoggingReset, 20000);
              }
            }
          } else if (snapshot.mode == "JT65" && analysis.messageWords.size() > 5
                     && analysis.messageWords.at(5) == "OOO") {
            setTxMessageAndProgress(plan, progress, 3, QsoProgress::RogerReport);
          } else {
            setTxMessageAndProgress(plan, progress, 2, QsoProgress::Report);
          }
          reacted = true;
        } else if (finalPayloadWord.contains(gridExpression()) && snapshot.specOp == SpecOp::EU_VHF) {
          if (!reportOrSerial) {
            setTxMessageAndProgress(plan, progress, 2, QsoProgress::Report);
          } else if (word2 == "R") {
            setTxMessageAndProgress(plan, progress, 4, QsoProgress::Rogers);
          } else {
            setTxMessageAndProgress(plan, progress, 3, QsoProgress::RogerReport);
          }
          reacted = true;
        } else if (snapshot.specOp == SpecOp::RTTY && rttyReport) {
          if (word2 == "R") {
            setTxMessageAndProgress(plan, progress, 4, QsoProgress::Rogers);
          } else {
            setTxMessageAndProgress(plan, progress, 3, QsoProgress::RogerReport);
          }
          appendTextEffect(plan, QsoReactionEffect::Kind::SetReceivedExchange,
                           fullWords[fullWordCount - 2] + " " + fullWords[fullWordCount - 1]);
          reacted = true;
        } else if (snapshot.specOp == SpecOp::FIELD_DAY && fieldDayMessage) {
          if (fieldDayLead == "R") {
            setTxMessageAndProgress(plan, progress, 4, QsoProgress::Rogers);
          } else {
            setTxMessageAndProgress(plan, progress, 3, QsoProgress::RogerReport);
          }
          reacted = true;
        } else {
          QString const word3 = analysis.messageWords.at(4);
          int const word3Number = word3.toInt();
          bool const signoffLike = word3 == "RRR"
            || (word3Number == 73 && progress == QsoProgress::Rogers)
            || word3 == "RR73" || (word3 == "R" && progress != QsoProgress::Report);
          if (signoffLike) {
            if (snapshot.mode == "FT4" && word3 == "RR73") {
              appendEffect(plan, QsoReactionEffect::Kind::RecordRr73Received);
            }
            appendBoolEffect(plan, QsoReactionEffect::Kind::SetTuMessage, false);
            appendTextEffect(plan, QsoReactionEffect::Kind::SetNextCall, QString {});
            static QRegularExpression const leadingRReport {"^R(?!R73|RR)"};
            static QRegularExpression const rogerAck {"^RR(?:R|73)$"};
            if (word3.contains(leadingRReport) && progress != QsoProgress::RogerReport) {
              selectMessage(plan, 4);
            } else if ((progress > QsoProgress::Calling && progress < QsoProgress::Rogers)
                       || word3.contains(rogerAck)) {
              selectMessage(plan, 5);
            } else if (progress == QsoProgress::Rogers) {
              if (snapshot.loggingEnabled) {
                if (!(snapshot.mode == "FT4" && snapshot.specOp == SpecOp::NA_VHF
                      && snapshot.ncccSprint)) {
                  appendEffect(plan, QsoReactionEffect::Kind::RequestLogQso);
                }
              } else {
                appendEffect(plan, QsoReactionEffect::Kind::CeaseAutoTx);
              }
              if ((snapshot.mode == "MSK144" || (snapshot.mode == "Q65" && snapshot.repeatTx))
                  && !snapshot.sendRr73) {
                appendIntEffect(plan, QsoReactionEffect::Kind::ClickTxMessage, 5);
                appendIntEffect(plan, QsoReactionEffect::Kind::ScheduleAutoFlagOff,
                                int(1000.0 * snapshot.trPeriod));
              } else {
                selectMessage(plan, 6);
              }
            } else if (snapshot.tx1Enabled) {
              selectMessageWithProgressBetween(plan, progress, 1, QsoProgress::Replying);
            } else {
              selectMessageWithProgressBetween(plan, progress, 2, QsoProgress::Report);
            }
            if (progress >= QsoProgress::RogerReport) {
              setProgress(plan, progress, QsoProgress::Signoff);
            }
            reacted = true;
          } else if ((progress >= QsoProgress::Report
                      || (progress >= QsoProgress::Replying
                          && (snapshot.mode == "MSK144" || snapshot.mode == "FT8"
                              || snapshot.mode == "FT4" || snapshot.mode == "Q65")))
                     && word3.startsWith('R')) {
            appendIntEffect(plan, QsoReactionEffect::Kind::SetTxMessageIndex, 4);
            setProgress(plan, progress, QsoProgress::Rogers);
            if (snapshot.specOp == SpecOp::RTTY) {
              int const rttyExchange = fullWords[fullWordCount - 2].toInt();
              if (rttyExchange >= 529 && rttyExchange <= 599) {
                appendTextEffect(plan, QsoReactionEffect::Kind::SetReceivedExchange,
                                 fullWords[fullWordCount - 2] + " " + fullWords[fullWordCount - 1]);
              }
            }
            appendIntEffect(plan, QsoReactionEffect::Kind::CheckTxMessage, 4);
            reacted = true;
          } else if (progress >= QsoProgress::Calling
                     && ((word3Number >= -50 && word3Number <= 49)
                         || (word3Number >= 529 && word3Number <= 599))) {
            if (snapshot.specOp == SpecOp::EU_VHF || snapshot.specOp == SpecOp::FIELD_DAY
                || snapshot.specOp == SpecOp::RTTY) {
              setTxMessageAndProgress(plan, progress, 2, QsoProgress::Report);
            } else if (word3.startsWith("R-") || word3.startsWith("R+")) {
              setTxMessageAndProgress(plan, progress, 4, QsoProgress::Rogers);
            } else {
              setTxMessageAndProgress(plan, progress, 3, QsoProgress::RogerReport);
            }
            reacted = true;
          }
        }
      } else if (analysis.messageWords.size() == 5
                 && snapshot.baseCall == analysis.messageWords.at(1)) {
        if (snapshot.loggingEnabled) appendEffect(plan, QsoReactionEffect::Kind::RequestLogQso);
        else appendEffect(plan, QsoReactionEffect::Kind::CeaseAutoTx);
        selectMessage(plan, 6);
        reacted = true;
      } else if (progress >= QsoProgress::Rogers && analysis.messageWords.size() > 3
                 && analysis.messageWords.at(2).contains(snapshot.baseCall)
                 && analysis.messageWords.at(3) == "73") {
        selectMessage(plan, 5);
        setProgress(plan, progress, QsoProgress::Signoff);
        reacted = true;
      } else if (!(snapshot.autoReply && progress > QsoProgress::Calling)) {
        if (analysis.messageWords.size() > 5
            && analysis.messageWords.at(2).contains(snapshot.baseCall)
            && analysis.messageWords.at(5) == "OOO") {
          selectMessageWithProgressBetween(plan, progress, 3, QsoProgress::RogerReport);
          reacted = true;
        } else if (!analysis.is73) {
          selectMessageWithProgressBetween(plan, progress, 2, QsoProgress::Report);
          if (snapshot.doubleClickAfterCqFrequency && snapshot.transmitting) {
            appendEffect(plan, QsoReactionEffect::Kind::StopTx);
            appendIntEffect(plan, QsoReactionEffect::Kind::StartTxAgainTimer, 1500);
          }
          appendBoolEffect(plan, QsoReactionEffect::Kind::SetDoubleClickAfterCqFrequency, false);
          reacted = true;
        }
      }
    } else if (analysis.firstCall == "DE" && analysis.messageWords.size() > 4
               && analysis.messageWords.at(4) == "73") {
      if (progress >= QsoProgress::Rogers
          && analysis.hisBaseCall == analysis.qsoPartnerBaseCall && snapshot.currentMessageType) {
        selectMessage(plan, 5);
        setProgress(plan, progress, QsoProgress::Signoff);
      } else if (snapshot.tx1Enabled) {
        selectMessageWithProgressBetween(plan, progress, 1, QsoProgress::Replying);
      } else {
        selectMessageWithProgressBetween(plan, progress, 2, QsoProgress::Report);
      }
      reacted = true;
    } else if (analysis.is73 && !message.isStandardMessage()) {
      selectMessage(plan, 5);
      setProgress(plan, progress, QsoProgress::Signoff);
      reacted = true;
    } else if (!(snapshot.autoEnabled && snapshot.selectedTxMessage == 3
                 && message.string().contains("73 "))) {
      if (snapshot.tx1Enabled) {
        selectMessageWithProgressBetween(plan, progress, 1, QsoProgress::Replying);
      } else {
        selectMessageWithProgressBetween(plan, progress, 2, QsoProgress::Report);
      }
      reacted = true;
    } else {
      reacted = true;
    }

    if (!reacted) {
      plan.reason = "message does not require a response";
      return plan;
    }
    plan.disposition = ReactionDisposition::Reacted;
    plan.reason = "message advances QSO reaction";
    appendReactedTail(message, snapshot, analysis, progress, plan);
    return plan;
  }

  QsoReactionPlan planAutoSequence(DecodedText const& message, QsoReactionSnapshot const& snapshot,
                                   AutoSequencePhase phase, unsigned startTolerance,
                                   unsigned stopTolerance)
  {
    QsoReactionPlan plan;
    if (phase == AutoSequencePhase::LegacyShortMessage) {
      if (qAbs(message.frequencyOffset() - snapshot.rxFrequency) > int(startTolerance)) {
        plan.reason = "short message is out of tolerance";
        return plan;
      }
      QString const text = message.string();
      if (text.contains(" " + snapshot.myCall + " ") && text.contains(" OOO")) {
        appendIntEffect(plan, QsoReactionEffect::Kind::SetTxMessage, 3);
      }
      if (text.contains(" RO")) appendIntEffect(plan, QsoReactionEffect::Kind::SetTxMessage, 4);
      if (text.contains(" RRR")) appendIntEffect(plan, QsoReactionEffect::Kind::SetTxMessage, 5);
      if (text.contains(" 73")) appendIntEffect(plan, QsoReactionEffect::Kind::SetTxMessage, 5);
      if (!plan.effects.isEmpty()) {
        plan.disposition = ReactionDisposition::Reacted;
        plan.reason = "short message advances auto sequence";
      } else {
        plan.reason = "short message does not advance auto sequence";
      }
      return plan;
    }

    QStringList const messageWords = message.messageWords();
    bool const is73 = messageWords.filter(QRegularExpression {"^(73|RR73)$"}).size();
    QString const messageWithoutHash = message.clean_string().mid(22).remove("<").remove(">");
    bool const mskRReport = snapshot.mode == "MSK144"
      && messageWithoutHash.indexOf(snapshot.dxCall + " R ") > 0;
    if (messageWords.size() <= 3 || !(message.isStandardMessage() || is73 || mskRReport)) {
      plan.reason = "message is not an auto-sequence candidate";
      return plan;
    }

    int const frequency = message.frequencyOffset();
    bool const withinTolerance = qAbs(snapshot.rxFrequency - frequency) <= int(startTolerance)
      || qAbs(snapshot.txFrequency - frequency) <= int(startTolerance);
    QStringList const words = messageWithoutHash.split(" ", SkipEmptyParts);
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
    if (euVhfSerial && message.clean_string().contains("<" + snapshot.myCall + "> ")) {
      appendTextEffect(plan, QsoReactionEffect::Kind::SetReceivedExchange,
                       message.clean_string().trimmed().right(13));
    }

    if (snapshot.autoEnabled
        && (snapshot.qsoProgress == QsoProgress::Replying
            || (!snapshot.tx1Enabled && snapshot.qsoProgress == QsoProgress::Report))
        && snapshot.specOp != SpecOp::HOUND
        && qAbs(snapshot.txFrequency - frequency) <= int(stopTolerance)
        && messageWords.at(2) != "DE"
        && !messageWords.at(2).contains(QRegularExpression {"(^(CQ|QRZ))|" + snapshot.baseCall})
        && messageWords.at(3).contains(Radio::base_callsign(snapshot.dxCall))) {
      appendEffect(plan, QsoReactionEffect::Kind::ClickStopTx);
      appendEffect(plan, QsoReactionEffect::Kind::LogStopped);
      plan.disposition = ReactionDisposition::Reacted;
      plan.reason = "reply from QSO partner appears directed to another caller";
      return plan;
    }

    bool const acceptable73 = isAcceptableFreeText73(message, messageWords, snapshot);
    bool const directlyForUs = messageWords.at(2).contains(snapshot.baseCall)
      && (messageWords.at(3).contains(Radio::base_callsign(snapshot.dxCall)) || euVhfSerial);
    bool const rr73Style = messageWords.at(1) == snapshot.baseCall;
    bool const type2Or73 = withinTolerance
      && (acceptable73 || (messageWords.at(2) == "DE"
                           && thirdPayloadWord.contains(Radio::base_callsign(snapshot.hisCall))));
    bool const autoReplyForUs = snapshot.callingCq && snapshot.autoReply
      && ((withinTolerance && messageWords.at(2) == "DE")
          || messageWords.at(2).contains(snapshot.baseCall));
    if (snapshot.specOp != SpecOp::FOX && snapshot.autoEnabled && snapshot.autoSequenceEnabled
        && ((!snapshot.callingCq && !snapshot.sentFirst73
             && (directlyForUs || rr73Style || type2Or73))
            || autoReplyForUs)) {
      appendEffect(plan, QsoReactionEffect::Kind::ProcessSyntheticMessageNow);
      plan.disposition = ReactionDisposition::Reacted;
      plan.reason = "message advances auto sequence";
    } else {
      plan.reason = "message does not advance auto sequence";
    }
    return plan;
  }

  QsoReactionPlan planWaitReplyCall(DecodedText const& message, QsoReactionSnapshot const& snapshot,
                                    WaitDecodeSource source)
  {
    QsoReactionPlan plan;
    bool const fastPolicy = source == WaitDecodeSource::Msk144FastDecoder
      && snapshot.mode == "MSK144";
    bool const slowPolicy = source == WaitDecodeSource::SlowDecoder
      && isSlowReactionMode(snapshot.mode);
    if ((!fastPolicy && !slowPolicy) || snapshot.hisCall.isEmpty()) {
      plan.reason = "decoder source does not match Wait policy";
      return plan;
    }

    bool const nccc = slowPolicy && snapshot.mode == "FT4"
      && snapshot.specOp == SpecOp::NA_VHF && snapshot.ncccSprint;
    if (!snapshot.waitFeaturesEnabled && !nccc
        && !(fastPolicy && snapshot.waitAndCallControlChecked)) {
      plan.reason = "Wait features are not active";
      return plan;
    }

    QString text = message.string();
    text.replace("<", "").replace(">", "");
    bool const direct = text.contains(" " + snapshot.myCall + " " + snapshot.hisCall);

    if (nccc && !snapshot.hisGrid.isEmpty()
        && text.contains(" " + snapshot.myCall + " " + snapshot.hisCall
                         + " R " + snapshot.hisGrid.left(4))) {
      if (snapshot.loggingEnabled) appendEffect(plan, QsoReactionEffect::Kind::RequestLogQso);
      appendIntEffect(plan, QsoReactionEffect::Kind::ScheduleStopTx, 500);
      plan.disposition = ReactionDisposition::Reacted;
    }

    bool waitAndCall = snapshot.waitAndCall;
    if (fastPolicy && snapshot.waitAndCallControlChecked && direct) {
      appendEffect(plan, QsoReactionEffect::Kind::StopWaitCallTimer);
      appendEffect(plan, QsoReactionEffect::Kind::DisableWaitAndCallControl);
      appendBoolEffect(plan, QsoReactionEffect::Kind::SetNoWaitAndCall, false);
      waitAndCall = false;
      plan.disposition = ReactionDisposition::Reacted;
    }

    bool const fullDuplexBlocked = fastPolicy && snapshot.fullDuplexEnabled && snapshot.txing;
    bool const reply = direct
      && (((!text.contains("73 ") && snapshot.waitFeaturesEnabled
            && !snapshot.autoButtonChecked && !fullDuplexBlocked)) || nccc);

    if (reply && slowPolicy && snapshot.specOp == SpecOp::HOUND
        && (text.mid(4, 2).contains("15") || text.mid(4, 2).contains("45"))) {
      plan.disposition = ReactionDisposition::AbortDecodeBatch;
      plan.reason = "hound ignores slow decode from wrong time slot";
      return plan;
    }

    if (reply) {
      appendEffect(plan, QsoReactionEffect::Kind::ResetWatchdog);
      appendBoolEffect(plan, QsoReactionEffect::Kind::SetDoubleClicked, true);
      appendEffect(plan, QsoReactionEffect::Kind::ProcessSyntheticMessageNow);
      appendBoolEffect(plan, QsoReactionEffect::Kind::SetAutoEnabled, true);
      if (nccc) {
        appendEffect(plan, QsoReactionEffect::Kind::RequestLogQsoUnlessSuppressed);
      } else if (snapshot.waitFeaturesEnabled) {
        appendIntEffect(plan, QsoReactionEffect::Kind::StartWaitReplyTimer,
                        int((fastPolicy ? 12000.0 : 8000.0) * snapshot.trPeriod));
      }
      plan.disposition = ReactionDisposition::Reacted;
    }

    bool call = false;
    if (waitAndCall && snapshot.specOp != SpecOp::FOX && snapshot.autoSequenceChecked
        && snapshot.waitFeaturesEnabled && (fastPolicy || !snapshot.noWaitAndCall)) {
      bool const callTrigger = text.contains("CQ " + snapshot.hisCall)
        || text.contains("CQ DX " + snapshot.hisCall)
        || text.contains(snapshot.hisCall + " RR73")
        || text.contains(snapshot.hisCall + " RRR")
        || text.contains(snapshot.hisCall + " 73");
      call = callTrigger && !fullDuplexBlocked;
    }

    if (call) {
      if (slowPolicy && !direct) {
        appendBoolEffect(plan, QsoReactionEffect::Kind::SetBlockRightDisplay, true);
      }
      appendBoolEffect(plan, QsoReactionEffect::Kind::SetDoubleClicked, true);
      appendEffect(plan, QsoReactionEffect::Kind::ProcessSyntheticMessageNow);
      appendBoolEffect(plan, QsoReactionEffect::Kind::SetAutoEnabled, true);
      appendBoolEffect(plan, QsoReactionEffect::Kind::SetNoWaitAndCall, true);
      appendIntEffect(plan, QsoReactionEffect::Kind::StartWaitCallTimer,
                      int(6200.0 * snapshot.trPeriod));
      plan.disposition = ReactionDisposition::Reacted;
    }

    if (plan.disposition == ReactionDisposition::NoReaction) {
      plan.reason = "message does not match Wait and Reply or Wait and Call";
    } else if (plan.reason.isEmpty()) {
      plan.reason = "message triggers Wait and Reply or Wait and Call";
    }
    return plan;
  }
}
