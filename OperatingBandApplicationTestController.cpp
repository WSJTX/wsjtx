// SPDX-License-Identifier: GPL-3.0-or-later
#include "OperatingBandApplicationTestController.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScopedValueRollback>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QVector>

#include "BestSPApplicationTestController.hpp"
#include "DecoderBackend.hpp"
#include "DecodedMessageReaction.hpp"
#include "models/Bands.hpp"
#include "models/FrequencyList.hpp"
#include "ui_mainwindow.h"
#include "widgets/mainwindow.h"

extern bool m_displayBand;
extern bool no_decodes_to_UDP;
extern bool m_band_changed;
extern bool programStart;
extern int m_msk144_tr2;
extern int m_msk144_tr6;

namespace
{
  using Frequency = Radio::Frequency;
  using Frequencies = FrequencyList_v2_101;

  Frequencies::Item item (Frequency frequency, Modes::Mode mode,
                          IARURegions::Region region = IARURegions::ALL)
  {
    return {frequency, mode, region, {}, {}, {}, {}, true};
  }

  void key (QLineEdit * editor, int code, QString const& text = {})
  {
    QKeyEvent press {QEvent::KeyPress, code, Qt::NoModifier, text};
    QApplication::sendEvent (editor, &press);
    QKeyEvent release {QEvent::KeyRelease, code, Qt::NoModifier, text};
    QApplication::sendEvent (editor, &release);
  }

  class HeldTimerExpiry final : public QObject
  {
  public:
    explicit HeldTimerExpiry (QTimer& timer) : timer_ {timer}
    {
      timer_.stop ();
      timer_.installEventFilter (this);
    }

    ~HeldTimerExpiry () override {timer_.removeEventFilter (this);}

    bool intercepted () const {return intercepted_;}

    bool hold ()
    {
      if (timer_.isActive ())
        {
          armed_ = true;
          timer_.stop ();
        }
      return armed_;
    }

  protected:
    bool eventFilter (QObject * watched, QEvent * event) override
    {
      if (watched != &timer_ || event->type () != QEvent::Timer
          || static_cast<QTimerEvent *> (event)->timerId () != timer_.timerId ()) return false;
      intercepted_ = true;
      hold ();
      return true;
    }

  private:
    QTimer& timer_;
    bool armed_ {false};
    bool intercepted_ {false};
  };
}

QString OperatingBandApplicationTestController::prepareSettings (QSettings * settings)
{
  auto const error = BestSPApplicationTestController::prepareSettings (settings);
  if (!error.isEmpty ()) return error;
  bool const echo_startup = qEnvironmentVariableIsSet ("WSJT_OPERATING_BAND_START_ECHO");
  settings->setValue ("Common/Mode", echo_startup ? "Echo" : "FT8");
  settings->setValue ("Common/DialFreq", QVariant::fromValue<Frequency> (
    echo_startup ? 144174000 : 14074000));
  if (echo_startup)
    {
      auto frequencies = Frequencies::default_items ();
      frequencies.append (item (14076000, Modes::Echo));
      settings->setValue ("Configuration/FrequenciesForRegionModes_v2", QVariant::fromValue (frequencies));
    }
  settings->setValue ("Configuration/WaitFeaturesEnabled", true);
  settings->setValue ("Configuration/VHFUHF", true);
  settings->setValue ("Configuration/pwrBandTxMemory", true);
  settings->setValue ("Configuration/pwrBandTuneMemory", true);
  settings->setValue ("Common/pwrBandTxMemory", QHash<QString, QVariant> {{"20m", 17}, {"40m", 93}});
  settings->setValue ("Common/pwrBandTuneMemory", QHash<QString, QVariant> {{"20m", 27}, {"40m", 103}});
  settings->sync ();
  return {};
}

OperatingBandApplicationTestController::OperatingBandApplicationTestController (
  MainWindow * window, DecoderBackend * backend, QObject * parent)
  : QObject {parent}, window_ {window}, backend_ {backend}
  , echo_startup_ {qEnvironmentVariableIsSet ("WSJT_OPERATING_BAND_START_ECHO")}
{
  retry_.setSingleShot (true);
  retry_.setInterval (20);
  connect (&retry_, &QTimer::timeout, this, [this] {prepare ();});
  timeout_.setSingleShot (true);
  timeout_.setInterval (10000);
  connect (&timeout_, &QTimer::timeout, this, [this] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    std::cerr << "Operating-band preparation: programStart=" << programStart
              << " backendReady=" << backend_->ready ()
              << " country=" << window_->m_logBook.countries ()->lookup ("W1AW")
                   .identity (AD1CCty::EntityView::DXCC).name.toStdString ()
              << " mode=" << window_->m_mode.toStdString ()
              << " rx=" << state.rx << " tx=" << state.tx
              << " correctedRx=" << state.correctedRx
              << " rigRx=" << window_->m_rigState.frequency ()
              << " rigOnline=" << window_->m_rigState.online ()
              << " configOnline=" << window_->m_config.is_transceiver_online ()
              << " pending=" << state.pendingRx << '/' << state.pendingTx
              << " pendingMode=" << window_->m_pendingModeRetune.pending ()
              << " modeActive=" << window_->m_modeEntries.active ()
              << " recovery=" << state.recoveryRequired
              << " monitor=" << window_->m_monitoring << std::endl;
    finish (false, "Timed out preparing isolated operating-band fixtures.");
  });
}

void OperatingBandApplicationTestController::begin ()
{
  window_->m_guiTimer.stop ();
  if (echo_startup_
      && !require (window_->m_mode == "Echo" && !window_->m_monitoring,
                   "Saved Echo startup returned with the wrong mode or Monitor enabled.")) return;
  timeout_.start ();
  prepare ();
}

void OperatingBandApplicationTestController::prepare ()
{
  if (finished_) return;
  if (programStart || !backend_->ready () || window_->m_logBook.countries ()->lookup ("W1AW")
      .identity (AD1CCty::EntityView::DXCC).name.isEmpty ())
    {
      retry_.start ();
      return;
    }
  if (!require (window_->m_config.is_dummy_rig (), "The test requires the dummy radio.")
      || !require (window_->m_config.transceiver_online (), "Cannot open the dummy radio.")) return;
  if (!window_->m_rigState.online ())
    {
      retry_.start ();
      return;
    }
  if (echo_startup_)
    {
      auto const state = window_->m_operatingFrequency.snapshot ();
      Frequency target {0};
      auto const row = window_->m_config.frequencies ()->best_working_frequency (state.rx);
      if (!state.rx || state.pendingRx || state.pendingTx
          || !window_->workingFrequencyAt (row, target) || state.rx != target)
        {
          retry_.start ();
          return;
        }
      timeout_.stop ();
      if (require (window_->m_mode == "Echo" && !window_->m_monitoring
                   && window_->ui->bandComboBox->currentText () == window_->nominalRxBand (),
                   "Radio readiness changed saved Echo monitoring or band presentation."))
        finish (true, "Saved Echo startup established its working frequency with Monitor stopped.");
      return;
    }
  auto const state = window_->m_operatingFrequency.snapshot ();
  if (state.pendingRx || state.pendingTx)
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
  if (run ()) finish (true, "All operating-band application scenarios passed.");
}

bool OperatingBandApplicationTestController::run ()
{
  return setFrequency (14074000) && filtering () && editorRequests ()
    && rendering () && modeEntry () && mskModeExit () && modalObservation ()
    && transitions () && hopping () && reporting ();
}

