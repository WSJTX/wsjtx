#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QInputDialog>
#include <QColor>
#include <QVector>
#include <QSettings>
#include <QtMath>
#include "MessageBox.hpp"
#include "commons.h"
#include "echograph.h"
#include "widegraph.h"
#include "messageaveraging.h"
#include "logqso.h"

using SpecOp = Configuration::SpecialOperatingActivity;

extern dec_data_t& dec_data;
extern int volatile itone[MAX_NUM_SYMBOLS];
extern int volatile itone0[MAX_NUM_SYMBOLS];
extern int volatile icw[NUM_CW_SYMBOLS];
extern int outBufSize;
extern int rc;
extern qint32 g_iptt;
extern QVector<QColor> g_ColorTbl;
extern bool verified;
extern bool blocked;
extern bool m_displayBand;
extern bool wait_and_call;
extern bool no_wait_and_call;
extern bool no_a7_decodes;
extern bool keep_frequency;
extern bool keep_msk144_frequency;
extern bool msk144qsy;
extern bool keep_last_tx_label;
extern int m_Nslots0;
extern int m_TxFreqFox;
extern bool not_erase;
extern bool first_Fox_alert;
extern bool second_Fox_alert;
extern bool no_Fox_alert;
extern int Dpoints;
extern int maxDPoints;
extern int dBpoints;
extern int dBpoints2;
extern int maxdBPoints;
extern int mindBPoints;
extern bool pounce;
extern bool filtered;
extern bool ignored;
extern bool selected;
extern bool keepTx5;
extern bool no_logging;
extern bool BlankLineInserted;
extern bool m_txing;
extern bool HoldTxFreqStatus;
extern bool m_band_changed;
extern bool m_muted;
extern bool no_decodes_to_UDP;
extern bool rigFailed;
extern bool programStart;
extern int m_msk144_tr;
extern int m_msk144_tr2;
extern int m_msk144_tr6;
extern QString txLog;
extern QString ignoreList;
extern QString ALLCALL7;
extern QString m_hisCall0;
extern QString earlyDecodes;


extern "C" {
  void genjtty_(char const * msg, int itone[], int* nsym, fortran_charlen_t);
  void gen_jttywave_(int itone[], int* nsym, int* nsps, float* bt, float* fsample, float* f0,
                    float xjunk[], float wave[], int* icmplx, int* nwave);
}

void MainWindow::on_monitorButton_clicked (bool checked)
{
  if (!m_transmitting) {
    auto prior = m_monitoring;
    monitor (checked);
    if (checked && !prior) {
      if (m_config.monitor_last_used () && m_mode!="Echo") {
        // put rig back where it was when last in control
        setRig (m_lastMonitoredFrequency);
        setXIT (ui->TxFreqSpinBox->value ());
      }
          // ensure FreqCal triggers
      if(m_mode=="FST4W") {
        on_sbFST4W_RxFreq_valueChanged(ui->sbFST4W_RxFreq->value());
      } else {
        on_RxFreqSpinBox_valueChanged (ui->RxFreqSpinBox->value ());
      }
    }
    //Get Configuration in/out of strict split and mode checking
    m_config.sync_transceiver (true, checked);
  } else {
    ui->monitorButton->setChecked (false); // disallow
  }
  if(m_mode=="Echo") m_echoRunning=false;
  check_button_color();
}

