#include <QApplication>
#include <QCommandLineParser>
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <windows.h>
#include "MMTTY_Messages.hpp"
#endif
#include "MainWindow.hpp"
#include "MessageLogger.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("bmtty");
    QCoreApplication::setApplicationVersion("1.0");

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
    MessageLogger::logText(QString("Registered MMTTY message: 0x%1").arg(MSG_MMTTY, 4, 16, QChar('0')));

    WId winId = window.winId();
    HWND hwnd = reinterpret_cast<HWND>(winId);
    DWORD threadId = GetCurrentThreadId();

    MessageLogger::logMessage("SENT (Broadcast)", MSG_MMTTY, TXM_THREAD, static_cast<LPARAM>(threadId));
    ::PostMessageA(HWND_BROADCAST, MSG_MMTTY, TXM_THREAD, static_cast<LPARAM>(threadId));
    
    MessageLogger::logMessage("SENT (Broadcast)", MSG_MMTTY, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
    ::PostMessageA(HWND_BROADCAST, MSG_MMTTY, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
    
    MessageLogger::logMessage("SENT (Broadcast)", MSG_MMTTY, TXM_START, 0x00000000);
    ::PostMessageA(HWND_BROADCAST, MSG_MMTTY, TXM_START, 0x00000000);
#endif

    return app.exec();
}