bool OperatingBandApplicationTestController::waitUntil (
  std::function<bool()> const& ready, QString const& failure)
{
  if (ready ()) return true;
  QEventLoop loop;
  QTimer poll;
  QTimer watchdog;
  poll.setInterval (5);
  watchdog.setSingleShot (true);
  connect (&poll, &QTimer::timeout, &loop, [&] {if (ready ()) loop.quit ();});
  connect (&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start ();
  watchdog.start (5000);
  loop.exec ();
  if (!ready ())
    {
      auto const state = window_->m_operatingFrequency.snapshot ();
      std::cerr << "Operating-band timeout: mode=" << window_->m_mode.toStdString ()
                << " rx=" << state.rx << " tx=" << state.tx << " correctedRx=" << state.correctedRx
                << " rig=" << window_->m_rigState.frequency () << " pending=" << state.pendingRx
                << '/' << state.pendingTx << " recovery=" << state.recoveryRequired
                << " monitor=" << window_->m_monitoring << std::endl;
    }
  return require (ready (), failure);
}

bool OperatingBandApplicationTestController::setFrequency (Frequency frequency, bool band_selection)
{
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return !state.pendingRx && !state.pendingTx;
  }, "The preceding frequency operation did not finish before the next request.")) return false;
  bool const accepted = band_selection
    ? window_->requestBandChange (frequency, MainWindow::FrequencyRequestOrigin::User)
    : window_->requestNominalFrequencyChange (frequency, MainWindow::FrequencyRequestOrigin::User);
  if (!accepted)
    {
      auto const state = window_->m_operatingFrequency.snapshot ();
      std::cerr << "Operating-band request rejected: target=" << frequency
                << " selection=" << band_selection << " rx=" << state.rx << " tx=" << state.tx
                << " pending=" << state.pendingRx << '/' << state.pendingTx
                << " recovery=" << state.recoveryRequired << std::endl;
    }
  if (!require (accepted, "The frequency request was rejected.")) return false;
  return waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == frequency && !state.pendingRx && !state.pendingTx
      && (!window_->m_monitoring || window_->m_rigState.frequency () == state.correctedRx);
  }, "The accepted frequency did not become established.");
}

bool OperatingBandApplicationTestController::filtering ()
{
  scenario_ = "mode and working-list changes";
  auto * frequencies = window_->m_config.frequencies ();
  auto * box = window_->ui->bandComboBox;
  frequencies->frequency_list (Frequencies::default_items ());
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14074000, true)) return false;
  frequencies->frequency_list ({item (14074000, Modes::FT8), item (14079000, Modes::JT9),
                                item (14076000, Modes::JT65)});
  bool intermediate_retune = false;
  auto const frequency_connection = connect (&window_->m_config, &Configuration::frequency_result,
    this, [&] (RigFrequency::Result const& result) {
      if (result.request.path == RigFrequency::Path::Rx && result.request.target == 14079000)
        intermediate_retune = true;
    });
  window_->ui->actionJT65->trigger ();
  auto const settled = waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 14076000 && !state.pendingRx && !state.pendingTx;
  }, "The JT65 mode change did not establish its working frequency.");
  disconnect (frequency_connection);
  if (!settled || !require (!intermediate_retune,
                            "JT65 setup issued an intermediate JT9 retune.")) return false;
  if (!require (window_->m_bandReportingTimer.interval () == int (600.0 * window_->m_TRperiod),
                "Mode-change effects ran before the target TR period was established.")) return false;
  frequencies->frequency_list (Frequencies::default_items ());
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14074000, true)) return false;
  auto const before = window_->m_operatingFrequency.snapshot ();
  window_->ui->actionQ65->trigger ();
  if (!require (frequencies->best_working_frequency (Frequency {14074000}) == -1
                && box->currentText () == "20m" && window_->m_operatingFrequency.rx () == before.rx
                && window_->m_operatingFrequency.tx () == before.tx,
                "Q65 filtering selected an unrelated operating band or frequency.")) return false;
  window_->ui->actionFT8->trigger ();
  if (!waitUntil ([&] {return !window_->m_operatingFrequency.snapshot ().pendingRx;},
                   "Returning to FT8 did not settle.")) return false;
  if (!require (box->currentText () == "20m", "Returning to FT8 lost 20m.")) return false;

  frequencies->frequency_list ({item (14074000, Modes::FT8), item (2301065000, Modes::Q65)});
  window_->ui->actionQ65->trigger ();
  if (!require (frequencies->rowCount () == 1 && box->currentText () == "20m"
                && window_->m_operatingFrequency.rx () == 14074000,
                "The 13cm fallback row replaced the operating band.")) return false;
  frequencies->frequency_list ({});
  if (!require (box->currentText () == "20m", "An empty working list lost the operating band.")) return false;

  frequencies->frequency_list ({item (14074000, Modes::Q65, IARURegions::R1),
                                item (2301065000, Modes::Q65, IARURegions::R2)});
  frequencies->filter (IARURegions::R1, Modes::Q65, true);
  box->setCurrentIndex (frequencies->best_working_frequency (Frequency {14074000}));
  frequencies->filter (IARURegions::R2, Modes::Q65, true);
  if (!require (frequencies->rowCount () == 1 && box->currentText () == "20m",
                "Region filtering changed the operating-band label.")) return false;

  frequencies->frequency_list ({item (14074000, Modes::Q65), item (2301065000, Modes::Q65)});
  box->setCurrentIndex (frequencies->best_working_frequency (Frequency {14074000}));
  frequencies->setDynamicSortFilter (false);
  auto const expiry = QDateTime::currentDateTimeUtc ().addDays (-1).toString (Qt::ISODate);
  auto const expiry_index = frequencies->index (box->currentIndex (), Frequencies::end_time_column);
  if (!require (frequencies->setData (expiry_index, expiry)
                && frequencies->rowCount () == 2,
                "The expiry prerequisite was not staged before the refresh.")) return false;
  window_->invalidate_frequencies_filter ();
  frequencies->setDynamicSortFilter (true);
  if (!require (frequencies->rowCount () == 1 && box->currentText () == "20m"
                && window_->m_operatingFrequency.rx () == 14074000,
                "The production time-refresh handler changed the operating band.")) return false;

  window_->ui->actionEcho->trigger ();
  if (!require (box->currentText () == "20m" && window_->m_operatingFrequency.rx () == 14074000,
                "Echo mode selected an unrelated band.")) return false;
  if (!require (!window_->m_monitoring,
                "Echo returned with Monitor enabled when no working frequency matched.")) return false;
  frequencies->frequency_list (Frequencies::default_items ());
  window_->ui->actionFT8->trigger ();
  return setFrequency (14074000, true);
}

bool OperatingBandApplicationTestController::enter (QString const& text, Frequency expected)
{
  auto * editor = window_->ui->bandComboBox->lineEdit ();
  editor->setFocus ();
  editor->selectAll ();
  for (auto const character : text) key (editor, 0, QString {character});
  key (editor, Qt::Key_Return);
  return waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    auto const band = window_->m_config.bands ()->find (expected);
    return state.rx == expected && !state.pendingRx && !state.pendingTx
      && window_->ui->bandComboBox->currentText () == (band.isEmpty () ? QString {"OOB"} : band);
  }, "Typed frequency entry did not settle at the accepted operating band.");
}