void MainWindow::on_autoButton_clicked (bool checked)
{
  if (ui->DX_Call_Button->isChecked() && m_specOp==SpecOp::HOUND && m_config.superFox() && !m_bDoubleClicked) return;  // for Wait & Call
  m_config.transceiver_tune (false);  // reset rig tuning
  if (checked && ui->tuneButton->isChecked() && !(m_mode=="WSPR" || m_mode=="FST4W")) return; // not allowed while tuning
  stopWRTimer.stop();                                       // stop any Wait & Reply timeout
  if (!checked && ui->DX_Call_Button->isChecked()) {
      stopWCTimer.stop();                                   // stop any Wait & Call timeout
      ui->DX_Call_Button->click ();                         // disable Wait & Call
      no_wait_and_call = false;                             // reset Wait & Call
  }
  m_specOp=m_config.special_op_id();
  ui->pbBandHopping->setChecked(false); // disable band hopping when Tx is enabled
  if (checked) {
      m_auto = checked;
      QTimer::singleShot (3000, [=] {pounce = false;});  // ensure to select only CQ messages
  } else {
      pounce = false;
      m_auto = false;
      m_bCallingCQ = false;
      ui->autoButton->setChecked(false);  // ensure autoButton is unchecked
      filtered = false;
      ignored = false;
      m_muted = false;
      Dpoints=0;                          // reset points
      maxDPoints=0;                       // reset points
      dBpoints=-28;                       // reset points
      dBpoints2=99;                       // reset points
      maxdBPoints=-28;                    // reset points
      mindBPoints=99;                     // reset points
  }
  m_maxPoints=-1;
  if (checked && ui->respondComboBox->isVisible() && ui->respondComboBox->currentText() != "CQ: None"
      && CALLING == m_QSOProgress) {
      m_bAutoReply = false;         // ready for next
      m_bCallingCQ = true;          // allows tail-enders to be picked up
  }
  statusUpdate ();
  m_bEchoTxOK=false;
  if(m_mode=="Echo" and m_auto) {
    m_nclearave=1;
    echocom_.nsum=0;
  }
  m_tAutoOn=QDateTime::currentMSecsSinceEpoch()/1000;
  if(m_mode=="Echo") m_echoRunning=false;
  check_button_color();
}

void MainWindow::on_stopButton_clicked()                       //stopButton
{
  ui->pbBandHopping->setChecked(false); // disable band hopping
  monitor (false);
  if(m_mode=="JTTY" and m_saveAll and !m_diskData) {
    jtty_save_wav();
  }
  m_loopall=false;
  if(m_bRefSpec) {
    MessageBox::information_message (this, tr ("Reference spectrum saved"));
    m_wideGraph->setReferenceSpectrumAvailable(
          QFile::exists(m_config.writeable_data_dir ().absoluteFilePath ("refspec.dat")));
    m_bRefSpec=false;
  }
  if (ui->DX_Call_Button->isChecked()) ui->DX_Call_Button->click ();
  stopWRTimer.stop();           // Stop any Wait & Reply timeout
  stopWCTimer.stop();           // Stop any Wait & Call timeout
  no_wait_and_call = false;
  m_specOp=m_config.special_op_id();
  if (ui->respondComboBox->isVisible() and ui->respondComboBox->currentIndex()!=0 and !m_diskData) {
    Dpoints=0;                          // reset points
    maxDPoints=0;                       // reset points
    dBpoints=-28;                       // reset points
    dBpoints2=99;                       // reset points
    maxdBPoints=-28;                    // reset points
    mindBPoints=99;                     // reset points
    if (!(m_mode=="Q65" or m_mode=="JT65")) {
      clearDX();                                   // clear dxCallEntry
      ui->dxGridEntry->clear ();                   // clear dxGridEntry
      if (!keepTx5) ui->tx5->setCurrentText("");   // clear tx5
    }
  }
  pounce = false;
  ui->autoButton->setChecked(false);  // ensure auoButton is unchecked
  filtered = false;
  ignored = false;
  m_muted = false;
  check_button_color();
}

void MainWindow::on_pbBandHopping_clicked()
{
  if (m_auto) ui->pbBandHopping->setChecked(false); // don't allow band hopping when in QSO
}

void MainWindow::on_DecodeButton_clicked (bool /* checked */) //Decode request
{
  if(m_mode=="MSK144") {
    ui->DecodeButton->setChecked(false);
  } else if(m_mode=="JTTY") {
    jtty_again();
  } else {
    if(m_mode!="WSPR" && !m_decoderBusy) {
      m_manualDecode=true;
      dec_data.params.newdat=0;
      dec_data.params.nagain=1;
      decode();
    }
  }
}

void MainWindow::on_ClrAvgButton_clicked()
{
  m_nclearave=1;
  if(m_mode=="Echo") {
    echocom_.nsum=0;
    m_echoGraph->clearAvg();
    m_wideGraph->restartTotalPower();
  } else {
    if(m_msgAvgWidget != NULL) {
      if(m_msgAvgWidget->isVisible()) m_msgAvgWidget->displayAvg("");
    }
    if(m_mode=="Q65") ndecodes_label.setText("0  0");
  }
}

