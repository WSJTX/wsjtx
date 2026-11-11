#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "JttyDecoder.hpp"
#include "JttyReceiveHistory.hpp"
#include "JttyRecording.hpp"
#include "JttySpectrum.hpp"
#include "JttyMessages.hpp"
#include "JttyReceiveLine.hpp"
#include "JttyReceiveResultController.hpp"
#include "JttyReceiveTiming.hpp"
#include "Detector/Detector.hpp"
#include "Logger.hpp"
#ifdef WIN32
#include "MMTTYIF.hpp"
#undef MessageBox
#endif
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextCursor>
#include <QScrollBar>
#include <algorithm>
#include <deque>

extern dec_data_t& dec_data;
extern "C" void refspectrum_(short*, int*, bool*, bool*, bool*, char const*, fortran_charlen_t);

namespace {
constexpr qint64 stepSamples = Jtty::receiveFrameSamples / 4;
constexpr qint64 windowSamples = Jtty::receiveFrameSamples + stepSamples;

JttyRecording::SavePolicy savePolicy(bool all, bool decoded)
{
  return all ? JttyRecording::SavePolicy::All
    : decoded ? JttyRecording::SavePolicy::Decoded : JttyRecording::SavePolicy::Off;
}

struct ReceiveSpan {
  quint64 id = 0;
  quint64 contextId = 0;
  quint64 displayGroup = 0;
  QDateTime anchor;
  QDateTime displayAnchor;
  DecodeOperatingContext context;
  JttyRecording::Settings recording;
  int low = 200, high = 2800;
  float center = 1500, tolerance = 50;
};

void renderLine(DisplayText* browser, JttyReceiveLine::Presentation const& presentation,
                JttyReceiveLine::Options const& options, JttyReceiveLine& line, bool chronological)
{
  auto cursor = line.render(*browser->document(), presentation, options, browser->contentFont(), chronological);
  if (cursor.isNull()) return;
  browser->setTextCursor(cursor);
  browser->ensureCursorVisible();
}
}

struct MainWindow::JttyReceiveState {
  using Results = Jtty::ReceiveResultController;
  using DecodeSource = Results::Source;
  Jtty::ReceiveHistory history;
  QMap<quint64, ReceiveSpan> spans;
  ReceiveSpan liveSpan, reviewSpan, diskSpan;
  QMap<quint64, ReceiveSpan> contexts;
  quint64 nextContext = 0, currentContext = 0, nextDrain = 0, nextReplay = quint64(1) << 62;
  quint64 nextDisplayGroup = 0;
  Jtty::Decoder live, review, diskDecoder;
  JttySpectrum spectrum, diskSpectrum;
  JttyRecording recording;
  Results results;
  JttyReceiveMailboxPtr liveMailbox;
  std::deque<JttyReceiveMailboxPtr> mailboxes;
  struct ReviewJob { ReceiveSpan span; qint64 first, stop; std::vector<short> pcm; bool picked; };
  std::deque<ReviewJob> jobs;
  bool scheduled = false, pumping = false, active = false, reviewing = false;
  bool disk = false, continuous = false;
  bool draining = false, drainCutoff = false, closeAfterDrain = false;
  std::function<void()> drainProgress;
#if defined(WSJT_ENABLE_LIVE_AUDIO_TEST)
  enum class FrequencyCheckStage { Initialize, Unchanged, Retune, Restore, Complete };
  FrequencyCheckStage frequencyCheckStage = FrequencyCheckStage::Initialize;
  Radio::Frequency frequencyCheckNominal = 0;
  quint64 frequencyCheckContext = 0;
  int drainIgnoredSources = 0;
  int drainTimeoutMs = 5000;
#endif
  int diskK = -1;
  int referenceMode = -1;
  qint64 acknowledged = -1;
  JttyRecording::SavePolicy policy = JttyRecording::SavePolicy::Off;

  void apply(MainWindow& window, QVector<Jtty::ReceiveUpdate> const& updates,
             ReceiveSpan const& span, DecodeSource source)
  {
    Results::Reception const reception {source, span.contextId, span.displayGroup,
      span.anchor, span.displayAnchor, span.context, span.center, span.tolerance};
    Results::Inputs const inputs {
      [&window] { return JttyReceiveLine::Options {window.ui->cbLowerCase->isChecked(),
                                                   window.ui->cbIncludeTime->isChecked()}; },
      [&window, this] { return Results::Selection {window.m_mode == "JTTY", currentContext,
        float(window.ui->RxFreqSpinBox_2->value()), float(window.ui->sbFtol_2->value())}; },
      [&window] {
#ifdef WIN32
        return Results::EchoOptions {bool(window.m_mmttyif), window.ui->cbLowerCase->isChecked()};
#else
        Q_UNUSED(window);
        return Results::EchoOptions {};
#endif
      },
      [&window] { return window.m_config.spot_to_psk_reporter(); }
    };
    Results::Effects const effects {
      [&window](Results::Pane pane) {
        auto* browser = pane == Results::Pane::All
          ? window.ui->decodedTextBrowser : window.ui->decodedTextBrowser2;
        browser->insertLineSpacer(MainWindow::tr("JTTY review"));
      },
      [&window](Results::Pane pane, JttyReceiveLine& line,
                JttyReceiveLine::Presentation const& presentation,
                JttyReceiveLine::Options const& options) {
        auto* browser = pane == Results::Pane::All
          ? window.ui->decodedTextBrowser : window.ui->decodedTextBrowser2;
        renderLine(browser, presentation, options, line, pane == Results::Pane::All);
      },
      [&window](QString const& text) {
#ifdef WIN32
        window.m_mmttyif->echo_message_to_n1mm(text);
#else
        Q_UNUSED(window);
        Q_UNUSED(text);
#endif
      },
      [this](qint64 first, qint64 last) { recording.noteDecoded(first, last); },
      [&window](QString const& text, DecodeOperatingContext const& context) {
        window.write_all("Rx", text, &context);
      },
      [&window](Results::Spot const& spot) {
        if (!window.m_psk_Reporter.addRemoteStation(spot.sender, spot.grid, spot.frequency,
                                                   spot.mode, spot.snr, spot.time))
          window.showStatusMessage(MainWindow::tr("PSK Reporter spot queue full; oldest spot dropped"));
      }
    };
    results.apply(updates, reception, inputs, effects);
  }
};

