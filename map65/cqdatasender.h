// cqdatasender.h

#ifndef CQDATASENDER_H
#define CQDATASENDER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QThread>

class CQDataSender : public QObject
{
    Q_OBJECT

public:
    explicit CQDataSender(QObject *parent = nullptr);
    ~CQDataSender();

public slots:
    void send(QString theUrl, const QString &data);  // This runs in the worker thread

signals:
    void resultReady(const QString &response);  // Success
    void errorOccurred(const QString &error);   // Error

private slots:
    void onFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_networkManager;
};

#endif // CQDATASENDER_H   