void MainWindow::on_EraseButton_clicked ()
{
  qint64 ms=QDateTime::currentMSecsSinceEpoch();
  if (m_config.alternate_erase_button()) {
     ui->decodedTextBrowser->erase ();
     if((ms-m_msErase)<500) {
       ui->decodedTextBrowser2->erase ();
     }
  } else {
     ui->decodedTextBrowser2->erase ();
     if(m_mode=="WSPR" or m_mode=="Echo" or m_mode=="FST4W") {
       ui->decodedTextBrowser->erase ();
     } else {
       if((ms-m_msErase)<500) {
         ui->decodedTextBrowser->erase ();
       }
     }
  }
  m_msErase=ms;
}

void MainWindow::on_txb1_clicked()
{
  if (ui->tx1->isEnabled ()) {
    m_ntx=1;
    m_QSOProgress = REPLYING;
    ui->txrb1->setChecked(true);
    if(m_transmitting) m_restart=true;
  }
  else {
    on_txb2_clicked ();
  }
}

void MainWindow::on_txb2_clicked()
{
    m_ntx=2;
    m_QSOProgress = REPORT;
    ui->txrb2->setChecked(true);
    if(m_transmitting) m_restart=true;
}

void MainWindow::on_txb3_clicked()
{
    m_ntx=3;
    m_QSOProgress = ROGER_REPORT;
    ui->txrb3->setChecked(true);
    if(m_transmitting) m_restart=true;
}

void MainWindow::on_txb4_clicked()
{
    m_ntx=4;
    m_QSOProgress = ROGERS;
    ui->txrb4->setChecked(true);
    if(m_transmitting) m_restart=true;
}

void MainWindow::on_txb5_clicked()
{
    m_ntx=5;
    m_QSOProgress = SIGNOFF;
    ui->txrb5->setChecked(true);
    if(m_transmitting) m_restart=true;
}

void MainWindow::on_txb6_clicked()
{
    m_ntx=6;
    m_QSOProgress = CALLING;
    set_dateTimeQSO(-1);
    ui->txrb6->setChecked(true);
    if(m_transmitting) m_restart=true;
    if(m_mode=="MSK144" && !keep_msk144_frequency && m_msk144basefreq > 0 && !programStart && !m_band_changed) {
      setRig(m_msk144basefreq);  // reset MSK144 QSY
      msk144qsy = false;
    }
}

void MainWindow::on_lookupButton_clicked()                    //Lookup button
{
  qint64 ms=QDateTime::currentMSecsSinceEpoch();
  lookup();
  if((ms-m_msErase)<500) {
    QString hisCall=ui->dxCallEntry->text();
    if (hisCall !="") QDesktopServices::openUrl (QUrl {"https://www.qrz.com/db/" + hisCall});
  }
  m_msErase=ms;
}

