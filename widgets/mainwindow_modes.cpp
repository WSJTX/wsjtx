#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "fastgraph.h"
#include "echograph.h"
#include "qt_helpers.hpp"
#include "activeStations.h"
#include "Detector/Detector.hpp"
#include "Modulator/Modulator.hpp"
#include "models/Bands.hpp"
#include <QSettings>
#include <QSignalBlocker>
#include <utility>

extern bool blocked;
extern bool HoldTxFreqStatus;
extern bool programStart;
extern int m_TxFreqFox;
extern int m_msk144_tr;
extern int m_msk144_tr2;
extern int m_msk144_tr6;

namespace
{
  constexpr int standard_messages_tab_index {0};
  constexpr int fox_queue_tab_index {1};
}

void MainWindow::requestModeChange (ModeEntry::Request request, std::function<void()> setup)
{
  auto const identity = m_modeEntries.submit (std::move (request));
  if (setup) m_modeSetupContinuations.insert (identity, std::move (setup));
  while (auto operation = m_modeEntries.takeNext ())
    {
      m_modeSetupActive = true;
      m_modeSetupMode = operation->request.mode;
      auto const configured = configureMode (operation->request.mode);
      if (configured)
        {
          m_modeEntries.accept (operation->identity);
          m_pendingModeRetune.cancel ();
        }
      auto const continuation = m_modeSetupContinuations.take (operation->identity);
      if (configured && continuation) continuation ();
      m_modeSetupActive = false;
      m_modeSetupMode.clear ();
      if (configured) completeModeFrequencyChange (*operation);
      m_modeEntries.finish (operation->identity);
    }
  resumeModeRetune ();
}

bool MainWindow::configureMode (QString const& mode)
{
  if (jttyDrainInProgress ())
    {
      ui->actionJTTY->setChecked (true);
      return false;
    }
  if (mode == "FST4") setupFST4 ();
  else if (mode == "FST4W") setupFST4W ();
  else if (mode == "FT4") setupFT4 ();
  else if (mode == "FT8") setupFT8 ();
  else if (mode == "JT4") setupJT4 ();
  else if (mode == "JT9") setupJT9 ();
  else if (mode == "JT65") setupJT65 ();
  else if (mode == "Q65") setupQ65 ();
  else if (mode == "JTTY") setupJTTY ();
  else if (mode == "MSK144") return setupMSK144 ();
  else if (mode == "WSPR") setupWSPR ();
  else if (mode == "Echo") setupEcho ();
  else if (mode == "FreqCal") setupFreqCal ();
  else return false;
  return true;
}

void MainWindow::scheduleModeRefresh (int delay, QString const& mode, std::function<void()> refresh)
{
  auto const identity = m_modeEntries.activeIdentity ();
  QTimer::singleShot (delay, this, [this, identity, mode, refresh = std::move (refresh)] {
    if (m_modeEntries.current (identity) && m_mode == mode) refresh ();
  });
}

void MainWindow::on_actionFST4_triggered()
{
  requestModeChange ({QStringLiteral ("FST4")});
}

void MainWindow::on_actionFST4W_triggered()
{
  requestModeChange ({QStringLiteral ("FST4W")});
}

void MainWindow::on_actionFT4_triggered()
{
  requestModeChange ({QStringLiteral ("FT4")});
}

void MainWindow::on_actionFT8_triggered()
{
  requestModeChange ({QStringLiteral ("FT8")});
}

void MainWindow::on_actionJT4_triggered()
{
  requestModeChange ({QStringLiteral ("JT4")});
}

void MainWindow::on_actionJT9_triggered()
{
  requestModeChange ({QStringLiteral ("JT9")});
}

void MainWindow::on_actionJT65_triggered()
{
  requestModeChange ({QStringLiteral ("JT65")});
}

void MainWindow::on_actionQ65_triggered()
{
  requestModeChange ({QStringLiteral ("Q65")});
}

void MainWindow::on_actionJTTY_triggered()
{
  requestModeChange ({QStringLiteral ("JTTY")});
}

void MainWindow::on_actionMSK144_triggered()
{
  requestModeChange ({QStringLiteral ("MSK144")});
}

void MainWindow::on_actionWSPR_triggered()
{
  requestModeChange ({QStringLiteral ("WSPR")});
}

void MainWindow::on_actionEcho_triggered()
{
  requestModeChange ({QStringLiteral ("Echo")});
}

void MainWindow::on_actionFreqCal_triggered()
{
  requestModeChange ({QStringLiteral ("FreqCal")});
}