int MainWindow::jttySnrForSelectedWord(QString const& word, bool leftPane) const
{
  if (!m_jttyReceive) return -10;
  return m_jttyReceive->results.snrForSelectedWord(word,
    leftPane ? Jtty::ReceiveResultController::Pane::All : Jtty::ReceiveResultController::Pane::Qso);
}

void MainWindow::refreshJttyReceiveLines()
{
  JttyReceiveLine::Options const options {ui->cbLowerCase->isChecked(), ui->cbIncludeTime->isChecked()};
  for (auto* pane : {ui->decodedTextBrowser, ui->decodedTextBrowser2}) {
    auto* scroll = pane->verticalScrollBar();
    bool const atEnd = scroll->value() == scroll->maximum();
    auto const anchor = pane->cursorForPosition(QPoint(1, 1));
    int const offset = pane->cursorRect(anchor).top();
    JttyReceiveLine::refresh(*pane->document(), options, pane->contentFont());
    scroll->setValue(atEnd ? scroll->maximum()
                          : scroll->value() + pane->cursorRect(anchor).top() - offset);
  }
}

void MainWindow::on_cbLowerCase_toggled(bool)
{
  if (m_mode == "JTTY") refreshJttyReceiveLines();
  updateJttyFunctionKeyLabels();
}

void MainWindow::cancelJttyReview()
{
  if (!m_jttyReceive) return;
  auto& state = *m_jttyReceive;
  bool const hadReview = state.reviewing || !state.jobs.empty();
  if (!hadReview) return;
  state.jobs.clear();
  state.review.end();
  state.review.takeUpdates();
  state.reviewing = false;
  state.results.resetReplay();
  if (hadReview && !state.disk) ui->DecodeButton->setChecked(false);
}

void MainWindow::initializeJttyReceive()
{
  if (m_jttyReceive) return;
  m_jttyReceive = std::make_shared<JttyReceiveState>();
  m_jttyReceive->recording.setErrorHandler([this](QString const& error) {
    LOG_WARN("JTTY recording stopped: " << error.toStdString());
    statusBar()->showMessage(tr("JTTY recording stopped: %1. Change the save setting to retry.").arg(error));
  });
}

void MainWindow::updateJttyReceivePolicy(bool continuous)
{
  if (continuous && jttyDrainInProgress()) return;
  if (!continuous && !m_jttyReceive) return;
  initializeJttyReceive();
  auto& state = *m_jttyReceive;
  if (continuous && !state.continuous) updateJttyReceiveContext();
  state.continuous = continuous;
  auto policy = continuous ? ReceivePolicy::ContinuousJtty : ReceivePolicy::Timed;
  m_detector->setReceivePolicy(policy);
  m_config.transceiver_receive_policy(policy);
  if (!continuous) cancelJttyReview();
}

void MainWindow::updateJttyReceiveContext()
{
  if (jttyDrainInProgress()) return;
  initializeJttyReceive();
  auto& state = *m_jttyReceive;
  ReceiveSpan span;
  span.contextId = ++state.nextContext;
  span.context = currentDecodeOperatingContext();
  span.context.diskData = false;
  span.context.periodFrequency = m_operatingFrequency.rx();
  span.recording.directory = m_config.save_directory().absolutePath();
  span.recording.myCall = m_config.my_callsign(); span.recording.myGrid = m_config.my_grid();
  span.recording.hisCall = m_hisCall; span.recording.hisGrid = m_hisGrid;
  span.recording.frequency = m_operatingFrequency.rx();
  span.low = m_wideGraph->nStartFreq(); span.high = m_wideGraph->Fmax();
  span.center = ui->RxFreqSpinBox_2->value(); span.tolerance = ui->sbFtol_2->value();
  state.currentContext = span.contextId;
  state.contexts.insert(span.contextId, span);
  // The two bounded mailboxes retain at most 2,048 events, including Begin.
  while (state.contexts.size() > 2048) state.contexts.erase(state.contexts.begin());
  m_detector->setReceiveContext(m_tci_audio ? 0 : span.contextId);
  m_config.transceiver_receive_context(m_tci_audio ? span.contextId : 0);
}