bool OperatingBandApplicationTestController::editorRequests ()
{
  scenario_ = "dropdown and typed requests";
  auto * frequencies = window_->m_config.frequencies ();
  auto * box = window_->ui->bandComboBox;
  frequencies->frequency_list ({item (7074000, Modes::FT8), item (14074000, Modes::FT8)});
  auto activate = [&] (int row) {
    box->setCurrentIndex (row);
    return QMetaObject::invokeMethod (box, "activated", Qt::DirectConnection, Q_ARG (int, row));
  };
  auto const row20 = frequencies->best_working_frequency (Frequency {14074000});
  box->setCurrentIndex (row20);
  if (!setFrequency (14081000)) return false;
  if (!require (box->currentIndex () == row20 && activate (row20),
                "Cannot activate the already-selected working row.")) return false;
  if (!waitUntil ([&] {
    return window_->m_operatingFrequency.rx () == 14074000
      && !window_->m_operatingFrequency.snapshot ().pendingRx;
  }, "Activating the selected row did not request its frequency.")) return false;

  if (!enter ("7.074", 7074000) || !enter ("20m", 14074000)
      || !enter ("080k", 14080000) || !enter ("23cm", 14080000)) return false;
  if (!require (!box->lineEdit ()->styleSheet ().contains ("red"),
                "An unresolved band submission retained its error style after restoration.")) return false;
  {
    QScopedValueRollback<bool> tuning {window_->m_tune, true};
    if (!require (activate (frequencies->best_working_frequency (Frequency {7074000}))
                  && window_->m_operatingFrequency.rx () == 14080000
                  && box->currentText () == "20m",
                  "Rejected dropdown activation changed operating state or left its candidate label.")) return false;
    if (!enter ("7.074", 14080000)) return false;
  }
  if (!enter ("15.000", 15000000)) return false;
  if (!require (box->lineEdit ()->styleSheet ().contains ("red"),
                "Accepted OOB input lacks its warning style.")) return false;
  frequencies->filter (IARURegions::ALL, Modes::Q65, true);
  if (!require (box->currentText () == "OOB" && box->lineEdit ()->styleSheet ().contains ("red"),
                "Refiltering removed the OOB text or style.")) return false;
  frequencies->frequency_list (Frequencies::default_items ());
  window_->ui->actionFT8->trigger ();
  window_->monitor (false);
  {
    QScopedValueRollback<bool> tci_audio {window_->m_tci_audio, true};
    QScopedValueRollback<bool> initial_error {window_->m_first_error, true};
    if (!require (window_->operatingBand ().isEmpty ()
                  && window_->requestBandChange (14074000, MainWindow::FrequencyRequestOrigin::User)
                  && window_->m_operatingFrequency.rx () == 14074000 && window_->m_monitoring
                  && window_->m_first_error && !window_->m_operatingFrequency.recoveryRequired (),
                  "Starting TCI monitoring used the previous OOB band instead of the accepted target.")) return false;
  }
  if (!waitUntil ([&] {return !window_->m_operatingFrequency.snapshot ().pendingRx;},
                   "The TCI OOB-to-valid request did not settle.")) return false;
  return setFrequency (14074000, true);
}

bool OperatingBandApplicationTestController::rendering ()
{
  scenario_ = "rendering has no operational effects";
  window_->ui->actionMSK144->trigger ();
  if (!setFrequency (14074000)) return false;
  window_->ui->actionBand_Buttons->setChecked (true);
  window_->m_msk144basefreq = 50000000;
  window_->m_freqNominalPeriod = 7074000;
  m_displayBand = true;
  no_decodes_to_UDP = false;
  auto const interval = window_->m_bandReportingTimer.interval ();
  auto const active = window_->m_bandReportingTimer.isActive ();
  auto const remembered = window_->m_operatingFrequency.remembered ();
  auto const powers = window_->m_pwrBandTxMemory;
  auto const tune_powers = window_->m_pwrBandTuneMemory;
  auto const monitoring = window_->m_monitoring;
  window_->startWaitRetry (true, 60000, QDateTime::currentDateTimeUtc ());
  if (!require (bool (window_->m_waitReplyAttempt), "Cannot establish the Wait retry sentinel.")) return false;
  window_->m_waitReplyAttempt->band = "40m";

  auto * editor = window_->ui->bandComboBox->lineEdit ();
  editor->setFocus ();
  editor->selectAll ();
  key (editor, 0, "1");
  key (editor, 0, "4");
  key (editor, 0, ".");
  auto const draft = editor->text ();
  for (int i = 0; i != 4; ++i) window_->displayDialFrequency ();
  if (!require (editor->text () == draft && window_->m_msk144basefreq == 50000000
                && window_->m_freqNominalPeriod == 7074000 && m_displayBand && !no_decodes_to_UDP
                && window_->m_bandReportingTimer.interval () == interval
                && window_->m_bandReportingTimer.isActive () == active
                && window_->m_operatingFrequency.remembered () == remembered
                && window_->m_pwrBandTxMemory == powers && window_->m_pwrBandTuneMemory == tune_powers
                && window_->m_monitoring == monitoring && window_->m_waitReplyAttempt
                && window_->stopWRTimer.isActive (),
                "Rendering modified a draft, Wait retry, frequency state, suppression, or band memory.")) return false;
  window_->cancelWaitRetry (true);
  window_->updateBandEditor (true);
  window_->ui->actionFT8->trigger ();
  return setFrequency (14074000, true);
}