void MainWindow::setupFST4()
{
  noteTxModeChange (QStringLiteral ("FST4"));
  scheduleModeRefresh (50, QStringLiteral ("FST4"), [=] {
    ui->TxFreqSpinBox->setValue(m_settings->value("TxFreq_old",1500).toInt());
    ui->RxFreqSpinBox->setValue(m_settings->value("RxFreq_old",1500).toInt());
    on_sbSubmode_valueChanged(ui->sbSubmode->value());
    ui->cbHoldTxFreq->setChecked (HoldTxFreqStatus);
  });
  m_mode="FST4";
  if(m_specOp==SpecOp::HOUND) {
  m_config.setSpecial_None();
  m_specOp=m_config.special_op_id();
  }
  ui->actionFST4->setChecked(true);
  m_bFast9=false;
  m_bFastMode=false;
  m_fastGraph->hide();
  m_wideGraph->show();
  initializeFFT(6912);
  setDecodeTitles(tr ("Band Activity"), tr ("Rx Frequency"));
  WSPR_config(false);
  if(m_config.single_decode()) {
    applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                      ModeUiControl::RxFrequency, ModeUiControl::FrequencyTolerance,
                      ModeUiControl::Report, ModeUiControl::TrPeriod,
                      ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                      ModeUiControl::CopyTxToRx, ModeUiControl::HoldTxFrequency,
                      ModeUiControl::DecodeDepth, ModeUiControl::Respond});
    m_wideGraph->setSingleDecode(true);
  } else {
    applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                      ModeUiControl::RxFrequency, ModeUiControl::Report,
                      ModeUiControl::TrPeriod, ModeUiControl::AutoSequence,
                      ModeUiControl::CopyRxToTx, ModeUiControl::CopyTxToRx,
                      ModeUiControl::HoldTxFrequency, ModeUiControl::DecodeDepth,
                      ModeUiControl::Respond, ModeUiControl::LowFrequency,
                      ModeUiControl::HighFrequency});
    m_wideGraph->setSingleDecode(false);
    ui->sbFtol->setValue(20);
  }
  setup_status_bar(false);
  ui->cbAutoSeq->setChecked(true);
  m_wideGraph->setMode(m_mode);
  m_wideGraph->setPeriod(m_TRperiod,6912);
  m_wideGraph->setRxFreq(ui->RxFreqSpinBox->value());
  m_wideGraph->setTol(ui->sbFtol->value());
  m_wideGraph->setTxFreq(ui->TxFreqSpinBox->value());
  m_wideGraph->setFST4_FreqRange(ui->sbF_Low->value(),ui->sbF_High->value());
  chk_FST4_freq_range();
  switch_mode (Modes::FST4);
  m_wideGraph->setMode(m_mode);
  ui->sbTR->values ({15, 30, 60, 120, 300, 900, 1800});
  ui->sbTR->setValue (m_settings->value ("TRPeriod_FST4", 60).toInt());    // remember sbTR settings by mode
  m_TRperiod=ui->sbTR->value();
  scheduleModeRefresh (50, QStringLiteral ("FST4"), [=] {on_sbTR_valueChanged (ui->sbTR->value());});
  setTxFirstModeEnabled (true);
  statusChanged();
  m_bOK_to_chk=true;
  chk_FST4_freq_range();
}

void MainWindow::setupFST4W()
{
  noteTxModeChange (QStringLiteral ("FST4W"));
  m_mode="FST4W";
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  ui->actionFST4W->setChecked(true);
  m_bFast9=false;
  m_bFastMode=false;
  m_fastGraph->hide();
  m_wideGraph->show();
  initializeFFT(6912);
  WSPR_config(true);
  applyModeUiState({ModeUiControl::WsprControls, ModeUiControl::DecodeDepth,
                    ModeUiControl::Fst4wTrPeriod});
  setup_status_bar(false);
  ui->band_hopping_group_box->setChecked(false);
  ui->band_hopping_group_box->setVisible(false);
  on_sbTR_FST4W_valueChanged (ui->sbTR_FST4W->value ());
  ui->WSPRfreqSpinBox->setMinimum(100);
  ui->WSPRfreqSpinBox->setMaximum(5000);
  m_wideGraph->setMode(m_mode);
  m_wideGraph->setPeriod(m_TRperiod,6912);
  m_wideGraph->setTxFreq(ui->WSPRfreqSpinBox->value());
  m_wideGraph->setRxFreq(ui->sbFST4W_RxFreq->value());
  m_wideGraph->setTol(ui->sbFST4W_FTol->value());
  ui->sbFtol->setValue(100);
  switch_mode (Modes::FST4W);
  statusChanged();
}

void MainWindow::setupFT4()
{
  noteTxModeChange (QStringLiteral ("FT4"));
  scheduleModeRefresh (50, QStringLiteral ("FT4"), [=] {
    ui->TxFreqSpinBox->setValue(m_settings->value("TxFreq_old",1500).toInt());
    ui->RxFreqSpinBox->setValue(m_settings->value("RxFreq_old",1500).toInt());
    on_sbSubmode_valueChanged(ui->sbSubmode->value());
    ui->cbHoldTxFreq->setChecked (HoldTxFreqStatus);
  });
  m_mode="FT4";
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  m_TRperiod=7.5;
  bool bVHF=m_config.enable_VHF_features();
  m_bFast9=false;
  m_bFastMode=false;
  WSPR_config(false);
  switch_mode (Modes::FT4);
  initializeFFT(6912);
  m_hsymStop=21;
  setup_status_bar (bVHF);
  m_toneSpacing=12000.0/576.0;
  ui->actionFT4->setChecked(true);
  m_wideGraph->setMode(m_mode);
  m_send_RR73=true;
  VHF_features_enabled(bVHF);
  ui->cbAutoSeq->setChecked(true);
  m_fastGraph->hide();
  m_wideGraph->show();
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  if (!m_tci_audio) {
    m_modulator->setTRPeriod(m_TRperiod); // TODO - not thread safe
    m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  }
  setDecodeTitles(tr ("Band Activity"), tr ("Rx Frequency"));
  setDecodeHeadings("  UTC   dB   DT Freq    " + tr ("Message"), "  UTC   dB   DT Freq    " + tr ("Message"));
  applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                    ModeUiControl::RxFrequency, ModeUiControl::Report,
                    ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                    ModeUiControl::CopyTxToRx, ModeUiControl::HoldTxFrequency,
                    ModeUiControl::DecodeDepth, ModeUiControl::Respond,
                    ModeUiControl::CqOnly});
  setTxButtonsEnabled(true);
  setTxFirstModeEnabled (true);
  chkFT4();
  statusChanged();
}