void MainWindow::updateJttySavePolicy()
{
  if (!m_jttyReceive) return;
  auto& state = *m_jttyReceive;
  state.policy = savePolicy(m_saveAll, m_saveDecoded);
  state.recording.updateSavePolicy(state.policy);
}

#if defined(WSJT_ENABLE_LIVE_AUDIO_TEST)
std::optional<QString> MainWindow::pollLiveAudioTestJttyFrequencyChanges()
{
  if (!m_automated_test || !m_config.is_dummy_rig() || m_mode != "JTTY" || !m_jttyReceive)
    return "JTTY frequency checks require the isolated test receiver.";
  auto const snapshot = m_operatingFrequency.snapshot();
  if (snapshot.recoveryRequired)
    return "The isolated JTTY receiver frequency requires CAT recovery.";
  if (snapshot.pendingRx || snapshot.pendingTx) return std::nullopt;

  auto& state = *m_jttyReceive;
  using Stage = JttyReceiveState::FrequencyCheckStage;
  switch (state.frequencyCheckStage) {
  case Stage::Initialize:
    if (!snapshot.rx
        && !requestNominalFrequencyChange(14078000, FrequencyRequestOrigin::User))
      return "Unable to initialize the isolated JTTY receiver frequency.";
    state.frequencyCheckStage = Stage::Unchanged;
    break;
  case Stage::Unchanged: {
    state.frequencyCheckNominal = snapshot.rx;
    state.frequencyCheckContext = state.currentContext;
    int const audioFrequency = ui->RxFreqSpinBox_2->value();
    ui->RxFreqSpinBox_2->setValue(audioFrequency + 1);
    ui->RxFreqSpinBox_2->setValue(audioFrequency);
    if (state.currentContext != state.frequencyCheckContext)
      return "Changing the JTTY audio-frequency selection ended reception.";
    if (!requestNominalFrequencyChange(state.frequencyCheckNominal, FrequencyRequestOrigin::User))
      return "The isolated JTTY receiver rejected an unchanged RF frequency.";
    state.frequencyCheckStage = Stage::Retune;
    break;
  }
  case Stage::Retune:
    if (state.currentContext != state.frequencyCheckContext)
      return "An unchanged JTTY RF frequency reset the receive context.";
    if (!requestNominalFrequencyChange(state.frequencyCheckNominal + 100, FrequencyRequestOrigin::User))
      return "The isolated JTTY receiver rejected an RF retune.";
    state.frequencyCheckStage = Stage::Restore;
    break;
  case Stage::Restore:
    if (state.currentContext == state.frequencyCheckContext)
      return "An actual JTTY RF retune did not replace the receive context.";
    if (!requestNominalFrequencyChange(state.frequencyCheckNominal, FrequencyRequestOrigin::User))
      return "Unable to restore the isolated JTTY receiver frequency.";
    state.frequencyCheckStage = Stage::Complete;
    break;
  case Stage::Complete:
    if (snapshot.rx != state.frequencyCheckNominal
        || snapshot.correctedRx != state.frequencyCheckNominal)
      return "The isolated JTTY receiver did not restore its RF frequency.";
    return QString {};
  }
  return std::nullopt;
}

bool MainWindow::startLiveAudioTestJttyWav(QString const& path)
{
  if (!m_automated_test || !m_config.is_dummy_rig() || m_mode != "JTTY"
      || m_monitoring || decoderBusy()) return false;
  m_diskData = true;
  // A previous load can be invalidated while its result callback processes events.
  m_discardJttyWavLoad = true;
  read_wav_file(path);
  return true;
}

QString MainWindow::checkLiveAudioTestJttyMailboxOverflow()
{
  if (!m_automated_test || !m_jttyReceive || m_jttyReceive->active)
    return "Mailbox checks require an idle isolated JTTY receiver.";
  auto& state = *m_jttyReceive;
  JttyReceivePublisher source, otherSource;
  source.setContext(state.currentContext);
  source.begin(100000);
  consumeJttyAudio(source.mailbox());
  pumpJttyReceive();
  if (!state.active) return "The test reception did not begin.";
  short const sample = 0;
  otherSource.begin(100000);
  for (int i = 0; i < 1023; ++i) otherSource.append(&sample, 1);
  otherSource.end(JttyReceiveReason::MonitorStopped);
  consumeJttyAudio(otherSource.mailbox());
  pumpJttyReceive();
  if (!state.active) return "Another source's overflow ended the active reception.";
  source.end(JttyReceiveReason::InputError);
  source.begin(101000);
  for (int i = 0; i < 1022; ++i) source.append(&sample, 1);
  source.end(JttyReceiveReason::MonitorStopped);
  consumeJttyAudio(source.mailbox());
  pumpJttyReceive();
  if (state.active) return "Mailbox overflow lost the active reception's end.";
  return {};
}