void MainWindow::on_addButton_clicked()                       //Add button
{
  if(!ui->dxGridEntry->text ().size ()) {
    MessageBox::warning_message (this, tr ("Add to CALL3.TXT")
                                 , tr ("Please enter a valid grid locator"));
    return;
  }
  m_call3Modified=false;
  QString hisCall=ui->dxCallEntry->text();
  QString hisgrid=ui->dxGridEntry->text();
  QString newEntry=hisCall + "," + hisgrid;

  //  int ret = MessageBox::query_message(this, tr ("Add to CALL3.TXT"),
  //       tr ("Is %1 known to be active on EME?").arg (newEntry));
  //  if(ret==MessageBox::Yes) {
  //    newEntry += ",EME,,";
  //  } else {
  newEntry += ",,,";
  //  }

  QFile f1 {m_config.writeable_data_dir ().absoluteFilePath ("CALL3.TXT")};
  if(!f1.open(QIODevice::ReadWrite | QIODevice::Text)) {
    MessageBox::warning_message (this, tr ("Add to CALL3.TXT")
                                 , tr ("Cannot open \"%1\" for read/write: %2")
                                 .arg (f1.fileName ()).arg (f1.errorString ()));
    return;
  }
  if(f1.size()==0) {
    QTextStream out(&f1);
    out << "ZZZZZZ"
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
        << Qt::endl
#else
        << endl
#endif
      ;
    f1.seek (0);
  }
  QFile f2 {m_config.writeable_data_dir ().absoluteFilePath ("CALL3.TMP")};
  if(!f2.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
    MessageBox::warning_message (this, tr ("Add to CALL3.TXT")
                                 , tr ("Cannot open \"%1\" for writing: %2")
                                 .arg (f2.fileName ()).arg (f2.errorString ()));
    return;
  }
  {
    QTextStream in(&f1);          //Read from CALL3.TXT
    QTextStream out(&f2);         //Copy into CALL3.TMP
    QString hc=hisCall;
    QString hc1="";
    QString hc2="000000";
    QString s;
    do {
      s=in.readLine();
      hc1=hc2;
      if(s.mid(0,2)=="//") {
        out << s + QChar::LineFeed; //Copy all comment lines
      } else {
        int i1=s.indexOf(",");
        hc2=s.mid(0,i1);
        if(hc>hc1 && hc<hc2) {
          out << newEntry + QChar::LineFeed;
          out << s + QChar::LineFeed;
          m_call3Modified=true;
        } else if(hc==hc2) {
          QString t {tr ("%1\nis already in CALL3.TXT"
                         ", do you wish to replace it?").arg (s)};
          int ret = MessageBox::query_message (this, tr ("Add to CALL3.TXT"), t);
          if(ret==MessageBox::Yes) {
            out << newEntry + QChar::LineFeed;
            m_call3Modified=true;
          }
        } else {
          if(s!="") out << s + QChar::LineFeed;
        }
      }
    } while(!s.isNull());
    if(hc>hc1 && !m_call3Modified) out << newEntry + QChar::LineFeed;
  }

  if(m_call3Modified) {
    auto const& old_path = m_config.writeable_data_dir ().absoluteFilePath ("CALL3.OLD");
    QFile f0 {old_path};
    if (f0.exists ()) f0.remove ();
    f1.copy (old_path);                       // copying as we want to
                                              // preserve symlinks
    f1.open (QFile::WriteOnly | QFile::Text); // truncates
    f2.seek (0);
    QByteArray tmp = f2.readAll();
    if (tmp != (const char*)NULL) f1.write (tmp);                 // copy contents
    else qDebug() << "tmp==NULL at f1.write";
    f2.remove ();
  }
}

void MainWindow::on_ignoreButton_clicked()                    //Ignore button
{
  addCallsignToignoreList();
}

void MainWindow::on_DX_Call_Button_clicked (bool checked)
{
  if((m_mode=="FT8" or m_mode=="FT4" or m_mode=="Q65" or m_mode=="FST4" or m_mode=="MSK144") &&
     (m_specOp==SpecOp::NONE or m_specOp==SpecOp::HOUND) && ui->cbAutoSeq->isChecked() && checked
     && (m_hisCall!="" or (m_mode=="FT8" && m_specOp==SpecOp::HOUND))) {
      wait_and_call = true;       // toggle Wait & Call on when allowed
  } else {
      wait_and_call = false;      // toggle Wait & Call off in any other case
      ui->DX_Call_Button->setChecked (false);
      if (m_specOp==SpecOp::HOUND && m_config.superFox() && !m_auto) clearDX();
  }
  check_button_color();
}

void MainWindow::on_genStdMsgsPushButton_clicked()          //genStdMsgs button
{
  ui->pbBandHopping->setChecked(false); // disable band hopping
  genStdMsgs(m_rpt);
  if (!m_bDoubleClicked && m_hisCall!="") {
      if (ui->tx1->isEnabled ()) {
          QTimer::singleShot (0, ui->txrb1, SLOT (click ()));   // Go to Tx1
      } else {
          QTimer::singleShot (0, ui->txrb2, SLOT (click ()));   // Go to Tx2 if Tx1 is disabled
      }
      m_bMyCallStd=stdCall(m_config.my_callsign()); //ft8md
      m_bHisCallStd=stdCall(m_hisCall); //ft8md
      
  }
}