void MainWindow::setupFT8()
{
  noteTxModeChange (QStringLiteral ("FT8"));
  scheduleModeRefresh (50, QStringLiteral ("FT8"), [=] {
    if(m_specOp!=SpecOp::FOX) ui->TxFreqSpinBox->setValue(m_settings->value("TxFreq_old",1500).toInt());
    if(m_specOp==SpecOp::FOX && !m_config.superFox()) ui->TxFreqSpinBox->setValue(m_TxFreqFox);
    if(SpecOp::HOUND == m_specOp && m_config.superFox()) {
      clearDX();
      // Stale F/H decodes left in the Band Activity / Rx Frequency windows
      // can be double-clicked to re-prime Tx against a callsign that is no
      // longer valid in SuperFox/Hound (where the target comes from the
      // Active Stations widget via the $VERIFY$ flow).  Erase them so the
      // user-trap surface is gone.  Reported by AF8C, 2026-05-03.
      ui->decodedTextBrowser->erase ();
      ui->decodedTextBrowser2->erase ();
    }
    on_sbSubmode_valueChanged(ui->sbSubmode->value());
    ui->cbHoldTxFreq->setChecked (HoldTxFreqStatus);
  });
  m_mode="FT8";
  bool bVHF=m_config.enable_VHF_features();
  m_bFast9=false;
  m_bFastMode=false;
  WSPR_config(false);
  initializeFFT(6912);
  if (m_multithreadFT8 && !(m_specOp==SpecOp::HOUND && m_config.superFox())) {
    if (m_ft8DecoderStart==0) m_hsymStop=49;
    else if (m_ft8DecoderStart==1) {
      m_hsymStop=50;
      m_earlyDecode2=46;
    }
    else if (m_ft8DecoderStart==2) m_hsymStop=48;
    else if (m_ft8DecoderStart==3) m_hsymStop=49;
    else if (m_ft8DecoderStart==4) m_hsymStop=50;
  } else {
    m_hsymStop=50;
    m_earlyDecode2=47;
  }
  setup_status_bar (bVHF);
  m_toneSpacing=0.0;                   //???
  ui->actionFT8->setChecked(true);     //???
  m_wideGraph->setMode(m_mode);
  VHF_features_enabled(bVHF);
  ui->cbAutoSeq->setChecked(true);
  m_TRperiod=15.0;
  ui->sbFtol->setValue (m_settings->value ("Ftol_SF", 50).toInt()); // restore last used Ftol parameter
  m_fastGraph->hide();
  m_wideGraph->show();
  ui->rh_decodes_headings_label->setText("  UTC   dB   DT Freq    " + tr ("Message"));
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  if (!m_tci_audio) {
    m_modulator->setTRPeriod(m_TRperiod); // TODO - not thread safe
    m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  }
  ui->rh_decodes_title_label->setText(tr ("Rx Frequency"));
  if(SpecOp::FOX==m_specOp) {
    ui->lh_decodes_title_label->setText(tr ("Stations calling DXpedition %1").arg (m_config.my_callsign()));
    ui->lh_decodes_headings_label->setText( "Call         Grid   dB  Freq   Dist Age Cont Score");
  } else {
    ui->lh_decodes_title_label->setText(tr ("Band Activity"));
    ui->lh_decodes_headings_label->setText( "  UTC   dB   DT Freq    " + tr ("Message"));
  }

  applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                    ModeUiControl::RxFrequency, ModeUiControl::Report,
                    ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                    ModeUiControl::CopyTxToRx, ModeUiControl::HoldTxFrequency,
                    ModeUiControl::DecodeDepth, ModeUiControl::ApFt8,
                    ModeUiControl::Respond, ModeUiControl::CqOnly});
  setTxButtonsEnabled(true);
  setTxFirstModeEnabled (true);
  ui->cbAutoSeq->setEnabled(true);
  if(SpecOp::FOX==m_specOp) {
    ui->txFirstCheckBox->setChecked(true);
    setTxFirstModeEnabled (false);
    ui->cbHoldTxFreq->setChecked(true);
    ui->cbAutoSeq->setEnabled(false);
    m_wideGraph->setSuperFox(false);
    if(m_config.superFox()) {
      ui->TxFreqSpinBox->setValue(750);            //SuperFox transmits at 750 Hz
      m_wideGraph->setSuperFox(true);
    } else {
      ui->TxFreqSpinBox->setValue(500);
    }
    applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                      ModeUiControl::RxFrequency, ModeUiControl::Report,
                      ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                      ModeUiControl::CopyTxToRx, ModeUiControl::HoldTxFrequency,
                      ModeUiControl::DecodeDepth, ModeUiControl::DxpedLabel,
                      ModeUiControl::RxAll, ModeUiControl::FoxQueueTab});
    ui->cbRxAll->setText(tr("Show Already Worked"));
    ui->labDXped->setText(specOpLabel());
    on_fox_log_action_triggered();
    if (m_ActiveStationsWidget) m_ActiveStationsWidget->setClickOK(true); // allow clicks
  }
  if(SpecOp::HOUND == m_specOp) {
    ui->houndButton->setChecked(true);
    ui->txFirstCheckBox->setChecked(false);
    setTxFirstModeEnabled (false);
    ui->cbAutoSeq->setEnabled(false);
    if(ui->tabWidget->currentIndex() == fox_queue_tab_index) {
      ui->tabWidget->setCurrentIndex(standard_messages_tab_index);
    }
    ui->cbHoldTxFreq->setChecked(true);
    m_wideGraph->setSuperHound(false);
    if(m_config.superFox()) {
      applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                        ModeUiControl::RxFrequency, ModeUiControl::FrequencyTolerance,
                        ModeUiControl::Report, ModeUiControl::AutoSequence,
                        ModeUiControl::CopyRxToTx, ModeUiControl::CopyTxToRx,
                        ModeUiControl::DecodeDepth, ModeUiControl::DxpedLabel,
                        ModeUiControl::RxAll});
      ui->cbRxAll->setEnabled(false);
      m_wideGraph->setRxFreq(ui->RxFreqSpinBox->value());
      m_wideGraph->setTol(ui->sbFtol->value());
      m_wideGraph->setSuperHound(true);
      clearDX();
      if(ui->RxFreqSpinBox->value() < 700 or ui->RxFreqSpinBox->value() > 800)
        ui->RxFreqSpinBox->setValue(750);
    } else {
      applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                        ModeUiControl::RxFrequency, ModeUiControl::Report,
                        ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                        ModeUiControl::CopyTxToRx, ModeUiControl::DecodeDepth,
                        ModeUiControl::DxpedLabel, ModeUiControl::RxAll});
      ui->cbRxAll->setEnabled(true);
      m_wideGraph->setSuperHound(false);
      ui->RxFreqSpinBox->setValue(m_settings->value("RxFreq_old",1500).toInt());
    }
    ui->labDXped->setText(specOpLabel());
    ui->txrb1->setChecked(true);
    setTxButtonsEnabled(false);
    if (m_ActiveStationsWidget) m_ActiveStationsWidget->setClickOK(false);
  } else {
    switch_mode (Modes::FT8);
  }
  if(m_specOp != SpecOp::HOUND) {
      ui->houndButton->setChecked(false);
      m_wideGraph->setSuperHound(false);
  }

  m_specOp=m_config.special_op_id();
  if(m_specOp!=SpecOp::NONE and m_specOp!=SpecOp::FOX and m_specOp!=SpecOp::HOUND) {
    QString t0 = specOpLabel();
    if(t0.isEmpty()) {
      ui->labDXped->setVisible(false);
    } else {
      ui->labDXped->setVisible(true);
      ui->labDXped->setText(t0);
    }
    if(m_specOp!=SpecOp::Q65_PILEUP) on_contest_log_action_triggered();
  }

  if((SpecOp::FOX==m_specOp or SpecOp::HOUND==m_specOp) and !m_config.superFox() and !m_config.split_mode()
     and !m_bWarnedSplit and m_config.rig_name() != "None") {
    QString errorMsg;
    MessageBox::critical_message (this,
       "Operation in FT8 DXpedition mode normally requires\n"
       " *Split* rig control (either *Rig* or *Fake It* on\n"
       "the *Settings | Radio* tab.)", errorMsg);
    m_bWarnedSplit=true;
  }
  m_houndVerified = false;
  updateHoundVerificationStyle ();
  statusChanged();
  configActiveStations();
}

