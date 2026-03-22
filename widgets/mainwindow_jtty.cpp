#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "commons.h"
#include "Audio/WavFile.hpp"
#include "Logger.hpp"
#include <QtConcurrent/QtConcurrentRun>
#include <iostream>

#ifdef WIN32
#include "MMTTYIF.hpp"
#include "MMTTY_Messages.hpp"
#undef MessageBox
#endif


extern dec_data_t dec_data;

#if QT_VERSION >= QT_VERSION_CHECK (5, 13, 0)
#define SkipEmptyParts Qt::SkipEmptyParts
#else
#define SkipEmptyParts QString::SkipEmptyParts
#endif

#define FCL fortran_charlen_t

extern "C" {
  void rjtty_sub_(short int d2[], int* k, int* nsps, float* f0, float* ftol);

void jtty_get_msgs_(float* f0, float* ftol, bool* all_new, bool* qso_new,
    char all_freqs[], char line[], fortran_charlen_t, fortran_charlen_t);

  void genjtty_(char const * msg, int itone[], int* nsym, fortran_charlen_t);

  void gen_jttywave_(int itone[], int* nsym, int* nsps, float* bt, float* fsample, float* f0,
                    float xjunk[], float wave[], int* icmplx, int* nwave);
}

void MainWindow::jtty_save_wav()
{
  //Save JTTY data to a .wav file
  QDateTime now {QDateTime::currentDateTimeUtc ()};
  qint64 ms = m_k0/12;
  auto const& tstart=now.addMSecs(-ms);
  m_fnameWE=m_config.save_directory().absoluteFilePath (tstart.toString("yyMMdd_hhmmss"));
  int samples=m_k0;
  short const * data = &dec_data.d2[0];
  m_saveWAVWatcher.setFuture (QtConcurrent::run ([=] {
    return Radio::WavFile::save (m_fnameWE, data, samples, m_config.my_callsign (),
                                 m_config.my_grid (), m_mode, m_nSubMode, m_freqNominalPeriod,
                                 m_hisCall, m_hisGrid);
  }));
}

void MainWindow::jtty_decode(int k)
{
  int nsps=384;
  char qso_freq[800];
  char all_freqs[2400];
  float f0 = ui->RxFreqSpinBox_2->value();
  float ftol = ui->sbFtol_2->value();
  bool all_new = true;
  bool qso_new = true;

  rjtty_sub_(dec_data.d2,&k,&nsps,&f0,&ftol);

  jtty_get_msgs_(&f0, &ftol, &all_new, &qso_new, &all_freqs[0],
                 &qso_freq[0], (FCL)2400, (FCL)800);

  QString allMsgs {QString::fromLatin1(all_freqs)};
  if(ui->cbLowerCase->isChecked()) allMsgs = allMsgs.toLower();
  if(all_new) {
      ui->decodedTextBrowser->clear();
      if(allMsgs.left(1) == " ") {
          ui->decodedTextBrowser->insertText(" " + allMsgs.trimmed());
      } else {
          ui->decodedTextBrowser->insertText(allMsgs.trimmed());
      }
#ifdef WIN32
      if (m_mmttyif) {
          m_mmttyif->echo_message_to_n1mm(allMsgs);
      }
#endif
  }
  if(qso_new) {
      QString message2 {QString::fromLatin1(qso_freq)};
      if(ui->cbLowerCase->isChecked()) message2 = message2.toLower();
      int n2=message2.length();
      if(n2 > 0) {
        ui->decodedTextBrowser2->clear();
        ui->decodedTextBrowser2->insertText(message2.trimmed());
#ifdef WIN32
        if (m_mmttyif) {
            m_mmttyif->echo_message_to_n1mm(message2);
        }
#endif
      }
  }
}

void MainWindow::jtty_tx(QString message)
{
  if (!m_jttyQueue) {
    m_jttyQueue = new JttyTxQueue(this);
    connect(m_jttyQueue, &JttyTxQueue::transmitMessage, this, &MainWindow::execute_jtty_tx);
    connect(m_jttyQueue, &JttyTxQueue::stopTransmit, this, &MainWindow::stopJttyTxIfEmpty);
    connect(m_jttyQueue, &JttyTxQueue::abortTransmit, this, &MainWindow::abort_jtty_tx);
  }
  m_jttyQueue->queueMessage(message);
}

