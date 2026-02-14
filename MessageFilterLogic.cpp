#include "MessageFilterLogic.hpp"
#include "Decoder/decodedtext.h"
#include "MessageFilter.hpp"
#include "logbook/logbook.h"
#include "CountryNames.hpp"
#include "logbook/AD1CCty.hpp"
#include <QRegularExpression>
#include <QStringList>

using SpecOp = Configuration::SpecialOperatingActivity;

MessageFilterLogic::FilterResult MessageFilterLogic::evaluateMSK144(const DecodedText& decodedtext, const FilterContext& ctx, LogBook* logbook)
{
    FilterResult result;
    QString text = decodedtext.string().replace("<","").replace(">","");

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    auto const SkipEmptyParts = Qt::SkipEmptyParts;
#else
    auto const SkipEmptyParts = QString::SkipEmptyParts;
#endif

    auto applyFiltering = [&](const QString& filterText, bool startsWith) {
        if (SpecOp::NONE == ctx.specOp && ctx.blacklisted && 
            (startsWith ? MessageFilter::startsWithAny(filterText, ctx.blacklistKeywords) 
                        : MessageFilter::containsAny(filterText, ctx.blacklistKeywords))) {
            if (!ctx.bypass) result.filtered = true;
            if (!(ctx.filtersForWaitAndPounceOnly || ctx.bypass)) {
                result.shouldReturn = true;
                return;
            }
            if (ctx.pounce && (ctx.respondMode == "CQ: Max Dist" || ctx.respondMode == "CQ: Max dB" || ctx.respondMode == "CQ: Min dB")) {
                result.resetPoints = true;
            }
        } else if (SpecOp::NONE == ctx.specOp && ctx.whitelisted && 
                   !(startsWith ? MessageFilter::startsWithAny(filterText, ctx.whitelistKeywords) 
                                : MessageFilter::containsAny(filterText, ctx.whitelistKeywords))) {
            if (!ctx.bypass) result.filtered = true;
            if (!(ctx.filtersForWaitAndPounceOnly || ctx.bypass)) {
                result.shouldReturn = true;
                return;
            }
            if (ctx.pounce && (ctx.respondMode == "CQ: Max Dist" || ctx.respondMode == "CQ: Max dB" || ctx.respondMode == "CQ: Min dB")) {
                result.resetPoints = true;
            }
        }

        if (ctx.hideTerritory1 || ctx.hideTerritory2 || ctx.hideTerritory3 || ctx.hideTerritory4 ||
            ctx.hideB4 || ctx.hideEU || ctx.hideAS || ctx.hideNA || ctx.hideSA || ctx.hideAF ||
            ctx.hideOC || ctx.hideAN) {
            
            QString deCall;
            QString deGrid;
            decodedtext.deCallAndGrid(deCall, deGrid);

            if (ctx.hideTerritory1 || ctx.hideTerritory2 || ctx.hideTerritory3 || ctx.hideTerritory4) {
                auto const& looked_up = logbook->countries()->lookup(deCall);
                auto countryName = Radio::CountryNames::abbreviate(looked_up.entity_name);
                if (ctx.hideTerritory1 && countryName.contains(ctx.territory1) && !ctx.territory1.isEmpty() && !ctx.bypass) result.filtered = true;
                if (ctx.hideTerritory2 && countryName.contains(ctx.territory2) && !ctx.territory2.isEmpty() && !ctx.bypass) result.filtered = true;
                if (ctx.hideTerritory3 && countryName.contains(ctx.territory3) && !ctx.territory3.isEmpty() && !ctx.bypass) result.filtered = true;
                if (ctx.hideTerritory4 && countryName.contains(ctx.territory4) && !ctx.territory4.isEmpty() && !ctx.bypass) result.filtered = true;
            }

            if (ctx.hideB4) {
                bool callB4onBand, countryB4onBand, gridB4onBand, continentB4onBand, CQZoneB4onBand, ITUZoneB4onBand;
                auto const& looked_up = logbook->countries()->lookup(deCall);
                logbook->match(deCall, ctx.mode, deGrid, looked_up, callB4onBand, countryB4onBand, gridB4onBand,
                               continentB4onBand, CQZoneB4onBand, ITUZoneB4onBand, ctx.currentBand);
                if (callB4onBand && !ctx.bypass) result.filtered = true;
            }

            if (ctx.hideEU || ctx.hideAS || ctx.hideNA || ctx.hideSA || ctx.hideAF || ctx.hideOC || ctx.hideAN) {
                auto const& looked_up = logbook->countries()->lookup(deCall);
                QString continent = AD1CCty::continent(looked_up.continent);
                if (ctx.hideEU && continent == "EU" && !ctx.bypass) result.filtered = true;
                if (ctx.hideAS && continent == "AS" && !ctx.bypass) result.filtered = true;
                if (ctx.hideNA && continent == "NA" && !ctx.bypass) result.filtered = true;
                if (ctx.hideSA && continent == "SA" && !ctx.bypass) result.filtered = true;
                if (ctx.hideAF && continent == "AF" && !ctx.bypass) result.filtered = true;
                if (ctx.hideOC && continent == "OC" && !ctx.bypass) result.filtered = true;
                if (ctx.hideAN && continent == "AN" && !ctx.bypass) result.filtered = true;
            }
        }
    };

    if (ctx.filtersForWord2) {
        QString text2 = "";
        QStringList tw = text.mid(24).split(" ", SkipEmptyParts);
        if (tw.size() < 2) {
            text2 = "___";
        } else if (tw[1].length() == 2 && tw[1].contains(QRegularExpression{"\\w\\w"})) {
            if (tw.size() > 2) {
                text2 = tw[2];
            } else {
                text2 = "___";
            }
        } else {
            text2 = tw[1];
        }

        if (!(SpecOp::NONE == ctx.specOp && ctx.alwaysPass && MessageFilter::startsWithAny(text2, ctx.passKeywords))) {
            applyFiltering(text2, true);
        }
    } else {
        if (!(SpecOp::NONE == ctx.specOp && ctx.alwaysPass && MessageFilter::containsAny(text, ctx.passKeywords))) {
            applyFiltering(text, false);
        }
    }

    return result;
}
