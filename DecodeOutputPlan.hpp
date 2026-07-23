#ifndef DECODEOUTPUTPLAN_HPP
#define DECODEOUTPUTPLAN_HPP

#include "Decoder/decodedtext.h"
#include "SpecialOperatingActivity.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace DecodeOutputPlan
{
  enum class ActionKind
  {
    ShowQsyMessage,
    AccumulateFoxActivity,
    IncrementDecodeCount,
    WriteAll,
    UploadWsprSpot,
    UpdateArrlActivity,
    FlushFoxActivity
  };

  struct Action
  {
    ActionKind kind;
    QString text;
  };

  struct PreparationContext
  {
    QString mode;
    SpecialOperatingActivity specOp {SpecialOperatingActivity::NONE};
    QString myCall;
    bool qsyEnabled {false};
    bool activeStationsAvailable {false};
    bool activeStationsWantedOnly {false};
    bool displayPoints {false};
    bool noOwnCall {false};
    bool diskData {false};
    bool noA7Decodes {false};
    bool multithreadFt8 {false};
    int ft8DecoderStart {0};
    quint64 nominalFrequency {0};
    bool reduceFalseDecodes {false};
    QString earlyDecodes;
  };

  enum class LineDisposition
  {
    Decode,
    Finish,
    Ignore
  };

  struct PreparedLine
  {
    LineDisposition disposition {LineDisposition::Decode};
    QByteArray rawLine;
    QByteArray normalizedLine;
    QString decodedOriginal;
    QString decodedForLogic;
    DecodedText logicMessage {QString {}};
    bool averaged {false};
    int averageCount {0};
    bool haveFrequencySpread {false};
    float frequencySpread {0.0f};
    // Finish-disposition fields parsed from <DecodeFinished>.
    bool batchHasDecodes {false};
    int q65SingleDecodes {0};
    int q65AveragedDecodes {0};
    QString reason;
    QVector<Action> actions;
  };

  struct KeywordFilterContext
  {
    QString mode;
    SpecialOperatingActivity specOp {SpecialOperatingActivity::NONE};
    bool alreadyFiltered {false};
    bool filterBySecondWord {false};
    bool alwaysPass {false};
    QStringList passKeywords;
    bool blacklistEnabled {false};
    QStringList blacklistKeywords;
    bool whitelistEnabled {false};
    QStringList whitelistKeywords;
    bool waitAndPounceOnly {false};
    bool bypass {false};
    bool pounce {false};
    QString respondSelection;
  };

  struct KeywordFilterDecision
  {
    bool filtered {false};
    // False stops processing the remaining buffered decoder records.
    bool continueBatch {true};
    bool alwaysPassed {false};
    bool resetPounceScores {false};
    QString selectedWord;
    QString reason;
  };

  struct VisibilityFilterContext
  {
    bool alreadyFiltered {false};
    bool alwaysPassed {false};
    bool bypass {false};
    bool hideTerritory1 {false};
    bool hideTerritory2 {false};
    bool hideTerritory3 {false};
    bool hideTerritory4 {false};
    QString territory1;
    QString territory2;
    QString territory3;
    QString territory4;
    bool hideWorkedBefore {false};
    bool hideEurope {false};
    bool hideAsia {false};
    bool hideNorthAmerica {false};
    bool hideSouthAmerica {false};
    bool hideAfrica {false};
    bool hideOceania {false};
    bool hideAntarctica {false};
    bool hideIgnored {false};
    QString ignoreList;
    bool hideWorkedToday {false};
    bool includeYesterday {false};
    QString txLog;
    QString today;
    QString yesterday;
    // Lazy resolvers so expensive logbook lookups run only when a matching
    // hide filter is enabled and the message was not always-passed.
    std::function<QString (QString const& deCall)> countryName;
    std::function<QString (QString const& deCall)> continent;
    std::function<bool (QString const& deCall, QString const& deGrid)> workedBeforeOnBand;
  };

  struct VisibilityFilterDecision
  {
    bool filtered {false};
    QString reason;
  };

  struct ExperimentalFilterContext
  {
    QString mode;
    bool multithreadFt8 {false};
    bool reduceFalseDecodes {false};
    // Contents of the shipped ALLCALL7.TXT callsign database.
    QString allCallsigns;
    bool alreadyFiltered {false};
  };

  struct ExperimentalFilterDecision
  {
    bool filtered {false};
    QString reason;
  };

  struct RoutingContext
  {
    QString mode;
    SpecialOperatingActivity specOp {SpecialOperatingActivity::NONE};
    QString myCall;
    QString baseCall;
    QString hisCall;
    int rxFrequency {0};
    int wideGraphRxFrequency {0};
    int q65Tolerance {10};
    bool enableVhfFeatures {false};
    bool includeAveragingVisible {false};
    bool includeAveraging {false};
    bool blockRightDisplay {false};
  };

  struct RoutingDecision
  {
    bool forUs {false};
    bool displayRight {false};
    bool competingFoxReport {false};
  };

  PreparedLine prepareLine(QByteArray const& rawLine, PreparationContext const& context);
  ExperimentalFilterDecision decideExperimentalFilter(DecodedText const& message,
                                                      ExperimentalFilterContext const& context);
  KeywordFilterDecision decideKeywordFilter(DecodedText const& message,
                                            KeywordFilterContext const& context);
  VisibilityFilterDecision decideVisibilityFilter(DecodedText const& message,
                                                  VisibilityFilterContext const& context);
  bool shouldDisplayLeft(bool averaged, QString const& mode, SpecialOperatingActivity specOp,
                         bool filtered, bool filtersForWaitAndPounceOnly);
  RoutingDecision decideRouting(DecodedText const& originalMessage, DecodedText const& displayMessage,
                                bool averaged, RoutingContext const& context);
  bool shouldPostPsk(QString const& mode, SpecialOperatingActivity specOp, bool superFox,
                     bool standardMessage, bool bandPostingAllowed);
  QVector<Action> finishBatchActions(QString const& mode, SpecialOperatingActivity specOp,
                                     bool activeStationsAvailable, QString const& accumulatedDecodes);
}

#endif // DECODEOUTPUTPLAN_HPP