bool OperatingBandApplicationTestController::modeEntry ()
{
  scenario_ = "mode-entry frequency commitment";
  auto * frequencies = window_->m_config.frequencies ();
  frequencies->frequency_list (Frequencies::default_items ());
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14074000, true)) return false;
  scenario_ = "JTTY shared audio offsets on mode entry";
  auto const saved_tx = window_->m_settings->value ("TxFreq_old", 1500);
  auto const saved_rx = window_->m_settings->value ("RxFreq_old", 1500);
  {
    // Seeding controls must not enqueue delayed writes over the saved entry values.
    QSignalBlocker const tx {window_->ui->TxFreqSpinBox};
    QSignalBlocker const tx_copy {window_->ui->TxFreqSpinBox_2};
    QSignalBlocker const rx {window_->ui->RxFreqSpinBox};
    QSignalBlocker const rx_copy {window_->ui->RxFreqSpinBox_2};
    window_->ui->TxFreqSpinBox->setValue (900);
    window_->ui->TxFreqSpinBox_2->setValue (900);
    window_->ui->RxFreqSpinBox->setValue (1100);
    window_->ui->RxFreqSpinBox_2->setValue (1100);
  }
  window_->m_settings->setValue ("TxFreq_old", 1200);
  window_->m_settings->setValue ("RxFreq_old", 1700);
  window_->ui->actionJTTY->trigger ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return window_->m_mode == "JTTY" && !window_->m_modeEntries.active ()
      && !state.pendingRx && !state.pendingTx
      && window_->ui->TxFreqSpinBox->value () == 1200
      && window_->ui->TxFreqSpinBox_2->value () == 1200
      && window_->ui->RxFreqSpinBox->value () == 1700
      && window_->ui->RxFreqSpinBox_2->value () == 1700;
  }, "JTTY entry did not restore the shared audio controls through its refresh callback.")) return false;
  window_->ui->TxFreqSpinBox->setValue (1300);
  window_->ui->RxFreqSpinBox->setValue (1800);
  auto const jtty_frequency = window_->m_operatingFrequency.rx ();
  window_->set_mode ("JTTY", ModeEntry::RetunePolicy::Preserve);
  if (!require (window_->m_mode == "JTTY" && window_->m_operatingFrequency.rx () == jtty_frequency
                && window_->ui->TxFreqSpinBox->value () == 1300
                && window_->ui->TxFreqSpinBox_2->value () == 1300
                && window_->ui->RxFreqSpinBox->value () == 1800
                && window_->ui->RxFreqSpinBox_2->value () == 1800,
                "Same-mode JTTY reconfiguration replaced the current shared audio offsets.")) return false;
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return !state.pendingRx && !state.pendingTx && !window_->m_modeEntries.active ();
  }, "JTTY reconfiguration did not settle before leaving the mode.")) return false;
  window_->m_settings->setValue ("TxFreq_old", saved_tx);
  window_->m_settings->setValue ("RxFreq_old", saved_rx);
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14074000, true)) return false;
  scenario_ = "mode-entry frequency commitment";
  window_->monitor (false);
  window_->m_operatingFrequency = OperatingFrequency {144174000};
  window_->ui->actionEcho->trigger ();
  if (!require (window_->m_pendingModeRetune.pending () && !window_->m_monitoring,
                "Echo entry did not retain its request while the RX frequency was unavailable.")) return false;
  Q_EMIT window_->m_config.frequency_observation (
    {window_->m_config.frequency_generation (), 0, RigFrequency::Path::Rx,
     0, RigFrequency::Basis::Readback, {}});
  if (!require (window_->m_pendingModeRetune.pending () && !window_->m_operatingFrequency.rx (),
                "An unavailable RX observation consumed the pending mode-entry request.")) return false;
  Q_EMIT window_->m_config.frequency_observation (
    {window_->m_config.frequency_generation (), 0, RigFrequency::Path::Rx,
     144174000, RigFrequency::Basis::Readback, {}});
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 144120000 && !state.pendingRx && !state.pendingTx
      && !window_->m_pendingModeRetune.pending ();
  }, "Usable RX evidence did not resume the pending Echo working-frequency request.")) return false;
  if (!require (!window_->m_monitoring, "Readiness resumed Echo with Monitor enabled.")) return false;
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (144174000, true)) return false;
  window_->ui->actionEcho->trigger ();
  if (!require (!window_->m_monitoring && !window_->ui->monitorButton->isChecked (),
                "Same-band Echo entry returned with Monitor enabled.")) return false;
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 144120000 && !state.pendingRx && !state.pendingTx;
  }, "Echo did not establish its final working frequency.")) return false;
  if (!require (!window_->m_monitoring && window_->ui->bandComboBox->currentText () == "2m",
                "Completing Echo's retune restarted Monitor or lost the band.")) return false;
  window_->monitor (true);
  if (!require (window_->m_monitoring, "Cannot start Echo monitoring before an external band change.")) return false;
  Q_EMIT window_->m_config.frequency_observation (
    {window_->m_config.frequency_generation (), 0, RigFrequency::Path::Rx,
     50200000, RigFrequency::Basis::Readback, {}});
  if (!require (!window_->m_monitoring && window_->processedOperatingBand () == "6m",
                "An external Echo band change did not stop Monitor.")) return false;

  window_->ui->actionFT8->trigger ();
  if (!require (window_->m_config.enable_VHF_features (), "Submode restore needs the VHF features.")) return false;
  auto const saved_jt9_submode = window_->m_settings->value ("SubMode", 0);
  auto const saved_jt65_submode = window_->m_settings->value ("SubMode_JT65", 0);
  window_->m_settings->setValue ("SubMode", 1);
  window_->m_settings->setValue ("SubMode_JT65", 0);
  for (auto const& mode : {QStringLiteral ("FreqCal"), QStringLiteral ("JT65")})
    {
      // Start from submode A so that restoring the saved submode emits valueChanged.
      window_->ui->actionFT8->trigger ();
      window_->ui->sbSubmode->setValue (0);
      (mode == "FreqCal" ? window_->ui->actionFreqCal : window_->ui->actionJT65)->trigger ();
      if (!waitUntil ([&] {return !window_->m_modeEntries.active ();},
                      "A mode entry with a saved JT9 submode did not finish.")) return false;
      if (!require (mode == window_->m_mode,
                    "Entering " + mode + " with saved JT9 submode B ended in another mode.")) return false;
      if (mode == "FreqCal" && !require (1 == window_->ui->sbSubmode->value (),
                                         "FreqCal entry did not restore the saved JT9 submode.")) return false;
    }
  window_->m_settings->setValue ("SubMode", saved_jt9_submode);
  window_->m_settings->setValue ("SubMode_JT65", saved_jt65_submode);
  window_->ui->actionFT8->trigger ();
  window_->ui->sbSubmode->setValue (0);

  frequencies->frequency_list ({item (10000000, Modes::FreqCal), item (15000000, Modes::FreqCal)});
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14998500)) return false;
  window_->ui->RxFreqSpinBox->setValue (1500);
  window_->ui->actionFreqCal->trigger ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9998500 && !state.pendingRx && !state.pendingTx;
  }, "FreqCal did not start at the first reference despite being near the later reference.")) return false;
  if (!require (window_->ui->RxFreqSpinBox->value () == 1500
                && window_->m_frequency_list_fcal_iter != frequencies->end ()
                && window_->m_frequency_list_fcal_iter->frequency_ == 10000000,
                "FreqCal's iterator or audio control disagrees with its initial reference.")) return false;
  window_->freqCalStep ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 14998500 && !state.pendingRx && !state.pendingTx;
  }, "FreqCal did not step from its initial reference to the second reference.")) return false;
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14998500)) return false;
  window_->ui->RxFreqSpinBox->setValue (1700);
  window_->ui->actionFreqCal->trigger ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9998500 && !state.pendingRx && !state.pendingTx;
  }, "FreqCal's initial reference depended on the prior audio control value.")) return false;
  if (!require (window_->ui->RxFreqSpinBox->value () == 1500,
                "Normal FreqCal entry retained the prior mode's audio offset.")) return false;
  window_->ui->RxFreqSpinBox->setValue (1700);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9998300 && !state.pendingRx && !state.pendingTx;
  }, "FreqCal did not establish the changed audio reference.")) return false;
  window_->set_mode ("FreqCal", ModeEntry::RetunePolicy::Preserve);
  if (!require (window_->ui->RxFreqSpinBox->value () == 1700
                && window_->m_operatingFrequency.rx () == 9998300,
                "Preserving FreqCal changed its dial/audio reference pair.")) return false;
  window_->freqCalStep ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 14998300 && !state.pendingRx && !state.pendingTx;
  }, "FreqCal did not advance to the reference that will be removed.")) return false;
  frequencies->frequency_list ({item (10000000, Modes::FreqCal)});
  window_->set_mode ("FreqCal", ModeEntry::RetunePolicy::Preserve);
  if (!require (window_->ui->RxFreqSpinBox->value () == 1700
                && window_->m_operatingFrequency.rx () == 14998300
                && window_->m_frequency_list_fcal_iter == frequencies->end (),
                "Preserving FreqCal retained a removed reference or changed its dial/audio pair.")) return false;
  window_->ui->RxFreqSpinBox->setValue (1800);
  if (!require (window_->m_operatingFrequency.rx () == 14998300
                && !window_->m_operatingFrequency.snapshot ().pendingRx,
                "Editing FreqCal audio retuned through a removed reference.")) return false;
  window_->freqCalStep ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9998200 && !state.pendingRx && !state.pendingTx;
  }, "FreqCal did not safely resume at the remaining reference after the list shrank.")) return false;

  frequencies->frequency_list ({item (10000000, Modes::FreqCal), item (15000000, Modes::FreqCal)});
  auto const monitoring_before_reference = window_->m_monitoring;
  window_->ui->RxFreqSpinBox->setValue (1000);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return !state.pendingRx && !state.pendingTx;
  }, "FreqCal did not settle after the audio change before choosing a reference.")) return false;
  int reference_row = -1;
  for (int row = 0; row < frequencies->rowCount () && reference_row < 0; ++row)
    {
      Frequency frequency;
      if (window_->workingFrequencyAt (row, frequency) && frequency == 15000000) reference_row = row;
    }
  if (!require (reference_row >= 0, "The FreqCal fixture lost its 15 MHz reference row.")) return false;
  window_->on_bandComboBox_activated (reference_row);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 14999000 && !state.pendingRx && !state.pendingTx;
  }, "Choosing a FreqCal reference did not put it at the Rx audio frequency.")) return false;
  if (!require (window_->m_frequency_list_fcal_iter != frequencies->end ()
                && window_->m_frequency_list_fcal_iter->frequency_ == 15000000,
                "Choosing a FreqCal reference left the calibration position elsewhere.")) return false;
  window_->freqCalStep ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9999000 && !state.pendingRx && !state.pendingTx;
  }, "The calibration cycle did not continue from the chosen FreqCal reference.")) return false;
  window_->on_bandComboBox_activated (reference_row);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 14999000 && !state.pendingRx && !state.pendingTx;
  }, "Choosing the FreqCal reference again did not put it at the Rx audio frequency.")) return false;
  window_->ui->RxFreqSpinBox->setValue (1200);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 14998800 && !state.pendingRx && !state.pendingTx;
  }, "A FreqCal audio change after choosing a reference did not keep that reference.")) return false;
  window_->ui->RxFreqSpinBox->setValue (1000);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 14999000 && !state.pendingRx && !state.pendingTx
      && window_->m_rigState.frequency () == state.correctedRx;
  }, "FreqCal did not return to the 1000 Hz audio reference.")) return false;
  if (!setFrequency (window_->m_operatingFrequency.rx () + 1000, true)) return false;
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 15000000 && !state.pendingRx && !state.pendingTx;
  }, "A 1 kHz FreqCal dial step onto a reference frequency was not a plain step.")) return false;
  window_->monitor (false);
  window_->ui->monitorButton->click ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return window_->m_monitoring && state.rx == 15000000 && !state.pendingRx && !state.pendingTx;
  }, "Restarting Monitor pulled a FreqCal dial set to a reference frequency back to reference - Rx.")) return false;
  if (!monitoring_before_reference) window_->monitor (false);

  auto const empty_list_frequency = window_->m_operatingFrequency.rx ();
  frequencies->frequency_list ({});
  window_->ui->actionFreqCal->trigger ();
  if (!require (window_->m_frequency_list_fcal_iter == frequencies->end ()
                && window_->m_operatingFrequency.rx () == empty_list_frequency
                && !window_->m_operatingFrequency.snapshot ().pendingRx,
                "FreqCal entry fabricated a target for an empty reference list.")) return false;
  window_->freqCalStep ();
  window_->ui->RxFreqSpinBox->setValue (1600);
  if (!require (window_->m_frequency_list_fcal_iter == frequencies->end ()
                && window_->m_operatingFrequency.rx () == empty_list_frequency
                && !window_->m_operatingFrequency.snapshot ().pendingRx,
                "An empty FreqCal reference list allowed an invalid step or audio retune.")) return false;

  // The "None" radio reports no ranges, so nothing is out of range until a test sets them.
  frequencies->frequency_list ({item (100000, Modes::FreqCal), item (10000000, Modes::FreqCal),
                                item (15000000, Modes::FreqCal)});
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14998500)) return false;
  if (!require (!window_->m_config.rig_ranges_pending ()
                && !window_->m_config.rig_cannot_tune (98500)
                && !window_->m_config.rig_cannot_tune (9998500),
                "The None radio reported ranges, or its ranges were still pending.")) return false;
  // Lower edge 150 kHz, so a 100 kHz reference is out of range.
  window_->m_config.set_rig_tuning_ranges_for_test ({{150000, 1500000000}});
  if (!require (window_->m_config.rig_cannot_tune (98500)
                && !window_->m_config.rig_cannot_tune (9998500)
                && window_->m_config.rig_cannot_tune (1600000000),
                "The set ranges did not reach the FreqCal checks.")) return false;
  window_->ui->actionFreqCal->trigger ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9998500 && !state.pendingRx && !state.pendingTx;
  }, "FreqCal entry did not skip the reference the radio cannot tune.")) return false;
  if (!require (window_->m_frequency_list_fcal_iter != frequencies->end ()
                && window_->m_frequency_list_fcal_iter->frequency_ == 10000000
                && window_->statusBar ()->currentMessage ().contains ("10.000 MHz"),
                "FreqCal did not name the first reference the radio can tune.")) return false;
  window_->freqCalStep ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 14998500 && !state.pendingRx && !state.pendingTx
      && window_->m_frequency_list_fcal_iter->frequency_ == 15000000;
  }, "FreqCal did not step to the second tunable reference.")) return false;
  window_->freqCalStep ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9998500 && !state.pendingRx && !state.pendingTx
      && window_->m_frequency_list_fcal_iter->frequency_ == 10000000;
  }, "The FreqCal cycle did not wrap past the reference the radio cannot tune.")) return false;

  frequencies->frequency_list ({item (100000, Modes::FreqCal), item (120000, Modes::FreqCal)});
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14998500)) return false;
  window_->statusBar ()->clearMessage ();
  window_->ui->actionFreqCal->trigger ();
  if (!require (window_->m_frequency_list_fcal_iter == frequencies->end ()
                && window_->m_operatingFrequency.rx () == 14998500
                && !window_->m_operatingFrequency.snapshot ().pendingRx
                && window_->statusBar ()->currentMessage ().startsWith ("No FreqCal reference"),
                "FreqCal entry requested a frequency when the radio can tune no reference.")) return false;
  window_->statusBar ()->clearMessage ();
  window_->freqCalStep ();
  if (!require (window_->m_frequency_list_fcal_iter == frequencies->end ()
                && window_->m_operatingFrequency.rx () == 14998500
                && !window_->m_operatingFrequency.snapshot ().pendingRx
                && window_->statusBar ()->currentMessage ().startsWith ("No FreqCal reference"),
                "The FreqCal cycle requested a reference the radio cannot tune.")) return false;

  // 151.6 kHz - 1500 Hz is in range; with Rx 1700 Hz the dial would fall below 150 kHz.
  frequencies->frequency_list ({item (151600, Modes::FreqCal), item (10000000, Modes::FreqCal)});
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14998500)) return false;
  window_->ui->actionFreqCal->trigger ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 150100 && !state.pendingRx && !state.pendingTx;
  }, "FreqCal did not start at a reference just inside the radio's range.")) return false;
  window_->ui->RxFreqSpinBox->setValue (1700);
  if (!require (window_->m_operatingFrequency.rx () == 150100
                && !window_->m_operatingFrequency.snapshot ().pendingRx
                && window_->ui->RxFreqSpinBox->value () == 1500
                && window_->ui->RxFreqSpinBox_2->value () == 1500,
                "An Rx change that leaves the radio's range retuned or left Rx out of step.")) return false;

  // The "Ignore range" box (FreqCal only) lets FreqCal use references below the range.
  frequencies->frequency_list ({item (100000, Modes::FreqCal), item (10000000, Modes::FreqCal)});
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14998500)) return false;
  if (!require (window_->ui->freqcal_range_check_box->isHidden (),
                "The Ignore range box is visible outside FreqCal.")) return false;
  window_->ui->actionFreqCal->trigger ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9998500 && !state.pendingRx && !state.pendingTx;
  }, "FreqCal entry did not skip the reference below the range before Ignore range is ticked.")) return false;
  if (!require (!window_->ui->freqcal_range_check_box->isHidden ()
                && !window_->ui->freqcal_range_check_box->isChecked ()
                && !window_->m_config.rig_range_override (),
                "The Ignore range box is not shown unchecked in FreqCal.")) return false;
  window_->statusBar ()->clearMessage ();
  window_->ui->freqcal_range_check_box->setChecked (true);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 98500 && !state.pendingRx && !state.pendingTx;
  }, "Ticking Ignore range did not restart FreqCal at the first reference.")) return false;
  if (!require (window_->m_config.rig_range_override ()
                && window_->m_frequency_list_fcal_iter != frequencies->end ()
                && window_->m_frequency_list_fcal_iter->frequency_ == 100000
                && !window_->statusBar ()->currentMessage ().contains ("radio's range"),
                "Ignore range did not select the first reference quietly.")) return false;
  window_->ui->freqcal_range_check_box->setChecked (false);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9998500 && !state.pendingRx && !state.pendingTx
      && window_->m_frequency_list_fcal_iter->frequency_ == 10000000;
  }, "Unticking Ignore range did not return FreqCal to the first reference in range.")) return false;
  if (!require (!window_->m_config.rig_range_override (),
                "Clearing Ignore range did not turn the override off.")) return false;

  // A saved "Ignore range" is shown checked on FreqCal entry and honoured there.
  window_->m_config.set_rig_range_override (true);
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14998500)) return false;
  window_->ui->actionFreqCal->trigger ();
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 98500 && !state.pendingRx && !state.pendingTx;
  }, "A saved Ignore range was not honoured on FreqCal entry.")) return false;
  if (!require (window_->ui->freqcal_range_check_box->isChecked (),
                "A saved Ignore range was not shown checked in FreqCal.")) return false;
  window_->ui->freqcal_range_check_box->setChecked (false);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == 9998500 && !state.pendingRx && !state.pendingTx;
  }, "Clearing the saved Ignore range did not return FreqCal to a reference in range.")) return false;

  // Unknown ranges (no ranges reported) never make a reference unreachable.
  window_->m_config.set_rig_tuning_ranges_for_test ({});
  if (!require (!window_->m_config.rig_cannot_tune (98500) && !window_->m_config.rig_cannot_tune (1600000000),
                "A rig without ranges had a reference treated as out of range.")) return false;

  frequencies->frequency_list (Frequencies::default_items ());
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14091000, true)) return false;
  window_->on_houndButton_clicked (true);
  if (!require (window_->m_specOp == MainWindow::SpecOp::HOUND
                && window_->m_operatingFrequency.rx () == 14091000
                && !window_->m_operatingFrequency.snapshot ().pendingRx,
                "Entering Hound replaced the nonstandard operating frequency.")) return false;
  window_->on_houndButton_clicked (false);
  if (!setFrequency (14074000, true)) return false;
  if (!require (window_->m_config.enable_VHF_features () && !window_->ui->cbFast9->isChecked (),
                "The remote JT9 fixture requires VHF features and initially disabled fast mode.")) return false;
  window_->remote_configure ("JT9", 50, "E", true, 15, 1200, {}, {}, false);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return !state.pendingRx && !state.pendingTx && !window_->m_modeEntries.active ();
  }, "Remote JT9 configuration did not finish its mode-entry request.")) return false;
  if (!require (window_->m_mode == "JT9" && window_->m_bFast9
                && window_->ui->cbFast9->isChecked () && window_->ui->sbSubmode->value () == 4
                && window_->m_TRperiod == 15 && window_->ui->RxFreqSpinBox->value () == 1200,
                "Fast JT9 setup overwrote the remote configuration's submode, period, or RX audio frequency.")) return false;
  window_->ui->actionFT8->trigger ();
  if (!setFrequency (14074000, true)) return false;
  bool gui_tick = false;
  auto const tick = connect (&window_->m_guiTimer, &QTimer::timeout, this, [&] {gui_tick = true;});
  window_->m_guiTimer.start (100);
  window_->ui->actionQ65->trigger ();
  auto const ticked = waitUntil ([&] {return gui_tick;}, "The normal GUI scheduler did not run.");
  window_->m_guiTimer.stop ();
  disconnect (tick);
  if (!ticked || !require (window_->ui->bandComboBox->currentText () == "20m"
                           && window_->nominalRxBand () == "20m",
                           "A normal GUI update changed the authoritative operating band.")) return false;
  window_->ui->actionFT8->trigger ();
  return setFrequency (14074000, true);
}

