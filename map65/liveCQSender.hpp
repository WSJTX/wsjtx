#ifndef LIVECQSENDER_HPP_
#define LIVECQSENDER_HPP_
#include <QObject>
#include <QTimer>
#include <QSslSocket>
#include <QScopedPointer>
#include "../qt_helpers.hpp"
#include "commons.h"

class QString;

class liveCQSender : public QObject
{
  Q_OBJECT

signals:
  void dataReady(const QByteArray &payload);

public slots:
  void init();        // Slot to initialize timers/sockets
  
    
public:
  explicit liveCQSender (QString const& myCall, QString const& myGrid, QString const& theUrl);
  ~liveCQSender ();

  void reconnect ();

  //
  // Returns false if PSK Reporter connection is not available
  //
  void addRemoteStation (QByteArray const& postByteArray, QString const& theUrl);

  Q_SIGNAL void errorOccurred (QString const& reason);

private slots:
  void handle_socket_error(QAbstractSocket::SocketError error);
  void onConnected();
  void onReadyRead();
  void sendData(const QByteArray &payload);

private:
  QString m_myCall;
  QString m_myGrid;
  QString m_theUrl;
  QScopedPointer<QSslSocket> socket; // Or: std::unique_ptr<QSslSocket
  QList<QByteArray> onConnectedRequests;
  
  // Copy constructor and assignment operator disabled
  Q_DISABLE_COPY(liveCQSender)
};

#endif
