#ifndef MMTTYIF_HPP
#define MMTTYIF_HPP

#include <QObject>
#include <QString>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class MMTTYIF : public QObject
{
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

    bool isRemoteInvoked() const { return m_remote_invoked; }
    void setRemoteInvoked(bool invoked) { m_remote_invoked = invoked; }

#ifdef Q_OS_WIN
    UINT getMttyMsg() const { return m_msgMtty; }
    void filterEvent(void *message);
#endif

signals:
    void app_is_quitting();
    void app_tx_char(char c);
    void app_ptt_on();
    void app_ptt_off(int lParam);
    
    void log_message(const QString &msg);
    void message_received();
    void rxm_handle_received();

public slots:
    void app_rx_char(char tx_char);
    void report_ptt_state(bool is_on);

private:
    QString getTargetName() const;

    quint32 m_baudRate = 0;
    quint32 m_heightWidth = 0;
    bool m_remote_invoked = false;

#ifdef Q_OS_WIN
    HWND m_targetHandle = nullptr;
    UINT m_msgMtty = 0;
    WId m_mainWindowId = 0;
#endif
};

#endif // MMTTYIF_HPP