void MainWindow::setupJT4()
{
  noteTxModeChange (QStringLiteral ("JT4"));
  scheduleModeRefresh (50, QStringLiteral ("JT4"), [=] {
    ui->TxFreqSpinBox->setValue(m_settings->value("TxFreq_old",1500).toInt());
    ui->RxFreqSpinBox->setValue(m_settings->value("RxFreq_old",1500).toInt());
  });
  m_mode="JT4";
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  bool bVHF=m_config.enable_VHF_features();
  WSPR_config(false);
  switch_mode (Modes::JT4);
  m_TRperiod=60.0;
  if (!m_tci_audio) {
    m_modulator->setTRPeriod(m_TRperiod); // TODO - not thread safe
    m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  }
  initializeFFT(6912);
  m_hsymStop=176;
  if(m_config.decode_at_52s()) m_hsymStop=184;
  m_toneSpacing=0.0;
  ui->actionJT4->setChecked(true);
  VHF_features_enabled(true);
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  m_wideGraph->setMode(m_mode);
  m_bFastMode=false;
  m_bFast9=false;
  setup_status_bar (bVHF);
  ui->sbSubmode->setMaximum(6);
  setDecodeHeadings("UTC   dB   DT Freq    " + tr ("Message"), "UTC   dB   DT Freq    " + tr ("Message"));
  if(bVHF) {
    // restore last used parameters
    ui->sbFtol->setValue (m_settings->value ("Ftol_JT4", 50).toInt());
    m_nSubMode=m_settings->value("SubMode_JT4",0).toInt();
    ui->sbSubmode->setValue(m_settings->value("SubMode_JT4",0).toInt());
    scheduleModeRefresh (50, QStringLiteral ("JT4"), [=] {on_sbSubmode_valueChanged(ui->sbSubmode->value());});
    m_bShMsgs=m_settings->value("ShMsgs_JT4",false).toBool();
    ui->cbShMsgs->setChecked(m_bShMsgs);
  } else {
    ui->sbSubmode->setValue(0);
  }
  if(bVHF) {
    applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                      ModeUiControl::RxFrequency, ModeUiControl::FrequencyTolerance,
                      ModeUiControl::Report, ModeUiControl::ShortMessages,
                      ModeUiControl::AutoSequence, ModeUiControl::Tx6,
                      ModeUiControl::CopyRxToTx, ModeUiControl::CopyTxToRx,
                      ModeUiControl::Submode, ModeUiControl::Sync,
                      ModeUiControl::ClearAverage, ModeUiControl::DecodeDepth,
                      ModeUiControl::IncludeAveraging,
                      ModeUiControl::IncludeCorrelation, ModeUiControl::Respond});
  } else {
    applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                      ModeUiControl::RxFrequency, ModeUiControl::Report,
                      ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                      ModeUiControl::CopyTxToRx, ModeUiControl::ClearAverage,
                      ModeUiControl::DecodeDepth, ModeUiControl::Respond});
  }
  fast_config(false);
  setTxFirstModeEnabled (true);
  statusChanged();
}

