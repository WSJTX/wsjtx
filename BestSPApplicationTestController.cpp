// SPDX-License-Identifier: GPL-3.0-or-later
#include "BestSPApplicationTestController.hpp"

#include <cstdlib>
#include <algorithm>
#include <functional>
#include <iostream>
#include <utility>

#include <QAction>
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTranslator>
#include <QVector>

#include "Audio/AudioInputSource.hpp"
#include "Audio/AudioOutputSink.hpp"
#include "DecodeClassification.hpp"
#include "DecoderBackend.hpp"
#include "Transceiver/TransceiverFactory.hpp"
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

namespace
{
  class ScriptedDecoderBackend final : public DecoderBackend
  {
  public:
    bool schedulesDecodes () const override {return false;}
    void start (LaunchSettings const&) override
    {
      running_ = true;
      Q_EMIT processStarted ();
      Q_EMIT launched ();
      Q_EMIT becameReady ();
    }
    void restart () override
    {
      Q_EMIT restartInitiated ();
      abandonDecode ();
      Q_EMIT launched ();
      Q_EMIT becameReady ();
    }
    void prepareForClose () override {abandonDecode ();}
    void shutdownAtClose () override {running_ = false;}
    bool ready () const override {return running_;}
    bool running () const override {return running_;}
    bool restartInProgress () const override {return false;}
    void beginInput () override {}
    void acceptAudio (short const *, int) override {}
    void applySettings (decoder_params_t const&) override {}
    bool submit (Audio) override
    {
      if (!running_ || active_) return false;
      active_ = ++next_;
      return true;
    }
    std::unique_ptr<CapturedDecode> capture () override {return {};}
    bool submit (CapturedDecode const&) override {return false;}
    void abandonDecode () override {active_ = 0; pending_.clear ();}
    void discardRetainedAudio () override {}
    RetainedAudio retainedAudio () const override {return {};}
    qint64 activeRequest () const override {return active_;}
    bool hasPendingOutput () const override {return !pending_.isEmpty ();}
    void deliverOutput (OutputHandler const& handler) override
    {
      while (!pending_.isEmpty ())
        {
          auto output = pending_.takeFirst ();
          if (output.current && output.kind == Output::Kind::CycleFinished)
            active_ = 0;
          handler (output);
        }
    }
    bool activeProgress (qint32& progress) const override {progress = 0; return active_;}
    QString failureDetail (QString const& reason) override {return reason;}
    QByteArray diagnosticsTail () override {return {};}
    QString diagnostics () const override {return QStringLiteral ("scripted Best S+P backend");}

    void record (QString const& payload, int snr = -10, int frequency = 1900,
                 QString const& time = "120000")
    {
      Output output;
      output.kind = Output::Kind::Record;
      output.current = true;
      output.request = active_;
      output.text = (QString {"%1 %2  0.3 %3 +  "}.arg (time).arg (snr, 3).arg (frequency, 4)
                     + payload + '\n').toUtf8 ();
      deliver (output);
    }
    void complete (bool stale = false)
    {
      Output output;
      output.kind = Output::Kind::CycleFinished;
      output.current = !stale;
      output.request = stale ? active_ - 1 : active_;
      output.counts.decoded = 1;
      deliver (output);
    }

  private:
    void deliver (Output const& output)
    {
      pending_.append (output);
      Q_EMIT outputAvailable ();
    }
    bool running_ {false};
    qint64 active_ {0};
    qint64 next_ {0};
    QVector<Output> pending_;
  };

  class SilentAudioInput final : public AudioInputSource
  {
  public:
    void start (QAudioDeviceInfo const&, int, AudioDevice *, unsigned,
                AudioDevice::Channel) override {}
    void suspend () override {}
    void resume () override {}
    void stop () override {}
    void reset (bool) override {}
  };

  class SilentAudioOutput final : public AudioOutputSink
  {
  public:
    void setFormat (QAudioDeviceInfo const&, unsigned, int) override {}
    void restart (QIODevice *, TxEvidence::TxPlaybackContext, qint64) override {}
    void suspend () override {}
    void resume () override {}
    void reset () override {}
    void stop () override {}
    void setAttenuation (qreal) override {}
    void resetAttenuation () override {}
    int bufferSize () const override {return 0;}
  };
}

std::unique_ptr<DecoderBackend> makeBestSPTestBackend ()
{
  return std::make_unique<ScriptedDecoderBackend> ();
}

std::unique_ptr<AudioInputSource> makeBestSPTestAudioInput ()
{
  return std::make_unique<SilentAudioInput> ();
}

std::unique_ptr<AudioOutputSink> makeBestSPTestAudioOutput ()
{
  return std::make_unique<SilentAudioOutput> ();
}

BestSPTestEnvironment::BestSPTestEnvironment ()
  : settingsPath_ {QDir {QStandardPaths::writableLocation (QStandardPaths::ConfigLocation)}
      .filePath (QCoreApplication::applicationName () + ".ini")}
  , dataPath_ {QStandardPaths::writableLocation (QStandardPaths::DataLocation)}
{
}

BestSPTestEnvironment::~BestSPTestEnvironment ()
{
  if (QDir::isAbsolutePath (settingsPath_)) QFile::remove (settingsPath_);
  if (QDir::isAbsolutePath (dataPath_)
      && QFileInfo {dataPath_}.fileName () == QCoreApplication::applicationName ())
    QDir {dataPath_}.removeRecursively ();
}

