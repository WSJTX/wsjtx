#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QString>
#include <QMap>
#include <QTimer>

class QTextEdit;
class MMTTYIF;

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

    MMTTYIF* getMmttyIf() const { return m_mmttyIf; }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
private slots:
    void jtty_tx_test(QString str);

private:
    CommandLineOptions m_options;
    QTextEdit *m_textEdit;
    quint32 m_baudRate = 0;
    quint32 m_heightWidth = 0;
    MMTTYIF *m_mmttyIf;
};

#endif // MAINWINDOW_HPP