void MainWindow::on_logQSOButton_clicked()                 //Log QSO button
{
  if (!((m_config.repeat_Tx() or !m_send_RR73) && (m_mode=="MSK144" or m_mode=="Q65"))) {
    if (SpecOp::NA_VHF==m_specOp && m_mode=="FT4" && m_config.NCCC_Sprint()) {
      QTimer::singleShot (int(850.0*m_TRperiod), [=] {cease_auto_Tx_after_QSO ();});
    } else {
      cease_auto_Tx_after_QSO ();
    }
  }

  if (!m_hisCall.size ()) {
    MessageBox::warning_message (this, tr ("Warning:  DX Call field is empty."));
    if ((SpecOp::NA_VHF == m_specOp or SpecOp::WW_DIGI == m_specOp) && m_config.autoLog()) return;  // prevent program crash
  }
  // m_dateTimeQSOOn should really already be set but we'll ensure it gets set to something just in case
  if (!m_dateTimeQSOOn.isValid ()) {
    auto now = QDateTime::currentDateTimeUtc();
    m_dateTimeQSOOn = now.addSecs (-(m_ntx - 2) * int(m_TRperiod) -
                                   int(fmod(double(now.time().second()),m_TRperiod)));
  }
  auto dateTimeQSOOff = QDateTime::currentDateTimeUtc();
  if (dateTimeQSOOff < m_dateTimeQSOOn) dateTimeQSOOff = m_dateTimeQSOOn;
  QString grid=m_hisGrid;
  if(grid=="....") grid="";

  // Optionally replace empty grids by "ZZ00"
  if(m_config.ZZ00() && m_hisGrid=="" && m_specOp!=SpecOp::NONE && m_specOp!=SpecOp::FOX && m_specOp!=SpecOp::HOUND) m_hisGrid = "ZZ00";

  switch( m_specOp )
    {
      case SpecOp::NA_VHF:
        m_xSent=m_config.my_grid().left(4);
        m_xRcvd=m_hisGrid.left(4);
        break;
      case SpecOp::EU_VHF:
        m_rptSent=m_xSent.split(" ").at(0).left(2);
        m_rptRcvd=m_xRcvd.split(" ").at(0).left(2);
        if(m_xRcvd.split(" ").size()>=2) m_hisGrid=m_xRcvd.split(" ").at(1);
        grid=m_hisGrid;
        ui->dxGridEntry->setText(grid);
        break;
      case SpecOp::FIELD_DAY:
        m_rptSent=m_xSent.split(" ").at(0);
        m_rptRcvd=m_xRcvd.split(" ").at(0);
        break;
      case SpecOp::RTTY:
        m_rptSent=m_xSent.split(" ").at(0);
        m_rptRcvd=m_xRcvd.split(" ").at(0);
        break;
      case SpecOp::WW_DIGI:
        m_xSent=m_config.my_grid().left(4);
        m_xRcvd=m_hisGrid.left(4);
        break;
      case SpecOp::ARRL_DIGI:
        m_xSent=m_config.my_grid().left(4);
        m_xRcvd=m_hisGrid.left(4);
        break;
      case SpecOp::Q65_PILEUP:
        m_xSent=m_config.my_grid().left(4);
        m_xRcvd=m_hisGrid;
        break;
      default: break;
    }

  m_logDlg->initLogQSO (m_hisCall, grid, m_mode, m_rptSent, m_rptRcvd,
                        m_dateTimeQSOOn, dateTimeQSOOff, m_freqNominal +
                        ui->TxFreqSpinBox->value(), m_noSuffix, m_xSent, m_xRcvd);
  m_inQSOwith="";
  if (ui->respondComboBox->isVisible() && ui->respondComboBox->currentText() != "CQ: None") {
        Dpoints=0;                          // reset points
        maxDPoints=0;                       // reset points
        dBpoints=-28;                       // reset points
        dBpoints2=99;                       // reset points
        maxdBPoints=-28;                    // reset points
        mindBPoints=99;                     // reset points
  }
  QTimer::singleShot (2000, [=] {
      pounce = false;
      filtered = false;
      read_txLog();
      check_button_color();
  });
  QTimer::singleShot (7000, [=] {
      read_txLog();
  });
  stopWRTimer.stop();           // Stop any Wait & Reply timeout
  stopWCTimer.stop();           // Stop any Wait & Call timeout
}