QString BestSPApplicationTestController::prepareSettings (QSettings * settings)
{
  settings->clear ();
  settings->setValue ("Configuration/MyCall", "K1ABC");
  settings->setValue ("Configuration/MyGrid", "FN42");
  settings->setValue ("Configuration/Rig", TransceiverFactory::basic_transceiver_name_);
  settings->setValue ("Configuration/BestSPNewDXCC", true);
  settings->setValue ("Configuration/BestSPNewCallOnBand", true);
  auto highlights = DecodeHighlightingModel::default_items ();
  std::reverse (highlights.begin (), highlights.end ());
  for (auto& highlight : highlights) highlight.enabled_ = false;
  settings->setValue ("Configuration/DecodeHighlighting", QVariant::fromValue (highlights));
  settings->setValue ("Configuration/DXCCEntity", false);
  settings->setValue ("Configuration/UDPServer", "");
  settings->setValue ("Configuration/PSKReporter", false);
  settings->setValue ("Tune/Mode", "FT4");
  settings->setValue ("Tune/Monitor", false);
  settings->setValue ("Common/Splash_v1.7", false);
  settings->sync ();
  QDir const data {QStandardPaths::writableLocation (QStandardPaths::DataLocation)};
  if (!QDir {}.mkpath (data.path ())) return "Cannot create isolated Best S+P data directory.";
  QFile countries {data.filePath ("cty.dat")};
  if (!countries.open (QIODevice::WriteOnly | QIODevice::Text)) return countries.errorString ();
  QByteArray const contents {
    "United States:05:08:NA:38:97:5:K\nK,W,N;\n"
    "Japan:25:45:AS:36:-138:-9:JA\nJA;\n"};
  if (countries.write (contents) != contents.size ()) return countries.errorString ();
  return {};
}

BestSPApplicationTestController::BestSPApplicationTestController (
  MainWindow * window, DecoderBackend * backend, QObject * parent)
  : QObject {parent}, window_ {window}, backend_ {backend}
{
  retry_.setSingleShot (true);
  retry_.setInterval (20);
  connect (&retry_, &QTimer::timeout, this, [this] {prepare ();});
  timeout_.setSingleShot (true);
  timeout_.setInterval (10000);
  connect (&timeout_, &QTimer::timeout, this, [this] {
    finish (false, "Timed out preparing the isolated logbook and dummy radio.");
  });
}

void BestSPApplicationTestController::begin ()
{
  window_->m_guiTimer.stop ();
  timeout_.start ();
  prepare ();
}

void BestSPApplicationTestController::prepare ()
{
  if (finished_) return;
  if (!backend_->ready () || window_->m_logBook.countries ()->lookup ("JA1ABC")
      .identity (AD1CCty::EntityView::DXCC).name.isEmpty ())
    {
      retry_.start ();
      return;
    }
  if (!require (window_->m_config.is_dummy_rig (), "Test requires the dummy radio.")) return;
  if (!require (window_->m_config.transceiver_online (), "Cannot open the dummy radio.")) return;
  if (!window_->m_rigState.online ())
    {
      retry_.start ();
      return;
    }
  // Settings processes queued rig reports while the scripted decoder is active.
  if (window_->m_rigState.frequency () != 14080000)
    {
      if (!require (window_->m_config.transceiver_frequency (
                      14080000, RigFrequencyChangePolicy::ChangeKind::NominalQsy),
                    "Cannot initialize the dummy radio frequency.")) return;
      retry_.start ();
      return;
    }
  timeout_.stop ();
  window_->on_actionFT4_triggered ();
  window_->m_guiTimer.stop ();
  if (!require (window_->m_logBook.add ("W1AAA", "FN31", "20m", "FT4",
                 "<call:5>W1AAA <gridsquare:4>FN31 <band:3>20m <mode:3>FT4"),
                "Cannot initialize isolated worked-before history.")) return;
  if (run ()) finish (true, "All Best S+P application scenarios passed.");
}

bool BestSPApplicationTestController::startRequest (quint64 frequency, QString const& scenario)
{
  scenario_ = scenario;
  window_->setBestSPArmed (false);
  window_->m_operatingFrequency = OperatingFrequency {frequency};
  window_->m_operatingFrequency.requestNominal (frequency, {}, false);
  window_->m_freqNominalPeriod = frequency;
  window_->m_currentBand = window_->m_config.bands ()->find (frequency);
  window_->m_currentBandPeriod = window_->m_currentBand;
  window_->m_dateTimeSeqStart = QDateTime::currentDateTimeUtc ();
  window_->m_diskData = false;
  window_->m_QSOProgress = QsoProgress::Calling;
  window_->m_auto = false;
  window_->m_tune = false;
  window_->m_transmitting = false;
  window_->m_tx_when_ready = false;
  window_->ptt1Timer.stop ();
  window_->ptt0Timer.stop ();
  window_->m_autoRespondPeriodState = {};
  window_->cancelAutomaticCQSelection ();
  window_->m_autoRespondScores.reset ();
  window_->m_autoRespondSelectionLatch.clear ();
  window_->m_bCallingCQ = false;
  window_->m_bAutoReply = false;
  pounce = false;
  wait_and_call = false;
  no_wait_and_call = false;
  g_iptt = 0;
  m_txing = false;
  window_->ui->autoButton->setChecked (false);
  window_->ui->dxCallEntry->clear ();
  window_->m_hisCall.clear ();
  window_->m_hisGrid.clear ();
  window_->ui->dxGridEntry->clear ();
  window_->ui->actionHideAS->setChecked (false);
  window_->ui->actionIgnoreB4->setChecked (false);
  window_->ui->actionIgnoreIgnored->setChecked (false);
  ignoreList.clear ();
  window_->ui->respondComboBox->setCurrentIndex (
    window_->ui->respondComboBox->findData (static_cast<int> (AutoRespondPolicy::None)));
  window_->ui->cbBypass->setChecked (false);
  dec_data.params = {};
  dec_data.params.nmode = 5;
  dec_data.params.ntrperiod = 7;
  dec_data.params.newdat = true;
  if (!require (window_->publishDecodeRequest (true) == MainWindow::DecodePublishResult::Published,
                "Request did not pass through MainWindow publication.")) return false;
  window_->ui->pbBestSP->click ();
  if (!require (window_->activeDecodeOperatingContextMatchesCurrent (),
                "Published context does not match the fixture operating state.")) return false;
  return require (window_->m_bestSP.armed () && window_->ui->pbBestSP->isChecked (),
                  "Best S+P did not arm through its button.");
}

