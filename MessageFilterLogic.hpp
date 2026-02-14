#ifndef MESSAGEFILTERLOGIC_HPP
#define MESSAGEFILTERLOGIC_HPP

#include <QString>
#include <QStringList>
#include "Configuration.hpp"

class DecodedText;
class LogBook;

class MessageFilterLogic
{
public:
    struct FilterContext {
        Configuration::SpecialOperatingActivity specOp {Configuration::SpecialOperatingActivity::NONE};
        bool bypass {false};
        bool filtersForWord2 {false};
        bool filtersForWaitAndPounceOnly {false};
        bool alwaysPass {false};
        bool blacklisted {false};
        bool whitelisted {false};
        QStringList passKeywords;
        QStringList blacklistKeywords;
        QStringList whitelistKeywords;
        
        // Geographical/B4 filters
        bool hideTerritory1 {false};
        bool hideTerritory2 {false};
        bool hideTerritory3 {false};
        bool hideTerritory4 {false};
        bool hideB4 {false};
        bool hideEU {false};
        bool hideAS {false};
        bool hideNA {false};
        bool hideSA {false};
        bool hideAF {false};
        bool hideOC {false};
        bool hideAN {false};
        
        QString territory1;
        QString territory2;
        QString territory3;
        QString territory4;
        
        QString currentBand;
        QString mode;

        bool pounce {false};
        QString respondMode;
    };

    struct FilterResult {
        bool filtered {false};
        bool shouldReturn {false};
        bool resetPoints {false};
    };

    static FilterResult evaluateMSK144(const DecodedText& decodedtext, const FilterContext& ctx, LogBook* logbook);
};

#endif // MESSAGEFILTERLOGIC_HPP