void MainWindow::on_tuneButton_clicked (bool checked)
{
  ui->pbBandHopping->setChecked(false); // disable band hopping
  // prevent tuning on top of a SuperFox message
  if (SpecOp::HOUND==m_specOp && m_config.superFox() && !m_tune) {
    QDateTime now = QDateTime::currentDateTimeUtc();
    int s = now.time().toString("ss").toInt();
    if ((s >= 0 && s < 15) || (s >= 30 && s < 45)) {
      ui->tuneButton->setChecked (false);
      m_config.transceiver_tune (false);  // reset rig tuning
      return;
    }
  }
  m_config.transceiver_tune (false);  // reset rig tuning
  if (blocked) return;
  if (m_auto && !(m_mode=="WSPR" || m_mode=="FST4W")) ui->autoButton->click();   // stop any other transmission
  stopWRTimer.stop();           // stop any Wait & Reply timeout
  stopWCTimer.stop();           // stop any Wait & Call timeout
  if (checked && m_config.tune_watchdog() && !(m_mode=="WSPR" || m_mode=="FST4W")) {
      tuneATU_Timer.start (m_config.tune_watchdog_time()*1000); // tune watchdog
  }
  if (!checked) {
      tuneATU_Timer.stop ();    // stop tune watchdog when stopping Tune manually
      ui->tuneButton->setText("Tune");
  }
  static bool lastChecked = false;
  if (lastChecked == checked) return;
  lastChecked = checked;
  if (checked && m_tune==false) { // we're starting tuning so remember Tx and change pwr to Tune value
    if (m_config.pwrBandTuneMemory ()) {
      auto const& curBand = ui->bandComboBox->currentText();
      m_pwrBandTxMemory[curBand] = ui->outAttenuation->value(); // remember our Tx pwr
      m_PwrBandSetOK = false;
      if (m_pwrBandTuneMemory.contains(curBand)) {
        ui->outAttenuation->setValue(m_pwrBandTuneMemory[curBand].toInt()); // set to Tune pwr
      }
      m_PwrBandSetOK = true;
    }
  }
  if (m_tune) {
    tuneButtonTimer.start(250);
  } else {
    m_sentFirst73=false;
    itone[0]=0;
    on_monitorButton_clicked (true);
    m_tune=true;
  }
  if (m_tci_audio) Q_EMIT m_config.transceiver_tune(checked);
  else Q_EMIT tune (checked);
}

void MainWindow::on_stopTxButton_clicked()                    // Stop Tx
{
  ui->pbBandHopping->setChecked(false); // disable band hopping
  if (m_tune) stop_tuning ();
  if (m_auto and !m_tuneup) auto_tx_mode (false);
  m_btxok=false;
  m_bCallingCQ = false;
  m_bAutoReply = false;         // ready for next
  m_maxPoints=-1;
  if (ui->DX_Call_Button->isChecked()) ui->DX_Call_Button->click ();
  stopWRTimer.stop();           // Stop any Wait & Reply timeout
  stopWCTimer.stop();           // Stop any Wait & Call timeout
  tuneATU_Timer.stop ();        // stop tune watchdog when stopping Tune manually
  no_wait_and_call = false;
  m_specOp=m_config.special_op_id();
  if (ui->respondComboBox->isVisible() && ui->respondComboBox->currentText() != "CQ: None") {
      Dpoints=0;                          // reset points
      maxDPoints=0;                       // reset points
      dBpoints=-28;                       // reset points
      dBpoints2=99;                       // reset points
      maxdBPoints=-28;                    // reset points
      mindBPoints=99;                     // reset points
  }
  pounce = false;
  ui->autoButton->setChecked(false);  // ensure auoButton is unchecked
  filtered = false;
  ignored = false;
  m_muted = false;
  check_button_color();
}

void MainWindow::on_pbR2T_clicked()
{
  ui->TxFreqSpinBox->setValue(ui->RxFreqSpinBox->value ());
}

void MainWindow::on_pbT2R_clicked()
{
  if (ui->RxFreqSpinBox->isEnabled ())
    {
      ui->RxFreqSpinBox->setValue (ui->TxFreqSpinBox->value ());
    }
}

void MainWindow::on_pbR2T_2_clicked()
{
    ui->TxFreqSpinBox_2->setValue(ui->RxFreqSpinBox_2->value ());
}

