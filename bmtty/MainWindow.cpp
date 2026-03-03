#include "MainWindow.hpp"
#include "MessageLogger.hpp"
#include <QLabel>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QCoreApplication>
#include <QDateTime>
#ifdef Q_OS_WIN
#include "MMTTY_Messages.hpp"
#endif

MainWindow::MainWindow(const CommandLineOptions &options, QWidget *parent)
    : QMainWindow(parent)
    , m_options(options)
    , m_inactivityTimer(new QTimer(this))
#ifdef Q_OS_WIN
    , m_targetHandle(nullptr)
    , m_msgMtty(::RegisterWindowMessageA("MMTTY"))
#endif
{
#ifdef Q_OS_WIN
    if (!m_options.hexValue.isEmpty()) {
        bool ok;
        HWND h = reinterpret_cast<HWND>(m_options.hexValue.toULongLong(&ok, 16));
        if (ok) {
            m_targetHandle = h;
        }
    }
#endif

    setWindowTitle("BMTTY Utility - Arguments Received");
    
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    
    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    
    QString info = "<b>Boolean Flags:</b><br>";
    for (auto it = m_options.flags.begin(); it != m_options.flags.end(); ++it) {
        info += QString("-%1: %2<br>").arg(it.key()).arg(it.value() ? "ON" : "OFF");
    }
    
    info += "<br><b>Parameter Options:</b><br>";
    info += QString("-h (Hex): %1<br>").arg(m_options.hexValue);
    info += QString("-C (String): %1<br>").arg(m_options.stringValue);
    info += QString("-T (Decimal): %1<br>").arg(m_options.decimalValue);
    
    m_textEdit->setHtml(info);
    layout->addWidget(m_textEdit);
    
    setCentralWidget(centralWidget);
    resize(500, 400);

    connect(m_inactivityTimer, &QTimer::timeout, this, &MainWindow::handleInactivityTimeout);
    m_inactivityTimer->start(7000); // 7 second auto-termination timer
}

MainWindow::~MainWindow()
{
}

void MainWindow::handleInactivityTimeout()
{
    // If no message received for 7 seconds, terminate.
    MessageLogger::logText(QString("%1 [EXIT] Inactivity timeout").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")));
    QCoreApplication::quit();
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == m_msgMtty) {
            // Log the received message
            QString logStr = MessageLogger::logMessage("RCVD", msg->message, msg->wParam, msg->lParam);
            if (m_textEdit) {
                m_textEdit->append(logStr);
            }

            WPARAM wParam = msg->wParam;
            LPARAM lParam = msg->lParam;

            switch (wParam) {
                case TXM_HANDLE:
                    // Other programs send their handle back to us
                    m_targetHandle = reinterpret_cast<HWND>(lParam);
                    break;
                case RXM_HANDLE: {
                    m_targetHandle = reinterpret_cast<HWND>(lParam);
                    
                    // Disable the inactivity timer
                    m_inactivityTimer->stop();

                    QString targetName = QString("0x%1").arg(lParam, 8, 16, QChar('0'));
                    QString logStrOut = MessageLogger::logMessage(QString("SENT (%1)").arg(targetName), m_msgMtty, TXM_PTTEVENT, 0);
                    if (m_textEdit) {
                        m_textEdit->append(logStrOut);
                    }
                    ::PostMessageA(m_targetHandle, m_msgMtty, TXM_PTTEVENT, 0);
                    break;
                }
                case RXM_EXIT: {
                    if (m_targetHandle == nullptr || m_targetHandle == reinterpret_cast<HWND>(lParam)) {
                        QCoreApplication::quit();
                    } else {
                        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
                        QString logStr = QString("%1 [%2] Ignored RXM_EXIT from handle: 0x%3").arg(timestamp).arg("RCVD").arg(lParam, 8, 16, QChar('0'));
                        MessageLogger::logText(logStr);
                        if (m_textEdit) {
                            m_textEdit->append(logStr);
                        }
                    }
                    break;
                }
                default:
                    if (wParam <= 0x0031) {
                        // Handle other RXM_ messages here if needed
                    }
                    break;
            }
            
            *result = 0;
            return true;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
