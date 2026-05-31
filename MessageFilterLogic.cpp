#include "MessageFilterLogic.hpp"

#include "MessageFilterRules.hpp"
#include "Decoder/decodedtext.h"
#include "logbook/logbook.h"
#include "CountryNames.hpp"
#include "logbook/AD1CCty.hpp"

MessageFilterLogic::FilterResult MessageFilterLogic::evaluateMSK144(const DecodedText& decodedtext, const FilterContext& ctx, LogBook* logbook)
{
    auto evaluation = MessageFilterRules::evaluateMSK144Text(decodedtext.string(), ctx);
    auto result = evaluation.result;

    if (result.shouldReturn || !evaluation.filtersApplied) return result;

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

    return result;
}