void MainWindow::execute_jtty_tx(QString message)
{
  int itone[848];
  int n=message.length();
  m_currentMessage = message;
  if(ui->cbLowerCase->isChecked()) message = message.toLower();

  // Display Tx message highlighted in yellow
  ui->decodedTextBrowser2->insertText(" ");
  QTextCursor cursor = ui->decodedTextBrowser2->textCursor();
  QTextCharFormat format = cursor.charFormat();
  format.setBackground(QBrush(QColor(Qt::yellow))); // Set background to yellow
  cursor.setCharFormat(format);
  cursor.insertText(message);
  // Reset format to default
  format.setBackground(QBrush(QColor(Qt::white)));
  cursor.setCharFormat(format);

  if(message.left(3) == "TU ") {
    // ### Must send "sent" and "rcvd" info to logqso here. ###
    logQSOTimer.start(0);
    int nr = ui->sbSerialNumber_2->value();
    m_xSent = QString::number(nr);
    ui->sbSerialNumber_2->setValue(nr+1);
  }

  QString t = " ";
  t = message + t.repeated(80-n);
  genjtty_(t.toLatin1().constData(), &itone[0], &m_nsym_jtty, (FCL)80);

  int nsps4=4*384;
  float bt=2.0;
  float fsample=48000.0;
  float f0=ui->TxFreqSpinBox_2->value ();
  int icmplx=0;
  int nwave=nsps4*m_nsym_jtty;
  gen_jttywave_(const_cast<int *>(itone), &m_nsym_jtty, &nsps4, &bt, &fsample, &f0,
                foxcom_.wave, foxcom_.wave, &icmplx, &nwave);
  monitor(false);
  if(!m_diskData && m_saveAll && (m_k0 > 53*384) && (m_k0 < 9999999)) {
    jtty_save_wav();
  }
  m_transmitting = true;

#ifdef WIN32
  if (m_mmttyif) {
    m_mmttyif->report_ptt_state(true);
  }
#endif

#ifdef WIN32
  if (m_mmttyif) {
    m_mmttyif->echo_message_to_n1mm(message);
  }
#endif

  int msTx=nwave/48.0 + 1000*m_config.txDelay();


  if (m_jttyQueue) {
      m_jttyQueue->onTxStarted(msTx);
  } else {
      QTimer::singleShot(msTx, this, SLOT (stopTx()));
  }
}

void MainWindow::abort_jtty_tx()
{
   if (m_jttyQueue) {
       m_jttyQueue->clearQueue();
   }
   
#ifdef WIN32
   if (m_mmttyif) {
       m_mmttyif->report_ptt_state(false);
   }
#endif

   stopTx();
}

void MainWindow::stopJttyTxIfEmpty()
{
   if (!m_jttyQueue || m_jttyQueue->isEmpty()) {

#ifdef WIN32   
    if (m_mmttyif) {
      m_mmttyif->report_ptt_state(false);
    }
#endif
       stopTx();
   }
}

void MainWindow::jtty_again()
{
  for(int k=3456; k<dec_data.params.kin; k+=3456) {
    jtty_decode(k);
  }
  decodeDone();
}

bool MainWindow::jtty_key_struck(QKeyEvent * e)
{
  QString t{};
  if(e->key() == Qt::Key_Escape) {
    abort_jtty_tx();
    return true;
  }
  if(e->key() == Qt::Key_F1) t = ui->msg1->text();
  if(e->key() == Qt::Key_F2) t = ui->msg2->text();
  if(e->key() == Qt::Key_F3) t = ui->msg3->text();
  if(e->key() == Qt::Key_F4) t = ui->msg4->text();
  if(e->key() == Qt::Key_F5) t = ui->msg5->text();
  if(e->key() == Qt::Key_F6) t = ui->msg6->text();
  if(e->key() == Qt::Key_F7) t = ui->msg7->text();
  if(e->key() == Qt::Key_F8) t = ui->msg8->text();
  if(t=="") return false;
  t=jtty_msg_expand(t);
  jtty_tx(t);
  return true;
}

