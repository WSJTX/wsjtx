#include "JttyTxQueue.hpp"
#include "Logger.hpp"

JttyTxQueue::JttyTxQueue(QObject *parent)
    : QObject(parent),
      m_transmitting(false)
{
}

JttyTxQueue::~JttyTxQueue()
{
    clearQueue();
}

void JttyTxQueue::queueMessage(const QString& message)
{
    m_queue.enqueue(message);

    if (!m_transmitting) {
        m_transmitting = true;
        QString nextMessage = m_queue.dequeue();
        emit transmitMessage(nextMessage);
    }
}

void JttyTxQueue::clearQueue()
{
    m_queue.clear();
    m_transmitting = false;
}

void JttyTxQueue::onMessageCompleted()
{
    if (!m_transmitting) {
        return;
    }

    if (m_queue.isEmpty()) {
        m_transmitting = false;
        emit stopTransmit();
    } else {
        QString nextMessage = m_queue.dequeue();
        LOG_INFO(QString{"JttyTxQueue::onMessageCompleted, nextmessage: %1"}.arg(nextMessage).toStdString());
        emit transmitMessage(nextMessage);
    }
}