void MainWindow::setupJT9()
{
  noteTxModeChange (QStringLiteral ("JT9"));
  m_mode="JT9";
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  bool bVHF=m_config.enable_VHF_features();
  // restore last used parameters
  if(bVHF && m_mode!="JT65" && !blocked) {
    ui->sbSubmode->setMaximum(7);
    m_bFast9=m_settings->value("JT9_Fast",false).toBool();
    ui->cbFast9->setChecked(m_bFast9 or m_bFastMode);
    ui->sbFtol->setValue (m_settings->value ("Ftol_JT9", 50).toInt());
    m_nSubMode=m_settings->value("SubMode",0).toInt();
    ui->sbSubmode->setValue(m_nSubMode);
    scheduleModeRefresh (50, QStringLiteral ("JT9"), [=] {
      on_sbTR_valueChanged (ui->sbTR->value());
      on_sbSubmode_valueChanged(ui->sbSubmode->value());
    });
  }
//  m_bFast9=ui->cbFast9->isChecked();
  m_bFastMode=m_bFast9;
  WSPR_config(false);
  switch_mode (Modes::JT9);
  initializeFFT(6912);
  m_hsymStop=173;
  if(m_config.decode_at_52s()) m_hsymStop=179;
  setup_status_bar (bVHF);
  m_toneSpacing=0.0;
  ui->actionJT9->setChecked(true);
  m_wideGraph->setMode(m_mode);
  VHF_features_enabled(bVHF);
  if(m_nSubMode>=4 and bVHF) {
    ui->cbFast9->setEnabled(true);
  } else {
    ui->cbFast9->setEnabled(false);
    ui->cbFast9->setChecked(false);
  }
  ui->sbSubmode->setMaximum(7);
  if(m_bFast9) {
    ui->sbTR->values ({5, 10, 15, 30});
    if(bVHF && m_mode!="JT65" && !blocked) ui->sbTR->setValue (m_settings->value ("TRPeriod", 15).toInt());  // restore last used TRperiod
    on_sbTR_valueChanged (ui->sbTR->value());
    m_wideGraph->hide();
    m_fastGraph->showNormal();
    ui->TxFreqSpinBox->setValue(700);
    ui->RxFreqSpinBox->setValue(700);
    setDecodeHeadings("  UTC   dB    T Freq    " + tr ("Message"), "  UTC   dB    T Freq    " + tr ("Message"));
  } else {
//    ui->cbAutoSeq->setChecked(false);
    if (m_mode != "FST4")
      {
        m_TRperiod=60.0;
        setDecodeHeadings("UTC   dB   DT Freq    " + tr ("Message"), "UTC   dB   DT Freq    " + tr ("Message"));
      }
  }
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  if (!m_tci_audio) {
    m_modulator->setTRPeriod(m_TRperiod); // TODO - not thread safe
    m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  }
  setDecodeTitles(tr ("Band Activity"), tr ("Rx Frequency"));
  if(bVHF) {
    applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                      ModeUiControl::RxFrequency, ModeUiControl::FrequencyTolerance,
                      ModeUiControl::Report, ModeUiControl::CqTxFrequency,
                      ModeUiControl::Fast9, ModeUiControl::AutoSequence,
                      ModeUiControl::CopyRxToTx, ModeUiControl::CopyTxToRx,
                      ModeUiControl::HoldTxFrequency, ModeUiControl::Submode,
                      ModeUiControl::Sync, ModeUiControl::DecodeDepth,
                      ModeUiControl::Respond});
  } else {
    applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                      ModeUiControl::RxFrequency, ModeUiControl::Report,
                      ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                      ModeUiControl::CopyTxToRx, ModeUiControl::HoldTxFrequency,
                      ModeUiControl::DecodeDepth, ModeUiControl::Respond,
                      ModeUiControl::CqOnly});
  }
  fast_config(m_bFastMode);
//  ui->cbAutoSeq->setVisible(m_bFast9);
  setTxFirstModeEnabled (true);
  statusChanged();
}

void MainWindow::setupJT65()
{
  noteTxModeChange (QStringLiteral ("JT65"));
  scheduleModeRefresh (50, QStringLiteral ("JT65"), [=] {
    ui->TxFreqSpinBox->setValue(m_settings->value("TxFreq_old",1500).toInt());
    ui->RxFreqSpinBox->setValue(m_settings->value("RxFreq_old",1500).toInt());
  });
  setupJT9();
  m_mode="JT65";
  m_jt65ClearAveragesPending = true;
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  bool bVHF=m_config.enable_VHF_features();
  WSPR_config(false);
  switch_mode (Modes::JT65);
  m_TRperiod=60.0;
  if (!m_tci_audio) {
    m_modulator->setTRPeriod(m_TRperiod); // TODO - not thread safe
    m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  }
  initializeFFT(6912);
  m_hsymStop=174;
  if(m_config.decode_at_52s()) m_hsymStop=183;
  m_toneSpacing=0.0;
  ui->actionJT65->setChecked(true);
  VHF_features_enabled(bVHF);
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  m_wideGraph->setMode(m_mode);
  m_wideGraph->setRxFreq(ui->RxFreqSpinBox->value());
  m_wideGraph->setTol(ui->sbFtol->value());
  m_wideGraph->setTxFreq(ui->TxFreqSpinBox->value());
  setup_status_bar (bVHF);
  m_bFastMode=false;
  m_bFast9=false;
  ui->sbSubmode->setMaximum(2);
  if(bVHF) {
    // restore last used parameters
    ui->sbFtol->setValue (m_settings->value ("Ftol_JT65", 50).toInt());
    m_nSubMode=m_settings->value("SubMode_JT65",0).toInt();
    ui->sbSubmode->setValue(m_settings->value("SubMode_JT65",0).toInt());
    scheduleModeRefresh (50, QStringLiteral ("JT65"), [=] {on_sbSubmode_valueChanged(ui->sbSubmode->value());});
    m_bShMsgs=m_settings->value("ShMsgs_JT65",false).toBool();
    ui->cbShMsgs->setChecked(m_bShMsgs);
  } else {
    ui->sbSubmode->setValue(0);
    setDecodeTitles(tr ("Band Activity"), tr ("Rx Frequency"));
  }
  if(bVHF) {
    applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                      ModeUiControl::RxFrequency, ModeUiControl::FrequencyTolerance,
                      ModeUiControl::Report, ModeUiControl::ShortMessages,
                      ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                      ModeUiControl::CopyTxToRx, ModeUiControl::Submode,
                      ModeUiControl::Sync, ModeUiControl::ClearAverage,
                      ModeUiControl::IncludeAveraging,
                      ModeUiControl::IncludeCorrelation, ModeUiControl::ApJt65,
                      ModeUiControl::Respond});
  } else {
    applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                      ModeUiControl::RxFrequency, ModeUiControl::Report,
                      ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                      ModeUiControl::CopyTxToRx, ModeUiControl::HoldTxFrequency,
                      ModeUiControl::DecodeDepth, ModeUiControl::Respond,
                      ModeUiControl::CqOnly});
  }
  fast_config(false);