void MainWindow::on_pbT2R_2_clicked()
{
    ui->RxFreqSpinBox_2->setValue (ui->TxFreqSpinBox_2->value ());
}

void MainWindow::on_readFreq_clicked()
{
  if (m_transmitting) return;

  if (m_config.transceiver_online ())
    {
      m_config.sync_transceiver (true, true);
    }
}

void MainWindow::on_cbFast9_clicked(bool b)
{
  if(m_mode=="JT9") {
    m_bFast9=b;
//    ui->cbAutoSeq->setVisible(b);
    blocked=true;   // needed to prevent a loop
    on_actionJT9_triggered();
    QTimer::singleShot (50, [=] {blocked = false;});   // needed to prevent a loop
    QTimer::singleShot (200, [=] {
      if(m_mode=="JT9") m_settings->setValue("JT9_Fast",m_bFast9);
    });
  }

  if(b) {
    m_TRperiod = ui->sbTR->value ();
  } else {
    m_TRperiod=60.0;
  }
  progressBar.setMaximum(int(m_TRperiod));
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  fast_config(b);
  statusChanged ();
}

void MainWindow::on_pbTxNext_clicked(bool b)
{
  if (b && !ui->autoButton->isChecked ())
    {
      ui->autoButton->click (); // make sure Tx is possible
    }
}

void MainWindow::on_pbFoxReset_clicked()
{
  if(m_specOp!=SpecOp::FOX) return;
  auto button = MessageBox::query_message (this, tr ("Confirm Reset"),
      tr ("Are you sure you want to clear the QSO queues?"));
  if(button == MessageBox::Yes) {
    FoxReset("Manual Reset");
  }
}

void MainWindow::on_pbFreeText_clicked()
{
  bool ok;
  if(m_config.superFox()) {
    m_freeTextMsg = QInputDialog::getText (this, tr("Free Text Message"),
           tr("Message:"), QLineEdit::Normal, m_freeTextMsg0, &ok).left(26);
  } else {
    m_freeTextMsg = QInputDialog::getText (this, tr("Free Text Message"),
           tr("Message:"), QLineEdit::Normal, m_freeTextMsg0, &ok).left(13);
  }
  if(ok) {
    m_freeTextMsg=m_freeTextMsg.toUpper();
    m_freeTextMsg0=m_freeTextMsg;
  }
}

void MainWindow::on_pbBestSP_clicked()
{
  m_bBestSPArmed = !m_bBestSPArmed;
  if(m_bBestSPArmed and !m_transmitting) ui->pbBestSP->setStyleSheet ("QPushButton{color:red}");
  if(!m_bBestSPArmed) ui->pbBestSP->setStyleSheet ("");
  if(m_bBestSPArmed) m_dateTimeBestSP=QDateTime::currentDateTimeUtc();
}

