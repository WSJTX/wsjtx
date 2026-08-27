#include "DecodeOutputPlan.hpp"

#include "MessageFilter.hpp"
#include "qt_helpers.hpp"

#include <QRegularExpression>
#include <QtMath>

#include <cstring>

namespace
{
  using Action = DecodeOutputPlan::Action;
  using ActionKind = DecodeOutputPlan::ActionKind;
  using SpecOp = SpecialOperatingActivity;

  // Matches a standalone signal report token, e.g. " R-10"; shared by right-window
  // routing and the competing-Fox alert so the two rules cannot drift.
  QRegularExpression const fox_report_regexp {" R\\W\\d"};

  void appendAction(QVector<Action>& actions, ActionKind kind, QString text = QString {})
  {
    actions.append(Action {kind, text});
  }

  bool isFalseDecode(QByteArray const& line, DecodedText const& decoded,
                     QString const& message, DecodeOutputPlan::PreparationContext const& context,
                     QString& reason)
  {
    bool const rejectA7 = (context.noA7Decodes && line.contains("a7") && !context.diskData)
      || (line.contains("a7") && SpecOp::NONE != context.specOp && !context.diskData
          && !(line.contains(" R ") || line.contains("RR73") || line.contains("CQ ")));
    if (rejectA7) {
      reason = "a7 decode rejected";
      return true;
    }

    bool const earlyMode = SpecOp::NONE == context.specOp
      || (context.multithreadFt8 && context.ft8DecoderStart < 2);
    // A blank key would match any accumulator via contains(""), silently
    // swallowing lines shorter than 24 bytes.
    QString const duplicateKey = QString::fromUtf8(line.mid(23, 19));
    bool const duplicate = context.mode == "FT8"
      && ((context.multithreadFt8 && context.ft8DecoderStart < 2)
          || context.nominalFrequency > 45000000)
      && !duplicateKey.trimmed().isEmpty()
      && context.earlyDecodes.contains(duplicateKey);
    bool const suspiciousLowSnr = SpecOp::NONE == context.specOp && decoded.snr() < -12
      && (message.contains("> <")
          || message.contains(QRegularExpression {"(\\w+)/P (\\w+)/P "})
          || message.contains(QRegularExpression {"(\\w+)/R (\\w+)/R "})
          || (message.contains("/R ") && message.contains("?"))
          || (message.contains(";") && message.contains(" R ")));
    if (earlyMode && (duplicate || suspiciousLowSnr)) {
      reason = duplicate ? "early duplicate rejected" : "suspicious low-SNR decode rejected";
      return true;
    }

    bool const suspiciousShape = (message.contains("/P ") && message.contains(" R "))
      || (message.contains(";") && message.contains("/R "))
      || (message.contains(";") && message.contains("/P "))
      || (message.contains("<...>") && message.contains(" R "))
      || (message.contains("<...>") && message.contains("/P "))
      || (message.contains("<...>") && message.contains(";"))
      || message.contains(QRegularExpression {"\\w\\w\\w\\w\\w\\w\\w\\w"})
      || message.contains(QRegularExpression {"\\d\\d\\d \\d\\d\\d"})
      || message.contains("3.") || message.contains("2.");
    bool const reduceFalseDecode = SpecOp::NONE == context.specOp && context.reduceFalseDecodes
      && context.mode == "FT8"
      && ((suspiciousShape && decoded.snr() < -15)
          || ((message.contains("/R ") || message.contains(" R ")) && decoded.snr() < -18)
          || ((message.contains("<...>") || message.contains(";")
               || message.contains("? a") || message.contains("/R ")
               || message.contains(" R ") || decoded.snr() < -20)
              && (message.contains("3.") || message.contains("2."))));
    if (reduceFalseDecode) {
      reason = "reduce-false-decodes rule rejected line";
      return true;
    }
    return false;
  }

  void parseAveragingInfo(QByteArray const& line, QString const& mode, bool& averaged, int& count)
  {
    if (mode != "JT4" && mode != "JT65" && mode != "Q65") return;

    // Decoder suffixes f/d/a/q carry pass-specific averaging counts;
    // '*' represents ten or more contributing decodes.
    int const nf = line.indexOf("f");
    if (nf > 0) {
      count = line.mid(nf + 1, 1).toInt();
      if (line.indexOf("f*") > 0) count = 10;
    }
    int nd = -1;
    if (nf < 0) nd = line.indexOf("d");
    if (nd > 0) {
      count = line.mid(nd + 2, 1).toInt();
      if (line.mid(nd + 2, 1) == "*") count = 10;
    }
    int na = -1;
    if (nf < 0 && nd < 0) na = line.indexOf("a");
    if (na > 0) {
      count = line.mid(na + 2, 1).toInt();
      if (line.mid(na + 2, 1) == "*") count = 10;
    }
    int nq = -1;
    if (nf < 0 && nd < 0 && na < 0) nq = line.indexOf("q");
    if (nq > 0) {
      count = line.mid(nq + 2, 1).toInt();
      if (line.mid(nq + 2, 1) == "*") count = 10;
    }
    averaged = count >= 2;
  }