bool BestSPApplicationTestController::record (QString const& payload)
{
  static_cast<ScriptedDecoderBackend *> (backend_)->record (payload);
  if (!require (window_->m_bestSP.armed (), "CQ processing canceled the arming intent.")) return false;
  return require (window_->ui->dxCallEntry->text ().isEmpty ()
                  && window_->m_QSOProgress == QsoProgress::Calling,
                  "A decode selected a station before completion.");
}

bool BestSPApplicationTestController::complete (QString const& expectedCall)
{
  if (!require (window_->activeDecodeOperatingContextMatchesCurrent ()
                && !window_->ptt1Timer.isActive () && !window_->m_transmitting && !m_txing
                && !g_iptt && !window_->m_tx_when_ready,
                QString {"Completion preconditions changed in the fixture: request frequency=%1, current frequency=%2."}
                  .arg (window_->m_activeJt9Decode.context.periodFrequency)
                  .arg (window_->m_operatingFrequency.rx ()))) return false;
  static_cast<ScriptedDecoderBackend *> (backend_)->complete ();
  if (!require (!backend_->activeRequest () && !window_->decoderBusy (),
                "Completion did not consume backend and GUI request ownership.")) return false;
  if (!require (window_->ui->dxCallEntry->text () == expectedCall,
                QString {"Expected selected call '%1', got '%2'; mode=%3 band=%4 armed=%5 feedback=%6."}
                  .arg (expectedCall, window_->ui->dxCallEntry->text (), window_->m_mode,
                        window_->m_currentBand).arg (window_->m_bestSP.armed ())
                  .arg (window_->ui->pbBestSP->accessibleDescription ()))) return false;
  std::cerr << "WSJT-X Best S+P integration: " << scenario_.toStdString () << " passed" << std::endl;
  return true;
}

bool BestSPApplicationTestController::run ()
{
  auto * scripted = static_cast<ScriptedDecoderBackend *> (backend_);
  if (!startRequest (14080000, "late DXCC priority")
      || !record ("CQ W1AW FN31") || !record ("CQ JA1ABC PM95")
      || !complete ("JA1ABC")) return false;

  if (!startRequest (14080000, "first observed tie")
      || !record ("CQ W1AW FN31") || !record ("CQ W9XYZ EN50")
      || !complete ("W1AW")) return false;

  if (!startRequest (14080000, "current filter fallback")
      || !record ("CQ JA1ABC PM95") || !record ("CQ W1AW FN31")) return false;
  window_->ui->actionHideAS->setChecked (true);
  if (!complete ("W1AW")) return false;

  if (!startRequest (14080000, "authoritative completion identity")
      || !record ("CQ JA1ABC PM95")) return false;
  scripted->complete (true);
  if (!require (window_->decoderBusy () && window_->ui->dxCallEntry->text ().isEmpty (),
                "A stale completion selected or consumed the active request.")
      || !complete ("JA1ABC")) return false;

  if (!startRequest (1000000, "unknown band has no callsign-on-band eligibility")) return false;
  auto const knownEntity = classifyDecode (DecodedText {"120000 -10  0.3 1900 +  CQ W1AW FN31"},
                                          "FT4", window_->m_currentBand, window_->m_logBook);
  if (!require (knownEntity.worked && knownEntity.worked->entity && !knownEntity.workedOnBand,
                "Fixture must have worked entity facts and absent band facts.")
      || !record ("CQ W1AW FN31") || !complete ({})) return false;

  if (!startRequest (1000000, "invalid sender cannot become a DXCC opportunity")
      || !record ("CQ NOTACALL FN31") || !complete ({})) return false;

  if (!startRequest (1000000, "unknown band retains new DXCC eligibility")) return false;
  window_->ui->actionIgnoreB4->setChecked (true);
  auto const newEntity = classifyDecode (DecodedText {"120000 -10  0.3 1900 +  CQ JA1ABC PM95"},
                                        "FT4", window_->m_currentBand, window_->m_logBook);
  if (!require (newEntity.worked && !newEntity.worked->entity && !newEntity.workedOnBand,
                 "Fixture must have new entity facts and absent band facts.")
      || !record ("CQ JA1ABC PM95") || !complete ("JA1ABC")) return false;
  return armingEligibility () && audioFrequencyChanges () && feedbackLifecycle ()
    && competingCQSelections () && contextAndReplyPaths () && translatedSelections ()
    && settingsCQSelection () && pounceSelections ();
}

bool BestSPApplicationTestController::audioFrequencyChanges ()
{
  auto const key = [this] (int code) {
    QKeyEvent event {QEvent::KeyPress, code, Qt::ShiftModifier};
    QApplication::sendEvent (window_, &event);
  };
  std::pair<QString, std::function<void ()>> const adjustments[] {
    {"Tx spin box", [this] {window_->ui->TxFreqSpinBox->setValue (1800);}},
    {"Rx spin box", [this] {window_->ui->RxFreqSpinBox->setValue (1600);}},
    {"Shift+F11", [&] {key (Qt::Key_F11);}},
    {"Shift+F12", [&] {key (Qt::Key_F12);}},
    {"waterfall and remote audio retuning", [this] {window_->setFreq4 (1600, 1800);}},
    {"Rx to Tx copy", [this] {window_->ui->pbR2T->click ();}},
    {"Tx to Rx copy", [this] {window_->ui->pbT2R->click ();}}
  };
  for (auto const& adjustment : adjustments)
    {
      window_->ui->RxFreqSpinBox->setValue (1500);
      window_->ui->TxFreqSpinBox->setValue (1700);
      window_->ui->cbHoldTxFreq->setChecked (true);
      if (!startRequest (14080000, adjustment.first + " preserves pending selection")
          || !record ("CQ W1AW FN31")) return false;
      auto const feedback = window_->ui->pbBestSP->accessibleDescription ();
      adjustment.second ();
      int const txFrequency = window_->ui->TxFreqSpinBox->value ();
      auto * accessible = QAccessible::queryAccessibleInterface (window_->ui->pbBestSP);
      if (!require (window_->ui->RxFreqSpinBox->value () != 1500 || txFrequency != 1700,
                    "The audio-frequency adjustment did not change a frequency.")
          || !require (window_->m_bestSP.armed () && window_->ui->pbBestSP->isChecked ()
                       && accessible && accessible->state ().checked
                       && window_->ui->pbBestSP->accessibleDescription () == feedback,
                       "Audio retuning canceled arming or replaced its accessible feedback.")
          || !record ("CQ JA1ABC PM95") || !complete ("JA1ABC")
          || !require (window_->ui->TxFreqSpinBox->value () == txFrequency,
                       "Selection did not preserve the adjusted Hold Tx frequency.")) return false;
    }
  window_->ui->cbHoldTxFreq->setChecked (false);
  return true;
}

