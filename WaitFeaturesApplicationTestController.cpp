// SPDX-License-Identifier: GPL-3.0-or-later
#include "WaitFeaturesApplicationTestController.hpp"

#include <cstdlib>
#include <iostream>

#include <QAction>
#include <QApplication>
#include <QAudioDeviceInfo>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QEventLoop>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>

#include "BestSPApplicationTestController.hpp"
#include "DecoderBackend.hpp"
#include "OperatingFrequency.hpp"
#include "commons.h"
#include "models/Bands.hpp"
#include "ui_mainwindow.h"
#include "widgets/mainwindow.h"

extern dec_data_t& dec_data;
extern int g_iptt;
extern bool m_txing;
extern bool pounce;
extern bool wait_and_call;
extern bool no_wait_and_call;
extern QString ignoreList;
extern QString earlyDecodes;

namespace
{
  void drainEvents ()
  {
    QEventLoop loop;
    QTimer::singleShot (20, &loop, &QEventLoop::quit);
    loop.exec ();
  }
}

QString WaitFeaturesApplicationTestController::prepareSettings (QSettings * settings)
{
  auto const error = BestSPApplicationTestController::prepareSettings (settings);
  if (!error.isEmpty ()) return error;
  settings->setValue ("Configuration/WaitFeaturesEnabled", true);
  settings->setValue ("Configuration/AutoLog", false);
  settings->setValue ("Configuration/PromptToLog", false);
  settings->setValue ("Tune/Mode", "FT8");
  settings->sync ();
  return {};
}

WaitFeaturesApplicationTestController::WaitFeaturesApplicationTestController (
  MainWindow * window, DecoderBackend * backend, QObject * parent)
  : QObject {parent}, window_ {window}, backend_ {backend}
{
  retry_.setSingleShot (true);
  retry_.setInterval (20);
  connect (&retry_, &QTimer::timeout, this, [this] {prepare ();});
  timeout_.setSingleShot (true);
  timeout_.setInterval (10000);
  connect (&timeout_, &QTimer::timeout, this, [this] {
    finish (false, "Timed out preparing the isolated Wait-feature fixtures.");
  });
}

void WaitFeaturesApplicationTestController::begin ()
{
  window_->m_guiTimer.stop ();
  timeout_.start ();
  prepare ();
}

void WaitFeaturesApplicationTestController::prepare ()
{
  if (finished_) return;
  if (!backend_->ready () || window_->m_logBook.countries ()->lookup ("W1AW")
      .identity (AD1CCty::EntityView::DXCC).name.isEmpty ())
    {
      retry_.start ();
      return;
    }
  if (!require (window_->m_config.is_dummy_rig (), "Test requires the dummy radio.")
      || !require (window_->m_config.transceiver_online (), "Cannot open the dummy radio.")) return;
  if (!window_->m_rigState.online ())
    {
      retry_.start ();
      return;
    }
  if (window_->m_rigState.frequency () != 14074000)
    {
      if (!require (window_->m_config.transceiver_frequency (
                      14074000, RigFrequencyChangePolicy::ChangeKind::NominalQsy),
                    "Cannot initialize the dummy radio frequency.")) return;
      retry_.start ();
      return;
    }
  timeout_.stop ();
  if (run ()) finish (true, "All Wait-feature application scenarios passed.");
}

