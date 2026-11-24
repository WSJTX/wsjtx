#include "DecodeOutputPlan.hpp"

#include "MessageFilter.hpp"
#include "Radio.hpp"
#include "qt_helpers.hpp"

#include <QRegularExpression>
#include <QtMath>

#include <cstring>

namespace
{
  using Action = DecodeOutputPlan::Action;
  using ActionKind = DecodeOutputPlan::ActionKind;
  using SpecOp = SpecialOperatingActivity;

  bool isResolvedCall(QString const& call, DecodedCallState state)
  {
    return !call.isEmpty() && call != "..."
      && (state == DecodedCallState::Literal || state == DecodedCallState::ResolvedHash);
  }

  bool isFoxRecipient(DecodedMessageFields const& fields, QString const& myCall)
  {
    return !myCall.isEmpty() && (fields.destination == myCall
      || fields.destination == Radio::base_callsign(myCall) || fields.destination == "DE");
  }

  bool isFoxReport(DecodedText const& message)
  {
    auto const& fields = message.fields();
    if (!message.hasValidHeader() || message.modeMarker() != "~" || message.isTX() || fields.hasMalformedCalls
        || !fields.acknowledgedRecipient.isEmpty() || fields.payloadWords.size() != 3
        || !isResolvedCall(fields.destination, fields.destinationState)
        || !isResolvedCall(fields.sender, fields.senderState)
        || !fields.completeTail || !fields.reportPresent
        || fields.exchangeKind != DECODER_EXCHANGE_RREPORT) return false;
    return true;
  }

  bool involvesCall(DecodedText const& message, QString const& call)
  {
    if (call.isEmpty()) return false;
    auto const& fields = message.fields();
    return fields.destination == call || fields.sender == call || fields.acknowledgedRecipient == call;
  }

  bool containsCallPair(DecodedText const& message, QString const& first, QString const& second)
  {
    if (first.isEmpty() || second.isEmpty()) return false;
    auto const& words = message.payloadWords();
    for (int index = 1; index < words.size(); ++index) {
      if (words[index - 1] == first && words[index] == second) return true;
    }
    return false;
  }

  void appendAction(QVector<Action>& actions, ActionKind kind, QString text = QString {})
  {
    actions.append(Action {kind, text});
  }

