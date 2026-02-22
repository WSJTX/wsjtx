#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "commons.h"
#include "Audio/WavFile.hpp"
#include <QtConcurrent/QtConcurrentRun>
#include <iostream>

extern dec_data_t dec_data;

#if QT_VERSION >= QT_VERSION_CHECK (5, 13, 0)
#define SkipEmptyParts Qt::SkipEmptyParts
#else
#define SkipEmptyParts QString::SkipEmptyParts
#endif

#define FCL fortran_charlen_t

extern "C" {
  void rjtty_sub_(short int d2[], int* k, int* nsps, float* f0, float* ftol,
                  float* xdt, float* f1, float* snr, char line[], fortran_charlen_t);

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
  static int k0=9999999;
  int nsps=384;
  char line[80];
  float f0 = ui->RxFreqSpinBox_2->value();
  float ftol = ui->sbFtol_2->value();
  static float xdt = 0.0;
  static float f1 = 0.0;
  float snr = 0.0;
  //  static int n0=0;
  static QString message0 = "";

  rjtty_sub_(dec_data.d2,&k,&nsps,&f0,&ftol,&xdt,&f1,&snr,&line[0],(FCL)80);
  QString message {QString::fromLatin1(line)};
  int n=message.length();
  if(n > 0 and n < 80) {
    bool eom=message.left(1)=="\n";
    if(eom) message=message.mid(1);
    if(k > k0 and !eom) {
      QTextCursor cursor = ui->decodedTextBrowser->textCursor();
      cursor.movePosition(QTextCursor::End);         //Cursor to end of text
      cursor.select(QTextCursor::LineUnderCursor);   //Select line under cursor
      cursor.removeSelectedText();                   //Remove the selected line
      cursor.deletePreviousChar();                   //Delete previous newline
      ui->decodedTextBrowser->setTextCursor(cursor); //Reset cursor back to browser
    }
    m_xRcvd="";
    QStringList w = message.split(" ",SkipEmptyParts);
    if((w.length() == 2) and (w[0] == "599")) m_xRcvd = w[1];
    if((w.length() == 3) and (w[1] == "599")) m_xRcvd = w[2];
    if(k != k0 and message != message0) {
      QString t;
      t = t.asprintf("%4d %+3d: ",int(f1+0.5),int(snr-20.0));
      ui->decodedTextBrowser->insertText(t + message.trimmed());
    }
    message0 = message;
    k0=k;
  }
}

void MainWindow::jtty_tx(QString message)
{
  int itone[848];
  int n=message.length();
  m_currentMessage = message;

  // Display Tx message highlighted in yellow
  ui->decodedTextBrowser->insertText(" ");
  QTextCursor cursor = ui->decodedTextBrowser->textCursor();
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
  float f0=1500.0;
  int icmplx=0;
  int nwave=nsps4*m_nsym_jtty;
  gen_jttywave_(const_cast<int *>(itone), &m_nsym_jtty, &nsps4, &bt, &fsample, &f0,
                foxcom_.wave, foxcom_.wave, &icmplx, &nwave);
  monitor(false);
  if(!m_diskData && m_saveAll && (m_k0 > 53*384) && (m_k0 < 9999999)) {
    jtty_save_wav();
  }
  m_transmitting = true;
  startTx2();
  int msTx=nwave/48.0 + 1000*m_config.txDelay();
  QTimer::singleShot(msTx, this, SLOT (stopTx()));
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
  if(e->key() == Qt::Key_F1) {
    jtty_tx("CQ " + m_config.my_callsign() + " CQ");
    return true;
  } else if(e->key() == Qt::Key_F2) {
    int n=ui->sbSerialNumber_2->value();
    QString t=QString::number(n);
    if(n < 10) t = "00"+t;
    if(n < 100) t = "0"+t;
    t = " 599 " + t;
    jtty_tx(ui->dxCallEntry->text() + t);
    return true;
  } else if(e->key() == Qt::Key_F3) {
    jtty_tx("TU " + m_config.my_callsign() + " CQ");
    return true;
  } else if(e->key() == Qt::Key_F4) {
    jtty_tx(m_config.my_callsign());
    return true;
  } else if(e->key() == Qt::Key_F5) {
    jtty_tx(ui->dxCallEntry->text());
    return true;
  } else if(e->key() == Qt::Key_F6) {
    int n=ui->sbSerialNumber_2->value();
    QString t=QString::number(n);
    if(n < 10) t = "00"+t;
    if(n < 100) t = "0"+t;
    t = " 599 " + t;
    jtty_tx("TU NOW " + ui->dxCallEntry->text() + t);
    return true;
  } else if(e->key() == Qt::Key_F7) {
    int n=ui->sbSerialNumber_2->value();
    QString t=QString::number(n);
    if(n < 10) t = "00"+t;
    if(n < 100) t = "0"+t;
    t = " 599 " + t;
    jtty_tx(ui->dxCallEntry->text() + t);
    return true;
  } else if(e->key() == Qt::Key_F8) {
    jtty_tx("AGN?");
    return true;
  } else if(e->key() == Qt::Key_F9) {
    jtty_tx("NR?");
    return true;
  } else if((e->key() == int(Qt::Key_Enter)) or (e->key() == int(Qt::Key_Return))) {
    jtty_tx(ui->Tx_Message->text());
    ui->Tx_Message->clear();
  }
  return false;
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
    std::cout << "AAA " << n << "\n";
}