bool WaitFeaturesApplicationTestController::startReception (
  QString const& mode, QString const& scenario)
{
  scenario_ = scenario;
  if (backend_->activeRequest ()) completeScriptedTestDecode (backend_);
  window_->setBestSPArmed (false);
  window_->cancelWaitRetry (true);
  window_->cancelWaitRetry (false);
  window_->auto_tx_mode (false);
  window_->m_guiTimer.stop ();
  if (mode == "Q65") window_->on_actionQ65_triggered ();
  else window_->on_actionFT8_triggered ();
  window_->m_guiTimer.stop ();
  window_->m_operatingFrequency = OperatingFrequency {14074000};
  window_->m_operatingFrequency.requestNominal (14074000, {}, false);
  window_->m_freqNominalPeriod = 14074000;
  window_->m_currentBand = window_->m_config.bands ()->find (14074000);
  window_->m_currentBandPeriod = window_->m_currentBand;
  window_->m_dateTimeSeqStart = QDateTime {QDate {2026, 11, 8}, QTime {12, 0}, Qt::UTC};
  window_->m_diskData = false;
  window_->m_tune = false;
  window_->m_transmitting = false;
  window_->m_tx_when_ready = false;
  window_->m_sentFirst73 = false;
  window_->m_bCallingCQ = false;
  window_->m_bAutoReply = false;
  window_->ptt1Timer.stop ();
  window_->ptt0Timer.stop ();
  window_->logQSOTimer.stop ();
  window_->m_autoRespondPeriodState = {};
  window_->cancelAutomaticCQSelection ();
  window_->m_autoRespondSelectionLatch.clear ();
  pounce = false;
  wait_and_call = false;
  no_wait_and_call = false;
  g_iptt = 0;
  m_txing = false;
  earlyDecodes.clear ();
  ignoreList.clear ();
  window_->ui->DX_Call_Button->setChecked (false);
  window_->ui->tuneButton->setChecked (false);
  window_->ui->actionIgnoreIgnored->setChecked (false);
  window_->ui->actionHideIgnored->setChecked (false);
  window_->ui->cbBypass->setChecked (false);
  window_->ui->cbAutoSeq->setChecked (true);
  window_->ui->respondComboBox->setCurrentIndex (
    window_->ui->respondComboBox->findData (static_cast<int> (AutoRespondPolicy::None)));
  window_->ui->dxCallEntry->setText ("W1AW");
  window_->ui->dxGridEntry->setText ("FN31");
  window_->ui->RxFreqSpinBox->setValue (1500);
  window_->ui->TxFreqSpinBox->setValue (1500);
  window_->genStdMsgs ("-10");
  window_->setTxMsg (2);
  window_->m_QSOProgress = QsoProgress::Report;
  if (mode == "Q65")
    {
      window_->ui->sbTR->setValue (30);
      window_->m_TRperiod = 30;
    }
  window_->check_button_color ();
  return require (window_->m_config.Wait_features_enabled (), "Wait features are not enabled.")
    && require (enableTxIs (false), "The waiting Enable Tx control is not yellow.")
    && publishReception ();
}

bool WaitFeaturesApplicationTestController::publishReception ()
{
  dec_data.params = {};
  dec_data.params.nmode = window_->m_mode == "Q65" ? 66 : 8;
  dec_data.params.ntrperiod = int (window_->m_TRperiod);
  dec_data.params.newdat = true;
  return require (window_->publishDecodeRequest (true) == MainWindow::DecodePublishResult::Published,
                  "The scripted reception was not published.")
    && require (window_->activeDecodeOperatingContextMatchesCurrent (),
                "The scripted reception has an obsolete operating context.");
}

bool WaitFeaturesApplicationTestController::nextReception ()
{
  completeScriptedTestDecode (backend_);
  window_->m_dateTimeSeqStart = window_->m_dateTimeSeqStart.addSecs (int (2 * window_->m_TRperiod));
  earlyDecodes.clear ();
  return publishReception ();
}

void WaitFeaturesApplicationTestController::record (
  QString const& payload, int frequency, QString const& suffix)
{
  auto const marker = window_->m_mode == "Q65" ? ":" : "~";
  auto const line = QString {"%1 -10  0.3 %2 %3  %4%5\n"}
    .arg (window_->m_dateTimeSeqStart.toString ("hhmmss"))
    .arg (frequency, 4).arg (marker).arg (payload.leftJustified (37, ' ')).arg (suffix);
  deliverScriptedTestDecode (backend_, line);
}

