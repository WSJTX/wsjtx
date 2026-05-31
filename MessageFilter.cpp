#include "MessageFilter.hpp"

bool MessageFilter::containsAny(const QString& text, const QStringList& keywords)
{
    for (const QString& keyword : keywords) {
        if (!keyword.isEmpty() && text.contains(keyword)) {
            return true;
        }
    }
    return false;
}

bool MessageFilter::startsWithAny(const QString& text, const QStringList& keywords)
{
    for (const QString& keyword : keywords) {
        if (!keyword.isEmpty() && text.startsWith(keyword)) {
            return true;
        }
    }
    return false;
}