QString MainWindow::jtty_msg_expand(QString t)
{
  if(!t.contains("%")) return t;
  for (int i=0; i<5; i++) {
    t=t.replace("%M",m_config.my_callsign());
    t=t.replace("%H",m_hisCall);
    t=t.replace("%Q",m_hisCall);
    if(t.contains("%N")) {
      int n=ui->sbSerialNumber_2->value();
      QString tn=QString::number(n);
      if  (n < 10) tn = "00"+tn;
      if(n   < 100) tn = "0"+tn;
      t=t.replace("%N",tn);
      if(!t.contains("%")) return t;
    }
    if(!t.contains("%")) return t;
  }
  return t;
}

void MainWindow::on_RxFreqSpinBox_2_valueChanged(int n)
{
    ui->RxFreqSpinBox->setValue(n);
}

void MainWindow::on_TxFreqSpinBox_2_valueChanged(int n)
{
    ui->TxFreqSpinBox->setValue(n);
}

void MainWindow::on_sbFtol_2_valueChanged (int n)
{
    if(n==999) std::cout << "AAA " << n << "\n";
}

void MainWindow::logText(const QString &text) {
  LOG_INFO(text);
}

#ifdef Q_OS_WIN

void MainWindow::initMMTTY(const QString& hexHandle) {
    if (!m_mmttyif) {
        m_mmttyif = new MMTTYIF(this);
    }
    connect(m_mmttyif, &MMTTYIF::log_message, this, &MainWindow::logText);

    // Register custom window message
    UINT MSG_MMTTY = ::RegisterWindowMessageA("MMTTY");

    // Print to logs the assigned custom message number for "MMTTY"
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    MMTTYIF::logText(QString("%1 [INIT] Registered MMTTY message: 0x%2")
                     .arg(timestamp)
                     .arg(MSG_MMTTY, 4, 16, QChar('0')));

    // Get the WId (Window ID) of the MainWindow instance
    WId winId = this->winId();
    HWND hwnd = reinterpret_cast<HWND>(winId);

    // Get our own thread ID
    DWORD threadId = GetCurrentThreadId();

    HWND targetHwnd = HWND_BROADCAST;
    QString targetName = "Broadcast";

    if (!hexHandle.isEmpty()) {
        bool ok;
        targetHwnd = reinterpret_cast<HWND>(hexHandle.toULongLong(&ok, 16));
        if (ok) {
            targetName = QString("0x%1").arg(hexHandle);
        } else {
            // Revert back to broadcast if parsing failed
            targetHwnd = HWND_BROADCAST;
        }
    }

    MMTTYIF::logMessage(QString("SENT (%1)").arg(targetName), MSG_MMTTY, TXM_THREAD, static_cast<LPARAM>(threadId));
    ::PostMessageA(targetHwnd, MSG_MMTTY, TXM_THREAD, static_cast<LPARAM>(threadId)); // Send Thread ID

    MMTTYIF::logMessage(QString("SENT (%1)").arg(targetName), MSG_MMTTY, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
    ::PostMessageA(targetHwnd, MSG_MMTTY, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd)); // Send Window Handle

    MMTTYIF::logMessage(QString("SENT (%1)").arg(targetName), MSG_MMTTY, TXM_START, 0x00000000);
    ::PostMessageA(targetHwnd, MSG_MMTTY, TXM_START, 0x00000000); // Send Start signal

    m_mmttyif->initialize(hexHandle, this->winId()); // Or however you get handles

    connect(m_mmttyif, &MMTTYIF::app_tx_string, this, &MainWindow::jtty_tx);
    connect(m_mmttyif, &MMTTYIF::inactivity_timeout, qApp, &QCoreApplication::quit);
    connect(m_mmttyif, &MMTTYIF::app_is_quitting, qApp, &QCoreApplication::quit);

    // Auto-switch to JTTY mode after MMTTY connects
    QTimer::singleShot(3000, this, [this]() {
         set_mode("JTTY");
    });
}

MMTTYIF *MainWindow::getMmttyIf() const {
    return m_mmttyif;
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (m_mmttyif && msg->message == m_mmttyif->getMttyMsg()) {
            m_mmttyif->filterEvent(message);
            *result = 0; // Return 0 to indicate we handled the message
            return true; // Stop standard Qt processing for this message
        }
    }

    // Call base class method for unhandled messages
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
