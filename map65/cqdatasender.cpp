// cqdatasender.cpp

#include "cqdatasender.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
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
  request.setRawHeader("User-Agent", "QMAP v0.5");
  request.setRawHeader("X-Custom-User-Agent", "QMAP v0.5");
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  QByteArray payload = data.toUtf8();
  request.setRawHeader("Content-Length",QByteArray::number(payload.size()));
  QNetworkReply *reply = m_networkManager->post(request, payload);
  qDebug() << "Current thread for CQDataSender::send:" << QThread::currentThread();
   // Use the OLD signal name: 'error' instead of 'errorOccurred'
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
  connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError) {
      emit errorOccurred(reply->errorString());
  });
#else
  connect(reply, static_cast<void(QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
          this, [this, reply](QNetworkReply::NetworkError error) {
      Q_UNUSED(error)
      emit errorOccurred(reply->errorString());
  });
#endif

  // Connect to sslErrors without QOverload
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
