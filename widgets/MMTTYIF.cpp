#include "MMTTYIF.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

namespace
{
  int constexpr maximum_header_bytes {64};
  int constexpr maximum_payload_bytes {64 * 1024};
  int constexpr maximum_frame_bytes {maximum_header_bytes + maximum_payload_bytes};

  bool resynchronizeAt (QByteArray * buffer, int next)
  {
    if (next < 0) {
      buffer->clear ();
      return false;
    }

    buffer->remove (0, next);
    return true;
  }

  bool resynchronize (QByteArray * buffer)
  {
    return resynchronizeAt (buffer, buffer->indexOf ('<', 1));
  }

  bool hasCompleteTrailingPayload (QByteArray const& command,
                                   QByteArray const& buffer,
                                   int payloadStart)
  {
    int const payloadSize = buffer.size () - payloadStart;
    return payloadSize == command.size ()
      && buffer.mid (payloadStart, payloadSize).compare (command, Qt::CaseInsensitive) == 0;
  }
}

MMTTYIF::MMTTYIF(QObject *parent) : QObject(parent),
                                    m_socket(new QTcpSocket(this)),
                                    m_retryTimer(new QTimer(this)) {
    m_retryTimer->setSingleShot(true);
    m_socket->setReadBufferSize(maximum_frame_bytes);

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
        if (close < 0) {
            if (m_rxBuffer.size() < maximum_header_bytes) return;

            emit log_message(QString("[TCP WARN] Command header exceeds %1-byte limit")
                             .arg(maximum_header_bytes));
            if (!resynchronize(&m_rxBuffer)) return;
            continue;
        }

        if (close + 1 > maximum_header_bytes) {
            emit log_message(QString("[TCP WARN] Command header exceeds %1-byte limit")
                             .arg(maximum_header_bytes));
            if (!resynchronize(&m_rxBuffer)) return;
            continue;
        }

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
                if (!resynchronize (&m_rxBuffer)) return;
                continue;
            }
            if (payloadLength > maximum_payload_bytes) {
                emit log_message(QString("[TCP WARN] Command payload length exceeds %1-byte limit in <%2>")
                                 .arg(maximum_payload_bytes)
                                 .arg(QString::fromLatin1(header)));
                if (!resynchronize (&m_rxBuffer)) return;
                continue;
            }
        }

        int payloadStart = close + 1;
        int payloadEnd = payloadStart;
        if (payloadLength >= 0) {
            int const availablePayload = m_rxBuffer.size() - payloadStart;
            if (availablePayload < payloadLength) return;
            payloadEnd = payloadStart + payloadLength;
        } else {
            int const next = m_rxBuffer.indexOf('<', payloadStart);
            payloadEnd = next >= 0 ? next : m_rxBuffer.size();
            if (payloadEnd - payloadStart > maximum_payload_bytes) {
                emit log_message(QString("[TCP WARN] Command payload exceeds %1-byte limit")
                                 .arg(maximum_payload_bytes));
                if (!resynchronizeAt (&m_rxBuffer, next)) return;
                continue;
            }
            if (next < 0
                && !hasCompleteTrailingPayload (command, m_rxBuffer, payloadStart)) return;
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