QString MainWindow::checkLiveAudioTestJttyDrain()
{
  if (!m_automated_test || !m_config.is_dummy_rig() || m_mode != "JTTY"
      || m_monitoring || decoderBusy())
    return "Drain checks require a stopped isolated JTTY receiver.";
  auto& state = *m_jttyReceive;
  bool const diskData = m_diskData;
  bool const saveAll = m_saveAll, saveDecoded = m_saveDecoded;
  m_diskData = false;
  m_saveAll = m_saveDecoded = false;
  QString error;
  for (bool missingAcknowledgement : {false, true}) {
    updateJttyReceivePolicy(true);
    auto const context = state.currentContext;
    JttyReceivePublisher source;
    source.setContext(context);
    source.begin(100000);
    std::vector<short> samples(3456);
    quint32 noise = 1;
    for (auto& sample : samples) {
      noise = noise * 1664525u + 1013904223u;
      sample = short(int(noise >> 20) - 2048);
    }
    constexpr int blocks = 64;
    for (int i = 0; i < blocks; ++i) source.append(samples.data(), int(samples.size()));
    if (!missingAcknowledgement) source.end(JttyReceiveReason::MonitorStopped);
    consumeJttyAudio(source.mailbox());
    state.drainIgnoredSources = missingAcknowledgement ? 2 : 0;
    state.drainTimeoutMs = missingAcknowledgement ? 1 : 5000;
    int progressCallbacks = 0;
    qint64 lastSearch = -1;
    bool latePublication = false;
    bool lateEndNotified = false;
    bool playbackStopped = false;
    QTimer heartbeat;
    auto const wavFrames = dec_data.params.kin;
    auto const wavSample = dec_data.d2[0];
    QEventLoop loadLoop;
    connect(&m_wav_load_coordinator, &WavLoadCoordinator::loadingChanged,
            &loadLoop, [&loadLoop](bool loading) { if (!loading) loadLoop.quit(); });
    m_discardJttyWavLoad = false;
    if (!m_wav_load_coordinator.start([] {
      auto result = std::make_shared<Radio::WavInputResult>();
      result->valid = true;
      result->frames = 1;
      result->samples = {12345};
      return result;
    })) {
      error = "Unable to start the isolated WAV load before JTTY drain.";
      break;
    }
    m_loopall = m_startAnother = m_bNoMoreFiles = m_diskData = true;
    connect(this, &MainWindow::endTransmitMessage, &heartbeat, [&] { playbackStopped = true; });
    if (missingAcknowledgement) {
      auto const epoch = m_jttyTxLifecycle.begin (JttyTxLifecycle::Backend::Local);
      if (!epoch.isValid () || !m_jttyTxLifecycle.addPending (1, 1))
        error = "Could not initialize the synthetic JTTY transmit session.";
    }
    connect(&heartbeat, &QTimer::timeout, this, [&] {
      if (!state.draining) return;
      if (state.active && state.live.nextSearchSample() != lastSearch) {
        lastSearch = state.live.nextSearchSample();
        ++progressCallbacks;
      }
      monitor(true);
      startJttyReview(0, 0, false);
      on_actionFT8_triggered();
      if (m_monitoring || !state.jobs.empty() || m_mode != "JTTY"
          || state.currentContext != context)
        error = "A reentrant callback replaced the draining JTTY receiver.";
      if (state.drainCutoff && !latePublication) {
        latePublication = true;
        source.append(samples.data(), int(samples.size()));
        consumeJttyAudio(source.mailbox());
      }
    });
    heartbeat.start(0);
    drainJttyReceive();
    heartbeat.stop();
    if (m_wav_load_coordinator.isLoading()) {
      QTimer timeout;
      timeout.setSingleShot(true);
      connect(&timeout, &QTimer::timeout, &loadLoop, &QEventLoop::quit);
      timeout.start(2000);
      loadLoop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (m_wav_load_coordinator.isLoading() || m_loopall || m_startAnother || m_bNoMoreFiles
        || m_diskData || dec_data.params.kin != wavFrames || dec_data.d2[0] != wavSample)
      error = "JTTY drain did not discard its pending WAV batch.";
    qint64 const expected = blocks * qint64(samples.size());
    if (state.active || !state.mailboxes.empty() || state.history.end(state.liveSpan.id) != expected
        || state.live.nextSearchSample() + windowSamples <= expected)
      error = "JTTY drain did not process the complete accepted audio batch.";
    if (progressCallbacks < 2)
      error = "JTTY drain did not yield between decoder work slices.";
    if (missingAcknowledgement && !latePublication)
      error = "The missing-acknowledgement drain did not exercise its input cutoff.";
    if (missingAcknowledgement && (!playbackStopped
        || m_jttyTxLifecycle.active () || m_monitoring))
      error = "JTTY drain did not stop pending playback without restarting reception.";
    lateEndNotified = source.end(JttyReceiveReason::MonitorStopped);
    if (missingAcknowledgement && !lateEndNotified)
      error = "JTTY input cutoff did not rearm the source notification.";
    consumeJttyAudio(source.mailbox());
    pumpJttyReceive();
    if (state.active || state.history.end(state.liveSpan.id) != expected)
      error = "Late source audio reopened a drained JTTY reception.";
    if (!error.isEmpty()) break;
  }
  state.drainIgnoredSources = 0;
  state.drainTimeoutMs = 5000;
  updateJttyReceivePolicy(true);
  m_diskData = diskData;
  m_saveAll = saveAll;
  m_saveDecoded = saveDecoded;
  updateJttySavePolicy();
  updateDecodeControls();
  if (error.isEmpty() && (!ui->actionOpen->isEnabled()
      || !ui->actionDecode_remaining_files_in_directory->isEnabled()
      || !ui->monitorButton->isEnabled()))
    error = "A cancelled JTTY WAV load left file controls disabled.";
  return error;
}
#endif

void MainWindow::drainJttyReceive()
{
  if (!m_jttyReceive || m_jttyReceive->draining) return;
  auto& state = *m_jttyReceive;
  state.draining = true;
  state.drainCutoff = false;
  if (m_wav_load_coordinator.isLoading()) m_discardJttyWavLoad = true;
  if (m_mode == "JTTY"
      && (m_jttyTxLifecycle.active () || !m_jttyTransmitQueue.empty ()
          || m_transmitting || m_tune)) {
    noteTxStopReason(m_closing ? TxEvidence::TxStopReason::UserHalt : TxEvidence::TxStopReason::ModeChange);
    stopTx();
  }
  auto const request = ++state.nextDrain;
  QEventLoop stopped;
  int acknowledged = 0;
  bool timedOut = false;
  bool finished = false;
  auto checkFinished = [&] {
    bool const pending = !state.mailboxes.empty() || state.pumping
      || (state.active && state.live.nextSearchSample() + windowSamples <= state.history.end(state.liveSpan.id));
    if ((acknowledged == 3 || timedOut) && !pending) {
      finished = true;
      stopped.quit();
    }
  };
  state.drainProgress = checkFinished;
  auto acknowledge = [&](int source, quint64 id, JttyReceiveMailboxPtr mailbox) {
    if (id != request || timedOut) return;
#if defined(WSJT_ENABLE_LIVE_AUDIO_TEST)
    if (state.drainIgnoredSources & source) return;
#endif
    consumeJttyAudio(std::move(mailbox));
    acknowledged |= source;
    checkFinished();
  };
  auto local = connect(m_detector, &Detector::continuousReceiveDrained, &stopped,
    [&](quint64 id, JttyReceiveMailboxPtr mailbox) { acknowledge(1, id, std::move(mailbox)); });
  auto tci = connect(&m_config, &Configuration::continuousReceiveDrained, &stopped,
    [&](quint64 id, JttyReceiveMailboxPtr mailbox) { acknowledge(2, id, std::move(mailbox)); });
  QTimer timeout;
  timeout.setSingleShot(true);
  connect(&timeout, &QTimer::timeout, &stopped, [&] {
    if (acknowledged == 3) return;
    timedOut = true;
    state.drainCutoff = true;
    // A missing source barrier must not let later publications extend this drain forever.
    for (auto& mailbox : state.mailboxes) {
      auto pending = mailbox->detachPending();
      if (state.liveMailbox == mailbox) state.liveMailbox = pending;
      mailbox = std::move(pending);
    }
    LOG_WARN("JTTY input did not acknowledge shutdown before timeout");
    checkFinished();
  });
  statusBar()->showMessage(tr("Finishing JTTY reception…"));
  if (m_monitoring) monitor(false);
  updateJttyReceivePolicy(false);
  m_detector->requestContinuousReceiveDrain(request);
  m_config.requestContinuousReceiveDrain(request);
  int timeoutMs = 5000;
#if defined(WSJT_ENABLE_LIVE_AUDIO_TEST)
  if (m_automated_test) timeoutMs = state.drainTimeoutMs;
#endif
  timeout.start(timeoutMs);
  if (!state.scheduled) {
    state.scheduled = true;
    QTimer::singleShot(0, this, &MainWindow::pumpJttyReceive);
  }
  checkFinished();
  if (!finished) stopped.exec(QEventLoop::ExcludeUserInputEvents);
  state.drainProgress = {};
  disconnect(local); disconnect(tci);
  finishJttyReception();
  finishJttyDisk();
  state.contexts.clear();
  state.draining = false;
  state.drainCutoff = false;
  if (timedOut) statusBar()->showMessage(tr("JTTY input did not acknowledge stopping; available audio was drained."));
  else statusBar()->clearMessage();
  if (state.closeAfterDrain) {
    state.closeAfterDrain = false;
    QTimer::singleShot(0, this, &MainWindow::close);
  }
}

void MainWindow::consumeJttyAudio(JttyReceiveMailboxPtr mailbox)
{
  if (!mailbox) return;
  initializeJttyReceive();
  auto& state = *m_jttyReceive;
  if (state.drainCutoff) {
    mailbox->discardPending();
    return;
  }
  if (std::find(state.mailboxes.begin(), state.mailboxes.end(), mailbox) == state.mailboxes.end())
    state.mailboxes.push_back(std::move(mailbox));
  if (!state.scheduled) {
    state.scheduled = true;
    QTimer::singleShot(0, this, &MainWindow::pumpJttyReceive);
  }
}

void MainWindow::finishJttyReception()
{
  if (!m_jttyReceive || !m_jttyReceive->active) return;
  auto& state = *m_jttyReceive;
  state.live.end();
  state.apply(*this, state.live.takeUpdates(), state.liveSpan, JttyReceiveState::DecodeSource::Live);
  state.recording.endReception();
  if (m_mode == "JTTY" && !m_diskData) m_wideGraph->jttyReceptionEnded();
  state.active = false;
  state.liveMailbox.reset();
}

void MainWindow::restartJttyReception()
{
  auto& state = *m_jttyReceive;
  bool const current = state.continuous && state.liveSpan.contextId == state.currentContext;
  finishJttyReception();
  if (current) {
    m_detector->endContinuousReception(JttyReceiveReason::InputError);
    m_config.transceiver_receive_discontinuity(JttyReceiveReason::InputError);
  }
  statusBar()->showMessage(tr("JTTY receive audio was lost; decoding restarted."));
}

void MainWindow::pumpJttyReceive()
{
  if (!m_jttyReceive) return;
  auto& state = *m_jttyReceive;
  state.scheduled = false;
  if (state.pumping) return;
  state.pumping = true;
  QElapsedTimer budget;
  budget.start();
  bool work = true;
  while (work && budget.elapsed() < 12) {
    work = false;
    if (state.active) {
      auto const end = state.history.end(state.liveSpan.id);
      auto const search = state.live.nextSearchSample();
      if (search + windowSamples <= end) {
        bool const current = m_mode == "JTTY" && state.liveSpan.contextId == state.currentContext;
        auto const first = state.live.nextRequiredSample();
        auto pcm = state.history.snapshot(state.liveSpan.id, first, search + windowSamples);
        int const processed = pcm.empty() ? -2 : state.live.process(pcm.data(), int(pcm.size()), first,
          search + windowSamples, 1,
          current ? m_wideGraph->nStartFreq() : state.liveSpan.low,
          current ? m_wideGraph->Fmax() : state.liveSpan.high,
          current ? ui->RxFreqSpinBox_2->value() : state.liveSpan.center,
          current ? ui->sbFtol_2->value() : state.liveSpan.tolerance);
        if (processed < 0) {
          LOG_WARN("JTTY required receive audio unavailable");
          restartJttyReception();
        } else {
          state.apply(*this, state.live.takeUpdates(), state.liveSpan, JttyReceiveState::DecodeSource::Live);
          state.recording.advanceDecoderWatermark(state.live.nextRequiredSample());
        }
        work = processed > 0;
        if (work) continue;
      }
#if defined(WSJT_ENABLE_LIVE_AUDIO_TEST)
      if (m_automated_test && state.acknowledged != end) {
        state.acknowledged = end;
        Q_EMIT liveAudioTestJttyFramesConsumed(end);
      }
#endif
    }
    if (!state.mailboxes.empty()) {
      auto const sourceMailbox = state.mailboxes.front();
      JttyReceiveEvent event;
      if (!sourceMailbox->take(event)) { state.mailboxes.pop_front(); work = true; continue; }
      work = true;
      if (event.kind == JttyReceiveEvent::Kind::Begin) {
        if (!state.contexts.contains(event.context)) {
          if (event.context) {
            LOG_WARN("JTTY operating context expired before input could be processed");
            statusBar()->showMessage(tr("JTTY receive audio was lost; decoding restarted."));
          }
          continue;
        }
        finishJttyReception();
        state.liveSpan = state.contexts[event.context];
        state.liveMailbox = sourceMailbox;
        state.liveSpan.id = event.session;
        state.liveSpan.displayGroup = ++state.nextDisplayGroup;
        state.liveSpan.anchor = QDateTime::fromMSecsSinceEpoch(event.anchorUtcMs, Qt::UTC);
        state.liveSpan.displayAnchor = state.liveSpan.anchor;
        state.spans[event.session] = state.liveSpan;
        state.live.begin(event.session, 0, event.firstSample);
        state.spectrum.begin(event.session, event.firstSample, event.anchorUtcMs);
        state.policy = savePolicy(m_saveAll, m_saveDecoded);
        auto settings = state.liveSpan.recording;
        settings.policy = state.policy;
        state.recording.beginReception(event.session, state.liveSpan.anchor, settings);
        state.active = true;
        state.acknowledged = -1;
        state.referenceMode = -1;
        int zero = 0;
        auto path = m_config.writeable_data_dir().absoluteFilePath("refspec.dat").toLocal8Bit();
        refspectrum_(dec_data.d2, &zero, &m_bClearRefSpec, &m_bRefSpec, &m_bUseRef,
                     path.constData(), path.size());
      } else if (event.kind == JttyReceiveEvent::Kind::Samples && state.active
                 && event.session == state.liveSpan.id) {
        auto& recording = state.liveSpan.recording;
        recording.directory = m_config.save_directory().absolutePath();
        recording.myCall = m_config.my_callsign(); recording.myGrid = m_config.my_grid();
        recording.hisCall = m_hisCall; recording.hisGrid = m_hisGrid;
        state.recording.updateMetadata(recording);
        auto policy = savePolicy(m_saveAll, m_saveDecoded);
        if (policy != state.policy) { state.recording.updateSavePolicy(policy); state.policy = policy; }
        m_bUseRef = m_wideGraph->useRef();
        auto path = m_config.writeable_data_dir().absoluteFilePath("refspec.dat").toLocal8Bit();
        int const referenceMode = m_bRefSpec ? 1 : m_bUseRef ? 2 : 0;
        if (state.referenceMode != referenceMode || m_bClearRefSpec) {
          int zero = 0;
          refspectrum_(event.samples.data(), &zero, &m_bClearRefSpec, &m_bRefSpec,
                       &m_bUseRef, path.constData(), path.size());
          state.referenceMode = referenceMode;
        }
        m_bClearRefSpec = false;
        for (int offset = 0; offset < int(event.samples.size());) {
          int count = std::min(3456, int(event.samples.size()) - offset);
          if (m_bRefSpec || m_bUseRef)
            refspectrum_(event.samples.data() + offset, &count, &m_bClearRefSpec,
                         &m_bRefSpec, &m_bUseRef, path.constData(), path.size());
          offset += count;
        }
        if (!state.history.append(event.session, event.firstSample, event.samples.data(), int(event.samples.size()))) {
          restartJttyReception(); continue;
        }
        state.recording.feed(event.firstSample, event.samples.data(), int(event.samples.size()));
        state.spectrum.process(event.firstSample, event.samples.data(), int(event.samples.size()), m_inGain,
          m_config.lowSidelobes(), m_wideGraph->smoothYellow() - 1, [this](JttySpectrumFrame const& frame) {
            if (m_mode == "JTTY" && !m_diskData) {
              m_wideGraph->jttyDataSink(frame);
              ui->signal_meter_widget->setValue(frame.powerDb, frame.maxPowerDb);
            }
          });
        m_dataAvailable = true;
        for (auto it = state.spans.begin(); it != state.spans.end();) {
          if (it.key() != state.liveSpan.id && state.history.first(it.key()) < 0) it = state.spans.erase(it);
          else ++it;
        }
      } else if (event.kind == JttyReceiveEvent::Kind::End || event.kind == JttyReceiveEvent::Kind::Gap) {
        // Overflow can discard session transitions, so a Gap ends its source mailbox's active reception.
        if (event.kind == JttyReceiveEvent::Kind::Gap
              ? state.liveMailbox == sourceMailbox
              : event.session == state.liveSpan.id) finishJttyReception();
        if (event.kind == JttyReceiveEvent::Kind::Gap)
          statusBar()->showMessage(tr("JTTY receive audio was lost; decoding restarted."));
      }
      continue;
    }
    if (!state.jobs.empty() && !state.disk) {
      auto& job = state.jobs.front();
      if (!state.reviewing) {
        state.review.end(); state.review.takeUpdates();
        state.review.begin(job.span.id, 0, job.first);
        state.reviewSpan = job.span;
        state.reviewing = true;
      }
      int steps = state.review.process(job.pcm.data(), int(job.pcm.size()), job.first, job.stop, 1,
        job.span.low, job.span.high, job.span.center, job.span.tolerance);
      auto updates = state.review.takeUpdates();
      bool done = steps <= 0;
      for (auto const& update : updates)
        if (job.picked && update.terminal == Jtty::ReceiveTerminal::Complete
            && std::abs(update.frequency - job.span.center) < job.span.tolerance) done = true;
      state.apply(*this, updates, job.span, JttyReceiveState::DecodeSource::Review);
      if (done) {
        state.review.end();
        state.apply(*this, state.review.takeUpdates(), job.span, JttyReceiveState::DecodeSource::Review);
        state.jobs.pop_front(); state.reviewing = false;
        if (state.jobs.empty()) finishDecodeUi();
      }
      work = true;
    }
  }
  state.pumping = false;
  if (work && !state.scheduled) {
    state.scheduled = true;
    QTimer::singleShot(0, this, &MainWindow::pumpJttyReceive);
  }
  if (state.drainProgress) state.drainProgress();
}

void MainWindow::startJttyReview(quint64 reception, qint64 sample, bool picked)
{
  if (jttyDrainInProgress()) return;
  initializeJttyReceive();
  auto& state = *m_jttyReceive;
  if (state.disk) {
    statusBar()->showMessage(tr("Wait for the recording to finish decoding before reviewing it."));
    return;
  }
  cancelJttyReview();
  auto add = [&](ReceiveSpan span, qint64 first, qint64 end, std::vector<short> pcm) {
    if (pcm.empty()) return;
    span.displayGroup = ++state.nextDisplayGroup;
    span.center = ui->RxFreqSpinBox_2->value(); span.tolerance = ui->sbFtol_2->value();
    span.low = m_wideGraph->nStartFreq(); span.high = m_wideGraph->Fmax();
    state.jobs.push_back({span, first, end, std::move(pcm), picked});
  };
  if (m_diskData && (!picked || reception == state.diskSpan.id)) {
    qint64 first = picked ? std::max<qint64>(0, sample - 5 * 12000) : 0;
    qint64 end = picked ? std::min<qint64>(dec_data.params.kin, first + 40 * 12000) : dec_data.params.kin;
    if (end > first && (!picked || (sample > 0 && sample <= dec_data.params.kin)))
      add(state.diskSpan, first, end, {dec_data.d2 + first, dec_data.d2 + end});
  } else if (picked) {
    auto const firstAvailable = state.history.first(reception);
    auto const endAvailable = state.history.end(reception);
    if (firstAvailable < 0 || sample <= firstAvailable || sample > endAvailable || !state.spans.contains(reception)) {
      statusBar()->showMessage(tr("Audio for this waterfall row is no longer available."));
      finishDecodeUi();
      return;
    }
    auto const first = std::max(firstAvailable, sample - 5 * 12000);
    auto const end = std::min(endAvailable, first + 40 * 12000);
    add(state.spans[reception], first, end, state.history.snapshot(reception, first, end));
  } else {
    for (auto const& span : state.spans) {
      auto first = state.history.first(span.id), end = state.history.end(span.id);
      if (first >= 0) add(span, first, end, state.history.snapshot(span.id, first, end));
    }
  }
  if (!state.jobs.empty()) {
    statusBar()->showMessage(tr("Review uses retained audio; earlier message context may be missing."));
    ui->DecodeButton->setChecked(true);
    if (!state.scheduled) { state.scheduled = true; QTimer::singleShot(0, this, &MainWindow::pumpJttyReceive); }
  } else finishDecodeUi();
}

bool MainWindow::jttyReviewBusy() const
{
  return m_jttyReceive && (m_jttyReceive->draining || m_jttyReceive->disk || !m_jttyReceive->jobs.empty());
}

bool MainWindow::jttyDrainInProgress() const
{
  return m_jttyReceive && m_jttyReceive->draining;
}

bool MainWindow::jttyDiskActive() const
{
  return m_jttyReceive && m_jttyReceive->disk;
}

void MainWindow::closeAfterJttyDrain()
{
  m_jttyReceive->closeAfterDrain = true;
}

void MainWindow::beginJttyDisk()
{
  initializeJttyReceive();
  auto& state = *m_jttyReceive;
  finishJttyDisk();
  cancelJttyReview();
  state.diskSpan = {};
  state.diskSpan.id = ++state.nextReplay;
  state.diskSpan.displayGroup = ++state.nextDisplayGroup;
  state.diskSpan.anchor = m_UTCdiskDateTime;
  state.diskSpan.displayAnchor = Jtty::jttyLineStartTimeUtc(m_UTCdiskDateTime, m_UTCdisk, 0);
  state.diskSpan.context = currentDecodeOperatingContext();
  state.diskSpan.context.diskData = true;
  state.diskSpan.low = m_wideGraph->nStartFreq(); state.diskSpan.high = m_wideGraph->Fmax();
  state.diskSpan.center = ui->RxFreqSpinBox_2->value(); state.diskSpan.tolerance = ui->sbFtol_2->value();
  state.diskK = -1;
  state.results.resetReplay();
}

void MainWindow::decodeJttyDisk(int k)
{
  if (!m_diskData) return;
  initializeJttyReceive();
  auto& state = *m_jttyReceive;
  if (!state.disk || k <= state.diskK) {
    finishJttyDisk();
    if (!state.diskSpan.id) beginJttyDisk();
    state.diskDecoder.begin(state.diskSpan.id, 0, 0);
    state.diskSpectrum.begin(state.diskSpan.id, 0, state.diskSpan.anchor.toMSecsSinceEpoch());
    state.disk = true; state.diskK = 0;
    state.results.resetReplay();
  }
  if (k > state.diskK)
    state.diskSpectrum.process(state.diskK, dec_data.d2 + state.diskK, k - state.diskK,
      m_inGain, m_config.lowSidelobes(), m_wideGraph->smoothYellow() - 1,
      [this](JttySpectrumFrame const& frame) { m_wideGraph->jttyDataSink(frame); });
  state.diskK = k;
  m_k0 = k;
  state.diskDecoder.process(dec_data.d2, k, 0, k, 0x7fffffff,
    state.diskSpan.low, state.diskSpan.high, state.diskSpan.center, state.diskSpan.tolerance);
  auto updates = state.diskDecoder.takeUpdates();
  state.apply(*this, updates, state.diskSpan, JttyReceiveState::DecodeSource::Wav);
}

void MainWindow::finishJttyDisk()
{
  if (!m_jttyReceive || !m_jttyReceive->disk) return;
  auto& state = *m_jttyReceive;
  state.diskDecoder.end();
  state.apply(*this, state.diskDecoder.takeUpdates(), state.diskSpan, JttyReceiveState::DecodeSource::Wav);
  m_wideGraph->jttyReceptionEnded();
  state.disk = false;
  if (!state.jobs.empty() && !state.scheduled) {
    state.scheduled = true;
    QTimer::singleShot(0, this, &MainWindow::pumpJttyReceive);
  }
}
