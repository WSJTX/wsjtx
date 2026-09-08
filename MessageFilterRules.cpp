#include "MessageFilterRules.hpp"

#include "MessageFilter.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace
{
  using SpecOp = SpecialOperatingActivity;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  auto const SkipEmptyParts = Qt::SkipEmptyParts;
#else
  auto const SkipEmptyParts = QString::SkipEmptyParts;
#endif

  QString msk144FilterText(QString const& text, MessageFilterLogic::FilterContext const& ctx)
  {
    if (!ctx.filtersForWord2) return text;

    QStringList const words = text.mid(24).split(" ", SkipEmptyParts);
    if (words.size() < 2) return "___";
    if (words[1].length() == 2 && words[1].contains(QRegularExpression{"\\w\\w"})) {
      return words.size() > 2 ? words[2] : "___";
    }
    return words[1];
  }

  bool containsFilterMatch(QString const& filterText, QStringList const& keywords, bool startsWith)
  {
    return startsWith ? MessageFilter::startsWithAny(filterText, keywords)
                      : MessageFilter::containsAny(filterText, keywords);
  }
}

MessageFilterRules::Evaluation MessageFilterRules::evaluateMSK144Text(QString text, MessageFilterLogic::FilterContext const& ctx)
{
  Evaluation evaluation;
  text = text.replace("<", "").replace(">", "");

  bool const startsWith = ctx.filtersForWord2;
  QString const filterText = msk144FilterText(text, ctx);

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