bool BestSPApplicationTestController::attemptArming (bool shortcut, QString const& expectedFeedback)
{
  if (shortcut)
    {
      QKeyEvent key {QEvent::KeyPress, Qt::Key_B, Qt::AltModifier};
      QApplication::sendEvent (window_, &key);
    }
  else window_->ui->pbBestSP->click ();
  return feedbackMatches (false, expectedFeedback);
}

bool BestSPApplicationTestController::feedbackMatches (bool armed, QString const& expected)
{
  auto * button = window_->ui->pbBestSP;
  auto * accessible = QAccessible::queryAccessibleInterface (button);
  return require (window_->m_bestSP.armed () == armed && button->isChecked () == armed
                  && accessible && accessible->state ().checkable
                  && accessible->state ().checked == armed,
                  "Controller, button, and accessible checked states diverged.")
    && require (button->accessibleDescription () == expected && button->statusTip () == expected
                && accessible->text (QAccessible::Description) == expected
                && window_->statusBar ()->currentMessage () == expected,
                "The expected feedback or accessible description was replaced.");
}

bool BestSPApplicationTestController::competingCQSelections ()
{
  auto * scripted = static_cast<ScriptedDecoderBackend *> (backend_);
  auto const selectPolicy = [this] (AutoRespondPolicy policy) {
    window_->ui->respondComboBox->setCurrentIndex (
      window_->ui->respondComboBox->findData (static_cast<int> (policy)));
  };
  auto const activate = [this, &selectPolicy] (bool waitCall) {
    window_->ui->cbAutoSeq->setChecked (true);
    if (waitCall)
      {
        window_->ui->dxCallEntry->setText ("W1AW");
        window_->ui->DX_Call_Button->setChecked (false);
        window_->ui->DX_Call_Button->click ();
        return require (wait_and_call && !no_wait_and_call,
                        "The Wait and Call control did not activate its intent.");
      }
    selectPolicy (AutoRespondPolicy::First);
    window_->ui->respondComboBox->setVisible (true);
    QApplication::setActiveWindow (window_);
    window_->ui->autoButton->setFocus ();
    if (!require (window_->ui->autoButton->hasFocus (),
                  "The Pounce activation control did not receive focus.")) return false;
    QMouseEvent event {QEvent::MouseButtonPress, QPointF {}, Qt::RightButton,
                       Qt::RightButton, Qt::NoModifier};
    QApplication::sendEvent (window_, &event);
    return require (pounce, "Right-click did not activate Wait and Pounce.");
  };
  auto const waitCallRejected = QCoreApplication::translate (
    "MainWindow", "Turn off Wait & Call before arming Best S+P.");
  auto const pounceRejected = QCoreApplication::translate (
    "MainWindow", "Turn off Wait & Pounce before arming Best S+P.");
  auto const waitCallCanceled = QCoreApplication::translate (
    "MainWindow", "Best S+P canceled: Wait & Call is active.");
  auto const pounceCanceled = QCoreApplication::translate (
    "MainWindow", "Best S+P canceled: Wait & Pounce is active.");

  for (bool const waitCall : {true, false})
    {
      for (bool const shortcut : {false, true})
        {
          if (!startRequest (14080000, QString {"active %1 rejects %2 arming"}
                             .arg (waitCall ? "Wait and Call" : "Wait and Pounce",
                                   shortcut ? "Alt+B" : "button"))) return false;
          window_->setBestSPArmed (false);
          if (!activate (waitCall)
              || !attemptArming (shortcut, waitCall ? waitCallRejected : pounceRejected)) return false;
          scripted->record ("CQ W1AW FN31");
          if (!complete ("W1AW")) return false;
        }
      if (!startRequest (14080000, QString {"activating %1 cancels pending Best S+P"}
                         .arg (waitCall ? "Wait and Call" : "Wait and Pounce"))
          || !record ("CQ JA1ABC PM95") || !activate (waitCall)
          || !feedbackMatches (false, waitCall ? waitCallCanceled : pounceCanceled)) return false;
      scripted->record ("CQ W1AW FN31");
      if (!complete ("W1AW")) return false;
    }

  if (!startRequest (14080000, "a DX call alone does not prevent Best S+P arming")) return false;
  window_->setBestSPArmed (false);
  window_->ui->dxCallEntry->setText ("W1AW");
  window_->ui->pbBestSP->click ();
  if (!require (window_->m_bestSP.armed (), "A DX call alone was treated as competing automation.")) return false;
  scripted->record ("K1ABC W1AW -10");
  if (!require (window_->m_auto && window_->m_QSOProgress != QsoProgress::Calling,
                "Directed Wait and Reply was suppressed after arming with a DX call.")
      || !complete ("W1AW")) return false;

  if (!startRequest (14080000, "inhibited Wait and Call does not compete")) return false;
  window_->setBestSPArmed (false);
  if (!activate (true)) return false;
  no_wait_and_call = true;
  window_->ui->pbBestSP->click ();
  if (!require (window_->m_bestSP.armed (), "Inhibited Wait and Call prevented arming.")
      || !complete ("W1AW")) return false;

  for (int const dormant : {0, 1, 2})
    {
      QString const scenarios[] {"response policy reactivates dormant Pounce",
                                 "Auto Seq reactivates dormant Pounce",
                                 "leaving SWL view reactivates dormant Pounce"};
      if (!startRequest (14080000, scenarios[dormant])) return false;
      window_->setBestSPArmed (false);
      if (!activate (false)) return false;
      if (dormant == 0) selectPolicy (AutoRespondPolicy::None);
      else if (dormant == 1) window_->ui->cbAutoSeq->setChecked (false);
      else window_->ui->actionSWL_Mode->trigger ();
      QKeyEvent key {QEvent::KeyPress, Qt::Key_B, Qt::AltModifier};
      QApplication::sendEvent (window_, &key);
      if (!require (pounce && window_->m_bestSP.armed (), "Dormant Pounce prevented arming.")
          || !record ("CQ JA1ABC PM95")) return false;
      if (dormant == 0) selectPolicy (AutoRespondPolicy::First);
      else if (dormant == 1) window_->ui->cbAutoSeq->setChecked (true);
      else window_->ui->actionSWL_Mode->trigger ();
      if (!feedbackMatches (false, pounceCanceled)) return false;
      scripted->record ("CQ W1AW FN31");
      if (!complete ("W1AW")) return false;
    }

  if (!startRequest (14080000, "no eligible winner retains arming across receptions")) return false;
  auto const armedFeedback = window_->ui->pbBestSP->accessibleDescription ();
  for (int reception = 0; reception != 3; ++reception)
    {
      if (!record ("CQ W1AAA FN31") || !complete ({})
          || !feedbackMatches (true, armedFeedback)) return false;
      window_->m_dateTimeSeqStart = window_->m_dateTimeSeqStart.addMSecs (7500);
      if (!require (window_->publishDecodeRequest (true) == MainWindow::DecodePublishResult::Published,
                    "The next reception could not publish its request.")) return false;
    }
  return record ("CQ JA1ABC PM95") && complete ("JA1ABC");
}

