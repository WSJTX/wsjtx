#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "models/Bands.hpp"
#include "models/StationList.hpp"
#include "activeStations.h"
#include <QSignalBlocker>
#include <limits>

extern bool programStart;
extern bool m_displayBand;
extern bool no_decodes_to_UDP;
extern bool no_a7_decodes;
extern bool not_erase;
extern bool msk144qsy;
extern bool keep_msk144_frequency;
extern bool m_band_changed;
extern int m_msk144_tr;
extern int m_msk144_tr2;
extern int m_msk144_tr6;

auto MainWindow::displayedDialFrequency () const -> Frequency
{
  return OperatingBand::displayedDial (m_operatingFrequency.snapshot (), m_rigState.ptt (), m_rigState.split ());
}

QString MainWindow::operatingBand () const
{
  return m_config.bands ()->find (displayedDialFrequency ());
}

QString MainWindow::operatingBandName () const
{
  auto const band = operatingBand ();
  return band.isEmpty () ? m_config.bands ()->oob () : band;
}

QString MainWindow::nominalRxBand () const
{
  return m_config.bands ()->find (m_operatingFrequency.rx ());
}

QString MainWindow::processedOperatingBand () const
{
  auto const& state = m_operatingBandController.state ();
  return state ? state->band : QString {};
}

void MainWindow::updateBandEditor (bool force)
{
  auto const band = operatingBand ();
  // Routine rendering preserves an unfinished frequency entry.
  if (!force && m_renderedBand && *m_renderedBand == band) return;
  m_renderedBand = band;
  ui->bandComboBox->setCurrentText (band.isEmpty () ? m_config.bands ()->oob () : band);
  on_bandComboBox_editTextChanged (ui->bandComboBox->currentText ());
}

OperatingBand::Context MainWindow::operatingBandContext () const
{
  OperatingBand::Context context;
  // Observations retain installed-mode policy during setup; radio activity below stays live.
  if (m_modeSetupActive && m_operatingBandController.context ())
    context = *m_operatingBandController.context ();
  else
    {
      context.mode = m_mode;
      context.period = m_TRperiod;
      context.starting = programStart;
      context.bandHopping = ui->pbBandHopping->isChecked ();
      context.waitAndCall = ui->DX_Call_Button->isChecked ();
      context.callingFrequency = ui->cbCQTx->isEnabled () && ui->cbCQTx->isVisible () && ui->cbCQTx->isChecked ();
      context.rememberTxPower = m_config.pwrBandTxMemory ();
      context.eraseActivity = m_config.erase_BandActivity ();
      context.preserveActivity = not_erase;
      context.reportingEnabled = m_config.spot_to_psk_reporter ();
      context.bandButtonsVisible = ui->actionBand_Buttons->isChecked ();
      context.temporaryMskQsy = msk144qsy;
      context.preserveMskBase = keep_msk144_frequency;
      context.fox = m_config.special_op_id () == SpecOp::FOX;
      context.waterfallStart = m_wideGraph->nStartFreq ();
      context.waterfallSpan = m_wideGraph->fSpan ();
      context.txAudio = ui->TxFreqSpinBox->value ();
    }
  context.monitoring = m_monitoring;
  context.transmitting = m_transmitting;
  context.tuning = m_tune;
  context.acceptedRetune = false;
  context.selectionEffects = false;
  return context;
}

void MainWindow::synchronizeOperatingBandState (FrequencyChangeIntent intent)
{
  auto context = operatingBandContext ();
  auto event = OperatingBand::Event::Observation;
  if (intent == FrequencyChangeIntent::NominalRequest) event = OperatingBand::Event::NominalRequest;
  if (intent == FrequencyChangeIntent::BandSelection) event = OperatingBand::Event::Selection;
  OperatingBand::State const state {m_operatingFrequency.snapshot (), displayedDialFrequency (), nominalRxBand (), operatingBand ()};
  auto const plan = m_operatingBandController.transition (state, context, event);
  applyOperatingBandPlan (plan, state, context);
}

void MainWindow::completeBandReportingTransition (quint64 identity)
{
  auto const report = m_operatingBandController.completeReporting (identity, m_operatingFrequency.rx ());
  if (!report) return;
  m_bandReportingTimer.stop ();
  m_freqNominalPeriod = report->rx;
  m_currentBandPeriod = report->band;
  m_displayBand = true;
  no_decodes_to_UDP = false;
  pskSetLocal (report->band);
}

