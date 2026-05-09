#ifndef PREFIXSUFFIX_HPP
#define PREFIXSUFFIX_HPP

#include <QSet>
#include <QString>

namespace Radio
{
    class PrefixSuffix
    {
    public:
        static QSet<QString> type1Prefixes();
        static QSet<QString> type1Suffixes();
    };
}

#endif // PREFIXSUFFIX_HPP