bool OperatingBandApplicationTestController::mskModeExit ()
{
  auto * frequencies = window_->m_config.frequencies ();
  frequencies->frequency_list ({item (50280000, Modes::MSK144), item (144360000, Modes::MSK144),
                                item (50313000, Modes::FT8), item (50318000, Modes::FT4),
                                item (50275000, Modes::Q65), item (50276000, Modes::JT65),
                                item (50278000, Modes::JT9)});
  struct Destination
  {
    QString mode;
    QAction * action;
    Frequency frequency;
    double period;
  };
  std::array<Destination, 4> const destinations {{
    {"FT8", window_->ui->actionFT8, 50313000, 15},
    {"FT4", window_->ui->actionFT4, 50318000, 7.5},
    {"Q65", window_->ui->actionQ65, 50275000, 30},
    {"JT65", window_->ui->actionJT65, 50276000, 60}
  }};
  QScopedValueRollback<int> remembered_period {m_msk144_tr6, 5};
  Frequency held_frequency {50280100};
  for (auto const& destination : destinations)
    for (bool pending : {false, true})
      {
        scenario_ = QString {"MSK144 to %1 with %2 radio work"}.arg (
          destination.mode, pending ? "pending" : "settled");
        window_->m_settings->setValue ("TRPeriod_Q65", 30);
        window_->ui->actionMSK144->trigger ();
        if (!setFrequency (144360000, true)) return false;
        window_->ui->sbTR->setValue (30);
        m_msk144_tr6 = 5;
        HeldTimerExpiry restore {window_->m_bandMskTimer};
        if (!setFrequency (50280000, true)) return false;
        if (destination.mode == "FT8" && !pending
            && !waitUntil ([&] {return restore.intercepted ();},
                           "The held MSK144 restore did not reach its timer delivery checkpoint.")) return false;
        if (!require (window_->m_mode == "MSK144" && restore.hold (),
                      "The MSK144 exit fixture did not arm its band-period restore callback.")) return false;

        QVector<RigFrequency::Request> held;
        if (pending)
          {
            auto const capture = [&held] (RigFrequency::Request const& request) {
              held.append (request);
              return true;
            };
            if (!require (window_->m_operatingFrequency.requestNominal (held_frequency, capture)
                          && window_->m_operatingFrequency.requestTx (held_frequency + 100, capture)
                          && held.size () == 2
                          && window_->m_operatingFrequency.snapshot ().pendingRx
                          && window_->m_operatingFrequency.snapshot ().pendingTx,
                          "The exit fixture did not accept and hold both radio paths.")) return false;
            held_frequency += 1000;
          }
        bool target_requested = false;
        bool intermediate_jt9 = false;
        QObject requests;
        connect (&window_->m_config, &Configuration::frequency_result, &requests,
                 [&] (RigFrequency::Result const& result) {
          if (result.request.path != RigFrequency::Path::Rx) return;
          if (result.request.target == destination.frequency) target_requested = true;
          if (result.request.target == 50278000) intermediate_jt9 = true;
        });
        destination.action->trigger ();
        if (!require (window_->m_mode == destination.mode
                      && window_->m_TRperiod == destination.period
                      && !window_->m_modeEntries.active (),
                      "Leaving MSK144 did not finish the destination mode setup.")) return false;
        if (pending)
          {
            if (!require (window_->m_pendingModeRetune.pending () && !target_requested,
                          "Mode entry did not wait for the pending radio paths.")) return false;
            for (auto const& request : held)
              Q_EMIT window_->m_config.frequency_result (
                {request, RigFrequency::Outcome::Pending, RigFrequency::Basis::Retained, 0});
            if (!require (window_->m_pendingModeRetune.pending () && !target_requested,
                          "Request acceptance was mistaken for radio readiness.")) return false;
            Q_EMIT window_->m_config.frequency_result (
              {held.at (0), RigFrequency::Outcome::Established,
               RigFrequency::Basis::Readback, held.at (0).target});
            if (!require (window_->m_pendingModeRetune.pending () && !target_requested
                          && !window_->m_operatingFrequency.snapshot ().pendingRx
                          && window_->m_operatingFrequency.snapshot ().pendingTx,
                          "RX readiness incorrectly released a mode retune while TX was pending.")) return false;
            Q_EMIT window_->m_config.frequency_result (
              {held.at (1), RigFrequency::Outcome::Established,
               RigFrequency::Basis::Readback, held.at (1).target});
          }
        if (!waitUntil ([&] {
          auto const state = window_->m_operatingFrequency.snapshot ();
          return state.rx == destination.frequency && !state.pendingRx && !state.pendingTx
            && !window_->m_pendingModeRetune.pending ()
            && window_->m_rigState.frequency () == state.correctedRx;
        }, "Leaving MSK144 did not establish the destination working frequency.")) return false;
        if (!require (target_requested && !intermediate_jt9
                      && window_->m_msk144basefreq == 0
                      && window_->ui->bandComboBox->currentText () == "6m"
                      && window_->m_bandReportingTimer.interval () == int (600 * destination.period)
                      && window_->m_bandApTimer.interval () == int (1500 * destination.period),
                      "Leaving MSK144 committed an intermediate frequency or stale mode effects.")) return false;
        auto const frequency = window_->m_operatingFrequency.rx ();
        auto const period_control = window_->ui->sbTR->value ();
        if (!require (QMetaObject::invokeMethod (&window_->m_bandMskTimer, "timeout", Qt::DirectConnection)
                      && window_->m_TRperiod == destination.period
                      && window_->ui->sbTR->value () == period_control
                      && window_->m_operatingFrequency.rx () == frequency,
                      "A late MSK144 restore callback changed the destination mode.")) return false;

        bool gui_tick = false;
        QObject checkpoint;
        connect (&window_->m_guiTimer, &QTimer::timeout, &checkpoint, [&] {gui_tick = true;});
        window_->m_guiTimer.start (100);
        auto const ticked = waitUntil ([&] {return gui_tick;}, "The exit checkpoint did not run the normal GUI update.");
        window_->m_guiTimer.stop ();
        if (!ticked || !require (window_->m_mode == destination.mode
                                && window_->m_TRperiod == destination.period
                                && window_->m_operatingFrequency.rx () == frequency
                                && window_->ui->bandComboBox->currentText () == "6m",
                                "The destination mode changed after the normal GUI update.")) return false;
      }
  frequencies->frequency_list (Frequencies::default_items ());
  window_->ui->actionFT8->trigger ();
  return setFrequency (14074000, true);
}