void MainWindow::on_houndButton_clicked (bool checked)
{
  if (checked) {
    HoldTxFreqStatus = ui->cbHoldTxFreq->isChecked();  // save state of the Hold Tx Freq checkbox
    m_config.setSpecial_Hound();
    ui->tx1->setVisible(true);
    ui->tx1->setEnabled(true);
    ui->txb1->setEnabled(true);
  } else {
    m_config.setSpecial_None();
    keep_frequency = true;
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  m_specOp=m_config.special_op_id();
  on_actionFT8_triggered();
  check_button_color();
}

void MainWindow::on_cbHoldTxFreq_clicked (bool)
{
    HoldTxFreqStatus = ui->cbHoldTxFreq->isChecked();  // save state of the Hold Tx Freq checkbox
}

void MainWindow::on_ft8Button_clicked()
{
    if (m_specOp==SpecOp::HOUND or m_specOp==SpecOp::FOX) {
      m_config.setSpecial_None();
      m_specOp=m_config.special_op_id();
    }
    on_actionFT8_triggered();
}

void MainWindow::on_ft4Button_clicked()
{
    on_actionFT4_triggered();
}

void MainWindow::on_msk144Button_clicked()
{
    on_actionMSK144_triggered();
}

void MainWindow::on_q65Button_clicked()
{
    if (m_specOp==SpecOp::Q65_PILEUP) {
      m_config.setSpecial_None();
      m_specOp=m_config.special_op_id();
    }
    on_actionQ65_triggered();
}

void MainWindow::on_jt65Button_clicked()
{
    on_actionJT65_triggered();
}

void MainWindow::on_echoButton_clicked()
{
    on_actionEcho_triggered();
}

void MainWindow::on_pb15A_clicked()
{
    ui->sbTR->setValue(15);
    ui->sbSubmode->setValue(0);
}

void MainWindow::on_pb15C_clicked()
{
    ui->sbTR->setValue(15);
    ui->sbSubmode->setValue(2);
    ui->TxFreqSpinBox->setValue(700);
}

void MainWindow::on_pb30B_clicked()
{
    ui->sbTR->setValue(30);
    ui->sbSubmode->setValue(1);
}

void MainWindow::on_pb60C_clicked()
{
    ui->sbTR->setValue(60);
    ui->sbSubmode->setValue(2);
}

void MainWindow::on_pb60D_clicked()
{
    ui->sbTR->setValue(60);
    ui->sbSubmode->setValue(3);
}

void MainWindow::on_pb60E_clicked()
{
    ui->sbTR->setValue(60);
    ui->sbSubmode->setValue(4);
    ui->TxFreqSpinBox->setValue(700);
}

void MainWindow::on_pb160_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (1840000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(1837000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb80_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (3573000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(3576000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb60_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (5357000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(5357000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb40_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (7074000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(7077000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb30_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (10136000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(10139000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb20_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (14074000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(14077000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb17_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (18100000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(18103000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb15_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (21074000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(21077000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb12_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (24915000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(24918000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb10_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (28074000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(28077000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb6_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr6);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (50313000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(50316000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb2_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr2);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (144074000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(144077000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb70_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (432074000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(432077000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb8_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (40680000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(40680000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb50_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr6);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (50313000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(50316000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb4_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr6);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (70154000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(70154000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb144_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr2);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (144074000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(144077000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb220_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (222174000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(222177000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb432_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (432174000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(432177000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb902_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (902174000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(902177000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb23_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (1296065000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(1296065000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb13_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (2304065000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(2304065000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb9_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (3400065000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(3400065000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb5G_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (5760200000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(5760200000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb10G_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (10368200000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(10368200000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pb24G_clicked()
{
  if (m_mode=="MSK144") {
    ui->sbTR->setValue (m_msk144_tr);
    programStart = true;
    QTimer::singleShot (250, [=] {programStart = false;});
  }
  auto const& row = m_config.frequencies ()->best_working_frequency (24048200000);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) {
    on_bandComboBox_activated (row);
  } else {
    keep_frequency = true;
    setRig(24048200000);
    QTimer::singleShot (250, [=] {keep_frequency = false;});
  }
  setXIT (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_pbSendMessage_clicked()
{
  jtty_tx(ui->Tx_Message->text().toUpper());
}

void MainWindow::on_pbF1_clicked()
{
  QString t = ui->msg1->text();
  t=jtty_msg_expand(t);
  jtty_tx(t.toUpper());
}

void MainWindow::on_pbF2_clicked()
{
  QString t = ui->msg2->text();
  t=jtty_msg_expand(t);
  jtty_tx(t.toUpper());
}

void MainWindow::on_pbF3_clicked()
{
  QString t = ui->msg3->text();
  t=jtty_msg_expand(t);
  jtty_tx(t.toUpper());
}

void MainWindow::on_pbF4_clicked()
{
  QString t = ui->msg4->text();
  t=jtty_msg_expand(t);
  jtty_tx(t.toUpper());
}

void MainWindow::on_pbF5_clicked()
{
  QString t = ui->msg5->text();
  t=jtty_msg_expand(t);
  jtty_tx(t.toUpper());
}

void MainWindow::on_pbF6_clicked()
{
  QString t = ui->msg6->text();
  t=jtty_msg_expand(t);
  jtty_tx(t.toUpper());
}

void MainWindow::on_pbF7_clicked()
{
  QString t = ui->msg7->text();
  t=jtty_msg_expand(t);
  jtty_tx(t.toUpper());
}

void MainWindow::on_pbF8_clicked()
{
  QString t = ui->msg8->text();
  t=jtty_msg_expand(t);
  jtty_tx(t.toUpper());
}