bool BestSPApplicationTestController::settingsCQSelection ()
{
  auto const editWaitFeatures = [this] (bool enabled, bool accept) {
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
      auto * option = dialog->findChild<QCheckBox *> ("enable_Wait_features_check_box");
      if (!option) {dialog->reject (); return;}
      visited = true;
      option->setChecked (enabled);
      // Null device fixtures keep Settings validation independent of installed audio hardware.
      for (auto const name : {"sound_input_combo_box", "sound_output_combo_box"})
        {
          auto * devices = dialog->findChild<QComboBox *> (name);
          if (devices && devices->currentIndex () < 0)
            devices->addItem ("Silent test device", QVariant::fromValue (QAudioDeviceInfo {}));
        }
      if (accept) dialog->accept ();
      else dialog->reject ();
    });
    guard.start (3000);
    window_->on_actionSettings_triggered ();
    guard.stop ();
    window_->m_guiTimer.stop ();
    return require (visited && !timedOut, "The isolated Wait-features Settings edit did not finish.");
  };
  if (!startRequest (14080000, "Settings reactivates dormant Pounce only on acceptance")) return false;
  window_->setBestSPArmed (false);
  window_->ui->cbAutoSeq->setChecked (true);
  window_->ui->respondComboBox->setCurrentIndex (
    window_->ui->respondComboBox->findData (static_cast<int> (AutoRespondPolicy::First)));
  if (!editWaitFeatures (false, true)
      || !require (!window_->m_config.Wait_features_enabled (), "Wait features were not disabled.")) return false;
  pounce = true;
  window_->ui->pbBestSP->click ();
  if (!require (window_->m_bestSP.armed (), "Disabled Wait features prevented Best S+P arming.")) return false;
  auto const feedback = window_->ui->pbBestSP->accessibleDescription ();
  if (!editWaitFeatures (true, false)
      || !require (!window_->m_config.Wait_features_enabled (), "Settings Cancel changed Wait features.")
      || !feedbackMatches (true, feedback)
      || !editWaitFeatures (true, true)
      || !require (window_->m_config.Wait_features_enabled () && pounce,
                   QString {"Settings acceptance did not reactivate Pounce: Wait features=%1, Pounce=%2."}
                     .arg (window_->m_config.Wait_features_enabled ()).arg (pounce))
      || !feedbackMatches (false, QCoreApplication::translate (
           "MainWindow", "Best S+P canceled: Wait & Pounce is active."))) return false;
  return complete ({});
}