//  if(ui->cbShMsgs->isChecked()) {
//    ui->cbAutoSeq->setChecked(false);
//    ui->cbAutoSeq->setVisible(false);
//  }
  if (m_config.decode_at_52s() && m_config.auto_astro() && !ui->actionAstronomical_data->isChecked())
    ui->actionAstronomical_data->setChecked (true);
  setTxFirstModeEnabled (true);
  statusChanged();
}

void MainWindow::setupQ65()
{
  noteTxModeChange (QStringLiteral ("Q65"));
  scheduleModeRefresh (50, QStringLiteral ("Q65"), [=] {
    ui->TxFreqSpinBox->setValue(m_settings->value("TxFreq_old",1500).toInt());
    ui->RxFreqSpinBox->setValue(m_settings->value("RxFreq_old",1500).toInt());
  });
  m_mode="Q65";
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  ui->actionQ65->setChecked(true);
  switch_mode(Modes::Q65);
  ui->cbAutoSeq->setChecked(true);
  fast_config(false);
  WSPR_config(false);
  setup_status_bar(true);
  m_bFastMode=false;
  m_bFast9=false;
  initializeFFT(6912);
  m_hsymStop=49;
  ui->sbTR->values ({15, 30, 60, 120, 300});
  // restore last used parameters
  ui->sbTR->setValue (m_settings->value ("TRPeriod_Q65", 30).toInt());
  m_TRperiod=ui->sbTR->value();
  ui->sbFtol->setValue (m_settings->value ("Ftol_Q65", 50).toInt());
  ui->sbSubmode->setValue(m_settings->value("SubMode_Q65",0).toInt());
  m_nSubMode=ui->sbSubmode->value();
  scheduleModeRefresh (50, QStringLiteral ("Q65"), [=] {
    on_sbTR_valueChanged (ui->sbTR->value());
    on_sbSubmode_valueChanged(ui->sbSubmode->value());
  });
  m_bShMsgs=m_settings->value("ShMsgs_Q65",false).toBool();
  ui->cbShMsgs->setChecked(m_bShMsgs);
  QString fname {QDir::toNativeSeparators(m_config.temp_dir().absoluteFilePath ("red.dat"))};
  m_wideGraph->setRedFile(fname);
  m_wideGraph->setMode(m_mode);
  m_wideGraph->setPeriod(m_TRperiod,6912);
  m_wideGraph->setTol(ui->sbFtol->value());
  m_wideGraph->setRxFreq(ui->RxFreqSpinBox->value());
  m_wideGraph->setTxFreq(ui->TxFreqSpinBox->value());
  switch_mode (Modes::Q65);
  applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                    ModeUiControl::RxFrequency, ModeUiControl::FrequencyTolerance,
                    ModeUiControl::Report, ModeUiControl::TrPeriod,
                    ModeUiControl::ShortMessages, ModeUiControl::AutoSequence,
                    ModeUiControl::Tx6, ModeUiControl::CopyRxToTx,
                    ModeUiControl::CopyTxToRx, ModeUiControl::Submode,
                    ModeUiControl::ClearAverage, ModeUiControl::DecodeDepth,
                    ModeUiControl::IncludeAveraging,
                    ModeUiControl::Respond, ModeUiControl::AutoClearAverage,
                    ModeUiControl::MaxDrift});
  setDecodeHeadings("UTC   dB   DT Freq    " + tr ("Message"), "UTC   dB   DT Freq    " + tr ("Message"));

  m_specOp=m_config.special_op_id();
  if(m_specOp!=SpecOp::NONE and m_specOp!=SpecOp::FOX and m_specOp!=SpecOp::HOUND) {
    QString t0 = specOpLabel();
    if(t0.isEmpty()) {
      ui->labDXped->setVisible(false);
    } else {
      ui->labDXped->setVisible(true);
      ui->labDXped->setText(t0);
    }
    if(m_specOp!=SpecOp::Q65_PILEUP) {
        on_contest_log_action_triggered();
    } else {
        if(ui->txrb1->isChecked()) on_txb2_clicked();
        ui->tx1->setEnabled(true);
        ui->txb1->setEnabled(true);
    }
  }
  if (m_config.decode_at_52s() && m_config.auto_astro() && !ui->actionAstronomical_data->isChecked())
    ui->actionAstronomical_data->setChecked (true);
  setTxFirstModeEnabled (true);
  statusChanged();
}

