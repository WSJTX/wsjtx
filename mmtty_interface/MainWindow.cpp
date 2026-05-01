#include "MainWindow.hpp"
#include "MMTTYIF.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QKeyEvent>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

MainWindow::MainWindow(const CommandLineOptions &options, QWidget *parent)
    : QMainWindow(parent), m_options(options), m_mmttyIf(new MMTTYIF(this))
{
  setWindowTitle("MMTTY Interface Utility - Arguments Received");

  QWidget *centralWidget = new QWidget(this);
  QVBoxLayout *layout = new QVBoxLayout(centralWidget);
  m_textEdit = new QTextEdit(this);
  m_textEdit->setReadOnly(true);
  QString info = "<b>Boolean Flags:</b><br>";
  for (auto it = m_options.flags.begin(); it != m_options.flags.end(); ++it) {
    info += QString("-%1: %2<br>").arg(it.key()).arg(it.value() ? "ON" : "OFF");
  }

  info += "<br><b>Parameter Options:</b><br>";
  info += QString("-h (Hex): %1<br>").arg(m_options.hexValue);
  info += QString("-C (String): %1<br>").arg(m_options.stringValue);
  info += QString("-T (Decimal): %1<br>").arg(m_options.decimalValue);

  m_textEdit->setHtml(info);
  layout->addWidget(m_textEdit);
  setCentralWidget(centralWidget);
  resize(500, 400);

  m_textEdit->installEventFilter(this);

  connect(m_mmttyIf, &MMTTYIF::log_message, m_textEdit, &QTextEdit::append);
}

MainWindow::~MainWindow() {}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  if (obj == m_textEdit && event->type() == QEvent::KeyPress) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    QString text = keyEvent->text();
    QString valid_rtty_chars("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvw"
                             "xyz0123456789\r\n \b?:'-,./#&$!");
    if (!text.isEmpty()) {
      char c = text.at(0).toLatin1();

      if (valid_rtty_chars.contains(c)) {
        m_mmttyIf->echo_message_to_n1mm(QString(c));
      }
    }
  }
  return QMainWindow::eventFilter(obj, event);
}


void MainWindow::jtty_tx_test(QString str) {
  m_mmttyIf->report_ptt_state(true);
  m_mmttyIf->echo_message_to_n1mm(str);

  int delayMs = 163 * str.length();
  QTimer::singleShot(delayMs, this,
                     [this]() { m_mmttyIf->report_ptt_state(false); });
}
