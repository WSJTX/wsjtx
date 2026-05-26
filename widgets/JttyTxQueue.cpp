#include "JttyTxQueue.hpp"
#include "WSJTXLogging.hpp"
//#include "AbstractLogWindow.hpp"
#include <string>
#include <exception>
#include <sstream>

#include <boost/version.hpp>
#include <boost/log/core.hpp>
#include <boost/log/utility/exception_handler.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/expressions/formatters/date_time.hpp>
#include <boost/log/expressions/predicates/channel_severity_filter.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/greg_day.hpp>
#include <boost/container/flat_map.hpp>

#include <QtGlobal>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QMessageLogContext>

#include "Logger.hpp"
#include "qt_helpers.hpp"

JttyTxQueue::JttyTxQueue(QObject *parent)
    : QObject(parent),
      m_transmitting(false)
{
    m_txTimer.setSingleShot(true);
    connect(&m_txTimer, &QTimer::timeout, this, &JttyTxQueue::onTxTimerTimeout);
}

JttyTxQueue::~JttyTxQueue()
{
    clearQueue();
}

void JttyTxQueue::queueMessage(const QString& message)
{
    m_queue.enqueue(message);

    if (!m_transmitting) {
        // If not transmitting, start the next one immediately
        QString nextMessage = m_queue.dequeue();
        emit transmitMessage(nextMessage);
    }
}

void JttyTxQueue::clearQueue()
{
    m_queue.clear();
    
    if (m_transmitting) {
        m_transmitting = false;
        if (m_txTimer.isActive()) {
            m_txTimer.stop();
        }
    }
}

void JttyTxQueue::onTxStarted(int durationMs)
{
    m_transmitting = true;
    LOG_INFO(QString{"JttyTxQueue::onTxStarted: %1"}.arg(durationMs));
    m_txTimer.start(durationMs);
}

void JttyTxQueue::onTxTimerTimeout()
{
    if (m_queue.isEmpty()) {
        m_transmitting = false;
        emit stopTransmit();
    } else {
        // First briefly tell the consumer that the current physical stream ended
        //emit stopTransmit();
        
        // Then immediately spool up the next pending message
        QString nextMessage = m_queue.dequeue();
        LOG_INFO(QString{"JttyTxQueue::onTxTimerTimeout, nextmessage: "}.arg(nextMessage));
        emit transmitMessage(nextMessage);
    }
}