void MainWindow::setupJTTY()
{
  bool const enteringJtty = m_mode != "JTTY";
  noteTxModeChange (QStringLiteral ("JTTY"));
  on_stopButton_clicked();
  m_mode = "JTTY";
  ui->actionJTTY->setChecked(true);
  switch_mode (Modes::JTTY);
  WSPR_config(false);
  VHF_features_enabled(false);
  m_wideGraph->setMode(m_mode);
  ui->cbAutoSeq->setChecked(false);
  m_bFastMode=false;
  m_bFast9=false;
  m_nsps=6912;
  initializeFFT(m_nsps);
  m_FFTSize = m_nsps / 2;
  if (m_tci_audio) Q_EMIT m_config.transceiver_blocksize (m_FFTSize);
  else Q_EMIT FFTSize (m_FFTSize);
  m_TRperiod=180;                   // Legacy timed interfaces; JTTY receive uses sample coordinates.
  m_hsymStop=620;
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  if (enteringJtty)
    {
      scheduleModeRefresh (50, QStringLiteral ("JTTY"), [=] {
        ui->TxFreqSpinBox->setValue(m_settings->value("TxFreq_old",1500).toInt());
        ui->RxFreqSpinBox->setValue(m_settings->value("RxFreq_old",1500).toInt());
      });
    }
//  ui->RxFreqSpinBox_2->setSingleStep(200);
  ui->sbFtol_2->values ({2, 5, 10, 20, 50, 100, 150, 200, 250, 300, 350, 400, 450, 500});
  // Reapply the marker because setting the spinbox to its existing value emits no change.
  m_wideGraph->setRxFreq(ui->RxFreqSpinBox_2->value());
  m_wideGraph->setTol(ui->sbFtol_2->value());
  m_fastGraph->hide();
  m_wideGraph->showNormal();
  setDecodeHeadings("", "");
  updateJttyDecodeHeadings();
  updateJttyPttStateLabel();
  setDecodeTitles(tr ("All Decodes"), tr ("QSO Frequency"));
  applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::TxFrequency,
                    ModeUiControl::RxFrequency, ModeUiControl::FrequencyTolerance,
                    ModeUiControl::Report, ModeUiControl::TrPeriod,
                    ModeUiControl::AutoSequence, ModeUiControl::CopyRxToTx,
                    ModeUiControl::CopyTxToRx, ModeUiControl::HoldTxFrequency,
                    ModeUiControl::DecodeDepth, ModeUiControl::Respond});
  // JTTY's decoder has no Fast/Deep behavior to select -- force Normal and
  // disable the other two rather than offer a choice that does nothing.
  ui->actionMediumDecode->setChecked(true);
  ui->actionQuickDecode->setEnabled(false);
  ui->actionDeepestDecode->setEnabled(false);
  setup_status_bar (false);
}


bool MainWindow::setupMSK144()
{
  if(SpecOp::EU_VHF < m_specOp) {
// We are rejecting the requested mode change, so re-check the old mode
    if("FT8"==m_mode) ui->actionFT8->setChecked(true);
    if("JT4"==m_mode) ui->actionJT4->setChecked(true);
    if("JT9"==m_mode) ui->actionJT9->setChecked(true);
    if("JT65"==m_mode) ui->actionJT65->setChecked(true);
    if("Q65"==m_mode) ui->actionQ65->setChecked(true);
    if("WSPR"==m_mode) ui->actionWSPR->setChecked(true);
    if("Echo"==m_mode) ui->actionEcho->setChecked(true);
    if("FreqCal"==m_mode) ui->actionFreqCal->setChecked(true);
    if("FST4"==m_mode) ui->actionFST4->setChecked(true);
    if("FST4W"==m_mode) ui->actionFST4W->setChecked(true);
// Make sure that MSK144 is not checked.
    ui->actionMSK144->setChecked(false);
    MessageBox::warning_message (this, tr ("Improper mode"),
       "MSK144 not available if Fox, Hound, Field Day, FT Roundup, WW Digi. or ARRL Digi contest is selected.");
    return false;
  }
  m_hsymStop=105;
  m_TRperiod=ui->sbTR->value();
  noteTxModeChange (QStringLiteral ("MSK144"));
  m_mode="MSK144";
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  ui->actionMSK144->setChecked(true);
  switch_mode (Modes::MSK144);
  initializeFFT(6, 7 * 512);
  setup_status_bar (true);
  m_toneSpacing=0.0;
  WSPR_config(false);
  VHF_features_enabled(true);
  ui->cbAutoSeq->setChecked(true);
  m_bFastMode=true;
  m_bFast9=false;
  ui->sbTR->values ({5, 10, 15, 30});
  // restore last used parameters
  if (!programStart) {
    auto const request = m_modeEntries.activeRequest ();
    auto const frequency = request && request->frequency
      ? *request->frequency : m_operatingFrequency.rx ();
    auto const band = m_config.bands ()->find (frequency);
    if (band=="2m") ui->sbTR->setValue (m_msk144_tr2);
    else if (band=="6m" or band=="4m") ui->sbTR->setValue (m_msk144_tr6);
    else ui->sbTR->setValue (m_msk144_tr);
  }
  m_TRperiod=ui->sbTR->value();
  setTxFirstModeEnabled (true);
  scheduleModeRefresh (50, QStringLiteral ("MSK144"), [=] {on_sbTR_valueChanged (ui->sbTR->value());});
  ui->sbFtol->setValue (m_settings->value ("Ftol_MSK144", 50).toInt());   // restore last used parameter
  m_bShMsgs=m_settings->value("ShMsgs_MSK144",false).toBool();
  ui->cbShMsgs->setChecked(m_bShMsgs);
  m_wideGraph->hide();
  m_fastGraph->showNormal();
  ui->TxFreqSpinBox->setValue(1500);
  ui->RxFreqSpinBox->setValue(1500);
  ui->RxFreqSpinBox->setMinimum(1400);
  ui->RxFreqSpinBox->setMaximum(1600);
  ui->RxFreqSpinBox->setSingleStep(10);
  setDecodeHeadings("  UTC   dB    T Freq    " + tr ("Message"), "  UTC   dB    T Freq    " + tr ("Message"));
  if (!m_tci_audio) {
    m_modulator->setTRPeriod(m_TRperiod); // TODO - not thread safe
    m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  }
  m_fastGraph->setTRPeriod(m_TRperiod);
  setDecodeTitles(tr ("Band Activity"), tr ("Tx Messages"));
  ui->actionMSK144->setChecked(true);
  ui->rptSpinBox->setMinimum(-8);
  ui->rptSpinBox->setMaximum(24);
  ui->rptSpinBox->setValue(0);
  ui->rptSpinBox->setSingleStep(1);
  ui->sbFtol->values ({20, 50, 100, 150, 200, 250, 300, 350, 400, 450, 500});
  applyModeUiState({ModeUiControl::TxFirst, ModeUiControl::RxFrequency,
                    ModeUiControl::FrequencyTolerance, ModeUiControl::Report,
                    ModeUiControl::TrPeriod, ModeUiControl::CqTxFrequency,
                    ModeUiControl::ShortMessages, ModeUiControl::AutoSequence,
                    ModeUiControl::DecodeDepth, ModeUiControl::Swl,
                    ModeUiControl::Respond});
  fast_config(m_bFastMode);
  statusChanged();

  QString t0 = specOpLabel();
  if(t0.isEmpty()) {
    ui->labDXped->setVisible(false);
  } else {
    ui->labDXped->setVisible(true);
    ui->labDXped->setText(t0);
    on_contest_log_action_triggered();
  }
  return true;
}