  bool shouldResetScores(DecodeOutputPlan::KeywordFilterContext const& context)
  {
    return context.pounce && (context.respondSelection == "CQ: Max Dist"
                              || context.respondSelection == "CQ: Max dB"
                              || context.respondSelection == "CQ: Min dB");
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
  PreparedLine prepareLine(QByteArray const& rawLine, PreparationContext const& context)
  {
    PreparedLine result;
    result.rawLine = rawLine;
    result.normalizedLine = rawLine;

    if (context.qsyEnabled) appendAction(result.actions, ActionKind::ShowQsyMessage,
                                         QString {rawLine});
    if (context.mode == "FT8" && context.specOp == SpecOp::FOX
        && context.activeStationsAvailable
        && (!context.activeStationsWantedOnly
            || rawLine.contains(" " + context.myCall.toLocal8Bit() + " ")
            || rawLine.contains(" <" + context.myCall.toLocal8Bit() + "> "))) {
      appendAction(result.actions, ActionKind::AccumulateFoxActivity, QString {rawLine});
    }

    if (auto p = std::strpbrk(result.normalizedLine.constData(), "\n\r")) {
      result.normalizedLine = result.normalizedLine.left(p - result.normalizedLine.constData());
    }
    if (context.displayPoints) result.normalizedLine.replace("a7", "  ");

    if (context.mode.startsWith("FST4")) {
      auto const spreadText = result.normalizedLine.mid(64, 6).trimmed();
      if (!spreadText.isEmpty()) {
        result.frequencySpread = spreadText.toFloat(&result.haveFrequencySpread);
        result.normalizedLine = result.normalizedLine.left(64);
      }
      auto const ownCall = context.myCall.toLocal8Bit();
      if (context.mode == "FST4W" && context.noOwnCall
          && (result.normalizedLine.contains(" " + ownCall + " ")
              || result.normalizedLine.contains("<" + ownCall + ">"))) {
        result.disposition = LineDisposition::Ignore;
        result.reason = "FST4W own-call decode ignored";
        return result;
      }
    }

    // Classify batch completion before false-decode rejection so it can never
    // be swallowed by a coincidental early-duplicate substring match.
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
    result.decodedForLogic = result.decodedOriginal;
    result.decodedForLogic.remove("TU; ");
    result.logicMessage = DecodedText {result.decodedForLogic};
    if (isFalseDecode(result.normalizedLine, result.logicMessage, result.decodedOriginal,
                      context, result.reason)) {
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

    parseAveragingInfo(result.normalizedLine, context.mode, result.averaged, result.averageCount);
    appendAction(result.actions, ActionKind::IncrementDecodeCount);
    appendAction(result.actions, ActionKind::WriteAll, actionLine.trimmed());
    if (context.mode == "FST4W") {
      appendAction(result.actions, ActionKind::UploadWsprSpot, actionLine);
    }
    if (!result.averaged && !(context.mode == "FT8" && context.specOp == SpecOp::FOX)
        && (context.mode == "FT4" || context.mode == "FT8") && context.displayPoints
        && DecodedText {actionLine}.isStandardMessage()) {
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

    QString deCall;
    QString deGrid;
    message.deCallAndGrid(deCall, deGrid);
    QStringList const words = message.string().mid(24).replace("<", "").replace(">", "")
      .replace("/P", "").replace("/R", "").replace("/QRP", "").replace("/5W", "")
      .split(" ", SkipEmptyParts);

    bool notInAllCallsigns = deCall != "TNX" && deCall != "73" && deCall != "GL"
      && deCall != "HNY" && deCall != "TU" && !deCall.left(4).contains("/")
      && !message.string().contains("<...>") && !context.allCallsigns.contains(deCall);
    if (!words.isEmpty() && !context.allCallsigns.contains(words[0])
        && !(message.string().contains(" CQ ") || message.string().contains("TNX")
             || message.string().contains("...") || message.string().contains("HNY")
             || message.string().contains("QSY") || message.string().contains("73 ")
             || message.string().contains("GL ") || message.string().contains("PSE")
             || message.string().contains("/") || message.string().contains("<...>"))) {
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
             || message.string().contains("<...>"))) {
      decision.filtered = true;
      decision.reason = "experimental FT8 callsign filter";
    }
    if (!words.isEmpty() && words[0] != "CQ" && words[0] != "TNX" && words[0] != "73 "
        && words[0] != "HNY" && words[0] != "QSY" && words[0] != "PSE"
        && !(plausiblePrefix(words[0]) || message.string().contains("/")
             || message.string().contains("<...>"))) {
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
    QString const text = message.string().replace("<", "").replace(">", "");

    QString selectedWord;
    bool passMatch = false;
    bool blacklistMatch = false;
    bool whitelistMiss = false;
    if (context.filterBySecondWord) {
      int const payloadOffset = context.mode == "FT8" || context.mode == "FT4"
        || context.mode == "Q65" || context.mode == "FST4" ? 24 : 22;
      auto const words = text.mid(payloadOffset).split(" ", SkipEmptyParts);
      if (words.size() < 2) {
        selectedWord = "___";
      } else if (words[1].length() == 2
                 && words[1].contains(QRegularExpression {"\\w\\w"})) {
        selectedWord = words.size() > 2 ? words[2] : "___";
      } else if (text.contains(";")) {
        selectedWord = words.size() > 3 ? words[3] : "___";
      } else {
        selectedWord = words[1];
      }
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

    QString deCall;
    QString deGrid;
    message.deCallAndGrid(deCall, deGrid);

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
    bool displayRight = averaged;
    int const audioFrequency = displayMessage.frequencyOffset();
    if (context.mode == "FT8" || context.mode == "FT4" || context.mode == "FST4"
        || context.mode == "Q65") {
      int const tolerance = context.mode == "Q65" ? context.q65Tolerance : 10;
      auto const parts = QString {displayMessage.string()}.remove("<").remove(">")
        .split(" ", SkipEmptyParts);
      if (parts.size() > 6) {
        decision.forUs = parts[5].contains(context.baseCall)
          || (parts[5] == "DE" && qAbs(context.rxFrequency - audioFrequency) <= tolerance);
        if (context.baseCall == context.myCall) {
          if (context.baseCall != parts[5]) decision.forUs = false;
        } else if (context.myCall != parts[5]) {
          // A shared base call does not make another prefix/suffix ours, as can
          // happen with multi-station special events.
          decision.forUs = false;
        }
        // Compound Hounds address the Fox using DE plus their full callsign.
        if (context.specOp == SpecOp::FOX && displayMessage.string().contains(" DE ")) {
          decision.forUs = true;
        }
        if (context.specOp == SpecOp::FOX && decision.forUs
            && displayMessage.string().contains(fox_report_regexp)) {
          displayRight = true;
        }
        if (context.specOp != SpecOp::FOX
            && (decision.forUs || qAbs(audioFrequency - context.wideGraphRxFrequency) <= 10)) {
          displayRight = true;
        }
        if (context.specOp == SpecOp::HOUND && !decision.forUs) displayRight = false;
        decision.competingFoxReport = context.specOp == SpecOp::FOX && audioFrequency < 1000
          && !decision.forUs && displayMessage.string().contains(fox_report_regexp);
      }
    } else if (qAbs(audioFrequency - context.wideGraphRxFrequency) <= 10
               && !context.enableVhfFeatures) {
      displayRight = true;
    }
    if (context.mode == "Q65" && !averaged
        && !displayMessage.string().contains(context.baseCall)) displayRight = false;
    if ((context.mode == "JT4" || context.mode == "Q65" || context.mode == "JT65")
        && displayMessage.string().contains(context.baseCall) && context.includeAveragingVisible
        && !context.includeAveraging) displayRight = true;
    QString unwrappedOriginal = originalMessage.string();
    unwrappedOriginal.replace("<", "").replace(">", "");
    // Keep messages involving our base call and current QSO partner in the Rx
    // Frequency pane even when they are outside the passband.
    if ((context.mode == "FT8" || context.mode == "FT4") && context.specOp != SpecOp::FOX
        && !context.baseCall.isEmpty() && !context.hisCall.isEmpty()
        && unwrappedOriginal.contains(context.baseCall + " " + context.hisCall)) {
      displayRight = true;
    }

    decision.displayRight = displayRight && !context.blockRightDisplay;
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