bool OperatingBandApplicationTestController::modalObservation ()
{
  scenario_ = "radio observations and queued mode entry during a modal dialog";
  if (!setFrequency (14074000, true)) return false;
  window_->ui->TxFreqSpinBox->setValue (1600);
  if (!waitUntil ([&] {return !window_->m_operatingFrequency.snapshot ().pendingTx;},
                   "The initial TX audio adjustment did not settle.")) return false;
  bool entered = false;
  bool observed = false;
  bool queued = false;
  bool timed_out = false;
  QTimer watchdog;
  watchdog.setSingleShot (true);
  connect (&watchdog, &QTimer::timeout, this, [&] {
    timed_out = true;
    window_->m_rigErrorMessageBox.done (0);
  });
  auto const setup = connect (window_->ui->TxFreqSpinBox, QOverload<int>::of (&QSpinBox::valueChanged),
    this, [&] (int value) {
      if (entered || value != 1500 || window_->m_mode != "Echo") return;
      entered = true;
      QTimer::singleShot (0, &watchdog, [&] {
        auto const in_operation = window_->m_modeEntries.active ();
        Q_EMIT window_->m_config.frequency_observation (
          {window_->m_config.frequency_generation (), 0, RigFrequency::Path::Rx,
           7076000, RigFrequency::Basis::Readback, {}});
        observed = window_->m_rigErrorMessageBox.isVisible () && in_operation
          && window_->m_operatingFrequency.rx () == 7076000
          && window_->processedOperatingBand () == "40m"
          && window_->ui->bandComboBox->currentText () == "40m";
        window_->ui->actionFT4->trigger ();
        queued = window_->m_modeEntries.active () && window_->m_mode == "Echo";
        window_->m_rigErrorMessageBox.done (0);
      });
      window_->rigFailure ("Operating-band integration test: injected radio fault during mode setup.");
    });
  {
    QScopedValueRollback<bool> error {window_->m_first_error, false};
    watchdog.start (5000);
    window_->ui->actionEcho->trigger ();
    watchdog.stop ();
  }
  disconnect (setup);
  if (!require (entered && observed && queued && !timed_out,
                "The modal loop deferred radio effects or executed a nested mode request prematurely.")) return false;
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return window_->m_mode == "FT4" && state.rx == 7047500 && !state.pendingRx && !state.pendingTx;
  }, "The queued mode request did not complete after the modal operation finished.")) return false;
  window_->ui->actionFT8->trigger ();
  return setFrequency (14074000, true);
}