bool BestSPApplicationTestController::pounceSelections ()
{
  auto * scripted = static_cast<ScriptedDecoderBackend *> (backend_);
  auto const arm = [this] {
    QApplication::setActiveWindow (window_);
    window_->ui->autoButton->setFocus ();
    if (!require (window_->ui->autoButton->hasFocus (),
                  "The Pounce activation control did not receive focus.")) return false;
    QMouseEvent event {QEvent::MouseButtonPress, QPointF {}, Qt::RightButton,
                       Qt::RightButton, Qt::NoModifier};
    QApplication::sendEvent (window_, &event);
    return require (pounce, "Right-click did not arm Pounce.");
  };
  auto const begin = [this, &arm] (AutoRespondPolicy policy, QString const& scenario) {
    if (!startRequest (14080000, scenario)) return false;
    window_->setBestSPArmed (false);
    window_->ui->cbAutoSeq->setChecked (true);
    window_->ui->respondComboBox->setVisible (true);
    window_->ui->respondComboBox->setCurrentIndex (
      window_->ui->respondComboBox->findData (static_cast<int> (policy)));
    window_->ui->txFirstCheckBox->setEnabled (true);
    return arm ();
  };
  auto const messages = [this] {
    return QStringList {window_->ui->tx1->text (), window_->ui->tx2->text (),
      window_->ui->tx3->text (), window_->ui->tx4->text (),
      window_->ui->tx5->currentText (), window_->ui->tx6->text ()};
  };
  auto const candidate = [scripted] (AutoRespondPolicy policy) {
    scripted->record ("CQ JA1ABC PM95", policy == AutoRespondPolicy::MinSignal ? -25 : -2, 2400);
  };
  auto const wait = [] (int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot (milliseconds, &loop, &QEventLoop::quit);
    loop.exec ();
  };
  for (auto const policy : {AutoRespondPolicy::MaxDistance, AutoRespondPolicy::MaxSignal,
                            AutoRespondPolicy::MinSignal})
    for (int origin = 0; origin != 3; ++origin)
      {
        if (!begin (policy, QString {"Pounce policy %1 preserves manual target from origin %2"}
                    .arg (static_cast<int> (policy)).arg (origin))) return false;
        scripted->record ("CQ W1AW FN31");
        if (!require (pounce && window_->m_auto && window_->ui->dxCallEntry->text () == "W1AW"
                      && window_->m_pounceSelectionTimer.isActive (),
                      "The initial automatic candidate did not open its selection window.")) return false;
        QString const manual {"120007 -10  0.3 1250 +  CQ W9XYZ EN50"};
        if (origin == 0) window_->doubleClickOnCall2 (manual, {}, Qt::NoModifier, {});
        else if (origin == 1) window_->doubleClickOnCall (manual, {}, Qt::NoModifier, {});
        else window_->replyToCQ (QTime {12, 0, 7}, -10, 0.3f, 1250, "+", "CQ W9XYZ EN50", false, 0);
        auto const selectedMessages = messages ();
        bool const txFirst = window_->ui->txFirstCheckBox->isChecked ();
        if (!require (!pounce && !window_->m_pounceSelectionTimer.isActive ()
                      && !window_->stopWCTimer.isActive () && window_->m_auto
                      && window_->ui->dxCallEntry->text () == "W9XYZ",
                      "An accepted manual selection did not retain Auto Tx and cancel Pounce.")) return false;
        candidate (policy);
        if (!require (window_->ui->dxCallEntry->text () == "W9XYZ"
                      && messages () == selectedMessages && window_->ui->RxFreqSpinBox->value () == 1250
                      && window_->ui->txFirstCheckBox->isChecked () == txFirst && window_->m_auto,
                      "A better automatic candidate replaced the manual call, messages, frequency, or period.")) return false;
        auto const progress = window_->m_QSOProgress;
        scripted->record ("K1ABC W9XYZ -05", -5, 1250, "120007");
        if (!require (window_->ui->dxCallEntry->text () == "W9XYZ" && window_->m_auto
                      && window_->m_QSOProgress != progress,
                      "A directed reply did not advance the manually selected QSO.")
            || !complete ("W9XYZ")) return false;
      }

  for (auto const policy : {AutoRespondPolicy::First, AutoRespondPolicy::MaxDistance,
                            AutoRespondPolicy::MaxSignal, AutoRespondPolicy::MinSignal})
    {
      if (!begin (policy, QString {"Pounce policy %1 retains automatic ranking and non-selection actions"}
                  .arg (static_cast<int> (policy)))) return false;
      scripted->record ("CQ W1AW FN31");
      window_->m_pounceSelectionTimer.start (90);
      auto const originalRemaining = window_->m_pounceSelectionTimer.remainingTime ();
      window_->doubleClickOnCall2 ("malformed", {}, Qt::NoModifier, {});
      window_->doubleClickOnCall2 ("120000  Tx      1259 +  CQ K1ABC FN42", {}, Qt::NoModifier, {});
      window_->setFreq4 (1600, 1800);
      if (!require (pounce && window_->m_pounceSelectionTimer.isActive (),
                    "A rejected selection or audio-frequency change canceled Pounce.")) return false;
      candidate (policy);
      auto const expected = policy == AutoRespondPolicy::First ? "W1AW" : "JA1ABC";
      if (!require (pounce && window_->ui->dxCallEntry->text () == expected
                    && window_->m_pounceSelectionTimer.remainingTime () <= originalRemaining,
                    "Automatic ranking failed or extended the original selection window.")) return false;
      window_->m_pounceSelectionTimer.start (1);
      wait (10);
      if (!require (!pounce && window_->stopWCTimer.isActive (),
                    "The selection window did not expire independently of the Wait and Call timeout.")) return false;
      window_->ui->txb6->click ();
      if (!require (!window_->stopWCTimer.isActive () && window_->m_auto,
                    "An explicit Tx6 action did not stop the Pounce timeout while retaining Auto Tx.")
          || !complete (expected)) return false;
    }

  if (!begin (AutoRespondPolicy::MaxSignal, "typing cancels Pounce while programmatic updates preserve it")) return false;
  window_->ui->dxCallEntry->setText ("W1AW");
  if (!require (pounce, "A programmatic target update canceled Pounce.")) return false;
  window_->ui->dxCallEntry->setFocus ();
  window_->ui->dxCallEntry->selectAll ();
  QKeyEvent edit {QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, "W"};
  QApplication::sendEvent (window_->ui->dxCallEntry, &edit);
  if (!require (!pounce && window_->ui->dxCallEntry->text () == "W",
                "A real DX Call edit did not cancel Pounce.")) return false;
  window_->auto_tx_mode (true);
  candidate (AutoRespondPolicy::MaxSignal);
  if (!require (!pounce && window_->ui->dxCallEntry->text () == "W",
                "Ordinary Enable Tx resurrected canceled Pounce.") || !complete ("W")) return false;

  for (auto const policy : {AutoRespondPolicy::First, AutoRespondPolicy::MaxSignal})
    {
      if (!begin (policy, QString {"Pounce policy %1 rearms without stale scores, latch, or timeout"}
                  .arg (static_cast<int> (policy)))) return false;
      candidate (policy);
      window_->m_pounceSelectionTimer.start (30);
      window_->ui->txb2->click ();
      if (!require (!pounce && !window_->m_autoRespondSelectionLatch.isSelected ()
                    && !window_->m_pounceSelectionTimer.isActive (),
                    "An explicit transmit-message control did not cancel automatic selection.")) return false;
      window_->auto_tx_mode (false);
      if (!arm ()) return false;
      wait (60);
      if (!require (pounce, "A canceled selection timeout cleared the new Pounce intent.")) return false;
      scripted->record ("CQ W1AW FN31", -15);
      if (!require (window_->ui->dxCallEntry->text () == "W1AW",
                    "Fresh Pounce arming retained an earlier First latch or better score.")
          || !complete ("W1AW")) return false;
    }
  return true;
}

