#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QString>
#include <QMap>
#include <QTimer>

class QTextEdit;
class MMTTYIF;

#ifdef Q_OS_WIN
#include <windows.h>
#endif

struct CommandLineOptions {
    QMap<QString, bool> flags;
    QString hexValue;
    QString stringValue;
    int decimalValue;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const CommandLineOptions &options, QWidget *parent = nullptr);
    ~MainWindow();

    MMTTYIF* getMmttyIf() const;

protected:
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#endif

private slots:
    void handleInactivityTimeout();

private:
    CommandLineOptions m_options;
    QTimer *m_inactivityTimer;
    QTextEdit *m_textEdit;
    quint32 m_baudRate = 0;
    quint32 m_heightWidth = 0;
    MMTTYIF *m_mmttyIf;
    
#ifdef Q_OS_WIN
    HWND m_targetHandle;
    UINT m_msgMtty;
#endif
};

#endif // MAINWINDOW_HPP