void MainWindow::applyOperatingBandPlan (OperatingBand::Plan const& plan,
                                        OperatingBand::State const& state,
                                        OperatingBand::Context const& context)
{
  auto const current = [this, &plan] {return m_operatingBandController.current (plan.revision);};
  if (plan.rxBandChanged) endQsoRepeat (QsoRepeatEnd::Superseded);
  if (plan.rxChanged)
    {
      if (context.mode == "JTTY") updateJttyReceiveContext ();
      if (context.mode == "JT65") m_jt65ClearAveragesPending = true;
      cancelPendingFt8Decode ("dial frequency changed");
      setBestSPArmed (false);
      genCQMsg ();
    }
  if (plan.refreshRxPresentation)
    {
      if (m_ActiveStationsWidget)
        {
          m_recentCall.clear ();
          if (context.mode != "Q65") m_ActiveStationsWidget->erase ();
        }
      m_secBandChanged = QDateTime::currentMSecsSinceEpoch () / 1000;
      m_wideGraph->setDialFreq (state.frequency.rx / 1.e6);
    }
  // Q65 Active Stations: #, Wanted only and QSOs are per band, so a band
  // change redraws them at once (not only at the next decode or log entry)
  if (plan.rxBandChanged && context.mode == "Q65" && m_ActiveStationsWidget) displayQ65ActiveStations ();
  if (plan.nominalChanged && m_astroWidget)
    m_astroWidget->nominal_frequency (state.frequency.rx, state.frequency.tx);
  if (plan.reporting)
    {
      auto const deadline = *plan.reporting;
      m_bandReportingTimer.stop ();
      disconnect (&m_bandReportingTimer, nullptr, this, nullptr);
      m_displayBand = false;
      no_decodes_to_UDP = true;
      m_bandReportingTimer.setSingleShot (true);
      connect (&m_bandReportingTimer, &QTimer::timeout, this,
               [this, deadline] {completeBandReportingTransition (deadline.identity);});
      m_bandReportingTimer.start (deadline.interval);
    }
  if (plan.apSuppression)
    {
      auto const deadline = *plan.apSuppression;
      m_bandApTimer.stop ();
      disconnect (&m_bandApTimer, nullptr, this, nullptr);
      no_a7_decodes = true;
      m_bandApTimer.setSingleShot (true);
      connect (&m_bandApTimer, &QTimer::timeout, this, [this, deadline] {
        if (m_operatingBandController.completeAP (deadline.identity)) no_a7_decodes = false;
      });
      m_bandApTimer.start (deadline.interval);
    }
  if (plan.disarmAutoRespond) m_autoRespondPeriodState.disarm ();
  if (plan.clearTemporaryMskQsy) msk144qsy = false;
  if (plan.stopAutoTx)
    {
      ui->stopTxButton->click ();
      auto_tx_mode (false);
    }
  if (!current ()) return;
  if (plan.flushSpots) m_psk_Reporter.sendReport ();
  if (plan.monitor && *plan.monitor != m_monitoring) monitor (*plan.monitor);
  if (!current ()) return;
  if (plan.initializeFreqCal) m_frequency_list_fcal_iter = m_config.frequencies ()->find (state.frequency.rx);
  if (plan.updateTxPlan) setXIT (context.txAudio);
  if (!current ()) return;
  if (plan.resetFox) FoxReset ("BandChange");
  if (plan.clearLastCall) m_lastloggedcall.clear ();
  if (plan.restorePower)
    {
      auto const key = state.band.isEmpty () ? m_config.bands ()->oob () : state.band;
      if (m_pwrBandTxMemory.contains (key)) ui->outAttenuation->setValue (m_pwrBandTxMemory[key].toInt ());
      else m_pwrBandTxMemory[key] = ui->outAttenuation->value ();
    }
  if (!current ()) return;
  if (plan.refreshWidegraph) m_wideGraph->setRxBand (state.band);
  if (plan.bandChanged) m_band_changed = true;
  if (plan.setSkedFrequency && m_astroWidget)
    {
      m_astroWidget->setSkedFreq (state.dial * 0.000001);
      m_skedFreq = state.dial * 0.000001;
    }
  if (plan.eraseActivity)
    {
      ui->decodedTextBrowser->erase ();
      ui->decodedTextBrowser2->erase ();
    }
  if (plan.mskRestore)
    {
      auto const deadline = *plan.mskRestore;
      auto const period = deadline.band == "2m" ? m_msk144_tr2
        : (deadline.band == "6m" || deadline.band == "4m") ? m_msk144_tr6 : m_msk144_tr;
      m_bandMskTimer.stop ();
      disconnect (&m_bandMskTimer, nullptr, this, nullptr);
      m_bandMskTimer.setSingleShot (true);
      connect (&m_bandMskTimer, &QTimer::timeout, this, [this, deadline, period] {
        if (m_operatingBandController.completeMskRestore (deadline.identity)) ui->sbTR->setValue (period);
      });
      m_bandMskTimer.start (deadline.interval);
    }
  if (plan.mskBase) m_msk144basefreq = *plan.mskBase;
  if (plan.refreshButtons) check_button_color ();
  if (!current ()) return;
  displayDialFrequency ();
  if (plan.rxChanged && !m_modeEntries.active ()) statusChanged ();
}

