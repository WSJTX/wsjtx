#include "MessageLogger.hpp"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#ifdef Q_OS_WIN
#include "MMTTY_Messages.hpp"
#endif

QString MessageLogger::getWParamEnumName(unsigned long long wParam)
{
#ifdef Q_OS_WIN
    switch (wParam) {
        case RXM_HANDLE: return "RXM_HANDLE";
        case RXM_REQHANDLE: return "RXM_REQHANDLE";
        case RXM_EXIT: return "RXM_EXIT";
        case RXM_PTT: return "RXM_PTT";
        case RXM_CHAR: return "RXM_CHAR";
        case RXM_WINPOS: return "RXM_WINPOS";
        case RXM_WIDTH: return "RXM_WIDTH";
        case RXM_REQPARA: return "RXM_REQPARA";
        case RXM_SETBAUD: return "RXM_SETBAUD";
        case RXM_SETMARK: return "RXM_SETMARK";
        case RXM_SETSPACE: return "RXM_SETSPACE";
        case RXM_SETSWITCH: return "RXM_SETSWITCH";
        case RXM_SETHAM: return "RXM_SETHAM";
        case RXM_SHOWSETUP: return "RXM_SHOWSETUP";
        case RXM_SETVIEW: return "RXM_SETVIEW";
        case RXM_SETSQLVL: return "RXM_SETSQLVL";
        case RXM_SHOW: return "RXM_SHOW";
        case RXM_SETFIG: return "RXM_SETFIG";
        case RXM_SETRESO: return "RXM_SETRESO";
        case RXM_SETLPF: return "RXM_SETLPF";
        case RXM_SETTXDELAY: return "RXM_SETTXDELAY";
        case RXM_UPDATECOM: return "RXM_UPDATECOM";
        case RXM_SUSPEND: return "RXM_SUSPEND";
        case RXM_NOTCH: return "RXM_NOTCH";
        case RXM_PROFILE: return "RXM_PROFILE";
        case RXM_TIMER: return "RXM_TIMER";
        case RXM_ENBFOCUS: return "RXM_ENBFOCUS";
        case RXM_SETDEFFREQ: return "RXM_SETDEFFREQ";
        case RXM_SETLENGTH: return "RXM_SETLENGTH";
        case RXM_ENBSHARED: return "RXM_ENBSHARED";
        case RXM_PTTFSK: return "RXM_PTTFSK";
        case RXM_SOUNDSOURCE: return "RXM_SOUNDSOURCE";

        case TXM_HANDLE: return "TXM_HANDLE";
        case TXM_REQHANDLE: return "TXM_REQHANDLE";
        case TXM_START: return "TXM_START";
        // TXM_CHAR has same value as RXM_CHAR but they are in different enums context,
        // although here they might overlap if the values map identically.
        // Actually TXM_CHAR is 0x8003 and RXM_CHAR is 4, etc.
        case TXM_CHAR: return "TXM_CHAR";
        case TXM_PTTEVENT: return "TXM_PTTEVENT";
        case TXM_HEIGHT: return "TXM_HEIGHT";
        case TXM_BAUD: return "TXM_BAUD";
        case TXM_MARK: return "TXM_MARK";
        case TXM_SPACE: return "TXM_SPACE";
        case TXM_SWITCH: return "TXM_SWITCH";
        case TXM_VIEW: return "TXM_VIEW";
        case TXM_LEVEL: return "TXM_LEVEL";
        case TXM_FIGEVENT: return "TXM_FIGEVENT";
        case TXM_RESO: return "TXM_RESO";
        case TXM_LPF: return "TXM_LPF";
        case TXM_THREAD: return "TXM_THREAD";
        case TXM_PROFILE: return "TXM_PROFILE";
        case TXM_NOTCH: return "TXM_NOTCH";
        case TXM_DEFSHIFT: return "TXM_DEFSHIFT";
        case TXM_RADIOFREQ: return "TXM_RADIOFREQ";
        case TXM_SHOWSETUP: return "TXM_SHOWSETUP";
        case TXM_SHOWPROFILE: return "TXM_SHOWPROFILE";
        default: return "";
    }
#endif
    return "";
}

QString MessageLogger::logMessage(const QString &direction, unsigned int msg, unsigned long long wParam, long long lParam)
{
    QString enumName = getWParamEnumName(wParam);
    QString enumText = enumName.isEmpty() ? "" : QString(" (%1)").arg(enumName);

    QString text = QString("Msg: 0x%1, wParam: 0x%2%3, lParam: 0x%4")
        .arg(msg, 4, 16, QChar('0'))
        .arg(wParam, 8, 16, QChar('0'))
        .arg(enumText)
        .arg(lParam, 8, 16, QChar('0'));
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString formatted = QString("%1 [%2] %3").arg(timestamp).arg(direction).arg(text);
    logText(formatted);
    return formatted;
}

void MessageLogger::logText(const QString &text)
{
    QString logPath = QCoreApplication::applicationDirPath() + "/bmtty.log";
    QFile file(logPath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << text << "\n";
        file.close();
    }
}
