#ifndef MESSAGE_LOGGER_HPP
#define MESSAGE_LOGGER_HPP

#include <QString>
#include <QtGlobal>

class MessageLogger
{
public:
    static QString logMessage(const QString &direction, unsigned int msg, unsigned long long wParam, long long lParam);
    static void logText(const QString &text);

private:
    static QString getWParamEnumName(unsigned long long wParam);
};

#endif // MESSAGE_LOGGER_HPP
