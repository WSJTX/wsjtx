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
#include <QMessageBox>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QPushButton>
#include <QSettings>

#include "BestSPApplicationTestController.hpp"
#include "DecoderBackend.hpp"
#include "DecodedMessageReaction.hpp"
#include "OperatingFrequency.hpp"
#include "QsoCompletion.hpp"
#include "commons.h"
#include "models/Bands.hpp"
#include "ui_mainwindow.h"
#include "widgets/mainwindow.h"

extern dec_data_t& dec_data;
extern int g_iptt;
extern bool m_txing;
extern bool pounce;
extern bool filtered;
extern bool ignored;
extern bool wait_and_call;
extern bool no_wait_and_call;
extern QString ignoreList;
extern QString earlyDecodes;
extern bool programStart;

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
  if (programStart || !backend_->ready () || window_->m_logBook.countries ()->lookup ("W1AW")
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
  auto const state = window_->m_operatingFrequency.snapshot ();
  if (state.pendingRx || state.pendingTx || window_->m_pendingModeRetune.pending ()
      || window_->m_modeEntries.active ())
    {
      retry_.start ();
      return;
    }
  if (state.rx != 14074000 || window_->m_rigState.frequency () != 14074000)
    {
      if (!require (window_->requestBandChange (
                      14074000, MainWindow::FrequencyRequestOrigin::User),
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
  if (!require (!window_->m_tune && !window_->tuneButtonTimer.isActive (),
                "The preceding scenario left Tune shutdown unfinished.")) return false;
  if (mode == "MSK144") window_->on_actionMSK144_triggered ();
  else if (mode == "Q65") window_->on_actionQ65_triggered ();
  else window_->on_actionFT8_triggered ();
  window_->m_guiTimer.stop ();
  if (!waitUntil ([this] {
        auto const state = window_->m_operatingFrequency.snapshot ();
        return !state.pendingRx && !state.pendingTx && !window_->m_pendingModeRetune.pending ()
          && !window_->m_modeEntries.active ();
      }, "Mode entry did not settle before preparing the scripted reception.")) return false;
  window_->m_operatingFrequency = OperatingFrequency {14074000};
  window_->m_operatingFrequency.requestNominal (14074000, {}, false);
  window_->m_freqNominalPeriod = 14074000;
  window_->m_currentBandPeriod = window_->nominalRxBand ();
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
    && (mode == "MSK144" || publishReception ());
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
    return setOption ("quick_call_check_box", enabled);
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
  if (!waitUntil ([this] {return !window_->m_tune && !window_->tuneButtonTimer.isActive ();},
                  "Tune shutdown did not complete before testing reply recovery.")) return false;
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
  auto const acknowledgedGeneration = window_->m_waitCallAttempt->generation;
  if (!nextReception ()) return false;
  record ("K1ABC W1AW -10");
  if (!require (!window_->stopWCTimer.isActive () && !window_->m_waitCallAttempt,
                "A later directed answer left the Wait and Call deadline armed.")) return false;
  window_->dispatchWaitRetryTimeout (false, acknowledgedGeneration);
  if (!require (enableTxIs (true), "The canceled Wait and Call deadline stopped the resumed QSO.")) return false;

  if (!startReception ("FT8", "unanswered Wait and Call reaches its deadline")) return false;
  window_->ui->DX_Call_Button->click ();
  record ("CQ W1AW FN31");
  if (!require (enableTxIs (true) && window_->m_waitCallAttempt,
                "The unanswered Wait and Call fixture did not start.")) return false;
  window_->stopWCTimer.setInterval (1);
  if (!waitUntil ([this] {
        return enableTxIs (false) && !window_->stopWCTimer.isActive ()
          && !window_->m_waitCallAttempt && !window_->ui->DX_Call_Button->isChecked ();
      }, "The unanswered Wait and Call attempt did not stop at its deadline.")) return false;

  if (!startReception ("FT8", "DX Call off and on starts a fresh calling attempt")) return false;
  window_->ui->DX_Call_Button->click ();
  record ("CQ W1AW FN31");
  if (!require (bool (window_->m_waitCallAttempt), "The initial calling attempt did not start.")) return false;
  auto const callGeneration = window_->m_waitCallAttempt->generation;
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
  auto const replacementCall = window_->m_waitCallAttempt->generation;
  window_->dispatchWaitRetryTimeout (false, callGeneration);
  if (!require (enableTxIs (true) && window_->stopWCTimer.isActive (),
                "The previous calling deadline stopped the rearmed attempt.")) return false;
  window_->dispatchWaitRetryTimeout (false, replacementCall);
  if (!require (enableTxIs (false) && !window_->stopWCTimer.isActive ()
                && !window_->m_waitCallAttempt,
                "The replacement calling deadline did not complete its own attempt.")) return false;

  if (!startReception ("FT8", "Halt Tx cancels the old retry before rearming")) return false;
  record ("K1ABC W1AW -10");
  if (!require (bool (window_->m_waitReplyAttempt), "The initial reply did not create retry state.")) return false;
  auto const generation = window_->m_waitReplyAttempt->generation;
  window_->on_stopTxButton_clicked ();
  if (!require (!window_->stopWRTimer.isActive () && !window_->m_waitReplyAttempt,
                "Halt Tx retained the prior reply deadline.")) return false;
  if (!nextReception ()) return false;
  record ("K1ABC W1AW -10");
  if (!require (enableTxIs (true) && window_->m_waitReplyAttempt
                && window_->m_waitReplyAttempt->generation != generation,
                "The reply after Halt Tx did not create a fresh attempt.")) return false;
  window_->dispatchWaitRetryTimeout (true, generation);
  if (!require (enableTxIs (true) && window_->stopWRTimer.isActive (),
                "An old retry deadline affected the newly armed attempt.")) return false;
  window_->stopWRTimer.setInterval (1);
  if (!waitUntil ([this] {
        return enableTxIs (false) && !window_->stopWRTimer.isActive () && !window_->m_waitReplyAttempt;
      }, "The replacement reply deadline did not complete through its timer callback.")) return false;
  return correctionScenarios ();
}

bool WaitFeaturesApplicationTestController::waitUntil (
  std::function<bool()> const& ready, QString const& failure)
{
  if (ready ()) return true;
  QEventLoop loop;
  QTimer poll;
  QTimer guard;
  poll.setInterval (5);
  guard.setSingleShot (true);
  connect (&poll, &QTimer::timeout, &loop, [&] {if (ready ()) loop.quit ();});
  connect (&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start ();
  guard.start (5000);
  loop.exec ();
  return require (ready (), failure);
}

bool WaitFeaturesApplicationTestController::setOption (
  QString const& name, bool enabled, std::function<bool()> const& afterAccept)
{
    bool visited {false};
    bool optionApplied {false};
    bool afterAcceptApplied {true};
    bool timedOut {false};
    QTimer guard;
    guard.setSingleShot (true);
    connect (&guard, &QTimer::timeout, window_, [&] {
      timedOut = true;
      if (auto * dialog = qobject_cast<QDialog *> (QApplication::activeModalWidget ())) dialog->reject ();
    });
    QTimer::singleShot (0, &guard, [&] {
      auto * dialog = qobject_cast<QDialog *> (QApplication::activeModalWidget ());
      if (!dialog) return;
      auto * option = dialog->findChild<QCheckBox *> (name);
      if (!option) {dialog->reject (); return;}
      visited = true;
      option->setChecked (enabled);
      optionApplied = option->isChecked () == enabled;
      for (auto const name : {"sound_input_combo_box", "sound_output_combo_box"})
        {
          auto * devices = dialog->findChild<QComboBox *> (name);
          if (devices && devices->currentIndex () < 0)
            devices->addItem ("Silent test device", QVariant::fromValue (QAudioDeviceInfo {}));
        }
      dialog->accept ();
      if (afterAccept) afterAcceptApplied = afterAccept ();
    });
    guard.start (3000);
    window_->on_actionSettings_triggered ();
    guard.stop ();
    window_->m_guiTimer.stop ();
    if (name == "quick_call_check_box") optionApplied = window_->m_config.quick_call () == enabled;
    if (name == "enable_Wait_features_check_box") optionApplied = window_->m_config.Wait_features_enabled () == enabled;
    if (name == "cbAutoLog") optionApplied = window_->m_config.autoLog () == enabled;
    return require (visited && !timedOut && optionApplied && afterAcceptApplied,
                    "The isolated setting was not applied: " + name);
}

bool WaitFeaturesApplicationTestController::correctionScenarios ()
{
  auto selectPolicy = [this] (AutoRespondPolicy policy) {
    window_->ui->respondComboBox->setVisible (true);
    window_->ui->respondComboBox->setCurrentIndex (
      window_->ui->respondComboBox->findData (static_cast<int> (policy)));
  };
  auto candidate = [this] (QString const& payload) {
    DecodedText decoded {"120000 -10  0.3 1900 ~  " + payload};
    window_->updateRespondTarget (decoded, decoded.string (), pounce,
                                 window_->m_dateTimeSeqStart, false);
  };
  for (auto const policy : {AutoRespondPolicy::First, AutoRespondPolicy::MaxDistance,
                           AutoRespondPolicy::MaxSignal, AutoRespondPolicy::MinSignal}) {
    if (!startReception ("FT8", "rejected Pounce candidate leaves selection available")) return false;
    selectPolicy (policy);
    pounce = true;
    filtered = ignored = false;
    window_->m_autoRespondScores.reset ();
    auto const call = window_->ui->dxCallEntry->text ();
    auto const tx1 = window_->ui->tx1->text ();
    auto const tx3 = window_->ui->tx3->text ();
    window_->rigTuneTimer.start (30000);
    candidate ("CQ JA1ABC PM95");
    if (!require (!window_->m_auto && window_->ui->dxCallEntry->text () == call
                  && window_->ui->tx1->text () == tx1 && window_->ui->tx3->text () == tx3
                  && !window_->m_autoRespondSelectionLatch.isSelected ()
                  && !window_->m_waitCallAttempt && !window_->stopWCTimer.isActive (),
                  "Rejected Pounce activation changed the contact or consumed its selection.")) return false;
    window_->rigTuneTimer.stop ();
    candidate ("CQ JA1ABC PM95");
    if (!require (window_->m_auto && window_->m_hisCall == "JA1ABC"
                  && window_->m_waitCallAttempt && window_->stopWCTimer.isActive (),
                  "The same candidate remained blocked after rig Tune ended.")) return false;
  }

  if (!startReception ("FT8", "a rejected current-period First candidate remains eligible")) return false;
  selectPolicy (AutoRespondPolicy::First);
  window_->m_QSOProgress = QsoProgress::Calling;
  window_->m_bCallingCQ = true;
  window_->setTxMsg (6);
  window_->auto_tx_mode (true);
  window_->m_autoRespondPeriodState.observePeriod (window_->m_dateTimeSeqStart, true, true,
                                                  AutoRespondPolicy::First);
  window_->rigTuneTimer.start (30000);
  candidate ("K1ABC JA1ABC PM95");
  if (!require (window_->m_autoRespondPeriodState.canClaimFirst (window_->m_dateTimeSeqStart)
                && window_->m_hisCall == "W1AW",
                "Rejected activation consumed the receive period's First claim.")) return false;
  window_->rigTuneTimer.stop ();
  candidate ("K1ABC JA1ABC PM95");
  if (!require (window_->m_hisCall == "JA1ABC",
                "A later valid First candidate did not select the caller.")) return false;

  for (bool const pounceAttempt : {false, true}) {
    for (bool const deferredFirst : {false, true}) {
      auto const name = QString {"%1 keeps its retry with %2 refresh first"}
        .arg (pounceAttempt ? "Pounce" : "Wait and Call")
        .arg (deferredFirst ? "deferred" : "ordinary");
      if (!startReception ("FT8", name)) return false;
      if (pounceAttempt) {
        selectPolicy (AutoRespondPolicy::First);
        pounce = true;
        candidate ("CQ JA1ABC PM95");
      } else {
        window_->ui->DX_Call_Button->click ();
        record ("CQ W1AW FN31");
      }
      if (!require (window_->m_auto && window_->m_waitCallAttempt,
                    "The unanswered attempt did not start.")) return false;
      auto const attempt = *window_->m_waitCallAttempt;
      auto const owner = window_->m_waitCallTimeoutOwner;
      auto const interval = window_->stopWCTimer.interval ();
      auto const remaining = window_->stopWCTimer.remainingTime ();
      auto const refresh = [this] {
        bool completed {false};
        QObject scope;
        QTimer::singleShot (0, &scope, [&] {
          window_->refreshWaitFeatureControls ();
          completed = true;
        });
        return waitUntil ([&] {return completed;}, "The deferred Wait refresh did not run.");
      };
      if (!setOption ("enable_Wait_features_check_box", false, [&] {
        if (!require (!window_->m_config.Wait_features_enabled (),
                      "The refresh ran before the new Wait setting was accepted.")) return false;
        return !deferredFirst || refresh ();
      })) return false;
      if (!deferredFirst && !refresh ()) return false;
      if (!require (window_->m_auto, "Disabling Wait stopped the active transmission.")
          || !require (window_->stopWCTimer.isActive (), "Disabling Wait stopped the retry timer.")
          || !require (bool (window_->m_waitCallAttempt), "Disabling Wait removed the retry attempt.")) return false;
      auto const& retained = *window_->m_waitCallAttempt;
      if (!require (retained.generation == attempt.generation, "The refresh replaced the retry generation.")
          || !require (window_->m_waitCallTimeoutOwner == owner, "The refresh changed the timeout owner.")
          || !require (retained.call == attempt.call && retained.mode == attempt.mode
                        && retained.band == attempt.band && retained.period == attempt.period
                        && retained.receivedPeriod == attempt.receivedPeriod,
                        "The refresh changed the retry context.")
          || !require (window_->stopWCTimer.interval () == interval
                        && window_->stopWCTimer.remainingTime () <= remaining,
                        "The refresh restarted the retry deadline.")
          || !require (!window_->ui->DX_Call_Button->isChecked () && !wait_and_call && !pounce,
                        "Disabling Wait left new automatic selections armed.")) return false;
      window_->stopWCTimer.setInterval (1);
      if (!waitUntil ([this] {return !window_->m_auto && !window_->m_waitCallAttempt;},
                      "The preserved retry deadline failed after Wait was disabled.")) return false;
      if (!setOption ("enable_Wait_features_check_box", true)) return false;
    }
  }

  for (bool const quickCall : {false, true}) {
    if (!setOption ("quick_call_check_box", quickCall)
        || !startReception ("FT8", "SuperHound Wait selection is independent of Quick Call")) return false;
    if (!window_->m_config.superFox ()) window_->m_config.toggle_SF ();
    window_->m_config.setSpecial_Hound ();
    window_->m_specOp = MainWindow::SpecOp::HOUND;
    window_->m_bDoubleClicked = false;
    window_->ui->DX_Call_Button->click ();
    if (!require (wait_and_call && window_->m_hisCall == "W1AW",
                  "SuperHound Wait and Call did not arm the typed Fox.")) return false;
    if (!require (!window_->setAutoTxEnabled (true, MainWindow::TxControlOrigin::Local)
                  && !window_->m_auto && !window_->ui->autoButton->isChecked (),
                  "A typed Fox alone authorized SuperHound transmission.")) return false;
    DecodedText fox {"120000 -10  0.3 1500 ~  CQ W1AW/P FN31"};
    window_->rigTuneTimer.start (30000);
    window_->processWaitReplyCall (fox, DecodedMessageReaction::WaitDecodeSource::SlowDecoder);
    if (!require (!window_->m_auto && window_->m_hisCall == "W1AW"
                  && !window_->m_waitCallAttempt,
                  "Rejected SuperHound activation changed or cleared the selected Fox.")) return false;
    window_->rigTuneTimer.stop ();
    window_->processWaitReplyCall (fox, DecodedMessageReaction::WaitDecodeSource::SlowDecoder);
    if (!require (window_->m_auto && window_->ui->autoButton->isChecked ()
                  && window_->m_hisCall == "W1AW/P"
                  && window_->ui->tx1->text ().contains ("W1AW/P")
                  && window_->ui->tx3->text ().contains ("W1AW/P")
                  && window_->m_waitCallAttempt,
                  "An eligible SuperHound selection failed or its generated exchange was cleared.")) return false;
    window_->m_config.setSpecial_None ();
    window_->m_specOp = MainWindow::SpecOp::NONE;
  }

  if (!startReception ("FT8", "rejected enabling still finalizes a queued contest hint")) return false;
  using Effect = DecodedMessageReaction::QsoReactionEffect;
  DecodedMessageReaction::QsoReactionPlan hintPlan;
  Effect hint;
  hint.kind = Effect::Kind::QueueContestHint;
  hint.contestHint = DecodedMessageReaction::ContestHint::FieldDay;
  hintPlan.effects.append (hint);
  Effect enable;
  enable.kind = Effect::Kind::SetAutoEnabled;
  enable.boolValue = true;
  hintPlan.effects.append (enable);
  Effect cleanup;
  cleanup.kind = Effect::Kind::SetDoubleClicked;
  cleanup.boolValue = false;
  hintPlan.effects.append (cleanup);
  window_->m_contestModeHintShown = false;
  window_->m_bDoubleClicked = true;
  window_->m_tune = true;
  window_->ui->tuneButton->setChecked (true);
  int hints = 0;
  bool hintTimedOut = false;
  QTimer closeHint;
  QTimer hintWatchdog;
  hintWatchdog.setSingleShot (true);
  connect (&hintWatchdog, &QTimer::timeout, &closeHint, [&] {
    hintTimedOut = true;
    if (auto * dialog = qobject_cast<QDialog *> (QApplication::activeModalWidget ())) dialog->reject ();
  });
  connect (&closeHint, &QTimer::timeout, window_, [&] {
    if (auto * box = qobject_cast<QMessageBox *> (QApplication::activeModalWidget ())) {
      ++hints;
      box->accept ();
    }
  });
  closeHint.start (1);
  hintWatchdog.start (5000);
  DecodedText direct {"120000 -10  0.3 1500 ~  K1ABC W1AW -10"};
  window_->applyQsoReactionPlan (hintPlan, direct);
  auto const hintFinished = waitUntil ([&] {
    return hintTimedOut || (hints == 1 && !QApplication::activeModalWidget ());
  }, "The queued contest hint did not finish.");
  closeHint.stop ();
  hintWatchdog.stop ();
  if (!hintFinished || !require (!hintTimedOut && !window_->m_auto && !window_->m_bDoubleClicked
                && window_->m_contestModeHintShown && hints == 1,
                "Rejected enabling discarded finalization or consumed an undisplayed hint.")) return false;
  window_->m_tune = false;
  window_->ui->tuneButton->setChecked (false);
  return legacyMessageScenarios () && tuneShutdownScenarios () && msk144Scenarios ();
}

bool WaitFeaturesApplicationTestController::legacyMessageScenarios ()
{
  if (!setOption ("cbAutoLog", true)
      || !startReception ("FT8", "repeated RR73 recovers a logged contact without logging it again")) return false;
  window_->m_QSOProgress = QsoProgress::Calling;
  auto const logged = window_->currentQsoCompletion ();
  logged->logState = QsoCompletion::LogState::Accepted;
  window_->m_currentMessage = "W1AW K1ABC 73";
  window_->m_currentMessageType = 0;
  window_->m_lastMessageSent = window_->m_currentMessage;
  window_->m_lastMessageType = 0;
  record ("K1ABC W1AW RR73");
  if (!require (window_->m_auto && window_->m_ntx == 5,
                "The retained logged contact did not select its recovery 73.")) return false;
  auto const cycle = qint64 (2 * window_->m_TRperiod * 1000);
  auto const phase = QDateTime::currentMSecsSinceEpoch () % cycle;
  window_->m_txFirst = phase >= cycle / 2;
  window_->ui->txFirstCheckBox->setChecked (window_->m_txFirst);
  window_->m_restart = true;
  window_->guiUpdate ();
  window_->m_guiTimer.stop ();
  if (!require (!window_->m_restart && !window_->m_generated_message_error
                && window_->m_currentMessage.trimmed () == "W1AW K1ABC 73"
                && window_->m_qsoCompletion == logged && logged->logState == QsoCompletion::LogState::Accepted
                && !window_->logQSOTimer.isActive () && window_->m_pendingLogQsos.empty ()
                && !window_->m_openLogQso,
                "Generating the recovery 73 queued a duplicate automatic log.")) return false;
  if (!setOption ("cbAutoLog", false)) return false;

  if (!setOption ("quick_call_check_box", true)) return false;
  for (bool const padded : {false, true}) {
    if (!startReception ("FT8", "UDP RR73 acceptance is independent of payload padding")) return false;
    window_->ui->cbHoldTxFreq->setChecked (false);
    window_->m_QSOProgress = QsoProgress::Rogers;
    auto const payload = QString {"K1ABC W1AW RR73"} + (padded ? " " : "");
    window_->replyToCQ (QTime {12, 0}, -10, 0.3f, 1500, "~", payload, false, 0);
    if (!require (enableTxIs (true) && window_->m_ntx == 5,
                  "UDP RR73 failed to select and enable the final response consistently.")) return false;
    if (!startReception ("FT8", "UDP final 73 does not rearm transmission")) return false;
    window_->ui->cbHoldTxFreq->setChecked (false);
    window_->m_QSOProgress = QsoProgress::Signoff;
    window_->replyToCQ (QTime {12, 0}, -10, 0.3f, 1500, "~",
                        QString {"K1ABC W1AW 73"} + (padded ? " " : ""), false, 0);
    if (!require (enableTxIs (false), "UDP final 73 restarted transmission.")) return false;
  }

  for (int const frequency : {1473, 1474}) {
    if (!startReception ("FT8", "legacy Hound RRR sequencing is independent of audio frequency")) return false;
    if (window_->m_config.superFox ()) window_->m_config.toggle_SF ();
    window_->m_config.setSpecial_Hound ();
    window_->m_specOp = MainWindow::SpecOp::HOUND;
    window_->genStdMsgs ("-10");
    window_->setTxMsg (3);
    window_->m_QSOProgress = QsoProgress::RogerReport;
    if (!require (!window_->ui->tx3->text ().isEmpty (), "The Hound response fixture has no Tx3.")) return false;
    if (!window_->setAutoTxEnabled (true, MainWindow::TxControlOrigin::Local)) return false;
    if (!nextReception ()) return false;
    record ("K1ABC W1AW RRR", frequency);
    if (!require (window_->m_auto && window_->m_ntx == 5,
                  "The legacy Hound fallback failed to select the same RRR response at both frequencies.")) return false;
    window_->m_config.setSpecial_None ();
    window_->m_specOp = MainWindow::SpecOp::NONE;
  }
  return true;
}

bool WaitFeaturesApplicationTestController::tuneShutdownScenarios ()
{
  enum class Start {Manual, Reply, Pounce};
  for (auto const start : {Start::Manual, Start::Reply, Start::Pounce}) {
    for (bool const explicitStop : {false, true}) {
      auto const name = QString {"%1 waits for %2 Tune shutdown"}
        .arg (start == Start::Manual ? "Manual Enable Tx" : start == Start::Reply ? "Wait and Reply" : "Pounce")
        .arg (explicitStop ? "explicit" : "button");
      if (!startReception ("FT8", name)) return false;
      if (start == Start::Pounce) {
        window_->ui->respondComboBox->setVisible (true);
        window_->ui->respondComboBox->setCurrentIndex (
          window_->ui->respondComboBox->findData (static_cast<int> (AutoRespondPolicy::First)));
      }
      window_->ui->tuneButton->click ();
      if (!require (window_->m_tune && window_->ui->tuneButton->isChecked (),
                    "The Tune fixture did not start.")) return false;
      auto const call = window_->ui->dxCallEntry->text ();
      auto const messages = [this] {
        return QStringList {window_->ui->tx1->text (), window_->ui->tx2->text (),
          window_->ui->tx3->text (), window_->ui->tx4->text (),
          window_->ui->tx5->currentText (), window_->ui->tx6->text ()};
      };
      auto const prepared = messages ();
      auto const request = [this, start] {
        if (start == Start::Manual) window_->ui->autoButton->click ();
        else if (start == Start::Reply) record ("K1ABC W1AW -10");
        else {
          pounce = true;
          DecodedText cq {"120000 -10  0.3 1900 ~  CQ JA1ABC PM95"};
          window_->updateRespondTarget (cq, cq.string (), pounce,
                                       window_->m_dateTimeSeqStart, false);
        }
      };
      auto const rejected = [&] {
        request ();
        return require (!window_->m_auto && !window_->ui->autoButton->isChecked (),
                        "Tune admitted a Tx start before shutdown completed.")
          && require (window_->ui->dxCallEntry->text () == call && messages () == prepared,
                      "Rejected enabling changed the selected contact or prepared messages.")
          && require (!window_->m_autoRespondSelectionLatch.isSelected ()
                      && window_->m_autoRespondScores.wouldImproveDistance (0)
                      && window_->m_autoRespondScores.wouldImproveMaximumDb (-10)
                      && window_->m_autoRespondScores.wouldImproveMinimumDb (-10),
                      "Rejected enabling consumed selection eligibility or a ranking score.")
          && require (!window_->m_waitReplyAttempt && !window_->m_waitCallAttempt
                      && !window_->stopWRTimer.isActive () && !window_->stopWCTimer.isActive (),
                      "Rejected enabling created a retry deadline.");
      };
      if (!rejected ()) return false;
      if (explicitStop) window_->stop_tuning ();
      else window_->ui->tuneButton->click ();
      if (!require (!window_->ui->tuneButton->isChecked () && window_->tuneButtonTimer.isActive ()
                    && window_->m_tune == !explicitStop,
                    "The Tune shutdown fixture does not represent the requested pending state.")) return false;
      if (start == Start::Reply && !nextReception ()) return false;
      if (!rejected ()) return false;
      if (!waitUntil ([this] {return !window_->m_tune && !window_->tuneButtonTimer.isActive ();},
                      "The real Tune shutdown did not complete.")) return false;
      if (start == Start::Reply && !nextReception ()) return false;
      request ();
      if (!require (enableTxIs (true), "The same request failed after Tune shutdown completed.")) return false;
      if (!waitUntil ([this] {
            return !window_->ptt0Timer.isActive () && !window_->m_rigState.ptt ();
          }, "Tune cleanup did not release PTT after accepting the new transmission.")) return false;
      if (!require (enableTxIs (true), "Old Tune cleanup stopped the accepted transmission.")) return false;
    }
  }
  return true;
}

bool WaitFeaturesApplicationTestController::msk144Scenarios ()
{
  if (!startReception ("MSK144", "MSK144 directed answer disarms an unfired Wait and Call")) return false;
  window_->ui->DX_Call_Button->click ();
  if (!require (wait_and_call && !window_->m_waitCallAttempt,
                "MSK144 Wait and Call did not arm without an attempt.")) return false;
  DecodedText direct {"120000 -10  0.3 1500 &  K1ABC W1AW -10"};
  window_->processWaitReplyCall (direct, DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
  if (!require (window_->m_auto && !wait_and_call && !window_->ui->DX_Call_Button->isChecked (),
                "The spontaneous MSK144 answer left Wait and Call armed.")) return false;
  DecodedText cq {"120000 -10  0.3 1500 &  CQ W1AW FN31"};
  window_->processWaitReplyCall (cq, DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
  if (!require (!window_->m_waitCallAttempt,
                "A subsequent MSK144 CQ started a new calling attempt.")) return false;

  if (!startReception ("MSK144", "same-period MSK144 answer retires a retained calling deadline")) return false;
  window_->ui->DX_Call_Button->click ();
  window_->processWaitReplyCall (cq, DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
  if (!require (window_->m_auto && window_->m_waitCallAttempt,
                "The MSK144 calling attempt did not start.")) return false;
  auto const attempt = *window_->m_waitCallAttempt;
  auto const owner = window_->m_waitCallTimeoutOwner;
  auto const callingPeriod = attempt.receivedPeriod;
  if (!setOption ("enable_Wait_features_check_box", false)) return false;
  if (!require (window_->m_auto && window_->stopWCTimer.isActive () && window_->m_waitCallAttempt,
                "Settings retired the MSK144 attempt before its answer arrived.")) return false;
  auto const& retained = *window_->m_waitCallAttempt;
  if (!require (retained.generation == attempt.generation && window_->m_waitCallTimeoutOwner == owner
                && retained.call == window_->m_hisCall && retained.mode == window_->m_mode
                && retained.band == window_->nominalRxBand () && retained.period == window_->m_TRperiod
                && retained.period == window_->ui->sbTR->value ()
                && retained.receivedPeriod == callingPeriod,
                "Settings changed the MSK144 attempt or its mode/period preconditions.")) return false;
  window_->m_dateTimeSeqStart = callingPeriod;
  window_->processWaitReplyCall (direct, DecodedMessageReaction::WaitDecodeSource::Msk144FastDecoder);
  if (!require (window_->m_auto && !window_->m_waitCallAttempt && !window_->stopWCTimer.isActive (),
                "A same-period answer left the retained calling deadline armed.")) return false;
  if (!setOption ("enable_Wait_features_check_box", true)) return false;

  return true;
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
