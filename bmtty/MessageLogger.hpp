#ifndef MESSAGE_LOGGER_HPP
#define MESSAGE_LOGGER_HPP

#include <QString>
#include <QtGlobal>

class MessageLogger
{
public:
    static void logMessage(const QString &direction, unsigned int msg, unsigned long long wParam, long long lParam);
    static void logText(const QString &text);
};

#endif // MESSAGE_LOGGER_HPP