  bool isFalseDecode(DecodedText const& decoded, DecodeOutputPlan::PreparationContext const& context,
                     QString& reason)
  {
    auto const& fields = decoded.fields();
    auto const& annotations = decoded.annotations();
    auto const message = decoded.payload();
    auto const& words = decoded.payloadWords();
    bool const rogerExchange = words.contains("R");
    auto const ap = context.displayPoints && annotations.ap == 7 ? 0 : annotations.ap;
    bool const rejectA7 = ap == 7 && !context.diskData
      && (context.noA7Decodes || (SpecOp::NONE != context.specOp
          && !rogerExchange && !words.contains("RR73") && !fields.cq));
    if (rejectA7) {
      reason = "a7 decode rejected";
      return true;
    }

    bool const earlyMode = SpecOp::NONE == context.specOp
      || (context.multithreadFt8 && context.ft8DecoderStart < 2);
    auto const duplicateKey = DecodeOutputPlan::duplicateKey(decoded);
    bool const duplicate = context.mode == "FT8"
      && ((context.multithreadFt8 && context.ft8DecoderStart < 2)
          || context.nominalFrequency > 45000000)
      && !duplicateKey.isEmpty() && context.earlyDecodeKeys.contains(duplicateKey);
    bool const rover = fields.destination.endsWith("/R") || fields.sender.endsWith("/R");
    bool const portable = fields.destination.endsWith("/P") || fields.sender.endsWith("/P");
    bool const unresolved = fields.destination == "..." || fields.sender == "..."
      || fields.acknowledgedRecipient == "...";
    bool const dual = !fields.acknowledgedRecipient.isEmpty() || message.contains(';');
    bool const twoHashes = (fields.destinationState == DecodedCallState::ResolvedHash
                           || fields.destinationState == DecodedCallState::UnresolvedHash)
      && (fields.senderState == DecodedCallState::ResolvedHash
          || fields.senderState == DecodedCallState::UnresolvedHash);
    bool const suspiciousLowSnr = SpecOp::NONE == context.specOp && decoded.snr() < -12
      && (twoHashes || (fields.destination.endsWith("/P") && fields.sender.endsWith("/P"))
          || (fields.destination.endsWith("/R") && fields.sender.endsWith("/R"))
          || (rover && annotations.lowConfidence) || (dual && rogerExchange));
    if (earlyMode && (duplicate || suspiciousLowSnr)) {
      reason = duplicate ? "early duplicate rejected" : "suspicious low-SNR decode rejected";
      return true;
    }

    bool const timingShape = qAbs(decoded.dt()) >= 2.0f;
    bool const suspiciousShape = (portable && rogerExchange) || (dual && rover)
      || (dual && portable) || (unresolved && rogerExchange)
      || (unresolved && portable) || (unresolved && dual)
      || message.contains(QRegularExpression {"\\w\\w\\w\\w\\w\\w\\w\\w"})
      || message.contains(QRegularExpression {"\\d\\d\\d \\d\\d\\d"})
      || timingShape;
    bool const reduceFalseDecode = SpecOp::NONE == context.specOp && context.reduceFalseDecodes
      && context.mode == "FT8"
      && ((suspiciousShape && decoded.snr() < -15)
          || ((rover || rogerExchange) && decoded.snr() < -18)
          || ((unresolved || dual || (annotations.lowConfidence && ap)
               || rover || rogerExchange || decoded.snr() < -20) && timingShape));
    if (reduceFalseDecode) {
      reason = "reduce-false-decodes rule rejected line";
      return true;
    }
    return false;
  }

  bool shouldResetScores(DecodeOutputPlan::KeywordFilterContext const& context)
  {
    return context.pounce && isScoringAutoRespondPolicy (context.respondPolicy);
  }

  bool hiddenByGeographyOrWorkedBefore(
    DecodeOutputPlan::VisibilityFilterContext const& context,
    QString const& deCall, QString const& deGrid)
  {
    if (context.bypass) return false;
    bool const usesTerritory = (context.hideTerritory1 && !context.territory1.isEmpty())
      || (context.hideTerritory2 && !context.territory2.isEmpty())
      || (context.hideTerritory3 && !context.territory3.isEmpty())
      || (context.hideTerritory4 && !context.territory4.isEmpty());
    if (usesTerritory && context.countryName) {
      auto const countryName = context.countryName(deCall);
      if (context.hideTerritory1 && !context.territory1.isEmpty()
          && countryName.contains(context.territory1)) return true;
      if (context.hideTerritory2 && !context.territory2.isEmpty()
          && countryName.contains(context.territory2)) return true;
      if (context.hideTerritory3 && !context.territory3.isEmpty()
          && countryName.contains(context.territory3)) return true;
      if (context.hideTerritory4 && !context.territory4.isEmpty()
          && countryName.contains(context.territory4)) return true;
    }
    if (context.hideWorkedBefore && context.workedBeforeOnBand
        && context.workedBeforeOnBand(deCall, deGrid)) return true;
    bool const usesContinent = context.hideEurope || context.hideAsia
      || context.hideNorthAmerica || context.hideSouthAmerica || context.hideAfrica
      || context.hideOceania || context.hideAntarctica;
    if (usesContinent && context.continent) {
      auto const continent = context.continent(deCall);
      if (context.hideEurope && continent == "EU") return true;
      if (context.hideAsia && continent == "AS") return true;
      if (context.hideNorthAmerica && continent == "NA") return true;
      if (context.hideSouthAmerica && continent == "SA") return true;
      if (context.hideAfrica && continent == "AF") return true;
      if (context.hideOceania && continent == "OC") return true;
      if (context.hideAntarctica && continent == "AN") return true;
    }
    return false;
  }
}

