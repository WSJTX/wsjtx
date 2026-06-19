// cqdatasender.cpp

#include "cqdatasender.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDebug>
#include <QThread>

CQDataSender::CQDataSender(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &CQDataSender::onFinished);
}

CQDataSender::~CQDataSender()
{
    m_networkManager->deleteLater();
}

void CQDataSender::send(QString theUrl, const QString &data)
{  
  QUrl url(theUrl);
  QNetworkRequest request(url);
  QByteArray userAgent = (QCoreApplication::applicationName() + " v"
                          + QCoreApplication::applicationVersion()).toUtf8();
  request.setRawHeader("User-Agent", userAgent);
  request.setRawHeader("X-Custom-User-Agent", userAgent);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  QByteArray payload = data.toUtf8();
  request.setRawHeader("Content-Length",QByteArray::number(payload.size()));
  QNetworkReply *reply = m_networkManager->post(request, payload);
  qDebug() << "Current thread for CQDataSender::send:" << QThread::currentThread();

  // Qt 5.15+ (ignored on 5.12)
  connect(reply,
        SIGNAL(errorOccurred(QNetworkReply::NetworkError)),
        this,
        SLOT(onReplyError()));

// Qt 5.12 (ignored on Qt 6)
  connect(reply,
        SIGNAL(error(QNetworkReply::NetworkError)),
        this,
        SLOT(onReplyError()));


    // SSL errors unchanged
  connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError> &errors) {
      QStringList errList;
      for (const auto &e : errors)
          errList << e.errorString();
      emit errorOccurred("SSL Errors: " + errList.join(", "));
  });
}

void CQDataSender::onFinished(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        QString response = QString::fromUtf8(reply->readAll());
        emit resultReady(response);
    } else {
        emit errorOccurred(reply->errorString());
    }
    reply->deleteLater();
    qDebug() << "Current thread for CQDataSender::onFinished:" << QThread::currentThread();
}   

void CQDataSender::onReplyError()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    emit errorOccurred(reply->errorString());
}
