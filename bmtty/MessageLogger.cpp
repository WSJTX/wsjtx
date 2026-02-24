#include "MessageLogger.hpp"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>

void MessageLogger::logMessage(const QString &direction, unsigned int msg, unsigned long long wParam, long long lParam)
{
    QString text = QString("Msg: 0x%1, wParam: 0x%2, lParam: 0x%3")
        .arg(msg, 4, 16, QChar('0'))
        .arg(wParam, 8, 16, QChar('0'))
        .arg(lParam, 8, 16, QChar('0'));
    
    logText(QString("[%1] %2").arg(direction).arg(text));
}

void MessageLogger::logText(const QString &text)
{
    QString logPath = QCoreApplication::applicationDirPath() + "/bmtty.log";
    QFile file(logPath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ") << text << "\n";
        file.close();
    }
}
