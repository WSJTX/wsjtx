#include "MMTTYIF.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

#ifdef Q_OS_WIN
#include "MMTTY_Messages.hpp"
#endif

MMTTYIF::MMTTYIF(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    m_msgMtty = ::RegisterWindowMessageA("MMTTY");
#endif
}

MMTTYIF::~MMTTYIF()
{
}

QString MMTTYIF::getTargetName() const
{
#ifdef Q_OS_WIN
    if (m_targetHandle == HWND_BROADCAST) {
        return "Broadcast";
    }
    return QString("0x%1").arg(reinterpret_cast<quintptr>(m_targetHandle), 8, 16, QChar('0'));
#else
    return "";
#endif
}

void MMTTYIF::initialize(const QString &hexHandleStr, WId mainWindowId)
{
#ifdef Q_OS_WIN
    m_mainWindowId = mainWindowId;

    m_targetHandle = HWND_BROADCAST;
    if (!hexHandleStr.isEmpty()) {
        bool ok;
        HWND h = reinterpret_cast<HWND>(hexHandleStr.toULongLong(&ok, 16));
        if (ok) {
            m_targetHandle = h;
        }
    }
    QString targetName = getTargetName();

    DWORD threadId = GetCurrentThreadId();

    QString logStrThread = logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_THREAD, static_cast<LPARAM>(threadId));
    emit log_message(logStrThread);
    ::PostMessageA(m_targetHandle, m_msgMtty, TXM_THREAD, static_cast<LPARAM>(threadId));

    if (m_mainWindowId) {
        HWND hwnd = reinterpret_cast<HWND>(m_mainWindowId);
        QString logStrHandle = logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
        emit log_message(logStrHandle);
        ::PostMessageA(m_targetHandle, m_msgMtty, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
    }

    QString logStrStart = logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_START, 0x00000000);
    emit log_message(logStrStart);
    ::PostMessageA(m_targetHandle, m_msgMtty, TXM_START, 0x00000000);
#else
    Q_UNUSED(hexHandleStr)
    Q_UNUSED(mainWindowId)
#endif
}

QString MMTTYIF::getWParamEnumName(unsigned long long wParam)
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
#else
    (void)wParam;
#endif
    return "";
}

QString MMTTYIF::logMessage(const QString &direction, unsigned int msg, unsigned long long wParam, long long lParam)
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

void MMTTYIF::logText(const QString &text)
{
    QString logPath = QCoreApplication::applicationDirPath() + "/bmtty.log";
    QFile file(logPath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << text << "\n";
        file.close();
    }
}

void MMTTYIF::shutdown()
{
    QString logStr = "[EXIT] MMTTYIF shutting down";
    logText(logStr);
    emit log_message(logStr);
}

void MMTTYIF::app_rx_char(char tx_char)
{
#ifdef Q_OS_WIN
    QString targetName = getTargetName();
    QString logStr = logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_CHAR, static_cast<LPARAM>(tx_char));
    emit log_message(logStr);
    ::PostMessageA(m_targetHandle, m_msgMtty, TXM_CHAR, static_cast<LPARAM>(tx_char));
#else
    Q_UNUSED(tx_char)
#endif
}

void MMTTYIF::report_ptt_state(bool is_on)
{
#ifdef Q_OS_WIN
    LPARAM lp = is_on ? 1 : 0;
    QString targetName = getTargetName();
    QString logStr = logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_PTTEVENT, lp);
    emit log_message(logStr);
    ::PostMessageA(m_targetHandle, m_msgMtty, TXM_PTTEVENT, lp);
#else
    Q_UNUSED(is_on)
#endif
}

#ifdef Q_OS_WIN
void MMTTYIF::filterEvent(void *message)
{
    MSG *msg = static_cast<MSG *>(message);
    
    QString logStr = logMessage("RCVD", msg->message, msg->wParam, msg->lParam);
    emit log_message(logStr);
    emit message_received();

    WPARAM wParam = msg->wParam;
    LPARAM lParam = msg->lParam;

    switch (wParam) {
        case RXM_HANDLE: {
            m_targetHandle = reinterpret_cast<HWND>(lParam);
            emit rxm_handle_received();

            QString targetName = getTargetName();
            QString logStrOut = logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_PTTEVENT, 0);
            emit log_message(logStrOut);
            ::PostMessageA(m_targetHandle, m_msgMtty, TXM_PTTEVENT, 0);
            break;
        }
        case RXM_EXIT: {
            if (m_targetHandle == nullptr || m_targetHandle == reinterpret_cast<HWND>(lParam)) {
                emit app_is_quitting();
            } else {
                QString logStrIgnore = QString("[RCVD] Ignored RXM_EXIT from handle: 0x%1").arg(lParam, 8, 16, QChar('0'));
                logText(logStrIgnore);
                emit log_message(logStrIgnore);
            }
            break;
        }
        case RXM_CHAR: {
            emit app_tx_char(static_cast<char>(lParam & 0xFF));
            break;
        }
        case RXM_PTT: {
            if (lParam == 2) {
                emit app_ptt_on();
            } else if (lParam == 0 || lParam == 1 || lParam == 4) {
                emit app_ptt_off(static_cast<int>(lParam));
            }
            break;
        }
        default:
            break;
    }
}
#endif
