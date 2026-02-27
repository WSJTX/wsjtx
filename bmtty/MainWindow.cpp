#include "MainWindow.hpp"
#include "MMTTYIF.hpp"
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
    , m_mmttyIf(new MMTTYIF(this))
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

    connect(m_mmttyIf, &MMTTYIF::log_message, m_textEdit, &QTextEdit::append); 
    connect(m_mmttyIf, &MMTTYIF::message_received, this, [this]() {
        m_inactivityTimer->start(7000);
    });

    connect(m_mmttyIf, &MMTTYIF::rxm_handle_received, this, [this]() {
        m_inactivityTimer->stop();
    });
    connect(m_mmttyIf, &MMTTYIF::app_is_quitting, qApp, &QCoreApplication::quit);
}

MainWindow::~MainWindow()
{
}

MMTTYIF* MainWindow::getMmttyIf() const
{
    return m_mmttyIf;
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
        if (msg->message == m_mmttyIf->getMttyMsg()) {
            m_mmttyIf->filterEvent(message);
            *result = 0;
            return true;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
