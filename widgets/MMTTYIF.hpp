#ifndef MMTTYIF_HPP
#define MMTTYIF_HPP

#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

class MMTTYIF : public QObject {
  Q_OBJECT

public:
  explicit MMTTYIF(QObject *parent = nullptr);
  ~MMTTYIF();

  void initialize(quint16 port);
  bool isConnected() const;

  static void logText(const QString &text);

signals:
  void inactivity_timeout();
  void app_is_quitting();
  void app_tx_string(QString str);
  void app_start_tx();
  void app_stop_tx();
  void app_abort_tx();

  void message_received();
  void log_message(const QString &msg);
  void connection_failed();

public slots:
  void echo_message_to_n1mm(const QString &message);
  void report_ptt_state(bool is_on);
  void shutdown();

private slots:
  void onConnected();
  void onDisconnected();
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  void onError(QAbstractSocket::SocketError socketError);
#else
  void onError(QAbstractSocket::SocketError socketError);
#endif
  void onRetryTimeout();
  void onReadyRead();

private:
  QTcpSocket *m_socket;
  QTimer *m_retryTimer;
  int m_connectionRetries;
  quint16 m_port;

  quint32 m_baudRate = 0;
  quint32 m_heightWidth = 0;
};

#endif // MMTTYIF_HPP
