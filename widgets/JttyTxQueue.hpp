#ifndef JTTY_TX_QUEUE_HPP
#define JTTY_TX_QUEUE_HPP

#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

class JttyTxQueue : public QObject
{
    Q_OBJECT

public:
    explicit JttyTxQueue(QObject *parent = nullptr);
    ~JttyTxQueue() override;

public slots:
    void queueMessage(const QString& message);
    void clearQueue();
    void onTxStarted(int durationMs);
    bool isEmpty() const { return m_queue.isEmpty(); }

signals:
    void transmitMessage(const QString& message);
    void stopTransmit();

private slots:
    void onTxTimerTimeout();

private:
    QQueue<QString> m_queue;
    bool m_transmitting;
    QTimer m_txTimer;
};

#endif // JTTY_TX_QUEUE_HPP
