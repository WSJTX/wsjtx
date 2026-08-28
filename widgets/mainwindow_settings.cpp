#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "commons.h"
#include "MessageBox.hpp"
#include "widgets/messageaveraging.h"
#include "widgets/activeStations.h"
#include "widgets/FoxLogWindow.hpp"
#include "widgets/CabrilloLogWindow.hpp"
#include "widgets/QSYMessageCreator.h"
#include "widgets/qsymonitor.h"
#include "validators/CallsignValidator.hpp"
#include "qt_helpers.hpp"

#include <QSettings>
#include <QVariant>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <QMessageBox>
#include <QAudioOutput>

extern dec_data_t& dec_data;
extern int outBufSize;
extern bool blocked;
extern int m_TxFreqFox;
extern bool HoldTxFreqStatus;
extern int m_msk144_tr;
extern int m_msk144_tr2;
extern int m_msk144_tr6;

namespace {
  Radio::Frequency constexpr default_frequency {14074000};
  constexpr int standard_messages_tab_index {0};
  constexpr int fox_queue_tab_index {1};
}

void MainWindow::writeSettings()
{
  m_settings->beginGroup("MainWindow");
  if (ui->actionSWL_Mode->isChecked ())
    {
      m_settings->setValue ("SWLView", true);
      m_settings->setValue ("ShowMenus", ui->cbMenus->isChecked ());
      m_settings->setValue ("geometry", geometries ()[0]);
      m_settings->setValue ("SWLModeGeometry", saveGeometry ());
      m_settings->setValue ("geometryNoControls", geometries ()[2]);
    }
  else
    {
      if (ui->cbMenus->isChecked())
        {
          m_settings->setValue ("SWLView", ui->actionSWL_Mode->isChecked ());
          m_settings->setValue ("ShowMenus", true);
          m_settings->setValue ("geometry", saveGeometry ());
          m_settings->setValue ("SWLModeGeometry", geometries ()[1]);
          m_settings->setValue ("geometryNoControls", geometries ()[2]);
        }
      else
        {
          m_settings->setValue ("SWLView", ui->actionSWL_Mode->isChecked ());
          m_settings->setValue ("ShowMenus", false);
          m_settings->setValue ("geometry", geometries ()[0]);
          m_settings->setValue ("SWLModeGeometry", geometries ()[1]);
          m_settings->setValue ("geometryNoControls", saveGeometry ());
        }
    }
  m_settings->setValue ("state", saveState ());
  m_settings->setValue("MRUdir", m_path);
  m_settings->setValue("TxFirst",m_txFirst);
  m_settings->setValue("DXcall",ui->dxCallEntry->text());
  m_settings->setValue("DXgrid",ui->dxGridEntry->text());
  m_settings->setValue("AstroDisplayed", m_astroWidget && m_astroWidget->isVisible());
  m_settings->setValue("MsgAvgDisplayed", m_msgAvgWidget && m_msgAvgWidget->isVisible ());
  m_settings->setValue("FoxLogDisplayed", m_foxLogWindow && m_foxLogWindow->isVisible ());
  m_settings->setValue("ContestLogDisplayed", m_contestLogWindow && m_contestLogWindow->isVisible ());
  m_settings->setValue("ActiveStationsDisplayed", m_ActiveStationsWidget && m_ActiveStationsWidget->isVisible ());
  m_settings->setValue("QSYMessageCreatorDisplayed", m_QSYMessageCreatorWidget && m_QSYMessageCreatorWidget->isVisible ());
  m_settings->setValue("ShowQSYMessages", ui->actionEnable_QSY_Popups->isChecked());
  m_settings->setValue("QSYMonitorDisplayed", m_qsymonitorWidget && m_qsymonitorWidget->isVisible ());
  m_settings->setValue("RespondCQ",ui->respondComboBox->currentIndex());
  m_settings->setValue("HoundSort",ui->comboBoxHoundSort->currentIndex());
  m_settings->setValue("FoxNlist",ui->sbNlist->value());
  m_settings->setValue("FoxNslots",m_Nslots0);
  m_settings->setValue("SerialNumber",ui->sbSerialNumber->value ());
  m_settings->setValue("SerialNumberJTTY",ui->sbSerialNumber_2->value ());
  m_settings->setValue("JTTY_msg1",ui->msg1->text());
  m_settings->setValue("JTTY_msg2",ui->msg2->text());
  m_settings->setValue("JTTY_msg3",ui->msg3->text());
  m_settings->setValue("JTTY_msg4",ui->msg4->text());
  m_settings->setValue("JTTY_msg5",ui->msg5->text());
  m_settings->setValue("JTTY_msg6",ui->msg6->text());
  m_settings->setValue("JTTY_msg7",ui->msg7->text());
  m_settings->setValue("JTTY_msg8",ui->msg8->text());
  m_settings->setValue("FoxTextMsg", m_freeTextMsg0);
  m_settings->setValue("WorkDupes", ui->cbWorkDupes->isChecked());
  m_settings->setValue("JTTY_LowerCase",ui->cbLowerCase->isChecked());
  m_settings->endGroup();

  // do this in the General group because we save the parameters from various places
  if(m_mode=="JT9") {
        m_settings->setValue("SubMode",ui->sbSubmode->value());
        m_settings->setValue("TRPeriod", ui->sbTR->value());
  }
  if(m_mode=="MSK144") m_settings->setValue("ShMsgs_MSK144",m_bShMsgs);
  if(m_mode=="Q65") m_settings->setValue("ShMsgs_Q65",m_bShMsgs);
  if(m_mode=="JT65") m_settings->setValue("ShMsgs_JT65",m_bShMsgs);
  if(m_mode=="JT4") m_settings->setValue("ShMsgs_JT4",m_bShMsgs);

  m_settings->beginGroup("Common");
  m_settings->setValue("Mode",m_mode);
  m_settings->setValue("SaveNone",ui->actionNone->isChecked());
  m_settings->setValue("SaveDecoded",ui->actionSave_decoded->isChecked());
  m_settings->setValue("SaveAll",ui->actionSave_all->isChecked());
  m_settings->setValue("RemoveAudioFiles",ui->actionRemove_after_30days->isChecked());
  m_settings->setValue("NDepth",m_ndepth);

  //ft8md
  m_settings->setValue ("MultithreadedFT8decoder",ui->actionUse_multithreaded_FT8_decoder->isChecked() );
  m_settings->setValue("NFT8Cycles",m_nFT8Cycles);
  m_settings->setValue("NFT8QSORXfreqSensitivity",m_nFT8RXfSens);
  m_settings->setValue("FT8threads",m_ft8threads);
  m_settings->setValue("FT8Sensitivity",m_ft8Sensitivity);
  m_settings->setValue("FT8DecoderStart",m_ft8DecoderStart);
  m_settings->setValue("FT8WideDXCallSearch",m_FT8WideDxCallSearch);
  m_settings->setValue("HideFT8Dupes",ui->actionHide_FT8_dupe_messages->isChecked());
  //ft8md

  m_settings->setValue("RxFreq",ui->RxFreqSpinBox->value());
  if(m_specOp!=SpecOp::FOX) m_settings->setValue("TxFreq",ui->TxFreqSpinBox->value());
  m_settings->setValue("TxFreqFox",m_TxFreqFox);
  m_settings->setValue("WSPRfreq",ui->WSPRfreqSpinBox->value());
  m_settings->setValue("FST4W_RxFreq",ui->sbFST4W_RxFreq->value());
  m_settings->setValue("FST4W_FTol",ui->sbFST4W_FTol->value());
  m_settings->setValue("JTTY_FTol",ui->sbFtol_2->value());
  m_settings->setValue("FST4_FLow",ui->sbF_Low->value());
  m_settings->setValue("FST4_FHigh",ui->sbF_High->value());
  m_settings->setValue("EchoToneSpacing",ui->sbToneSpacing->value());
  m_settings->setValue("EchoFixedTone",ui->rbFixedTone->isChecked());
  m_settings->setValue("EchoMessageRB",ui->rbEchoMessage->isChecked());
  m_settings->setValue("EchoCW",ui->rbEchoCW->isChecked());
  m_settings->setValue("EchoMessage",ui->leEchoMessage->text());
  m_settings->setValue("DTtol",m_DTtol);
  m_settings->setValue("MinSync",m_minSync);
  m_settings->setValue ("AutoSeq", ui->cbAutoSeq->isChecked ());
  m_settings->setValue ("RxAll", ui->cbRxAll->isChecked ());
// m_settings->setValue("ShMsgs",m_bShMsgs);
  m_settings->setValue("SWL",ui->cbSWL->isChecked());
  if(m_mode=="MSK144" && hasMsk144BaseFrequency ()) {
    m_settings->setValue ("DialFreq", QVariant::fromValue(m_msk144basefreq));  // MSK144 QSY
  } else {
    m_settings->setValue ("DialFreq", QVariant::fromValue(m_lastMonitoredFrequency));
  }
  m_settings->setValue("SkedFreq",m_skedFreq);
  m_settings->setValue("OutAttenuation", ui->outAttenuation->value ());
  m_settings->setValue("NoSuffix",m_noSuffix);
  m_settings->setValue("GUItab",ui->tabWidget->currentIndex());
  m_settings->setValue("OutBufSize",outBufSize);
  m_settings->setValue ("HoldTxFreq", ui->cbHoldTxFreq->isChecked ());
  m_settings->setValue ("CQonly", ui->cbCQonly->isChecked ());
  m_settings->setValue ("BypassFilters", ui->cbBypass->isChecked ());
  m_settings->setValue("PctTx", ui->sbTxPercent->value ());
  m_settings->setValue("RoundRobin",ui->RoundRobin->currentText());
  m_settings->setValue("dBm",m_dBm);
  m_settings->setValue("RR73",m_send_RR73);
  m_settings->setValue ("WSPRPreferType1", ui->WSPR_prefer_type_1_check_box->isChecked ());
  m_settings->setValue("UploadSpots",m_uploadWSPRSpots);
  m_settings->setValue("NoOwnCall",ui->cbNoOwnCall->isChecked());
  m_settings->setValue ("BandHopping", ui->band_hopping_group_box->isChecked ());
  m_settings->setValue ("MaxDrift", ui->sbMaxDrift->value());
  m_settings->setValue ("TRPeriod_FST4W", ui->sbTR_FST4W->value ());
  m_settings->setValue("FastMode",m_bFastMode);
  m_settings->setValue("Fast9",m_bFast9);
  m_settings->setValue ("CQTxfreq", ui->sbCQTxFreq->value ());
  m_settings->setValue("pwrBandTxMemory",m_pwrBandTxMemory);
  m_settings->setValue("pwrBandTuneMemory",m_pwrBandTuneMemory);
  m_settings->setValue ("FT8AP", ui->actionEnable_AP_FT8->isChecked ());
  m_settings->setValue ("JT65AP", ui->actionEnable_AP_JT65->isChecked ());
  m_settings->setValue ("AutoClearAvg", ui->actionAuto_Clear_Avg->isChecked ());
  m_settings->setValue ("DisableClicksOnWaterfall", ui->actionDisable_clicks_on_waterfall->isChecked ());
  m_settings->setValue("SplitterState",ui->decodes_splitter->saveState());
  m_settings->setValue("Blanker",ui->sbNB->value());
  m_settings->setValue("Score",m_score);
  m_settings->setValue("labDXpedText",ui->labDXped->text());
  m_settings->setValue("EchoAvg",ui->sbEchoAvg->value());
  {
    QList<QVariant> coeffs;     // suitable for QSettings
    for (auto const& coeff : m_phaseEqCoefficients)
      {
        coeffs << coeff;
      }
    m_settings->setValue ("PhaseEqualizationCoefficients", QVariant {coeffs});
  }
  m_settings->setValue ("bh_160m", ui->cb160m->isChecked() );
  m_settings->setValue ("bh_80m", ui->cb80m->isChecked() );
  m_settings->setValue ("bh_60m", ui->cb60m->isChecked() );
  m_settings->setValue ("bh_40m", ui->cb40m->isChecked() );
  m_settings->setValue ("bh_30m", ui->cb30m->isChecked() );
  m_settings->setValue ("bh_20m", ui->cb20m->isChecked() );
  m_settings->setValue ("bh_17m", ui->cb17m->isChecked() );
  m_settings->setValue ("bh_15m", ui->cb15m->isChecked() );
  m_settings->setValue ("bh_12m", ui->cb12m->isChecked() );
  m_settings->setValue ("bh_10m", ui->cb10m->isChecked() );
  m_settings->setValue ("bh_6m", ui->cb6m->isChecked() );
  m_settings->setValue ("bh_4m", ui->cb4m->isChecked() );
  m_settings->setValue ("bh_2m", ui->cb2m->isChecked() );
  m_settings->setValue ("bh_70cm", ui->cb70cm->isChecked() );
  m_settings->setValue ("bh_2mMSK", ui->cb2mMSK->isChecked() );
  m_settings->setValue ("bh_80mFT4", ui->cb80mFT4->isChecked() );
  m_settings->setValue ("bh_40mFT4", ui->cb40mFT4->isChecked() );
  m_settings->setValue ("bh_30mFT4", ui->cb30mFT4->isChecked() );
  m_settings->setValue ("bh_20mFT4", ui->cb20mFT4->isChecked() );
  m_settings->setValue ("bh_17mFT4", ui->cb17mFT4->isChecked() );
  m_settings->setValue ("bh_15mFT4", ui->cb15mFT4->isChecked() );
  m_settings->setValue ("bh_12mFT4", ui->cb12mFT4->isChecked() );
  m_settings->setValue ("bh_10mFT4", ui->cb10mFT4->isChecked() );
  m_settings->setValue ("bh_QRG1", ui->cbQRG1->isChecked() );
  m_settings->setValue ("bh_QRG2", ui->cbQRG2->isChecked() );
  m_settings->setValue ("bh_QRG3", ui->cbQRG3->isChecked() );
  m_settings->setValue ("bh_QRG4", ui->cbQRG4->isChecked() );
  m_settings->setValue ("bh_QRG5", ui->cbQRG5->isChecked() );
  m_settings->setValue ("bh_QRG6", ui->cbQRG6->isChecked() );
  m_settings->setValue ("bh_QRG7", ui->cbQRG7->isChecked() );
  m_settings->setValue ("bh_QRG8", ui->cbQRG8->isChecked() );
  m_settings->setValue ("QRG1", ui->sbQRG1->value ());
  m_settings->setValue ("QRG2", ui->sbQRG2->value ());
  m_settings->setValue ("QRG3", ui->sbQRG3->value ());
  m_settings->setValue ("QRG4", ui->sbQRG4->value ());
  m_settings->setValue ("QRG5", ui->sbQRG5->value ());
  m_settings->setValue ("QRG6", ui->sbQRG6->value ());
  m_settings->setValue ("QRG7", ui->sbQRG7->value ());
  m_settings->setValue ("QRG8", ui->sbQRG8->value ());
  m_settings->setValue ("reduceFalseDecodes", ui->actionReduce_false_decodes->isChecked() );
  m_settings->setValue ("HideAPInfo", ui->actionHide_AP_info->isChecked() );
  m_settings->setValue ("FullDuplexMode", ui->actionFull_Duplex_Mode->isChecked() );
  m_settings->setValue ("actionDontSplitALLTXT", ui->actionDon_t_split_ALL_TXT->isChecked() );
  m_settings->setValue ("splitAllTxtYearly", ui->actionSplit_ALL_TXT_yearly->isChecked() );
  m_settings->setValue ("splitAllTxtMonthly", ui->actionSplit_ALL_TXT_monthly->isChecked() );
  m_settings->setValue ("disableWritingOfAllTxt", ui->actionDisable_writing_of_ALL_TXT->isChecked() );
  m_settings->setValue ("DisableEventLogging", ui->actionDisable_event_logging->isChecked() );
  m_settings->setValue ("DarkStyle", ui->actionUse_Dark_Style->isChecked() );
  m_settings->setValue ("BandButtons", ui->actionBand_Buttons->isChecked() );
  m_settings->setValue ("VHFUHFButtons", ui->actionVHF_UHF_Buttons->isChecked() );
  m_settings->setValue ("tx1State", ui->tx1->isEnabled() );
  m_settings->setValue ("HighlightB4", ui->actionHighlightB4->isChecked() );
  m_settings->setValue ("HighlightToday", ui->actionHighlightToday->isChecked() );
  m_settings->setValue ("HighlightIgnored", ui->actionHighlightIgnored->isChecked() );
  m_settings->setValue ("HideB4", ui->actionHideB4->isChecked() );
  m_settings->setValue ("HideToday", ui->actionHideToday->isChecked() );
  m_settings->setValue ("HideIgnored", ui->actionHideIgnored->isChecked() );
  m_settings->setValue ("IgnoreB4", ui->actionIgnoreB4->isChecked() );
  m_settings->setValue ("IgnoreToday", ui->actionIgnoreToday->isChecked() );
  m_settings->setValue ("IgnoreIgnored", ui->actionIgnoreIgnored->isChecked() );
  m_settings->setValue ("HideTerritory1", ui->actionHideTerritory1->isChecked() );
  m_settings->setValue ("HideTerritory2", ui->actionHideTerritory2->isChecked() );
  m_settings->setValue ("HideTerritory3", ui->actionHideTerritory3->isChecked() );
  m_settings->setValue ("HideTerritory4", ui->actionHideTerritory4->isChecked() );
  m_settings->setValue ("HighlightTerritory1", ui->actionHighlightTerritory1->isChecked() );
  m_settings->setValue ("HighlightTerritory2", ui->actionHighlightTerritory2->isChecked() );
  m_settings->setValue ("HighlightTerritory3", ui->actionHighlightTerritory3->isChecked() );
  m_settings->setValue ("HighlightTerritory4", ui->actionHighlightTerritory4->isChecked() );
  m_settings->setValue ("HideEU", ui->actionHideEU->isChecked() );
  m_settings->setValue ("HideNA", ui->actionHideNA->isChecked() );
  m_settings->setValue ("HideSA", ui->actionHideSA->isChecked() );
  m_settings->setValue ("HideAS", ui->actionHideAS->isChecked() );
  m_settings->setValue ("HideAF", ui->actionHideAF->isChecked() );
  m_settings->setValue ("HideOC", ui->actionHideOC->isChecked() );
  m_settings->setValue ("HideAN", ui->actionHideAN->isChecked() );
  m_settings->setValue ("HighlightWhitelistEntries", ui->actionHighlight_Whitelist_entries->isChecked() );
  m_settings->endGroup();
}