bool WaitFeaturesApplicationTestController::enableTxIs (bool enabled) const
{
  return window_->m_auto == enabled && window_->ui->autoButton->isChecked () == enabled
    && window_->ui->autoButton->styleSheet ().contains (enabled ? "#ff0000" : "#ffff00");
}

bool WaitFeaturesApplicationTestController::run ()
{
  if (!startReception ("FT8", "manual Enable Tx off resumes on a directed reply")) return false;
  window_->ui->autoButton->click ();
  if (!require (enableTxIs (true), "Enable Tx did not turn red when enabled.")) return false;
  window_->ui->autoButton->click ();
  if (!require (enableTxIs (false), "Disabling Enable Tx did not leave the yellow waiting state.")) return false;
  record ("K1ABC W1AW R-10");
  if (!require (enableTxIs (true) && window_->m_ntx == 4 && window_->stopWRTimer.isActive (),
                "The directed reply did not restore Auto-Tx and select its response.")) return false;

  if (!startReception ("FT8", "frequency ending in 73 does not suppress a report")) return false;
  record ("K1ABC W1AW -10", 1473);
  if (!require (enableTxIs (true) && window_->m_ntx == 3,
                "A report at 1473 Hz failed to enable its reply.")) return false;

  if (!startReception ("FT8", "RR73 resumes with a final 73")) return false;
  window_->m_QSOProgress = QsoProgress::Rogers;
  record ("K1ABC W1AW RR73");
  if (!require (enableTxIs (true) && window_->m_ntx == 5,
                "RR73 did not re-enable the final 73 response.")) return false;

  if (!startReception ("FT8", "a final 73 leaves transmission disabled")) return false;
  window_->m_QSOProgress = QsoProgress::Signoff;
  record ("K1ABC W1AW 73");
  if (!require (enableTxIs (false) && !window_->stopWRTimer.isActive ()
                && !window_->m_waitReplyAttempt,
                "A final 73 restarted transmission or armed a retry deadline.")) return false;

  if (!startReception ("FT8", "Ignore List blocks Wait and Reply")) return false;
  window_->ui->actionIgnoreIgnored->setChecked (true);
  ignoreList = "W1AW,";
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (false) && !window_->stopWRTimer.isActive ()
                && !window_->m_waitReplyAttempt,
                "An ignored station enabled transmission or started a retry deadline.")) return false;
  window_->ui->actionIgnoreIgnored->setChecked (false);
  if (!nextReception ()) return false;
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (true), "Removing Ignore List filtering did not restore the reply.")) return false;

  if (!startReception ("FT8", "Hide Ignored remains a display-only filter")) return false;
  window_->ui->actionHideIgnored->setChecked (true);
  ignoreList = "W1AW,";
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (true) && window_->stopWRTimer.isActive (),
                "A display-only Hide Ignored filter blocked the directed reply.")) return false;

  if (!startReception ("FT8", "WAV input cannot arm Wait and Reply")) return false;
  completeScriptedTestDecode (backend_);
  window_->m_diskData = true;
  if (!publishReception ()) return false;
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (false) && !window_->stopWRTimer.isActive ()
                && !window_->m_waitReplyAttempt,
                "A recorded decode armed transmission or its deadline.")) return false;
  window_->m_diskData = false;
  if (!nextReception ()) return false;
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (true) && window_->stopWRTimer.isActive (),
                "Returning to live reception did not restore Wait and Reply.")) return false;

  auto const setQuickCall = [this] (bool enabled) {
    bool visited {false};
    bool timedOut {false};
    QTimer guard;
    guard.setSingleShot (true);
    connect (&guard, &QTimer::timeout, window_, [&] {
      timedOut = true;
      if (auto * dialog = qobject_cast<QDialog *> (QApplication::activeModalWidget ())) dialog->reject ();
    });
    QTimer::singleShot (0, window_, [&] {
      auto * dialog = qobject_cast<QDialog *> (QApplication::activeModalWidget ());
      if (!dialog) return;
      auto * option = dialog->findChild<QCheckBox *> ("quick_call_check_box");
      if (!option) {dialog->reject (); return;}
      visited = true;
      option->setChecked (enabled);
      for (auto const name : {"sound_input_combo_box", "sound_output_combo_box"})
        {
          auto * devices = dialog->findChild<QComboBox *> (name);
          if (devices && devices->currentIndex () < 0)
            devices->addItem ("Silent test device", QVariant::fromValue (QAudioDeviceInfo {}));
        }
      dialog->accept ();
    });
    guard.start (3000);
    window_->on_actionSettings_triggered ();
    guard.stop ();
    window_->m_guiTimer.stop ();
    return require (visited && !timedOut && window_->m_config.quick_call () == enabled,
                    "The isolated Quick Call setting was not applied.");
  };
  for (bool const quickCall : {false, true})
    {
      scenario_ = "Quick Call does not duplicate a Wait activation";
      if (!setQuickCall (quickCall) || !startReception ("FT8", scenario_)) return false;
      auto const generation = window_->m_waitRetryGeneration;
      record ("K1ABC W1AW -10");
      if (!require (enableTxIs (true) && window_->stopWRTimer.isActive ()
                    && window_->m_waitReplyAttempt && window_->m_waitRetryGeneration == generation + 1,
                    "A directed reply failed to create exactly one retry deadline.")) return false;
    }

  if (!startReception ("Q65", "averaged Q65 reply reaches Wait processing")) return false;
  record ("K1ABC W1AW R-10", 1500, " q23");
  if (!require (enableTxIs (true) && window_->m_ntx == 4 && window_->stopWRTimer.isActive (),
                "An averaged Q65 report did not enable the response.")) return false;

  if (!startReception ("FT8", "Tune rejects a reply without consuming a retry")) return false;
  window_->ui->tuneButton->click ();
  if (!require (window_->m_tune, "The Tune fixture did not activate.")) return false;
  record ("K1ABC W1AW -10");
  if (!require (!window_->m_auto && !window_->stopWRTimer.isActive ()
                && !window_->m_waitReplyAttempt,
                "A rejected reply armed Auto-Tx or its deadline while tuning.")) return false;
  window_->stop_tuning ();
  if (!nextReception ()) return false;
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (true) && window_->stopWRTimer.isActive (),
                "A valid reply after Tune failed to start a fresh retry.")) return false;

  if (!startReception ("FT8", "pending rig Tune rejects automatic enabling")) return false;
  window_->rigTuneTimer.start (6000);
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (false) && window_->rigTuneTimer.isActive ()
                && !window_->stopWRTimer.isActive () && !window_->m_waitReplyAttempt,
                "Rejected Auto-Tx canceled rig Tune or started a reply deadline.")) return false;
  window_->rigTuneTimer.stop ();
  if (!nextReception ()) return false;
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (true) && window_->stopWRTimer.isActive (),
                "A valid reply after rig Tune failed to start a fresh retry.")) return false;

  if (!startReception ("FT8", "Wait and Call response cancels its deadline")) return false;
  window_->ui->DX_Call_Button->click ();
  if (!require (wait_and_call, "The Wait and Call control did not arm.")) return false;
  record ("CQ W1AW FN31");
  if (!require (enableTxIs (true) && window_->stopWCTimer.isActive () && window_->m_waitCallAttempt,
                "Wait and Call did not start its transmission and deadline.")) return false;
  record ("K1ABC W1AW -10");
  if (!require (window_->stopWCTimer.isActive () && window_->m_waitCallAttempt,
                "A same-period decode prematurely acknowledged Wait and Call.")) return false;
  window_->stopWCTimer.setInterval (1);
  if (!nextReception ()) return false;
  record ("K1ABC W1AW -10");
  if (!require (!window_->stopWCTimer.isActive () && !window_->m_waitCallAttempt,
                "A later directed answer left the Wait and Call deadline armed.")) return false;
  drainEvents ();
  if (!require (enableTxIs (true), "The canceled Wait and Call deadline stopped the resumed QSO.")) return false;

  if (!startReception ("FT8", "unanswered Wait and Call reaches its deadline")) return false;
  window_->ui->DX_Call_Button->click ();
  record ("CQ W1AW FN31");
  if (!require (enableTxIs (true) && window_->m_waitCallAttempt,
                "The unanswered Wait and Call fixture did not start.")) return false;
  window_->stopWCTimer.setInterval (1);
  drainEvents ();
  if (!require (enableTxIs (false) && !window_->stopWCTimer.isActive ()
                && !window_->m_waitCallAttempt && !window_->ui->DX_Call_Button->isChecked (),
                "The unanswered Wait and Call attempt did not stop at its deadline.")) return false;

  if (!startReception ("FT8", "DX Call off and on starts a fresh calling attempt")) return false;
  window_->ui->DX_Call_Button->click ();
  record ("CQ W1AW FN31");
  if (!require (bool (window_->m_waitCallAttempt), "The initial calling attempt did not start.")) return false;
  auto const callGeneration = window_->m_waitCallAttempt->generation;
  window_->stopWCTimer.setInterval (1);
  window_->ui->DX_Call_Button->click ();
  if (!require (!window_->stopWCTimer.isActive () && !window_->m_waitCallAttempt
                && !wait_and_call && !no_wait_and_call,
                "Turning off DX Call retained the old calling attempt.")) return false;
  window_->ui->autoButton->click ();
  window_->ui->DX_Call_Button->click ();
  if (!nextReception ()) return false;
  record ("CQ W1AW FN31");
  if (!require (enableTxIs (true) && window_->m_waitCallAttempt
                && window_->m_waitCallAttempt->generation != callGeneration,
                "Rearming DX Call did not create a new calling attempt.")) return false;
  drainEvents ();
  if (!require (enableTxIs (true) && window_->stopWCTimer.isActive (),
                "The previous calling deadline stopped the rearmed attempt.")) return false;

  if (!startReception ("FT8", "Halt Tx cancels the old retry before rearming")) return false;
  record ("K1ABC W1AW -10");
  if (!require (bool (window_->m_waitReplyAttempt), "The initial reply did not create retry state.")) return false;
  auto const generation = window_->m_waitReplyAttempt->generation;
  window_->stopWRTimer.setInterval (1);
  window_->on_stopTxButton_clicked ();
  if (!require (!window_->stopWRTimer.isActive () && !window_->m_waitReplyAttempt,
                "Halt Tx retained the prior reply deadline.")) return false;
  if (!nextReception ()) return false;
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (true) && window_->m_waitReplyAttempt
                && window_->m_waitReplyAttempt->generation != generation,
                "The reply after Halt Tx did not create a fresh attempt.")) return false;
  drainEvents ();
  return require (enableTxIs (true) && window_->stopWRTimer.isActive (),
                  "An old retry deadline affected the newly armed attempt.");
}

bool WaitFeaturesApplicationTestController::require (bool condition, QString const& message)
{
  if (!condition) finish (false, scenario_ + ": " + message);
  return condition;
}

void WaitFeaturesApplicationTestController::finish (bool success, QString const& message)
{
  if (finished_) return;
  finished_ = true;
  succeeded_ = success;
  timeout_.stop ();
  retry_.stop ();
  window_->m_guiTimer.stop ();
  window_->cancelWaitRetry (true);
  window_->cancelWaitRetry (false);
  window_->auto_tx_mode (false);
  std::cerr << "WSJT-X Wait features integration: " << message.toStdString () << std::endl;
  QCoreApplication::exit (success ? EXIT_SUCCESS : EXIT_FAILURE);
}
