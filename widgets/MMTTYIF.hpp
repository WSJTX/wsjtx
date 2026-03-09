#ifndef MMTTYIF_HPP
#define MMTTYIF_HPP

#include <QObject>
#include <QString>
#include <QWidget>

#include <QTimer>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#undef MessageBox
#endif

class MMTTYIF : public QObject {
  Q_OBJECT

public:
  explicit MMTTYIF(QObject *parent = nullptr);
  ~MMTTYIF();

  void initialize(const QString &hexHandleStr, WId mainWindowId);
  void shutdown();

  quint32 baudRate() const { return m_baudRate; }
  void setBaudRate(quint32 baud) { m_baudRate = baud; }

  quint32 heightWidth() const { return m_heightWidth; }
  void setHeightWidth(quint32 hw) { m_heightWidth = hw; }

  int txDelayMs() const { return m_txDelayMs; }
  void setTxDelayMs(int ms) { m_txDelayMs = ms; }

  bool isRemoteInvoked() const { return m_remote_invoked; }
  void setRemoteInvoked(bool invoked) { m_remote_invoked = invoked; }

#ifdef Q_OS_WIN
  UINT getMttyMsg() const { return m_msgMtty; }
  void filterEvent(void *message);
#endif

signals:
  void app_is_quitting();
  void app_tx_string(QString str);
  void app_ptt_on();
  void app_ptt_off(int lParam);

  void log_message(const QString &msg);
  void message_received();
  void rxm_handle_received();
  void inactivity_timeout();

public slots:
  void app_rx_char(char tx_char);
  void report_ptt_state(bool is_on);
  void echo_tx_message_to_n1mm(const QString &message);

public:
  static QString logMessage(const QString &direction, unsigned int msg,
                            unsigned long long wParam, long long lParam);
  static void logText(const QString &text);

private slots:
  void handleInactivityTimeout();
  void handleTxBufferTimeout();

private:
  QString getTargetName() const;
  static QString getWParamEnumName(unsigned long long wParam);

  QTimer *m_inactivityTimer = nullptr;
  QTimer *m_txTimer = nullptr;
  QString m_txBuffer;
  quint32 m_baudRate = 0;
  quint32 m_heightWidth = 0;
  bool m_remote_invoked = false;
  int m_txDelayMs = 40;

#ifdef Q_OS_WIN
  HWND m_targetHandle = nullptr;
  UINT m_msgMtty = 0;
  WId m_mainWindowId = 0;
#endif
};

#endif // MMTTYIF_HPP