namespace DecodeOutputPlan
{
  QString duplicateKey(DecodedText const& message)
  {
    return message.hasValidHeader() && !message.fields().hasMalformedCalls
      ? message.payloadWords().join(' ') : QString {};
  }

  PreparedLine prepareLine(QByteArray const& rawLine, PreparationContext const& context)
  {
    PreparedLine result;
    result.rawLine = rawLine;
    result.normalizedLine = rawLine;

    if (context.qsyEnabled) appendAction(result.actions, ActionKind::ShowQsyMessage,
                                         QString {rawLine});
    if (auto p = std::strpbrk(result.normalizedLine.constData(), "\n\r")) {
      result.normalizedLine = result.normalizedLine.left(p - result.normalizedLine.constData());
    }
    DecodedText const rawMessage {QString::fromUtf8(result.normalizedLine)};
    if (context.mode == "FT8" && context.specOp == SpecOp::FOX
        && context.activeStationsAvailable
        && (!context.activeStationsWantedOnly
            || involvesCall(rawMessage, context.myCall))) {
      appendAction(result.actions, ActionKind::AccumulateFoxActivity, QString {rawLine});
    }

    if (context.displayPoints && rawMessage.annotations().ap == 7) {
      auto const position = result.normalizedLine.lastIndexOf("a7");
      if (position >= 0) result.normalizedLine.replace(position, 2, "  ");
    }

    if (context.mode.startsWith("FST4")) {
      auto const spreadText = result.normalizedLine.mid(64, 6).trimmed();
      if (!spreadText.isEmpty()) {
        result.frequencySpread = spreadText.toFloat(&result.haveFrequencySpread);
        result.normalizedLine = result.normalizedLine.left(64);
      }
      if (context.mode == "FST4W" && context.noOwnCall
          && involvesCall(rawMessage, context.myCall)) {
        result.disposition = LineDisposition::Ignore;
        result.reason = "FST4W own-call decode ignored";
        return result;
      }
    }

    // Batch completion must bypass decode-only filtering.
    if (result.normalizedLine.indexOf("<DecodeFinished>") >= 0) {
      result.disposition = LineDisposition::Finish;
      // decoder.f90 emits nsynced, ndecoded, and navg0 after the marker;
      // Q65 packs single/averaged counts as 1000*n0+n1.
      result.batchHasDecodes = result.normalizedLine.mid(20, 4).toInt() > 0;
      int const length = result.normalizedLine.trimmed().size();
      int const packedCounts = result.normalizedLine.trimmed().mid(length - 7).toInt();
      result.q65SingleDecodes = packedCounts / 1000;
      result.q65AveragedDecodes = packedCounts % 1000;
      return result;
    }

    result.decodedOriginal = QString::fromUtf8(result.normalizedLine.constData());
    result.originalMessage = result.decodedOriginal == rawMessage.string()
      ? rawMessage : DecodedText {result.decodedOriginal};
    result.decodedForLogic = result.decodedOriginal;
    if (rawMessage.payload().startsWith("TU; ")) {
      auto const position = result.decodedForLogic.indexOf(rawMessage.payload());
      if (position >= 0) result.decodedForLogic.remove(position, 4);
    }
    result.logicMessage = result.decodedForLogic == rawMessage.string()
      ? rawMessage : DecodedText {result.decodedForLogic};
    if (isFalseDecode(result.logicMessage, context, result.reason)) {
      result.disposition = LineDisposition::Ignore;
      return result;
    }

    if (context.mode != "FT8" && context.mode != "FT4" && !context.mode.startsWith("FST4")
        && context.mode != "Q65") {
      // Expand legacy 22-character payloads to the 37-character display layout.
      result.normalizedLine = result.normalizedLine.left(44) + "              "
        + result.normalizedLine.mid(44);
    }
    QString const actionLine = QString::fromUtf8(result.normalizedLine.constData());

    if (context.mode == "JT4" || context.mode == "JT65" || context.mode == "Q65") {
      result.averageCount = result.logicMessage.annotations().averageCount;
      result.averaged = result.averageCount >= 2;
    }
    appendAction(result.actions, ActionKind::IncrementDecodeCount);
    appendAction(result.actions, ActionKind::WriteAll, actionLine.trimmed());
    if (context.mode == "FST4W") {
      appendAction(result.actions, ActionKind::UploadWsprSpot, actionLine);
    }
    if (!result.averaged && !(context.mode == "FT8" && context.specOp == SpecOp::FOX)
        && (context.mode == "FT4" || context.mode == "FT8") && context.displayPoints
        && result.logicMessage.isStandardMessage()) {
      appendAction(result.actions, ActionKind::UpdateArrlActivity);
    }
    return result;
  }

