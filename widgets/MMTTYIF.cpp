#include "MMTTYIF.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

MMTTYIF::MMTTYIF(QObject *parent) : QObject(parent),
                                    m_socket(new QTcpSocket(this)),
                                    m_retryTimer(new QTimer(this)) {
    m_retryTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::readyRead, this, &MMTTYIF::onReadyRead);
    connect(m_socket, &QTcpSocket::connected, this, &MMTTYIF::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &MMTTYIF::onDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_socket, &QTcpSocket::errorOccurred, this, &MMTTYIF::onError);
#else
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, &MMTTYIF::onError);
#endif

    connect(m_retryTimer, &QTimer::timeout, this, &MMTTYIF::onRetryTimeout);
}

MMTTYIF::~MMTTYIF() {
    shutdown();
}

void MMTTYIF::initialize(quint16 port) {
    m_port = port;
    m_connectionRetries = 0;
    
    QString logStr = QString("[INIT] Starting TCP connection to 127.0.0.1:%1").arg(m_port);
    emit log_message(logStr);

    m_socket->connectToHost("127.0.0.1", m_port);
}

void MMTTYIF::onConnected() {
    m_connectionRetries = 0;
    QString logStr = QString("[TCP] Connected to N1MM Logger+ on port %1").arg(m_port);
    emit log_message(logStr);
}

void MMTTYIF::onDisconnected() {
    QString logStr = QString("[TCP] Disconnected from N1MM Logger+");
    emit log_message(logStr);
}

void MMTTYIF::onError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError)
    if (m_connectionRetries < 5) {
        m_connectionRetries++;
        QString logStr = QString("[TCP] Connection error, retrying (%1/5) in 1s...").arg(m_connectionRetries);
        emit log_message(logStr);
        m_retryTimer->start(1000);
    } else {
        QString logStr = QString("[TCP] Failed to connect after 5 retries.");
        emit log_message(logStr);
        emit connection_failed();
    }
}

void MMTTYIF::onRetryTimeout() {
    m_socket->connectToHost("127.0.0.1", m_port);
}

void MMTTYIF::onReadyRead() {
    QByteArray data = m_socket->readAll();
    QString buffer = QString::fromLatin1(data);
    
    QString logStr = QString("[TCP RCVD] %1").arg(buffer);
    emit log_message(logStr);
    emit message_received();

    // The messages could be bundled, so we use a simple regex to extract commands.
    // e.g. <TXTEXT:14>This is a test<XMIT:2>ON
    QRegularExpression re("<([^:]+)(?::(\\div>|\\d+))?>([^<]*)");
    QRegularExpressionMatchIterator i = re.globalMatch(buffer);
    
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString cmd = match.captured(1);
        int length = match.captured(2).toInt();
        QString content = match.captured(3);

        if (cmd == "TXTEXT") {
            // content might be longer than `length` due to regex greediness, so trim to length.
            QString text = content.left(length);
            emit app_tx_string(text);
        } else if (cmd == "XMIT") {
            if (content.startsWith("ON")) {
                emit app_start_tx();
            } else if (content.startsWith("OFF")) {
                emit app_stop_tx();
            }
        } else if (cmd == "ABORT") {
            emit app_abort_tx();
        } else if (cmd == "CLOSE") {
            emit app_is_quitting();
        }
    }
}



void MMTTYIF::shutdown() {
    QString logStr = "[EXIT] MMTTYIF shutting down";
    emit log_message(logStr);
    
    if (m_socket && m_socket->isOpen()) {
        m_socket->disconnectFromHost();
    }
}

bool MMTTYIF::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void MMTTYIF::echo_message_to_n1mm(const QString &message) {
    if (isConnected()) {
        if (message.isEmpty()) {
            QString logStr = QString("MMTTYIF::echo_message_to_n1mm - Ignoring Empty message");
            emit log_message(logStr);
            return;
        }
        QString msgToSend = QString("<RXTEXT:%1>%2").arg(message.length()).arg(message);
        m_socket->write(msgToSend.toLatin1());
        m_socket->flush();
        
        QString logStr = QString("[TCP SENT] %1").arg(msgToSend);
        emit log_message(logStr);
    }
}
#define SKIP_OUTPUT_COMPLETE
void MMTTYIF::report_ptt_state(bool is_on) {
    if (isConnected() && !is_on) {
       #ifdef SKIP_OUTPUT_COMPLETE 
        QString logStr = QString("[TCP SENT] Skipping OUTPUTCOMPLETE");
        emit log_message(logStr);
       #else
        QString msgToSend = "<OUTPUTCOMPLETE>";
        m_socket->write(msgToSend.toLatin1());
        m_socket->flush();
        
        QString logStr = QString("[TCP SENT] %1").arg(msgToSend);
        emit log_message(logStr);
       #endif
    }
}
