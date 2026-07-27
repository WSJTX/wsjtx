#include "MMTTYIF.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

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
    m_rxBuffer.append(data);
    
    QString logStr = QString("[TCP RCVD] %1").arg(QString::fromLatin1(data));
    emit log_message(logStr);
    emit message_received();

    parseBufferedCommands();
}

void MMTTYIF::parseBufferedCommands() {
    while (!m_rxBuffer.isEmpty()) {
        int const open = m_rxBuffer.indexOf('<');
        if (open < 0) {
            emit log_message(QString("[TCP WARN] Dropping unframed data: %1")
                             .arg(QString::fromLatin1(m_rxBuffer)));
            m_rxBuffer.clear();
            return;
        }

        if (open > 0) {
            emit log_message(QString("[TCP WARN] Dropping data before command: %1")
                             .arg(QString::fromLatin1(m_rxBuffer.left(open))));
            m_rxBuffer.remove(0, open);
        }

        int const close = m_rxBuffer.indexOf('>');
        if (close < 0) return;

        QByteArray const header = m_rxBuffer.mid(1, close - 1);
        int const colon = header.indexOf(':');
        QByteArray const command = (colon >= 0 ? header.left(colon) : header).toUpper();
        int payloadLength = -1;
        if (colon >= 0) {
            bool ok = false;
            payloadLength = header.mid(colon + 1).toInt(&ok);
            if (!ok || payloadLength < 0) {
                emit log_message(QString("[TCP WARN] Invalid command length in <%1>")
                                 .arg(QString::fromLatin1(header)));
                m_rxBuffer.remove(0, close + 1);
                continue;
            }
        }

        int payloadStart = close + 1;
        int payloadEnd = payloadStart;
        if (payloadLength >= 0) {
            payloadEnd = payloadStart + payloadLength;
            if (m_rxBuffer.size() < payloadEnd) return;
        } else {
            int const next = m_rxBuffer.indexOf('<', payloadStart);
            payloadEnd = next >= 0 ? next : m_rxBuffer.size();
        }

        QByteArray const payload = m_rxBuffer.mid(payloadStart, payloadEnd - payloadStart);
        m_rxBuffer.remove(0, payloadEnd);
        dispatchCommand(command, payload);
    }
}

void MMTTYIF::dispatchCommand(QByteArray const& command, QByteArray const& payload) {
    QString const content = QString::fromLatin1(payload);

    if (command == "TXTEXT") {
        emit app_tx_string(content.toUpper());
    } else if (command == "XMIT") {
        if (content.startsWith("ON", Qt::CaseInsensitive)) {
            emit app_start_tx();
        } else if (content.startsWith("OFF", Qt::CaseInsensitive)) {
            emit app_stop_tx();
        }
    } else if (command == "ABORT") {
        emit app_abort_tx();
    } else if (command == "CLOSE") {
        emit app_is_quitting();
    } else {
        emit log_message(QString("[TCP WARN] Ignoring unknown command <%1>")
                         .arg(QString::fromLatin1(command)));
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

void MMTTYIF::report_output_complete() {
    if (isConnected()) {
        QString msgToSend = "<OUTPUTCOMPLETE>";
        m_socket->write(msgToSend.toLatin1());
        m_socket->flush();
        
        QString logStr = QString("[TCP SENT] %1").arg(msgToSend);
        emit log_message(logStr);
    }
}

void MMTTYIF::report_ptt_state(bool is_on) {
    if (isConnected() && !is_on) {
        emit log_message("[TCP INFO] PTT off; OUTPUTCOMPLETE is sent from JTTY drain");
    }
}