bool BestSPApplicationTestController::armingEligibility ()
{
  auto const qsoFeedback = QCoreApplication::translate (
    "MainWindow", "Best S+P is available before starting a QSO.");
  auto const txFeedback = QCoreApplication::translate (
    "MainWindow", "Best S+P is available in FT4 while receiving.");
  window_->m_auto = false;
  window_->ui->autoButton->setChecked (false);
  for (auto const progress : {QsoProgress::Replying, QsoProgress::Report, QsoProgress::RogerReport,
                              QsoProgress::Rogers, QsoProgress::Signoff})
    for (bool const shortcut : {false, true})
      {
        scenario_ = QString {"QSO progress %1 rejects %2 arming"}
          .arg (static_cast<int> (progress)).arg (shortcut ? "Alt+B" : "button");
        window_->setBestSPArmed (false);
        window_->m_QSOProgress = progress;
        if (!attemptArming (shortcut, qsoFeedback)) return false;
        std::cerr << "WSJT-X Best S+P integration: " << scenario_.toStdString () << " passed" << std::endl;
      }
  window_->m_QSOProgress = QsoProgress::Calling;
  for (auto const condition : {"transmitting", "full-duplex TX", "PTT requested",
                               "awaiting PTT", "PTT delay timer"})
    for (bool const shortcut : {false, true})
      {
        scenario_ = QString {"%1 rejects %2 arming"}.arg (condition, shortcut ? "Alt+B" : "button");
        window_->setBestSPArmed (false);
        window_->m_transmitting = QString {condition} == "transmitting";
        m_txing = QString {condition} == "full-duplex TX";
        g_iptt = QString {condition} == "PTT requested";
        window_->m_tx_when_ready = QString {condition} == "awaiting PTT";
        if (QString {condition} == "PTT delay timer") window_->ptt1Timer.start (60000);
        bool const rejected = attemptArming (shortcut, txFeedback);
        window_->m_transmitting = false;
        m_txing = false;
        g_iptt = 0;
        window_->m_tx_when_ready = false;
        window_->ptt1Timer.stop ();
        if (!rejected) return false;
        std::cerr << "WSJT-X Best S+P integration: " << scenario_.toStdString () << " passed" << std::endl;
      }
  return true;
}

bool BestSPApplicationTestController::feedbackLifecycle ()
{
  auto const shortcut = [this] {
    QKeyEvent key {QEvent::KeyPress, Qt::Key_B, Qt::AltModifier};
    QApplication::sendEvent (window_, &key);
  };
  auto const armedFeedback = QCoreApplication::translate (
    "MainWindow", "Best S+P armed. Waiting for decode completion.");
  auto const canceledFeedback = QCoreApplication::translate ("MainWindow", "Best S+P canceled.");
  auto const timedOutFeedback = QCoreApplication::translate ("MainWindow", "Best S+P timed out.");
  auto const selectedFeedback = QCoreApplication::translate ("MainWindow", "Best S+P selected %1: %2.")
    .arg ("JA1ABC", QCoreApplication::translate ("MainWindow", "new DXCC entity"));

  if (!startRequest (14080000, "completed selection synchronizes accessible state")
      || !record ("CQ JA1ABC PM95") || !complete ("JA1ABC")
      || !feedbackMatches (false, selectedFeedback)) return false;
  scenario_ = "redundant disarm preserves selection reason";
  window_->setBestSPArmed (false);
  if (!feedbackMatches (false, selectedFeedback)) return false;
  std::cerr << "WSJT-X Best S+P integration: " << scenario_.toStdString () << " passed" << std::endl;

  scenario_ = "common TX-start preserves selection reason";
  window_->m_tune = true;
  window_->guiUpdate ();
  bool const txRequested = g_iptt || window_->m_tx_when_ready || window_->ptt1Timer.isActive ();
  bool const feedbackRetained = feedbackMatches (false, selectedFeedback);
  window_->requestTxStop (TxEvidence::TxStopReason::UserHalt);
  m_txing = false;
  if (!require (txRequested, "The common TX-start request path was not exercised.")
      || !feedbackRetained) return false;
  std::cerr << "WSJT-X Best S+P integration: " << scenario_.toStdString () << " passed" << std::endl;

  scenario_ = "new arming and true cancellation replace terminal feedback";
  window_->m_QSOProgress = QsoProgress::Calling;
  shortcut ();
  if (!feedbackMatches (true, armedFeedback)) return false;
  shortcut ();
  if (!feedbackMatches (false, canceledFeedback)) return false;
  std::cerr << "WSJT-X Best S+P integration: " << scenario_.toStdString () << " passed" << std::endl;

  scenario_ = "GUI timeout synchronizes consumed controller intent";
  shortcut ();
  if (!feedbackMatches (true, armedFeedback)) return false;
  window_->m_bestSP.arm (QDateTime::currentDateTimeUtc ().addSecs (-121));
  window_->m_sec0 = -1;
  window_->guiUpdate ();
  if (!feedbackMatches (false, timedOutFeedback)) return false;
  std::cerr << "WSJT-X Best S+P integration: " << scenario_.toStdString () << " passed" << std::endl;
  return true;
}

