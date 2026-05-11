#ifndef LIVECQSENDER_HPP_
#define LIVECQSENDER_HPP_
#include <boost/log/sources/severity_channel_logger.hpp>   
#include <boost/log/trivial.hpp>
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
#ifdef Q_OS_MACOS
    // Disable Boost.Log on macOS (crashes during default sink init)
    #define M65_LOG(sev) if (true) {} else std::cerr
#else
    using logger_type = boost::log::sources::severity_channel_logger_mt<
        boost::log::trivial::severity_level
    >;

    #define M65_LOG(sev) BOOST_LOG_SEV(logger_, sev)
#endif
#ifndef Q_OS_MACOS
    logger_type logger_;
#endif

  QString m_myCall;
  QString m_myGrid;
  QString m_theUrl;
  QScopedPointer<QSslSocket> socket; // Or: std::unique_ptr<QSslSocket
  QList<QByteArray> onConnectedRequests;
  
  // Copy constructor and assignment operator disabled
  Q_DISABLE_COPY(liveCQSender)
};

#endif