  ExperimentalFilterDecision decideExperimentalFilter(DecodedText const& message,
                                                      ExperimentalFilterContext const& context)
  {
    ExperimentalFilterDecision decision;
    decision.filtered = context.alreadyFiltered;
    if (context.mode != "FT8" || context.multithreadFt8 || !context.reduceFalseDecodes
        || message.snr() >= -20) return decision;

    auto const& fields = message.fields();
    auto const payload = message.payload();
    QString deCall = fields.sender;
    QStringList words = message.payloadWords();
    for (auto& word : words) {
      word = word.replace("/P", "").replace("/R", "").replace("/QRP", "").replace("/5W", "");
    }

    bool notInAllCallsigns = deCall != "TNX" && deCall != "73" && deCall != "GL"
      && deCall != "HNY" && deCall != "TU" && !deCall.left(4).contains("/")
      && !payload.contains("<...>") && !context.allCallsigns.contains(deCall);
    if (!words.isEmpty() && !context.allCallsigns.contains(words[0])
        && !(fields.cq || words.contains("TNX") || payload.contains("...")
             || words.contains("HNY") || words.contains("QSY") || words.contains("73")
             || words.contains("GL") || words.contains("PSE") || payload.contains("/"))) {
      notInAllCallsigns = true;
    }
    if (!notInAllCallsigns) return decision;

    deCall = deCall.replace("<", "").replace(">", "").replace("/P", "").replace("/R", "")
      .replace("/QRP", "").replace("/5W", "");
    auto const plausiblePrefix = [] (QString const& word) {
      return word.left(3).contains(QRegularExpression {"\\w\\d\\w"})
        || word.left(3).contains(QRegularExpression {"\\d\\w\\d"})
        || word.left(3).contains(QRegularExpression {"\\w\\w\\d"});
    };
    if (deCall != "TNX" && deCall != "73" && deCall != "GL" && deCall != "HNY"
        && !(plausiblePrefix(deCall) || deCall.left(4).contains("/")
             || payload.contains("<...>"))) {
      decision.filtered = true;
      decision.reason = "experimental FT8 callsign filter";
    }
    if (!words.isEmpty() && words[0] != "CQ" && words[0] != "TNX" && words[0] != "73 "
        && words[0] != "HNY" && words[0] != "QSY" && words[0] != "PSE"
        && !(plausiblePrefix(words[0]) || payload.contains("/")
             || payload.contains("<...>"))) {
      decision.filtered = true;
      decision.reason = "experimental FT8 payload filter";
    }
    return decision;
  }