void MainWindow::completeModeFrequencyChange (ModeEntry::Operation const& operation)
{
  auto const& request = operation.request;
  auto const policy = ModeEntry::policyFor (request, m_specOp == SpecOp::HOUND);
  auto context = operatingBandContext ();
  bool accepted = false;
  std::optional<Frequency> target;
  if (request.mode == "FreqCal")
    {
      auto const rx = m_operatingFrequency.rx ();
      auto const audio = ui->RxFreqSpinBox->value ();
      auto const frequencies = m_config.frequencies ();
      m_frequency_list_fcal_iter = audio >= 0 && rx <= std::numeric_limits<Frequency>::max () - audio
        ? frequencies->find (rx + audio) : frequencies->end ();
    }
  if (m_modeEntries.current (operation.identity) && policy.target != ModeEntry::TargetKind::None
      && nominalFrequencyChangeAllowed (request.userInitiated ? FrequencyRequestOrigin::User : FrequencyRequestOrigin::Automatic))
    {
      if (policy.target == ModeEntry::TargetKind::Explicit) target = request.frequency;
      else if (policy.target == ModeEntry::TargetKind::CalibrationStart)
        {
          m_frequency_list_fcal_iter = m_config.frequencies ()->begin ();
          if (m_frequency_list_fcal_iter != m_config.frequencies ()->end ())
            target = OperatingBand::calibrationDial (m_frequency_list_fcal_iter->frequency_, ui->RxFreqSpinBox->value ());
        }
      else
        {
          Frequency frequency;
          if (workingFrequencyAt (m_config.frequencies ()->best_working_frequency (m_operatingFrequency.rx ()), frequency)) target = frequency;
        }
      auto const state = m_operatingFrequency.snapshot ();
      if (!state.rx || state.pendingRx || state.pendingTx)
        m_pendingModeRetune.defer (operation, m_config.frequency_generation (), state.rx);
      else if (target)
        {
          auto const origin = request.userInitiated ? FrequencyRequestOrigin::User : FrequencyRequestOrigin::Automatic;
          accepted = tryRequestNominalFrequencyChange (*target, origin, true);
        }
    }
  context.acceptedRetune = accepted;
  context.selectionEffects = policy.selectionEffects;
  if (m_tci_audio && !operatingBand ().isEmpty ()) Q_EMIT m_config.transceiver_period (context.period);
  OperatingBand::State const state {m_operatingFrequency.snapshot (), displayedDialFrequency (), nominalRxBand (), operatingBand ()};
  auto const plan = m_operatingBandController.transition (state, context, OperatingBand::Event::ModeConfigured);
  applyOperatingBandPlan (plan, state, context);
  updateBandEditor (true);
  statusChanged ();
}

void MainWindow::resumeModeRetune ()
{
  if (m_modeEntries.active ()) return;
  auto const operation = m_pendingModeRetune.resume (m_modeEntries.acceptedIdentity (),
    m_config.frequency_generation (), m_operatingFrequency.snapshot ());
  if (operation) completeModeFrequencyChange (*operation);
}
