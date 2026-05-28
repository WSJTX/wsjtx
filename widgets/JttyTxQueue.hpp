#ifndef JTTY_TX_QUEUE_HPP
#define JTTY_TX_QUEUE_HPP

#include <QObject>
#include <QQueue>
#include <QString>

class JttyTxQueue : public QObject
{
    Q_OBJECT

public:
    explicit JttyTxQueue(QObject *parent = nullptr);
    ~JttyTxQueue() override;

public slots:
    void queueMessage(const QString& message);
    void clearQueue();
    void onMessageCompleted();
    bool isEmpty() const { return m_queue.isEmpty(); }

signals:
    void transmitMessage(const QString& message);
    void stopTransmit();

private:
    QQueue<QString> m_queue;
    bool m_transmitting;
};

#endif // JTTY_TX_QUEUE_HPP