  KeywordFilterDecision decideKeywordFilter(DecodedText const& message,
                                            KeywordFilterContext const& context)
  {
    KeywordFilterDecision decision;
    decision.filtered = context.alreadyFiltered;
    QString const text = message.payloadWords().join(' ');

    QString selectedWord;
    bool passMatch = false;
    bool blacklistMatch = false;
    bool whitelistMiss = false;
    if (context.filterBySecondWord) {
      selectedWord = message.fields().sender;
      if (selectedWord.isEmpty()) selectedWord = "___";
      passMatch = MessageFilter::startsWithAny(selectedWord, context.passKeywords);
      blacklistMatch = MessageFilter::startsWithAny(selectedWord, context.blacklistKeywords);
      whitelistMiss = !MessageFilter::startsWithAny(selectedWord, context.whitelistKeywords);
    } else {
      passMatch = MessageFilter::containsAny(text, context.passKeywords);
      blacklistMatch = MessageFilter::containsAny(text, context.blacklistKeywords);
      whitelistMiss = !MessageFilter::containsAny(text, context.whitelistKeywords);
    }
    decision.selectedWord = selectedWord;

    decision.alwaysPassed = context.specOp == SpecOp::NONE && context.alwaysPass && passMatch;
    if (decision.alwaysPassed) return decision;

    bool rejectedByList = false;
    if ((context.specOp == SpecOp::NONE || context.specOp == SpecOp::HOUND)
        && context.blacklistEnabled && blacklistMatch) {
      rejectedByList = true;
      decision.reason = "blacklist matched";
    } else if ((context.specOp == SpecOp::NONE || context.specOp == SpecOp::HOUND)
               && context.whitelistEnabled && whitelistMiss) {
      rejectedByList = true;
      decision.reason = "whitelist did not match";
    }
    if (rejectedByList) {
      if (!context.bypass) decision.filtered = true;
      if (!context.waitAndPounceOnly && !context.bypass) {
        decision.continueProcessing = false;
        return decision;
      }
      if (shouldResetScores(context)) decision.resetPounceScores = true;
    }
    return decision;
  }

  VisibilityFilterDecision decideVisibilityFilter(DecodedText const& message,
                                                  VisibilityFilterContext const& context)
  {
    VisibilityFilterDecision decision;
    decision.filtered = context.alreadyFiltered;

    auto const& deCall = message.fields().sender;
    auto const& deGrid = message.fields().grid;

    if (!context.alwaysPassed && hiddenByGeographyOrWorkedBefore(context, deCall, deGrid)) {
      decision.filtered = true;
      decision.reason = "geography or worked-before filter";
    }
    if (!context.bypass && context.hideIgnored && context.ignoreList.contains(deCall + ",")) {
      decision.filtered = true;
      if (decision.reason.isEmpty()) decision.reason = "ignore-list filter";
    }
    if (!context.bypass && context.hideWorkedToday) {
      QRegularExpression const today {context.today + ",[0-9][0-9]:[0-9][0-9]:[0-9][0-9]," + deCall + ","};
      QRegularExpression const yesterday {context.yesterday + ",[0-9][0-9]:[0-9][0-9]:[0-9][0-9]," + deCall + ","};
      if (context.txLog.contains(today)
          || (context.includeYesterday && context.txLog.contains(yesterday))) {
        decision.filtered = true;
        if (decision.reason.isEmpty()) decision.reason = "recently-worked filter";
      }
    }
    return decision;
  }

  bool shouldDisplayLeft(bool averaged, QString const& mode, SpecOp specOp, bool filtered,
                         bool filtersForWaitAndPounceOnly)
  {
    return !averaged && !(mode == "FT8" && specOp == SpecOp::FOX)
      && (!filtered || filtersForWaitAndPounceOnly);
  }

