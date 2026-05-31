#ifndef MESSAGEFILTER_HPP
#define MESSAGEFILTER_HPP

#include <QString>
#include <QStringList>

class MessageFilter
{
public:
    static bool containsAny(const QString& text, const QStringList& keywords);
    static bool startsWithAny(const QString& text, const QStringList& keywords);
};

#endif // MESSAGEFILTER_HPP
