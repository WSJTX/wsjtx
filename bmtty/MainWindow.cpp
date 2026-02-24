#include "MainWindow.hpp"
#include "MessageLogger.hpp"
#include <QLabel>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QCoreApplication>
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
    setWindowTitle("BMTTY Utility - Arguments Received");
    
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    
    QTextEdit *textEdit = new QTextEdit(this);
    textEdit->setReadOnly(true);
    
    QString info = "<b>Boolean Flags:</b><br>";
    for (auto it = m_options.flags.begin(); it != m_options.flags.end(); ++it) {
        info += QString("-%1: %2<br>").arg(it.key()).arg(it.value() ? "ON" : "OFF");
    }
    
    info += "<br><b>Parameter Options:</b><br>";
    info += QString("-h (Hex): %1<br>").arg(m_options.hexValue);
    info += QString("-C (String): %1<br>").arg(m_options.stringValue);
    info += QString("-T (Decimal): %1<br>").arg(m_options.decimalValue);
    
    textEdit->setHtml(info);
    layout->addWidget(textEdit);
    
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
    QCoreApplication::quit();
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == m_msgMtty) {
            // Log the received message
            MessageLogger::logMessage("RCVD", msg->message, msg->wParam, msg->lParam);

            // Reset the inactivity timer on any MMTTY message
            m_inactivityTimer->start(7000);

            WPARAM wParam = msg->wParam;
            LPARAM lParam = msg->lParam;

            if (wParam == TXM_HANDLE) {
                // Other programs send their handle back to us
                m_targetHandle = reinterpret_cast<HWND>(lParam);
            } else if (wParam == RXM_EXIT) {
                QCoreApplication::quit();
            } else if (wParam <= 0x0031) {
                // Handle other RXM_ messages here if needed
            }
            
            *result = 0;
            return true;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
