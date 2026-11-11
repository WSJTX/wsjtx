#include "MessageFilterRules.hpp"

#include "MessageFilter.hpp"

namespace
{
  using SpecOp = SpecialOperatingActivity;

  bool containsFilterMatch(QString const& filterText, QStringList const& keywords, bool startsWith)
  {
    return startsWith ? MessageFilter::startsWithAny(filterText, keywords)
                      : MessageFilter::containsAny(filterText, keywords);
  }
}

MessageFilterRules::Evaluation MessageFilterRules::evaluateMSK144Text(
  QString const& payload, QString const& sender, MessageFilterLogic::FilterContext const& ctx)
{
  Evaluation evaluation;

  bool const startsWith = ctx.filtersForWord2;
  QString filterText = payload;
  if (startsWith) filterText = sender.isEmpty() ? QString {"___"} : sender;

  if (SpecOp::NONE == ctx.specOp && ctx.alwaysPass && containsFilterMatch(filterText, ctx.passKeywords, startsWith)) {
    return evaluation;
  }

  evaluation.filtersApplied = true;

  if (SpecOp::NONE == ctx.specOp && ctx.blacklisted && containsFilterMatch(filterText, ctx.blacklistKeywords, startsWith)) {
    if (!ctx.bypass) evaluation.result.filtered = true;
    if (!(ctx.filtersForWaitAndPounceOnly || ctx.bypass)) {
      evaluation.result.shouldReturn = true;
      return evaluation;
    }
    if (ctx.pounce && isScoringAutoRespondPolicy (ctx.respondPolicy)) {
      evaluation.result.resetPoints = true;
    }
  } else if (SpecOp::NONE == ctx.specOp && ctx.whitelisted && !containsFilterMatch(filterText, ctx.whitelistKeywords, startsWith)) {
    if (!ctx.bypass) evaluation.result.filtered = true;
    if (!(ctx.filtersForWaitAndPounceOnly || ctx.bypass)) {
      evaluation.result.shouldReturn = true;
      return evaluation;
    }
    if (ctx.pounce && isScoringAutoRespondPolicy (ctx.respondPolicy)) {
      evaluation.result.resetPoints = true;
    }
  }

  return evaluation;
}