bool BestSPApplicationTestController::contextAndReplyPaths ()
{
  auto * scripted = static_cast<ScriptedDecoderBackend *> (backend_);
  if (!startRequest (14080000, "obsolete operating context cannot select")
      || !record ("CQ JA1ABC PM95")) return false;
  window_->m_operatingFrequency.requestNominal (7074000, {}, false);
  scripted->complete ();
  if (!require (!window_->decoderBusy () && !window_->m_bestSP.armed ()
                && !window_->ui->pbBestSP->isChecked () && window_->ui->dxCallEntry->text ().isEmpty (),
                "Completion selected a station from an obsolete operating context.")) return false;
  std::cerr << "WSJT-X Best S+P integration: " << scenario_.toStdString () << " passed" << std::endl;

  if (!startRequest (1000000, "unknown-band ignored call stays ineligible")) return false;
  window_->ui->actionIgnoreIgnored->setChecked (true);
  ignoreList = "JA1ABC,";
  if (!record ("CQ JA1ABC PM95") || !complete ({})) return false;

  if (!startRequest (14080000, "rearming retains all in-flight observations")
      || !record ("CQ W1AW FN31")) return false;
  window_->ui->pbBestSP->click ();
  scripted->record ("CQ JA1ABC PM95");
  if (!require (!window_->m_bestSP.armed () && window_->ui->dxCallEntry->text ().isEmpty (),
                "A canceled request selected a station.")) return false;
  window_->ui->pbBestSP->click ();
  if (!require (window_->m_bestSP.armed (), "The active decode could not be rearmed.")
      || !complete ("JA1ABC")) return false;

  if (!startRequest (14080000, "directed Wait and Reply survives Best S+P arming")) return false;
  window_->ui->dxCallEntry->setText ("W1AW");
  if (!require (window_->m_bestSP.armed () && window_->m_config.Wait_features_enabled (),
                "The Wait and Reply fixture is not armed.")) return false;
  scripted->record ("K1ABC W1AW -10");
  if (!require (window_->m_auto && window_->m_QSOProgress != QsoProgress::Calling,
                "A directed Wait and Reply message was suppressed.")
      || !complete ("W1AW")) return false;

  for (auto const policy : {AutoRespondPolicy::First, AutoRespondPolicy::MaxDistance})
    {
      if (!startRequest (14080000, policy == AutoRespondPolicy::First
                         ? "direct reply reaches CQ First while Best S+P is armed"
                         : "direct reply reaches CQ Max Distance while Best S+P is armed")) return false;
      window_->ui->respondComboBox->setVisible (true);
      window_->ui->respondComboBox->setCurrentIndex (
        window_->ui->respondComboBox->findData (static_cast<int> (policy)));
      window_->genStdMsgs ("-10");
      window_->setTxMsg (6);
      window_->m_bCallingCQ = true;
      window_->ui->autoButton->click ();
      window_->m_autoRespondPeriodState.observePeriod (
        window_->m_dateTimeSeqStart, true, window_->pendingCqAutoRespondIntent (), policy);
      if (!require (window_->m_bestSP.armed ()
                    && window_->m_autoRespondPeriodState.accepts (window_->m_dateTimeSeqStart),
                    "The pending-CQ responder fixture was not armed for this reception.")) return false;
      scripted->record ("K1ABC W1AW FN31");
      if (!require (window_->ui->dxCallEntry->text () == "W1AW"
                    && window_->m_auto && window_->m_QSOProgress != QsoProgress::Calling,
                    "A directed reply was suppressed instead of reaching the CQ responder.")
          || !complete ("W1AW")) return false;
    }
  return true;
}

bool BestSPApplicationTestController::translatedSelections ()
{
  class DuplicateLabels final : public QTranslator
  {
    QString translate (char const *, char const * source, char const *, int) const override
    {
      return QString {source}.startsWith ("New ") ? QStringLiteral ("Duplicate translated label") : QString {};
    }
  } duplicateLabels;
  QCoreApplication::installTranslator (&duplicateLabels);
  bool const duplicateSelection = require (
    DecodeHighlightingModel::highlight_name (DecodeHighlightingModel::Highlight::DXCC)
      == DecodeHighlightingModel::highlight_name (DecodeHighlightingModel::Highlight::CallBand)
    && DecodeHighlightingModel::highlight_name (DecodeHighlightingModel::Highlight::DXCC) != "New DXCC",
    "The synthetic translator did not produce duplicate opportunity labels.")
    && startRequest (14080000, "duplicate translated labels preserve operational ranking")
    && record ("CQ W1AW FN31") && record ("CQ JA1ABC PM95") && complete ("JA1ABC");
  QCoreApplication::removeTranslator (&duplicateLabels);
  if (!duplicateSelection) return false;

  QTranslator japanese;
  if (!require (japanese.load (":/Translations/wsjtx_ja.qm"),
                "The actual Japanese application catalog could not be loaded.")) return false;
  QCoreApplication::installTranslator (&japanese);
  bool const japaneseSelection = require (
    DecodeHighlightingModel::highlight_name (DecodeHighlightingModel::Highlight::DXCC) != "New DXCC",
    "The actual Japanese catalog did not translate the opportunity label.")
    && startRequest (14080000, "Japanese catalog preserves operational ranking")
    && record ("CQ W1AW FN31") && record ("CQ JA1ABC PM95") && complete ("JA1ABC");
  QCoreApplication::removeTranslator (&japanese);
  return japaneseSelection;
}

bool BestSPApplicationTestController::require (bool condition, QString const& message)
{
  if (!condition) finish (false, scenario_ + ": " + message);
  return condition;
}

void BestSPApplicationTestController::finish (bool success, QString const& message)
{
  if (finished_) return;
  finished_ = true;
  succeeded_ = success;
  timeout_.stop ();
  retry_.stop ();
  window_->m_guiTimer.stop ();
  std::cerr << "WSJT-X Best S+P integration: " << message.toStdString () << std::endl;
  QCoreApplication::exit (success ? EXIT_SUCCESS : EXIT_FAILURE);
}