bool OperatingBandApplicationTestController::transitions ()
{
  scenario_ = "accepted and observed operating transitions";
  if (!setFrequency (7074000, true)) return false;
  if (!require (window_->operatingBand () == "40m" && window_->processedOperatingBand () == "40m"
                && window_->m_bandReportingTimer.isActive () && !m_displayBand && no_decodes_to_UDP,
                "A cross-band request did not apply its operational effects.")) return false;
  if (!require (window_->m_config.pwrBandTxMemory () && window_->m_config.pwrBandTuneMemory ()
                && window_->ui->outAttenuation->value () == 93
                && window_->m_pwrBandTxMemory.value ("20m").toInt () == 17,
                "The transition did not restore the target band's attenuation memory.")) return false;
  window_->monitor (false);
  if (!setFrequency (14074000)) return false;
  if (!require (!window_->m_monitoring && window_->processedOperatingBand () == "20m",
                "An offline request depended on a later rig report to update its band.")) return false;

  Q_EMIT window_->m_config.transceiver_update (window_->m_rigState);
  if (!require (window_->m_rigState.online () && !window_->m_operatingFrequency.snapshot ().pendingRx,
                "External evidence lacks a ready online rig context.")) return false;
  Q_EMIT window_->m_config.frequency_observation (
    {window_->m_config.frequency_generation (), 0, RigFrequency::Path::Rx,
     7076000, RigFrequency::Basis::Readback, {}});
  if (!require (window_->m_operatingFrequency.rx () == 7076000
                && window_->processedOperatingBand () == "40m",
                "External frequency evidence did not apply a band transition.")) return false;

  window_->monitor (true);
  if (!setFrequency (14349900, true)) return false;
  window_->m_operatingFrequency.setDesiredCorrection (200, 0);
  if (!require (window_->reapplyCurrentRigFrequencyCorrection (), "Cannot request the correction crossing.")) return false;
  if (!waitUntil ([&] {
    return window_->m_operatingFrequency.correctedRx () == 14350100
      && !window_->m_operatingFrequency.snapshot ().pendingRx;
  }, "The correction crossing did not become established.")) return false;
  if (!require (window_->m_operatingFrequency.rx () == 14349900 && window_->operatingBand ().isEmpty ()
                && window_->ui->bandComboBox->currentText () == "OOB",
                "An established correction crossing did not update the effective band.")) return false;
  window_->m_operatingFrequency.setDesiredCorrection (0, 0);
  if (!require (window_->reapplyCurrentRigFrequencyCorrection (), "Cannot restore the RX correction.")) return false;
  if (!waitUntil ([&] {
    return window_->m_operatingFrequency.correctedRx () == 14349900
      && !window_->m_operatingFrequency.snapshot ().pendingRx;
  }, "The RX correction did not settle.")) return false;

  auto transmitting = window_->m_rigState;
  transmitting.ptt (true);
  transmitting.split (true);
  transmitting.tx_frequency (7074000);
  Q_EMIT window_->m_config.transceiver_update (transmitting);
  Q_EMIT window_->m_config.frequency_observation (
    {window_->m_config.frequency_generation (), 0, RigFrequency::Path::Tx,
     7074000, RigFrequency::Basis::Readback, {}});
  if (!require (window_->displayedDialFrequency () == 7074000 && window_->operatingBand () == "40m"
                && window_->m_operatingFrequency.rx () == 14349900,
                "Split TX did not use the established TX band independently of nominal RX.")) return false;
  transmitting.ptt (false);
  Q_EMIT window_->m_config.transceiver_update (transmitting);
  if (!require (window_->operatingBand () == "20m" && window_->processedOperatingBand () == "20m",
                "PTT release did not restore the RX operating band.")) return false;

  window_->ui->actionMSK144->trigger ();
  if (!setFrequency (144360000, true)) return false;
  {
    QScopedValueRollback<int> remembered_period {m_msk144_tr6, 5};
    window_->ui->sbTR->setValue (30);
    m_msk144_tr6 = 5;
    window_->m_bandMskTimer.stop ();
    bool expired = false;
    QObject expiry;
    connect (&window_->m_bandMskTimer, &QTimer::timeout, &expiry, [&] {expired = true;});
    if (!setFrequency (50260000, true)) return false;
    if (!waitUntil ([&] {
      return expired && window_->ui->sbTR->value () == 5 && window_->m_TRperiod == 5;
    }, "The registered MSK144 band-period callback did not restore its captured period.")) return false;
  }
  auto const base = window_->m_msk144basefreq;
  auto const line = QString {"120000 -10  0.3 2300 &  CQ W1AW FN31"};
  window_->handleDecodeSelection (line, Qt::ControlModifier | Qt::AltModifier,
    DecodedMessageReaction::SelectionOrigin::ManualRightPane, "W1AW");
  if (!waitUntil ([&] {
    return window_->m_operatingFrequency.rx () == base + 800
      && !window_->m_operatingFrequency.snapshot ().pendingRx;
  }, "The MSK144 Ctrl-selection did not establish its temporary QSY.")) return false;
  if (!require (window_->m_msk144basefreq == base,
                "A temporary MSK144 QSY overwrote the return frequency.")) return false;
  m_band_changed = false;
  window_->on_txrb6_toggled (true);
  if (!waitUntil ([&] {
    return window_->m_operatingFrequency.rx () == base
      && !window_->m_operatingFrequency.snapshot ().pendingRx;
  }, "MSK144 did not return to its saved base frequency.")) return false;
  window_->handleDecodeSelection (line, Qt::ControlModifier | Qt::AltModifier,
    DecodedMessageReaction::SelectionOrigin::ManualRightPane, "W1AW");
  if (!waitUntil ([&] {
    return window_->m_operatingFrequency.rx () == base + 800
      && !window_->m_operatingFrequency.snapshot ().pendingRx;
  }, "Cannot establish a second temporary MSK144 QSY.")) return false;
  {
    QScopedValueRollback<int> remembered_period {m_msk144_tr2, 15};
    HeldTimerExpiry restore {window_->m_bandMskTimer};
    if (!setFrequency (144360000, true)) return false;
    if (!require (window_->m_msk144basefreq == 144360000 && restore.hold (),
                  "An explicit MSK144 band selection retained the obsolete temporary-QSY base.")) return false;
    window_->ui->sbTR->setValue (30);
    if (!require (QMetaObject::invokeMethod (&window_->m_bandMskTimer, "timeout", Qt::DirectConnection)
                  && window_->ui->sbTR->value () == 30 && window_->m_TRperiod == 30,
                  "A stale MSK144 band-period callback overwrote the user's period change.")) return false;
  }
  m_band_changed = false;
  window_->on_txrb6_toggled (true);
  if (!require (window_->m_operatingFrequency.rx () == 144360000
                && !window_->m_operatingFrequency.snapshot ().pendingRx,
                "MSK144 Tx6 returned to a base superseded by an explicit band selection.")) return false;
  window_->ui->actionFT8->trigger ();
  return setFrequency (14074000, true);
}

