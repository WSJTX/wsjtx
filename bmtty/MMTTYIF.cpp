#include "MMTTYIF.hpp"
#include "MessageLogger.hpp"
#include <QCoreApplication>

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

    QString logStrThread = MessageLogger::logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_THREAD, static_cast<LPARAM>(threadId));
    emit log_message(logStrThread);
    ::PostMessageA(m_targetHandle, m_msgMtty, TXM_THREAD, static_cast<LPARAM>(threadId));

    if (m_mainWindowId) {
        HWND hwnd = reinterpret_cast<HWND>(m_mainWindowId);
        QString logStrHandle = MessageLogger::logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
        emit log_message(logStrHandle);
        ::PostMessageA(m_targetHandle, m_msgMtty, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
    }

    QString logStrStart = MessageLogger::logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_START, 0x00000000);
    emit log_message(logStrStart);
    ::PostMessageA(m_targetHandle, m_msgMtty, TXM_START, 0x00000000);
#else
    Q_UNUSED(hexHandleStr)
    Q_UNUSED(mainWindowId)
#endif
}

void MMTTYIF::shutdown()
{
    QString logStr = "[EXIT] MMTTYIF shutting down";
    MessageLogger::logText(logStr);
    emit log_message(logStr);
}

void MMTTYIF::app_rx_char(char tx_char)
{
#ifdef Q_OS_WIN
    QString targetName = getTargetName();
    QString logStr = MessageLogger::logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_CHAR, static_cast<LPARAM>(tx_char));
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
    QString logStr = MessageLogger::logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_PTTEVENT, lp);
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
    
    QString logStr = MessageLogger::logMessage("RCVD", msg->message, msg->wParam, msg->lParam);
    emit log_message(logStr);
    emit message_received();

    WPARAM wParam = msg->wParam;
    LPARAM lParam = msg->lParam;

    switch (wParam) {
        case RXM_HANDLE: {
            m_targetHandle = reinterpret_cast<HWND>(lParam);
            emit rxm_handle_received();

            QString targetName = getTargetName();
            QString logStrOut = MessageLogger::logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_PTTEVENT, 0);
            emit log_message(logStrOut);
            ::PostMessageA(m_targetHandle, m_msgMtty, TXM_PTTEVENT, 0);
            break;
        }
        case RXM_EXIT: {
            if (m_targetHandle == nullptr || m_targetHandle == reinterpret_cast<HWND>(lParam)) {
                emit app_is_quitting();
            } else {
                QString logStrIgnore = QString("[RCVD] Ignored RXM_EXIT from handle: 0x%1").arg(lParam, 8, 16, QChar('0'));
                MessageLogger::logText(logStrIgnore);
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