void MainWindow::setupWSPR()
{
  noteTxModeChange (QStringLiteral ("WSPR"));
  m_mode="WSPR";
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  WSPR_config(true);
  switch_mode (Modes::WSPR);
  m_TRperiod=120.0;
  if (!m_tci_audio) {
    m_modulator->setTRPeriod(m_TRperiod); // TODO - not thread safe
    m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  }
  initializeFFT(6912);
  m_hsymStop=396;
  m_toneSpacing=12000.0/8192.0;
  setup_status_bar (false);
  ui->actionWSPR->setChecked(true);
  VHF_features_enabled(false);
  ui->WSPRfreqSpinBox->setMinimum(1400);
  ui->WSPRfreqSpinBox->setMaximum(1600);
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  m_wideGraph->setMode(m_mode);
  m_bFastMode=false;
  m_bFast9=false;
  ui->TxFreqSpinBox->setValue(ui->WSPRfreqSpinBox->value());
  applyModeUiState({ModeUiControl::WsprControls, ModeUiControl::DecodeDepth});
  fast_config(false);
  enterBeaconMode ();
  statusChanged();
}

void MainWindow::setupEcho()
{
  noteTxModeChange (QStringLiteral ("Echo"));
  int nd=int(m_ndepth&3);
  setupJT4();
  // Don't allow decoding depth to be changed just because Echo mode was entered:
  if(nd==1) ui->actionQuickDecode->setChecked (true);
  if(nd==2) ui->actionMediumDecode->setChecked (true);
  if(nd==3) ui->actionDeepestDecode->setChecked (true);

  m_mode="Echo";
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  ui->actionEcho->setChecked(true);
  m_TRperiod=3.0;
  if (!m_tci_audio) {
    m_modulator->setTRPeriod(m_TRperiod); // TODO - not thread safe
    m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  }
  initializeFFT(6912);
  m_hsymStop=9;
  m_toneSpacing=1.0;
  switch_mode(Modes::Echo);
  setup_status_bar (true);
  m_wideGraph->setMode(m_mode);
  ui->TxFreqSpinBox->setValue(1500);
  ui->TxFreqSpinBox->setEnabled (false);
  if(!m_echoGraph->isVisible()) m_echoGraph->show();
  if (!ui->actionAstronomical_data->isChecked ()) {
    ui->actionAstronomical_data->setChecked (true);
  }
  m_bFastMode=false;
  m_bFast9=false;
  WSPR_config(true);
  ui->lh_decodes_headings_label->setText("  UTC    Hour    Level  Doppler  Width  Dgrd     N     Q     DF    SNR   dBerr   TS  EchoMsg");
  applyModeUiState({ModeUiControl::ClearAverage, ModeUiControl::EchoGraph});
  fast_config(false);
  ui->sbEchoAvg->values ({1, 2, 5, 10, 20, 50, 100});
  statusChanged();
}

void MainWindow::setupFreqCal()
{
  auto const request = m_modeEntries.activeRequest ();
  auto const rx_audio = request && request->retune == ModeEntry::RetunePolicy::Preserve
    ? ui->RxFreqSpinBox->value () : 1500;
  noteTxModeChange (QStringLiteral ("FreqCal"));
  setupJT9();
  m_mode="FreqCal";
  if(m_specOp==SpecOp::HOUND) {
    m_config.setSpecial_None();
    m_specOp=m_config.special_op_id();
  }
  ui->actionFreqCal->setChecked(true);
  switch_mode(Modes::FreqCal);
  m_wideGraph->setMode(m_mode);
  ui->sbTR->values ({5, 10, 15, 30});
  on_sbTR_valueChanged (ui->sbTR->value());
  if (!m_tci_audio) {
    m_modulator->setTRPeriod(m_TRperiod); // TODO - not thread safe
    m_detector->setTRPeriod(m_TRperiod); // marshals to the audio thread
  }
  initializeFFT(6912);
  m_hsymStop=((int(m_TRperiod/0.288))/8)*8;
  {
    QSignalBlocker const primary {ui->RxFreqSpinBox};
    QSignalBlocker const secondary {ui->RxFreqSpinBox_2};
    ui->RxFreqSpinBox->setValue(rx_audio);
    ui->RxFreqSpinBox_2->setValue(rx_audio);
    m_wideGraph->setRxFreq(rx_audio);
  }
  setup_status_bar (true);
//                               18:15:47      0  1  1500  1550.349     0.100    3.5   10.2
  ui->lh_decodes_headings_label->setText("  UTC      Freq CAL Offset  fMeas       DF     Level   S/N");
  ui->measure_check_box->setChecked (false);
  {
    QSignalBlocker const range {ui->freqcal_range_check_box};
    ui->freqcal_range_check_box->setChecked (m_config.rig_range_override ());
  }
  applyModeUiState({ModeUiControl::RxFrequency, ModeUiControl::FrequencyTolerance,
                    ModeUiControl::TrPeriod, ModeUiControl::Measure,
                    ModeUiControl::FreqCalRange});
  statusChanged();
}
