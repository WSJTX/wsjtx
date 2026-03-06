#include <QApplication>
#include <QCommandLineParser>
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <windows.h>
#include "MMTTY_Messages.hpp"
#endif
#include "MainWindow.hpp"
#include "MMTTYIF.hpp"

#include <QDateTime>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("bmtty");
    QCoreApplication::setApplicationVersion("1.0");

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
#ifdef Q_OS_WIN
    MMTTYIF::logText(QString("%1 [INIT] Command line: %2").arg(timestamp).arg(QString::fromWCharArray(GetCommandLineW())));
#else
    MMTTYIF::logText(QString("%1 [INIT] Command line: %2").arg(timestamp).arg(app.arguments().join(' ')));
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription("BMTTY Utility Command Line Parser");
    //parser.addHelpOption();
    //parser.addVersionOption();

    // Boolean flags
    QStringList flags = {"s", "t", "u", "r", "f", "d", "m", "Z", "n", "p", "a"};
    for (const QString &flag : flags) {
        parser.addOption({flag, QString("Enable %1 flag.").arg(flag)});
    }

    // Options with parameters
    QCommandLineOption hexOption("h", "Hexadecimal value.", "hex"); hexOption.setFlags(QCommandLineOption::ShortOptionStyle);   
    QCommandLineOption stringOption("C", "String value.", "string"); stringOption.setFlags(QCommandLineOption::ShortOptionStyle);
    QCommandLineOption decimalOption("T", "Decimal value.", "decimal"); decimalOption.setFlags(QCommandLineOption::ShortOptionStyle);

    parser.addOption(hexOption);
    parser.addOption(stringOption);
    parser.addOption(decimalOption);

    parser.process(app);

    CommandLineOptions opts;
    for (const QString &flag : flags) {
        opts.flags[flag] = parser.isSet(flag);
    }
    opts.hexValue = parser.value(hexOption);
    opts.stringValue = parser.value(stringOption);
    opts.decimalValue = parser.value(decimalOption).toInt();

    MainWindow window(opts);
    window.show();

#ifdef Q_OS_WIN
    UINT MSG_MMTTY = ::RegisterWindowMessageA("MMTTY");
    timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    MMTTYIF::logText(QString("%1 [INIT] Registered MMTTY message: 0x%2").arg(timestamp).arg(MSG_MMTTY, 4, 16, QChar('0')));

    WId winId = window.winId();
    HWND hwnd = reinterpret_cast<HWND>(winId);
    DWORD threadId = GetCurrentThreadId();

    HWND targetHwnd = HWND_BROADCAST;
    QString targetName = "Broadcast";
    if (!opts.hexValue.isEmpty()) {
        bool ok;
        targetHwnd = reinterpret_cast<HWND>(opts.hexValue.toULongLong(&ok, 16));
        if (ok) {
            targetName = QString("0x%1").arg(opts.hexValue);
        } else {
            targetHwnd = HWND_BROADCAST;
        }
    }

    MMTTYIF::logMessage(QString("SENT (%1)").arg(targetName), MSG_MMTTY, TXM_THREAD, static_cast<LPARAM>(threadId));
    ::PostMessageA(targetHwnd, MSG_MMTTY, TXM_THREAD, static_cast<LPARAM>(threadId));
    
    MMTTYIF::logMessage(QString("SENT (%1)").arg(targetName), MSG_MMTTY, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
    ::PostMessageA(targetHwnd, MSG_MMTTY, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
    
    MMTTYIF::logMessage(QString("SENT (%1)").arg(targetName), MSG_MMTTY, TXM_START, 0x00000000);
    ::PostMessageA(targetHwnd, MSG_MMTTY, TXM_START, 0x00000000);
#endif

    return app.exec();
}