void MainWindow::readSettings()
{
  ui->cbAutoSeq->setVisible(false);
  ui->respondComboBox->setVisible(false);

  m_settings->beginGroup("MainWindow");
  std::array<QByteArray, 3> the_geometries;
  the_geometries[0] = m_settings->value ("geometry", saveGeometry ()).toByteArray ();
  the_geometries[1] = m_settings->value ("SWLModeGeometry", saveGeometry ()).toByteArray ();
  the_geometries[2] = m_settings->value ("geometryNoControls", saveGeometry ()).toByteArray ();
  auto SWL_mode = m_settings->value ("SWLView", false).toBool ();
  auto show_menus = m_settings->value ("ShowMenus", true).toBool ();
  ui->actionSWL_Mode->setChecked (SWL_mode);
  ui->cbMenus->setChecked (show_menus);
  ui->cbLowerCase->setChecked(m_settings->value("JTTY_LowerCase",false).toBool());
  auto current_view_mode = SWL_mode ? 1 : show_menus ? 0 : 2;
  change_layout (current_view_mode);
  geometries (current_view_mode, the_geometries);
  restoreState (m_settings->value ("state", saveState ()).toByteArray ());
  setDXInfo(m_settings->value ("DXcall", QString {}).toString (),
           m_settings->value ("DXgrid", QString {}).toString ());
  m_path = m_settings->value("MRUdir", m_config.save_directory ().absolutePath ()).toString ();
  m_txFirst = m_settings->value("TxFirst",false).toBool();
  auto displayAstro = m_settings->value ("AstroDisplayed", false).toBool ();
  auto displayMsgAvg = m_settings->value ("MsgAvgDisplayed", false).toBool ();
  auto displayFoxLog = m_settings->value ("FoxLogDisplayed", false).toBool ();
  auto displayContestLog = m_settings->value ("ContestLogDisplayed", false).toBool ();
  bool displayActiveStations = m_settings->value ("ActiveStationsDisplayed", false).toBool ();
  bool displayQSYMessageCreator = m_settings->value ("QSYMessageCreatorDisplayed", false).toBool ();
  bool displayQSYMonitor = m_settings->value("QSYMonitorDisplayed", false).toBool ();
  bool enableQSYpopups = m_settings->value("ShowQSYMessages", true).toBool ();
  ui->respondComboBox->setCurrentIndex(m_settings->value("RespondCQ",0).toInt());
  ui->comboBoxHoundSort->setCurrentIndex(m_settings->value("HoundSort",3).toInt());
  ui->sbNlist->setValue(m_settings->value("FoxNlist",12).toInt());
  m_Nslots=m_settings->value("FoxNslots",3).toInt();
  m_Nslots0=m_Nslots;
  if(!m_config.superFox()) ui->sbNslots->setValue(m_Nslots);
  ui->sbSerialNumber->setValue (m_settings->value ("SerialNumber", 1).toInt ());
  ui->sbSerialNumber_2->setValue (m_settings->value ("SerialNumberJTTY", 1).toInt ());
  ui->msg1->setText(m_settings->value("JTTY_msg1","CQ %M CQ").toString());
  ui->msg2->setText(m_settings->value("JTTY_msg2","%H 599 %N").toString());
  ui->msg3->setText(m_settings->value("JTTY_msg3","%H TU CQ %M CQ").toString());
  ui->msg4->setText(m_settings->value("JTTY_msg4","%M").toString());
  ui->msg5->setText(m_settings->value("JTTY_msg5","%H").toString());
  ui->msg6->setText(m_settings->value("JTTY_msg6","TU NOW %Q 599 %N").toString());
  ui->msg7->setText(m_settings->value("JTTY_msg7","%H AGN?").toString());
  ui->msg8->setText(m_settings->value("JTTY_msg8","599 %N").toString());
  m_freeTextMsg0=m_settings->value("FoxTextMsg","").toString();
  m_freeTextMsg=m_freeTextMsg0;
  ui->cbWorkDupes->setChecked(m_settings->value("WorkDupes",false).toBool());
  m_settings->endGroup();

  m_settings->beginGroup("Common");
  m_mode=m_settings->value("Mode","FT8").toString();
  m_settings->endGroup();

  // do this outside of settings group because it uses groups internally
  ui->actionAstronomical_data->setChecked (displayAstro);
  ui->actionEnable_QSY_Popups->setChecked (enableQSYpopups);

  // do this in the General group because we save the parameters from various places
  if(m_mode=="JT9") {
    blocked=true;
    m_nSubMode=m_settings->value("SubMode",0).toInt();
    ui->sbSubmode->setValue(m_nSubMode);
    ui->sbFtol->setValue (m_settings->value("Ftol_JT9", 50).toInt());
    ui->sbTR->setValue (m_settings->value ("TRPeriod", 15).toInt());
    QTimer::singleShot (50, [=] {blocked = false;});
  }
  if (m_mode=="FT8") {
    ui->sbFtol->setValue (m_settings->value("Ftol_SF", 50).toInt());
  }
  if (m_mode=="Q65") {
    m_nSubMode=m_settings->value("SubMode_Q65",0).toInt();
    ui->sbSubmode->setValue(m_nSubMode);
    ui->sbFtol->setValue (m_settings->value("Ftol_Q65", 50).toInt());
    ui->sbTR->setValue (m_settings->value ("TRPeriod_Q65", 30).toInt());
  }
  if (m_mode=="JT65") {
    m_nSubMode=m_settings->value("SubMode_JT65",0).toInt();
    ui->sbSubmode->setValue(m_nSubMode);
    ui->sbFtol->setValue (m_settings->value("Ftol_JT65", 50).toInt());
  }
  if (m_mode=="JT4") {
    m_nSubMode=m_settings->value("SubMode_JT4",0).toInt();
    ui->sbSubmode->setValue(m_nSubMode);
    ui->sbFtol->setValue (m_settings->value("Ftol_JT4", 50).toInt());
    ui->sbTR->setValue (m_settings->value ("TRPeriod_FST4", 60).toInt());
  }
  if (m_mode=="MSK144") {
    ui->sbFtol->setValue (m_settings->value("Ftol_MSK144",50).toInt());
    m_msk144_tr2=m_settings->value ("TRPeriod_MSK144_2m", 30).toInt();
    m_msk144_tr6=m_settings->value ("TRPeriod_MSK144_6m", 15).toInt();
    m_msk144_tr=m_settings->value ("TRPeriod_MSK144", 30).toInt();
    QTimer::singleShot (3000, [=] {
      if (m_currentBand=="2m") ui->sbTR->setValue (m_msk144_tr2);
      else if (m_currentBand=="6m" or m_currentBand=="4m") ui->sbTR->setValue (m_msk144_tr6);
      else ui->sbTR->setValue (m_msk144_tr);
    });
  }
  if (m_mode=="MSK144") m_bShMsgs=m_settings->value("ShMsgs_MSK144",false).toBool();
  if (m_mode=="Q65") m_bShMsgs=m_settings->value("ShMsgs_Q65",false).toBool();
  if (m_mode=="JT65") m_bShMsgs=m_settings->value("ShMsgs_JT65",false).toBool();
  if (m_mode=="JT4") m_bShMsgs=m_settings->value("ShMsgs_JT4",false).toBool();

  m_settings->beginGroup("Common");
  ui->cb160m->setChecked(m_settings->value("bh_160m", false).toBool());
  ui->cb80m->setChecked(m_settings->value("bh_80m", false).toBool());
  ui->cb60m->setChecked(m_settings->value("bh_60m", false).toBool());
  ui->cb40m->setChecked(m_settings->value("bh_40m", false).toBool());
  ui->cb30m->setChecked(m_settings->value("bh_30m", false).toBool());
  ui->cb20m->setChecked(m_settings->value("bh_20m", false).toBool());
  ui->cb17m->setChecked(m_settings->value("bh_17m", false).toBool());
  ui->cb15m->setChecked(m_settings->value("bh_15m", false).toBool());
  ui->cb12m->setChecked(m_settings->value("bh_12m", false).toBool());
  ui->cb10m->setChecked(m_settings->value("bh_10m", false).toBool());
  ui->cb6m->setChecked(m_settings->value("bh_6m", false).toBool());
  ui->cb4m->setChecked(m_settings->value("bh_4m", false).toBool());
  ui->cb2m->setChecked(m_settings->value("bh_2m", false).toBool());
  ui->cb70cm->setChecked(m_settings->value("bh_70cm", false).toBool());
  ui->cb2mMSK->setChecked(m_settings->value("bh_2mMSK", false).toBool());
  ui->cb80mFT4->setChecked(m_settings->value("bh_80mFT4", false).toBool());
  ui->cb40mFT4->setChecked(m_settings->value("bh_40mFT4", false).toBool());
  ui->cb30mFT4->setChecked(m_settings->value("bh_30mFT4", false).toBool());
  ui->cb20mFT4->setChecked(m_settings->value("bh_20mFT4", false).toBool());
  ui->cb17mFT4->setChecked(m_settings->value("bh_17mFT4", false).toBool());
  ui->cb15mFT4->setChecked(m_settings->value("bh_15mFT4", false).toBool());
  ui->cb12mFT4->setChecked(m_settings->value("bh_12mFT4", false).toBool());
  ui->cb10mFT4->setChecked(m_settings->value("bh_10mFT4", false).toBool());
  ui->cbQRG1->setChecked(m_settings->value("bh_QRG1", false).toBool());
  ui->cbQRG2->setChecked(m_settings->value("bh_QRG2", false).toBool());
  ui->cbQRG3->setChecked(m_settings->value("bh_QRG3", false).toBool());
  ui->cbQRG4->setChecked(m_settings->value("bh_QRG4", false).toBool());
  ui->cbQRG5->setChecked(m_settings->value("bh_QRG5", false).toBool());
  ui->cbQRG6->setChecked(m_settings->value("bh_QRG6", false).toBool());
  ui->cbQRG7->setChecked(m_settings->value("bh_QRG7", false).toBool());
  ui->cbQRG8->setChecked(m_settings->value("bh_QRG8", false).toBool());
  ui->sbQRG1->setValue (m_settings->value ("QRG1", 3567).toInt ());
  ui->sbQRG2->setValue (m_settings->value ("QRG2", 7056).toInt ());
  ui->sbQRG3->setValue (m_settings->value ("QRG3", 10131).toInt ());
  ui->sbQRG4->setValue (m_settings->value ("QRG4", 14090).toInt ());
  ui->sbQRG5->setValue (m_settings->value ("QRG5", 18095).toInt ());
  ui->sbQRG6->setValue (m_settings->value ("QRG6", 21091).toInt ());
  ui->sbQRG7->setValue (m_settings->value ("QRG7", 24911).toInt ());
  ui->sbQRG8->setValue (m_settings->value ("QRG8", 28091).toInt ());
  ui->actionReduce_false_decodes->setChecked(m_settings->value("reduceFalseDecodes", false).toBool());
  ui->actionHide_AP_info->setChecked(m_settings->value("HideAPInfo", false).toBool());
  ui->actionFull_Duplex_Mode->setChecked(m_settings->value("FullDuplexMode", false).toBool());
  ui->labDXped->setText(m_settings->value("labDXpedText",QString {}).toString ());
  ui->actionDon_t_split_ALL_TXT->setChecked(m_settings->value("actionDontSplitALLTXT", true).toBool());
  ui->actionSplit_ALL_TXT_yearly->setChecked(m_settings->value("splitAllTxtYearly", false).toBool());
  ui->actionSplit_ALL_TXT_monthly->setChecked(m_settings->value("splitAllTxtMonthly", false).toBool());
  ui->actionDisable_writing_of_ALL_TXT->setChecked(m_settings->value("disableWritingOfAllTxt", false).toBool());
  ui->actionDisable_event_logging->setChecked(m_settings->value("DisableEventLogging", false).toBool());
  ui->actionUse_Dark_Style->setChecked(m_settings->value("DarkStyle", false).toBool());
  ui->actionBand_Buttons->setChecked(m_settings->value("BandButtons", true).toBool());
  ui->actionVHF_UHF_Buttons->setChecked(m_settings->value("VHFUHFButtons", false).toBool());
  ui->tx1->setEnabled(m_settings->value("tx1State", true).toBool());
  ui->actionHighlightB4->setChecked(m_settings->value("HighlightB4", false).toBool());
  ui->actionHighlightToday->setChecked(m_settings->value("HighlightToday", false).toBool());
  ui->actionHighlightIgnored->setChecked(m_settings->value("HighlightIgnored", false).toBool());
  ui->actionHideB4->setChecked(m_settings->value("HideB4", false).toBool());
  ui->actionHideToday->setChecked(m_settings->value("HideToday", false).toBool());
  ui->actionHideIgnored->setChecked(m_settings->value("HideIgnored", false).toBool());
  ui->actionIgnoreB4->setChecked(m_settings->value("IgnoreB4", false).toBool());
  ui->actionIgnoreToday->setChecked(m_settings->value("IgnoreToday", false).toBool());
  ui->actionIgnoreIgnored->setChecked(m_settings->value("IgnoreIgnored", false).toBool());
  ui->actionHideTerritory1->setChecked(m_settings->value("HideTerritory1", false).toBool());
  ui->actionHideTerritory2->setChecked(m_settings->value("HideTerritory2", false).toBool());
  ui->actionHideTerritory3->setChecked(m_settings->value("HideTerritory3", false).toBool());
  ui->actionHideTerritory4->setChecked(m_settings->value("HideTerritory4", false).toBool());
  ui->actionHighlightTerritory1->setChecked(m_settings->value("HighlightTerritory1", false).toBool());
  ui->actionHighlightTerritory2->setChecked(m_settings->value("HighlightTerritory2", false).toBool());
  ui->actionHighlightTerritory3->setChecked(m_settings->value("HighlightTerritory3", false).toBool());
  ui->actionHighlightTerritory4->setChecked(m_settings->value("HighlightTerritory4", false).toBool());
  ui->actionHideEU->setChecked(m_settings->value("HideEU", false).toBool());
  ui->actionHideNA->setChecked(m_settings->value("HideNA", false).toBool());
  ui->actionHideSA->setChecked(m_settings->value("HideSA", false).toBool());
  ui->actionHideAS->setChecked(m_settings->value("HideAS", false).toBool());
  ui->actionHideAF->setChecked(m_settings->value("HideAF", false).toBool());
  ui->actionHideOC->setChecked(m_settings->value("HideOC", false).toBool());
  ui->actionHideAN->setChecked(m_settings->value("HideAN", false).toBool());
  ui->actionHighlight_Whitelist_entries->setChecked(m_settings->value("HighlightWhitelistEntries", false).toBool());
//  m_mode=m_settings->value("Mode","FT8").toString();
  ui->actionNone->setChecked(m_settings->value("SaveNone",true).toBool());
  ui->actionSave_decoded->setChecked(m_settings->value("SaveDecoded",false).toBool());
  ui->actionSave_all->setChecked(m_settings->value("SaveAll",false).toBool());
  ui->actionRemove_after_30days->setChecked(m_settings->value("RemoveAudioFiles",false).toBool());
  ui->RxFreqSpinBox->setValue(0); // ensure a change is signaled
  ui->RxFreqSpinBox->setValue(m_settings->value("RxFreq",1500).toInt());
  ui->sbFST4W_RxFreq->setValue(0);
  ui->sbFST4W_RxFreq->setValue(m_settings->value("FST4W_RxFreq",1500).toInt());
  ui->sbF_Low->setValue(m_settings->value("FST4_FLow",600).toInt());
  ui->sbF_High->setValue(m_settings->value("FST4_FHigh",1400).toInt());
  ui->sbFST4W_FTol->setValue(m_settings->value("FST4W_FTol",100).toInt());
  ui->sbFtol_2->setValue(m_settings->value("JTTY_FTol",20).toInt());
  ui->sbToneSpacing->setValue(m_settings->value("EchoToneSpacing",10).toInt());
  ui->rbFixedTone->setChecked(m_settings->value("EchoFixedTone",true).toBool());
  ui->rbEchoMessage->setChecked(m_settings->value("EchoMessageRB",false).toBool());
  ui->rbEchoCW->setChecked(m_settings->value("EchoCW",false).toBool());
  ui->leEchoMessage->setText (CallsignValidator::normalizeStoredInput (
      m_settings->value ("EchoMessage", QString {}).toString (),
      ui->leEchoMessage->maxLength (), false));
  m_minSync=m_settings->value("MinSync",0).toInt();
  ui->syncSpinBox->setValue(m_minSync);
  ui->cbAutoSeq->setChecked (m_settings->value ("AutoSeq", false).toBool());
  ui->cbRxAll->setChecked (m_settings->value ("RxAll", false).toBool());
// m_bShMsgs=m_settings->value("ShMsgs",false).toBool();
  m_bSWL=m_settings->value("SWL",false).toBool();
  m_bFast9=m_settings->value("Fast9",false).toBool();
  m_bFastMode=m_settings->value("FastMode",false).toBool();
  ui->sbMaxDrift->setValue (m_settings->value ("MaxDrift",0).toInt());
  ui->sbTR_FST4W->setValue (m_settings->value ("TRPeriod_FST4W", 15).toInt());
  m_lastMonitoredFrequency = m_settings->value ("DialFreq",
    QVariant::fromValue<Frequency> (default_frequency)).value<Frequency> ();
  m_skedFreq=m_settings->value("SkedFreq",1296.065).toDouble();
  QTimer::singleShot (1000, [=] {if (m_astroWidget) m_astroWidget->setSkedFreq(m_skedFreq);});
  if(m_mode=="MSK144") m_msk144basefreq = m_lastMonitoredFrequency;  // MSK144 QSY
  ui->WSPRfreqSpinBox->setValue(0); // ensure a change is signaled
  ui->WSPRfreqSpinBox->setValue(m_settings->value("WSPRfreq",1500).toInt());
  ui->TxFreqSpinBox->setValue(0); // ensure a change is signaled
  if(m_specOp!=SpecOp::FOX) ui->TxFreqSpinBox->setValue(m_settings->value("TxFreq",1500).toInt());
  m_TxFreqFox=m_settings->value("TxFreqFox",300).toInt();
  if(m_specOp==SpecOp::FOX && !m_config.superFox()) ui->TxFreqSpinBox->setValue(m_TxFreqFox);
  m_ndepth=m_settings->value("NDepth",3).toInt();

  //ft8md
  ui->actionUse_multithreaded_FT8_decoder->setChecked(m_settings->value("MultithreadedFT8decoder", false).toBool());
  m_multithreadFT8 = ui->actionUse_multithreaded_FT8_decoder->isChecked();
  dec_data.params.lmultift8 = m_multithreadFT8;

  m_nFT8Cycles=m_settings->value("NFT8Cycles",3).toInt(); if(!(m_nFT8Cycles>=1 && m_nFT8Cycles<=3)) m_nFT8Cycles=3;
  if(m_nFT8Cycles==1) ui->actionDecFT8cycles1->setChecked(true);
  else if(m_nFT8Cycles==2) ui->actionDecFT8cycles2->setChecked(true);
  else if(m_nFT8Cycles==3) ui->actionDecFT8cycles3->setChecked(true);

  m_nFT8RXfSens=m_settings->value("NFT8QSORXfreqSensitivity",3).toInt(); if(!(m_nFT8RXfSens>=1 && m_nFT8RXfSens<=3)) m_nFT8RXfSens=3;
  if(m_nFT8RXfSens==1) ui->actionRXfLow->setChecked(true);
  else if(m_nFT8RXfSens==2) ui->actionRXfMedium->setChecked(true);
  else if(m_nFT8RXfSens==3) ui->actionRXfHigh->setChecked(true);

  m_ft8threads=m_settings->value("FT8threads",0).toInt();
  if(!(m_ft8threads>=0 && m_ft8threads<25)) m_ft8threads=0;
  if(m_ft8threads==0) ui->actionMTAuto->setChecked(true);
  else if(m_ft8threads==1) ui->actionMT1->setChecked(true);
  else if(m_ft8threads==2) ui->actionMT2->setChecked(true);
  else if(m_ft8threads==3) ui->actionMT3->setChecked(true);
  else if(m_ft8threads==4) ui->actionMT4->setChecked(true);
  else if(m_ft8threads==5) ui->actionMT5->setChecked(true);
  else if(m_ft8threads==6) ui->actionMT6->setChecked(true);
  else if(m_ft8threads==7) ui->actionMT7->setChecked(true);
  else if(m_ft8threads==8) ui->actionMT8->setChecked(true);
  else if(m_ft8threads==9) ui->actionMT9->setChecked(true);
  else if(m_ft8threads==10) ui->actionMT10->setChecked(true);
  else if(m_ft8threads==11) ui->actionMT11->setChecked(true);
  else if(m_ft8threads==12) ui->actionMT12->setChecked(true);
//  qDebug() << "m_ft8threads is " << m_ft8threads;
  dec_data.params.nmt = m_ft8threads;

  ui->actionHide_FT8_dupe_messages->setChecked(m_settings->value("HideFT8Dupes",true).toBool());

  m_ft8Sensitivity=m_settings->value("FT8Sensitivity",3).toInt();
  if(!(m_ft8Sensitivity>0 && m_ft8Sensitivity<=3)) m_ft8Sensitivity=3;
  if(m_ft8Sensitivity==1) ui->actionFT8SensMin->setChecked(true);
  else if(m_ft8Sensitivity==2) ui->actionlowFT8thresholds->setChecked(true);
  else if(m_ft8Sensitivity==3) ui->actionFT8subpass->setChecked(true);

  m_ft8DecoderStart=m_settings->value("FT8DecoderStart",3).toInt();
  if(!(m_ft8DecoderStart>=0 && m_ft8DecoderStart<=4)) m_ft8DecoderStart=3;
  if(m_ft8DecoderStart==0) ui->actionStartTwoStage->setChecked(true);
  else if(m_ft8DecoderStart==1) ui->actionStartThreeStage->setChecked(true);
  else if(m_ft8DecoderStart==2) ui->actionStartEarly->setChecked(true);
  else if(m_ft8DecoderStart==3) ui->actionStartNormal->setChecked(true);
  else if(m_ft8DecoderStart==4) ui->actionStartLate->setChecked(true);

  m_FT8WideDxCallSearch=m_settings->value("FT8WideDXCallSearch",true).toBool();
  ui->actionFT8WidebandDXCallSearch->setChecked(m_FT8WideDxCallSearch);
  //ft8md

  ui->sbTxPercent->setValue (m_settings->value ("PctTx", 20).toInt ());
  on_sbTxPercent_valueChanged (ui->sbTxPercent->value ());
  ui->RoundRobin->setCurrentText(m_settings->value("RoundRobin",tr("Random")).toString());
  m_dBm=m_settings->value("dBm",37).toInt();
  m_send_RR73=m_settings->value("RR73",false).toBool();
  m_score=m_settings->value("Score",0).toInt();
  if (m_send_RR73) genStdMsgs (m_rpt);
  ui->WSPR_prefer_type_1_check_box->setChecked (m_settings->value ("WSPRPreferType1", true).toBool ());
  m_uploadWSPRSpots=m_settings->value("UploadSpots",false).toBool();
  ui->cbNoOwnCall->setChecked(m_settings->value("NoOwnCall",false).toBool());
  ui->band_hopping_group_box->setChecked (m_settings->value ("BandHopping", false).toBool());
  // setup initial value of tx attenuator
  m_block_pwr_tooltip = true;
  ui->outAttenuation->setValue (m_settings->value ("OutAttenuation", 0).toInt ());
  m_block_pwr_tooltip = false;
  ui->sbCQTxFreq->setValue (m_settings->value ("CQTxFreq", 260).toInt());
  m_noSuffix=m_settings->value("NoSuffix",false).toBool();
  int n=m_settings->value("GUItab",standard_messages_tab_index).toInt();
  if (SpecOp::FOX==m_specOp) {
    ui->tabWidget->setCurrentIndex(n);
  } else {
    // The Fox queue tab must be shown once so Qt initializes its height.
    ui->pbFreeText->setVisible(false);
    ui->cbSendMsg->setVisible(false);
    ui->tabWidget->setCurrentIndex(fox_queue_tab_index);
    ui->tabWidget->setCurrentIndex(n);
  }
  outBufSize=m_settings->value("OutBufSize",4096).toInt();
  ui->cbHoldTxFreq->setChecked (m_settings->value ("HoldTxFreq", false).toBool ());
  HoldTxFreqStatus = m_settings->value ("HoldTxFreq", false).toBool ();
  ui->cbCQonly->setChecked (m_settings->value ("CQonly", false).toBool ());
  ui->cbBypass->setChecked (m_settings->value ("BypassFilters", false).toBool ());
  m_pwrBandTxMemory=m_settings->value("pwrBandTxMemory").toHash();
  m_pwrBandTuneMemory=m_settings->value("pwrBandTuneMemory").toHash();
  ui->actionEnable_AP_FT8->setChecked (m_settings->value ("FT8AP", false).toBool());
  ui->actionEnable_AP_JT65->setChecked (m_settings->value ("JT65AP", false).toBool());
  ui->actionAuto_Clear_Avg->setChecked (m_settings->value ("AutoClearAvg", false).toBool());
  ui->actionDisable_clicks_on_waterfall->setChecked (m_settings->value ("DisableClicksOnWaterfall", false).toBool());
  ui->decodes_splitter->restoreState(m_settings->value("SplitterState").toByteArray());
  ui->sbNB->setValue(m_settings->value("Blanker",0).toInt());
  ui->sbEchoAvg->setValue(m_settings->value("EchoAvg",10).toInt());
  {
    auto const& coeffs = m_settings->value ("PhaseEqualizationCoefficients"
                                            , QList<QVariant> {0., 0., 0., 0., 0.}).toList ();
    m_phaseEqCoefficients.clear ();
    for (auto const& coeff : coeffs)
      {
        m_phaseEqCoefficients.append (coeff.value<double> ());
      }
  }
  m_settings->endGroup();

  // use these initialisation settings to tune the audio o/p buffer
  // size and audio thread priority
  m_settings->beginGroup ("Tune");
  m_audioThreadPriority = static_cast<QThread::Priority> (m_settings->value ("Audio/ThreadPriority", QThread::TimeCriticalPriority).toInt () % 8);
  m_settings->endGroup ();

  m_specOp=m_config.special_op_id();
  checkMSK144ContestType();
  if (displayMsgAvg) on_actionMessage_averaging_triggered();
  if (displayFoxLog) on_fox_log_action_triggered ();
  if (displayContestLog) on_contest_log_action_triggered ();
  if (displayActiveStations) on_actionActiveStations_triggered();
  if (displayQSYMessageCreator) on_actionQSYMessage_Creator_triggered();
  if (displayQSYMonitor) on_actionQSY_Monitor_triggered();

#ifdef WIN32
  if (m_config.alert_Enabled()) {  // testing and initializing the default audio device for playing audible alerts
      QAudioOutput info(QAudioDeviceInfo::defaultOutputDevice());
      QString audioPath = app_sounds_directory (m_config.voicesPath());
      QAudioFormat format;
      format.setCodec("audio/pcm");
      format.setSampleRate (48000);
      format.setChannelCount (1);
      format.setSampleSize (16);
      format.setSampleType(QAudioFormat::SignedInt);
      QAudioOutput* audio;
      audio = new QAudioOutput(format, this);
      QFile *effect = new QFile(this);
      effect->setFileName(QString("%1/%2").arg(audioPath, "Testing123.wav"));
      effect->open(QIODevice::ReadOnly);
      audio->start(effect);
  }
#endif
}