  RoutingDecision decideRouting(DecodedText const& originalMessage, DecodedText const& displayMessage,
                                bool averaged, RoutingContext const& context)
  {
    RoutingDecision decision;
    if (!displayMessage.hasValidHeader()) return decision;
    bool displayRight = averaged;
    int const audioFrequency = displayMessage.frequencyOffset();
    auto const& fields = displayMessage.fields();
    if (context.mode == "FT8" || context.mode == "FT4" || context.mode == "FST4"
        || context.mode == "Q65") {
      int const tolerance = context.mode == "Q65" ? context.q65Tolerance : 10;
      if (!fields.sender.isEmpty() && !fields.hasMalformedCalls) {
        decision.forUs = !context.myCall.isEmpty()
          && (fields.destination == context.myCall || fields.acknowledgedRecipient == context.myCall);
        if (context.specOp == SpecOp::FOX) {
          decision.forUs = isFoxRecipient(fields, context.myCall);
        } else if (fields.destination == "DE" && context.myCall == context.baseCall
                   && qAbs(context.rxFrequency - audioFrequency) <= tolerance) {
          decision.forUs = true;
        }
        if (context.specOp == SpecOp::FOX && decision.forUs
            && isFoxReport(displayMessage)) {
          displayRight = true;
        }
        if (context.specOp != SpecOp::FOX
            && (decision.forUs || qAbs(audioFrequency - context.wideGraphRxFrequency) <= 10)) {
          displayRight = true;
        }
        if (context.specOp == SpecOp::HOUND && !decision.forUs) displayRight = false;
        decision.competingFoxReport = context.specOp == SpecOp::FOX && audioFrequency < 1000
          && !decision.forUs && isFoxReport(displayMessage);
      }
    } else if (qAbs(audioFrequency - context.wideGraphRxFrequency) <= 10
               && !context.enableVhfFeatures) {
      displayRight = true;
    }
    if (context.mode == "Q65" && !averaged
        && !involvesCall(displayMessage, context.baseCall)) displayRight = false;
    if ((context.mode == "JT4" || context.mode == "Q65" || context.mode == "JT65")
        && involvesCall(displayMessage, context.baseCall) && context.includeAveragingVisible
        && !context.includeAveraging) displayRight = true;
    // Keep messages involving our base call and current QSO partner in the Rx
    // Frequency pane even when they are outside the passband.
    if ((context.mode == "FT8" || context.mode == "FT4") && context.specOp != SpecOp::FOX
        && containsCallPair(originalMessage, context.baseCall, context.hisCall)) {
      displayRight = true;
    }

    decision.displayRight = displayRight && !context.blockRightDisplay;
    return decision;
  }

  FoxReportDecision decideFoxReport(DecodedText const& message, FoxReportContext const& context)
  {
    FoxReportDecision decision;
    auto const& fields = message.fields();
    if (!isFoxReport(message)) {
      decision.reason = "message is not a resolved Fox acknowledgement";
      return decision;
    }
    if (!isFoxRecipient(fields, context.myCall)) {
      decision.reason = "Fox acknowledgement is addressed to another station";
      return decision;
    }
    if (context.activeHounds.contains(fields.sender)) {
      decision.houndCall = fields.sender;
    } else if (!fields.sender.contains('/')) {
      for (auto const& hound : context.activeHounds) {
        if (hound.contains('/') && Radio::base_callsign(hound) == fields.sender) {
          if (!decision.houndCall.isEmpty()) {
            decision.houndCall.clear();
            decision.reason = "Fox acknowledgement has an ambiguous Hound base call";
            return decision;
          }
          decision.houndCall = hound;
        }
      }
    }
    if (decision.houndCall.isEmpty()) {
      decision.reason = "Fox acknowledgement has no active Hound";
      return decision;
    }
    decision.report = fields.exchange.mid(1);
    return decision;
  }

  bool shouldPostPsk(QString const& mode, SpecOp specOp, bool superFox,
                     bool standardMessage, bool bandPostingAllowed)
  {
    bool const modeAllowed = mode != "FT8" || specOp != SpecOp::HOUND || superFox;
    return modeAllowed && bandPostingAllowed && (mode == "FST4W" || standardMessage);
  }

  QVector<Action> finishBatchActions(QString const& mode, SpecOp specOp,
                                     bool activeStationsAvailable, QString const& accumulatedDecodes)
  {
    QVector<Action> actions;
    if (mode == "FT8" && specOp == SpecOp::FOX && activeStationsAvailable) {
      appendAction(actions, ActionKind::FlushFoxActivity, accumulatedDecodes);
    }
    return actions;
  }
}