bool OperatingBandApplicationTestController::reporting ()
{
  scenario_ = "reporting timer dispatch";
  if (!setFrequency (7074000)) return false;
  if (!require (window_->m_bandReportingTimer.isActive () && !m_displayBand && no_decodes_to_UDP,
                "An accepted retune did not arm reporting suppression.")) return false;
  if (!require (QMetaObject::invokeMethod (&window_->m_bandReportingTimer, "timeout", Qt::DirectConnection),
                "Cannot dispatch the registered reporting timeout.")) return false;
  return require (m_displayBand && !no_decodes_to_UDP && window_->m_freqNominalPeriod == 7074000
                  && window_->m_currentBandPeriod == "40m" && !window_->m_bandReportingTimer.isActive (),
                  "The registered reporting callback did not publish its RX frequency and band.");
}

bool OperatingBandApplicationTestController::hopping ()
{
  scenario_ = "multi-mode and beacon band hopping";
  for (auto * checkbox : {window_->ui->cb160m, window_->ui->cb80m, window_->ui->cb60m,
       window_->ui->cb40m, window_->ui->cb30m, window_->ui->cb20m, window_->ui->cb17m,
       window_->ui->cb15m, window_->ui->cb12m, window_->ui->cb10m, window_->ui->cb6m,
       window_->ui->cb4m, window_->ui->cb2m, window_->ui->cb70cm, window_->ui->cb80mFT4,
       window_->ui->cb40mFT4, window_->ui->cb30mFT4, window_->ui->cb20mFT4,
       window_->ui->cb17mFT4, window_->ui->cb15mFT4, window_->ui->cb12mFT4,
       window_->ui->cb10mFT4, window_->ui->cb2mMSK, window_->ui->cbQRG1,
       window_->ui->cbQRG2, window_->ui->cbQRG3, window_->ui->cbQRG4,
       window_->ui->cbQRG5, window_->ui->cbQRG6, window_->ui->cbQRG7, window_->ui->cbQRG8})
    checkbox->setChecked (false);
  window_->ui->cb40mFT4->setChecked (true);
  window_->ui->pbBandHopping->setChecked (true);
  window_->bandHopping (true);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return window_->m_mode == "FT4" && state.rx == 7047500 && !state.pendingRx && !state.pendingTx;
  }, "The FT8-to-FT4 hop did not establish its target mode and frequency.")) return false;
  if (!require (window_->processedOperatingBand () == "40m" && window_->ui->bandComboBox->currentText () == "40m"
                && window_->m_bandReportingTimer.interval () == int (600.0 * window_->m_TRperiod),
                "The multi-mode hop completed its band effects before target-mode setup.")) return false;
  window_->ui->pbBandHopping->setChecked (false);

  window_->ui->actionEcho->trigger ();
  if (!require (!window_->m_monitoring,
                "Echo returned with Monitor enabled before the custom-frequency hop.")) return false;
  window_->ui->cb40mFT4->setChecked (false);
  window_->ui->cbQRG1->setChecked (true);
  window_->ui->sbQRG1->setValue (14123);
  window_->ui->pbBandHopping->setChecked (true);
  window_->bandHopping (true);
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return window_->m_mode == "FT8" && state.rx == 14123000 && !state.pendingRx && !state.pendingTx;
  }, "The custom-frequency hop did not leave Echo at its requested frequency.")) return false;
  if (!require (window_->m_monitoring
                && window_->m_bandReportingTimer.interval () == int (600.0 * window_->m_TRperiod),
                "The custom-frequency hop applied Echo monitoring or period behavior after mode setup.")) return false;
  window_->ui->pbBandHopping->setChecked (false);

  window_->ui->actionWSPR->trigger ();
  if (!waitUntil ([&] {return !window_->m_operatingFrequency.snapshot ().pendingRx;},
                   "Entering WSPR left a frequency request pending.")) return false;
  auto const row = window_->m_config.frequencies ()->best_working_frequency (Frequency {7074000});
  Frequency target {0};
  if (!require (window_->workingFrequencyAt (row, target), "The beacon-hop fixture lacks a 40m row.")) return false;
  QTemporaryDir helper_directory;
  if (!require (helper_directory.isValid (), "Cannot isolate the beacon hardware hook.")) return false;
#ifdef Q_OS_WIN
  QFile helper {helper_directory.filePath ("user_hardware.bat")};
  QByteArray const script {"@exit /b 0\r\n"};
#else
  QFile helper {helper_directory.filePath ("user_hardware")};
  QByteArray const script {"#!/bin/sh\nexit 0\n"};
#endif
  if (!require (helper.open (QIODevice::WriteOnly) && helper.write (script) == script.size (),
                "Cannot prepare the isolated beacon hardware hook.")) return false;
  helper.close ();
  if (!require (helper.setPermissions (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
                "Cannot make the isolated beacon hook executable.")) return false;
  auto const previous_environment = window_->p3.processEnvironment ();
  auto environment = QProcessEnvironment::systemEnvironment ();
#ifdef Q_OS_WIN
  environment.insert ("PATH", helper_directory.path () + ";" + environment.value ("SystemRoot") + "/System32");
#else
  environment.insert ("PATH", helper_directory.path () + ":/usr/bin:/bin");
#endif
  window_->p3.setProcessEnvironment (environment);
  BeaconTx::HoppingProposal proposal;
  proposal.frequenciesIndex = row;
  proposal.periodName = "test";
  if (!require (window_->applyBeaconBandChange (proposal), "The beacon hop was rejected.")) return false;
  if (!waitUntil ([&] {
    auto const state = window_->m_operatingFrequency.snapshot ();
    return state.rx == target && !state.pendingRx && !state.pendingTx
      && window_->p3.state () == QProcess::NotRunning;
  }, "The beacon hop or isolated hardware hook did not complete.")) return false;
  window_->p3.setProcessEnvironment (previous_environment);
  if (!require (window_->processedOperatingBand () == "40m" && window_->ui->bandComboBox->currentText () == "40m"
                && window_->p3.exitStatus () == QProcess::NormalExit && window_->p3.exitCode () == 0,
                "The beacon hop lost its operating-band state or hardware-hook isolation.")) return false;
  window_->ui->actionFT8->trigger ();
  return setFrequency (14074000, true);
}

bool OperatingBandApplicationTestController::require (bool condition, QString const& message)
{
  if (!condition) finish (false, scenario_ + ": " + message);
  return condition;
}

void OperatingBandApplicationTestController::finish (bool success, QString const& message)
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
  std::cerr << "WSJT-X operating-band integration: " << message.toStdString () << std::endl;
  QCoreApplication::exit (success ? EXIT_SUCCESS : EXIT_FAILURE);
}
