#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "commons.h"
#include "JttyMessages.hpp"
#include "JttyN1mm.hpp"
#include "JttyReceiveLine.hpp"
#include "JttyTransmitText.hpp"
#include "Logger.hpp"
#include "models/DecodeHighlightingModel.hpp"
#include <QByteArray>
#include <QDateTime>
#include <QSettings>
#include <QTextCharFormat>
#include <QTextCursor>
#include <algorithm>
#include "Modulator/Modulator.hpp"
#ifdef WIN32
#include "MMTTYIF.hpp"
#undef MessageBox
#endif


extern qint32 g_iptt;

namespace
{
  Jtty::NativeExchangeProfile jttyExchangeProfile(Configuration const& configuration)
  {
    switch (configuration.special_op_id()) {
    case Configuration::SpecialOperatingActivity::FIELD_DAY:
      return Jtty::NativeExchangeProfile::FieldDay;
    case Configuration::SpecialOperatingActivity::RTTY:
      return Jtty::NativeExchangeProfile::RttyRoundup;
    default:
      return Jtty::NativeExchangeProfile::None;
    }
  }

  Jtty::NativeMacroContext jttyNativeMacroContext(
      Configuration const& configuration, QString const& hisCall, int serialNumber,
      int snr)
  {
    Jtty::NativeMacroContext context;
    context.myCall = configuration.my_callsign();
    context.hisCall = hisCall;
    context.serialNumber = serialNumber;
    context.grid = configuration.my_grid();
    context.snr = snr;
    context.exchangeProfile = jttyExchangeProfile(configuration);

    switch (context.exchangeProfile) {
    case Jtty::NativeExchangeProfile::FieldDay:
      context.configuredExchange = Jtty::normalizedFieldDayExchange(
        configuration.Field_Day_Exchange());
      break;
    case Jtty::NativeExchangeProfile::RttyRoundup:
      context.configuredExchange = configuration.RTTY_Exchange();
      break;
    default:
      break;
    }
    return context;
  }

  QString jttyNativeEncodeError(int status)
  {
    if (status == static_cast<int>(Jtty::NativeEncodeStatus::UnknownSection)) {
      return QStringLiteral("Field Day section is not registered in the ARRL/RAC table");
    }
    return QStringLiteral("native atom encoding failed");
  }
}

// Backstop cap: batches at least this many words per auto-advance burst even if the operator never pauses (the idle timer below normally fires first).
constexpr int kJttyAutoAdvanceBatchWords = 5;

// A typing pause this long, with anything safely committable pending, flushes it rather than waiting for kJttyAutoAdvanceBatchWords to fill up.
constexpr int kJttyAutoAdvanceIdleMs = 1500;

extern "C" {
  void genjtty_atoms_c(Jtty::NativeAtomDescriptor const atoms[], int natoms,
                       int itone[], int* nsym, int* status);
}

#ifdef WIN32
static QString append_separator(QString message) {
    if (!message.isEmpty()) {
        QChar lastChar = message.at(message.length() - 1);
        if (lastChar != '\r' && lastChar != '\n' && lastChar != ' ') {
            message += "\r\n";
        }
    }
    return message;
}
#endif



void MainWindow::updateJttyDecodeHeadings()
{
  QString const prefix = ui->cbIncludeTime->isChecked()
    ? QStringLiteral("  UTC  Freq  dB  ") : QStringLiteral("Freq  dB  ");
  ui->lh_decodes_headings_label->setText(prefix + tr ("Message"));
  ui->rh_decodes_headings_label->setText(prefix + tr ("Message"));
}

void MainWindow::on_cbIncludeTime_toggled(bool)
{
  if (m_mode == "JTTY") {
    updateJttyDecodeHeadings();
    refreshJttyReceiveLines();
  }
}

void MainWindow::jtty_tx(QString message)
{
  // Render and enqueue immediately; the shared transmit buffer chains messages
  // gaplessly while playback is underway.
  submitJttyText(message);
}

qint64 MainWindow::submitJttyText(QString message)
{
  qint64 const requestId = ++m_jttyTxRequestId;
  execute_jtty_tx(requestId, message);
  return requestId;
}

// Ctrl+K / Send button: one-shot send -- force-flushes, marks final (EOM), and disarms; typing more afterward needs another explicit Alt+J or Ctrl+K, nothing auto-advances.
void MainWindow::commitJttyLiveEntry()
{
  if (m_mode != "JTTY") return;
  m_jttyAutoAdvanceIdleTimer.stop ();
  commitJttyLiveEntryPlan (/*forceFlush=*/true, Jtty::maxCompactAtomWords, /*isFinal=*/true);
  m_jttyLiveEntryArmed = false;
}

// Alt+J: force-flushes like Ctrl+K but never marks final, and arms auto-advance/idle-flush; stays open (continuously filler-padded by finishJttyDrain) until Alt+K signs off or Halt Tx/Esc cancels it.
void MainWindow::startJttyLiveStream()
{
  if (m_mode != "JTTY") return;
  m_jttyLiveEntryArmed = true;
  m_jttyAutoAdvanceIdleTimer.stop ();
  commitJttyLiveEntryPlan (/*forceFlush=*/true, Jtty::maxCompactAtomWords, /*isFinal=*/false);
}

// Once armed, batches several words per commit rather than firing per word (each commit is its own transmission, and one-word bursts showed up as one decode line per word); fires on kJttyAutoAdvanceBatchWords piling up, or a pause via idleFlushJttyLiveEntry.
void MainWindow::autoAdvanceJttyLiveEntry()
{
  if (m_mode != "JTTY" || !m_jttyLiveEntryArmed || m_guardingJttyLiveEntryLock) return;
  QString const uncommitted = ui->Tx_Message->toPlainText ().mid (m_jttyLiveEntryCommitted);
  int const completeWords = Jtty::completeWordCount (uncommitted);
  if (completeWords - Jtty::maxCompactAtomWords >= kJttyAutoAdvanceBatchWords) {
    m_jttyAutoAdvanceIdleTimer.stop ();
    commitJttyLiveEntryPlan (/*forceFlush=*/false, Jtty::maxCompactAtomWords, /*isFinal=*/false);
    return;
  }
  // Arms on any uncommitted content, complete word or not -- idleFlushJttyLiveEntry releases even a trailing not-yet-space-terminated word once the pause happens.
  if (!uncommitted.trimmed ().isEmpty ()) {
    m_jttyAutoAdvanceIdleTimer.start (kJttyAutoAdvanceIdleMs);
  } else {
    m_jttyAutoAdvanceIdleTimer.stop ();
  }
}

// Force-flushes everything typed, including a trailing word with no trailing space yet, but isFinal=false unlike Ctrl+K, so the message stays open; a detected pause means no keystroke is coming to extend an atom or a partial word, so the usual holdback/complete-word protections no longer apply. Doesn't queue filler itself -- finishJttyDrain keeps the session continuously padded for as long as it's armed.
void MainWindow::idleFlushJttyLiveEntry()
{
  if (m_mode != "JTTY" || !m_jttyLiveEntryArmed || m_guardingJttyLiveEntryLock) return;
  commitJttyLiveEntryPlan (/*forceFlush=*/true, Jtty::maxCompactAtomWords, /*isFinal=*/false);
}

void MainWindow::sendJttyFillerPadding()
{
  qint64 const requestId = ++m_jttyTxRequestId;
  execute_jtty_tx (requestId, Jtty::jttyFillerText, /*isFinal=*/false);
}

bool MainWindow::commitJttyLiveEntryPlan(bool forceFlush, int holdbackWords, bool isFinal)
{
  QString const uncommitted = ui->Tx_Message->toPlainText ().mid (m_jttyLiveEntryCommitted);
  auto const plan = Jtty::planIncrementalCommit (uncommitted, forceFlush, holdbackWords);
  if (plan.text.trimmed ().isEmpty ()) return false;

  QString expanded = jtty_msg_expand (plan.text);
  if (ui->cbLowerCase->isChecked ()) expanded = expanded.toLower ();
  expanded = Jtty::prepareTransmitText (expanded).text;

  m_guardingJttyLiveEntryLock = true;
  QTextCursor replace (ui->Tx_Message->document ());
  replace.setPosition (m_jttyLiveEntryCommitted);
  replace.setPosition (m_jttyLiveEntryCommitted + plan.length, QTextCursor::KeepAnchor);
  replace.insertText (expanded);
  qint64 const requestId = ++m_jttyTxRequestId;
  m_jttyLiveEntryPending.append ({requestId, m_jttyLiveEntryCommitted,
                                  m_jttyLiveEntryCommitted + expanded.size ()});
  m_jttyLiveEntryCommitted += expanded.size ();
  applyJttyLiveEntryFormatting ();
  m_guardingJttyLiveEntryLock = false;

  execute_jtty_tx (requestId, expanded, isFinal);

  for (auto& pending : m_jttyLiveEntryPending) {
    if (pending.requestId != requestId) continue;
    for (auto const& request : m_jttyTransmitQueue.requests ()) {
      if (request.id != requestId) continue;
      for (auto const& segment : request.segments) {
        pending.totalFrames += segment.tones.size () / 59;
      }
      break;
    }
    break;
  }
  updateJttyLiveEntryFrameLabel ();
  return true;
}

// Ctrl+Shift+K: clears the whole box, including locked/sent text, cancelling anything of it still queued/in flight.
void MainWindow::clearJttyLiveEntry()
{
  if (m_mode != "JTTY") return;
  m_jttyAutoAdvanceIdleTimer.stop ();
  if (m_jttyTxLifecycle.active () || !m_jttyTransmitQueue.empty ()) {
    abort_jtty_tx ();
  }
  m_jttyLiveEntryPending.clear ();
  m_jttyLiveEntryCommitted = 0;
  m_jttyLiveEntryFramesSent = 0;
  m_jttyLiveEntryArmed = false;
  m_jttyLiveEntryLockedText.clear ();
  m_guardingJttyLiveEntryLock = true;
  ui->Tx_Message->clear ();
  ui->Tx_Message->setCurrentCharFormat (QTextCharFormat {});
  m_guardingJttyLiveEntryLock = false;
  updateJttySendButton ();
  updateJttyLiveEntryFrameLabel ();
}

// Inserts text at the cursor like ordinary typing, redirecting to the end first if the cursor (or its selection) reaches into the locked prefix.
void MainWindow::insertJttyLiveEntryText(QString const& text)
{
  if (m_mode != "JTTY") return;
  QTextCursor cursor = ui->Tx_Message->textCursor ();
  if (qMin (cursor.position (), cursor.anchor ()) < m_jttyLiveEntryCommitted) {
    cursor.movePosition (QTextCursor::End);
  }
  cursor.insertText (text);
  ui->Tx_Message->setTextCursor (cursor);
}

// Ctrl+H: chat-style opener identifying both stations ("<hisCall> <myCall> "), expanded like any other macro at commit time.
void MainWindow::insertJttyChatOpener()
{
  insertJttyLiveEntryText (QStringLiteral ("%H %M "));
}

// Alt+K: sign-off ("DE <myCall> K "), then finalizes (marks EOM) and disarms, so finishJttyDrain stops topping up with filler and PTT drops once this finishes playing.
void MainWindow::insertJttyChatSignoff()
{
  insertJttyLiveEntryText (QStringLiteral ("DE %M K "));
  m_jttyAutoAdvanceIdleTimer.stop ();
  commitJttyLiveEntryPlan (/*forceFlush=*/true, Jtty::maxCompactAtomWords, /*isFinal=*/true);
  m_jttyLiveEntryArmed = false;
}

// Fraction (0..1) of a pending commit that should show as sent, stepping one JTTY frame at a time using pack_jtty's own frame_starts boundaries (segment.frameCharStarts) rather than an even split, which put the boundary mid-atom; falls back to an even split if a segment's boundaries weren't captured. Each frame is a fixed 59*1536 samples (genjtty_frames, lib/jtty/genjtty.f90). Returns 0 for a requestId not yet enqueued (not started, never "finished").
double MainWindow::jttyRequestSentFraction(qint64 requestId) const
{
  qint64 const samplesPerFrame = 59LL * 1536;
  for (auto const& request : m_jttyTransmitQueue.requests ()) {
    if (request.id != requestId) continue;
    int totalChars = 0;
    double doneChars = 0.0;
    for (auto const& segment : request.segments) {
      int const chars = segment.text.size ();
      totalChars += chars;
      int const nframes = segment.tones.size () / 59;
      if (nframes <= 0 || chars <= 0 || !segment.endSample) continue;
      if (m_jttyQueueProgress.served_samples >= segment.endSample) {
        doneChars += chars;
        continue;
      }
      qint64 const startSample = segment.endSample - segment.sampleCount ();
      qint64 const elapsed = m_jttyQueueProgress.served_samples - startSample;
      if (elapsed <= 0) continue;
      int const framesDone = int (qMin (qint64 (nframes), elapsed / samplesPerFrame));
      if (framesDone <= 0) continue;
      if (segment.frameCharStarts.size () == nframes) {
        doneChars += framesDone < nframes
          ? segment.frameCharStarts.at (framesDone) : chars;
      } else {
        doneChars += chars * (double (framesDone) / nframes);
      }
    }
    return totalChars > 0 ? doneChars / totalChars : 0.0;
  }
  return 0.0;
}

// Same elapsed-sample accounting as jttyRequestSentFraction, as a frame count for the label rather than a char fraction for the highlight.
int MainWindow::jttyRequestFramesDone(qint64 requestId) const
{
  qint64 const samplesPerFrame = 59LL * 1536;
  for (auto const& request : m_jttyTransmitQueue.requests ()) {
    if (request.id != requestId) continue;
    int doneFrames = 0;
    for (auto const& segment : request.segments) {
      int const nframes = segment.tones.size () / 59;
      if (nframes <= 0 || !segment.endSample) continue;
      if (m_jttyQueueProgress.served_samples >= segment.endSample) {
        doneFrames += nframes;
        continue;
      }
      qint64 const startSample = segment.endSample - segment.sampleCount ();
      qint64 const elapsed = m_jttyQueueProgress.served_samples - startSample;
      if (elapsed <= 0) continue;
      doneFrames += int (qMin (qint64 (nframes), elapsed / samplesPerFrame));
    }
    return doneFrames;
  }
  return 0;
}

// Dry-run of execute_jtty_tx's segmentation/encoding, without enqueuing, to preview an uncommitted message's frame count.
int MainWindow::countJttyTransmitFrames(QString const& preparedMessage) const
{
  auto const encoded = Jtty::encodeTransmitText (preparedMessage,
                                                 jttyExchangeProfile (m_config), true);
  if (encoded.status != Jtty::TransmitTextStatus::Encoded) return 0;
  int totalFrames = 0;
  for (auto const& segment : encoded.segments) {
    totalFrames += segment.transmit.tones.size () / Jtty::transmitFrameSymbols;
  }
  return totalFrames;
}

// Updates the "Frames: sent/total" label: drained history plus pending commits' exact/live-progress totals, plus a dry-run estimate for the uncommitted tail.
void MainWindow::updateJttyLiveEntryFrameLabel()
{
  if (m_mode != "JTTY") {
    ui->labelJttyFrameCount->clear ();
    return;
  }

  int sentFrames = m_jttyLiveEntryFramesSent;
  int committedFrames = m_jttyLiveEntryFramesSent;
  for (auto const& pending : m_jttyLiveEntryPending) {
    committedFrames += pending.totalFrames;
    sentFrames += jttyRequestFramesDone (pending.requestId);
  }

  QString const uncommitted =
    ui->Tx_Message->toPlainText ().mid (m_jttyLiveEntryCommitted);
  int uncommittedFrames = 0;
  if (!uncommitted.trimmed ().isEmpty ()) {
    QString probe = jtty_msg_expand (uncommitted);
    if (ui->cbLowerCase->isChecked ()) probe = probe.toLower ();
    probe = Jtty::prepareTransmitText (probe).text;
    uncommittedFrames = countJttyTransmitFrames (probe);
  }

  int const totalFrames = committedFrames + uncommittedFrames;
  if (totalFrames <= 0) {
    ui->labelJttyFrameCount->clear ();
  } else {
    ui->labelJttyFrameCount->setText (
      tr ("Frames: %1/%2").arg (sentFrames).arg (totalFrames));
  }
}

// Repaints committed text sent/queued by pending-span progress and resets everything past it to plain formatting, including the about-to-be-typed char format (so new text never inherits the preceding run). Caller must already hold m_guardingJttyLiveEntryLock.
void MainWindow::applyJttyLiveEntryFormatting()
{
  auto * document = ui->Tx_Message->document ();
  QTextCursor all (document);
  all.select (QTextCursor::Document);
  all.setCharFormat (QTextCharFormat {});

  // "Sent" uses the same Tx highlight as an echoed Tx line in the QSO Frequency pane, distinct from plain-gray "queued."
  auto const resolved = DecodeHighlightingModel::resolve_colors (
    m_config.decode_highlighting ().items (),
    {DecodeHighlightingModel::Highlight::Tx}, QColor (Qt::yellow),
    QColor (Qt::black));
  QTextCharFormat sentFormat;
  sentFormat.setBackground (resolved.background_);
  sentFormat.setForeground (resolved.foreground_);
  QTextCharFormat queuedFormat;
  queuedFormat.setForeground (QColor (Qt::gray));

  auto paintSent = [&] (int from, int to) {
    if (to <= from) return;
    QTextCursor cursor (document);
    cursor.setPosition (from);
    cursor.setPosition (to, QTextCursor::KeepAnchor);
    cursor.setCharFormat (sentFormat);
  };

  int paintedTo = 0;
  for (auto const& pending : m_jttyLiveEntryPending) {
    paintSent (paintedTo, pending.start);
    double const fraction = jttyRequestSentFraction (pending.requestId);
    int const split = pending.start + qBound (
      0, qRound ((pending.end - pending.start) * fraction),
      pending.end - pending.start);
    paintSent (pending.start, split);
    if (pending.end > split) {
      QTextCursor queued (document);
      queued.setPosition (split);
      queued.setPosition (pending.end, QTextCursor::KeepAnchor);
      queued.setCharFormat (queuedFormat);
    }
    paintedTo = pending.end;
  }
  paintSent (paintedTo, m_jttyLiveEntryCommitted);

  m_jttyLiveEntryLockedText = ui->Tx_Message->toPlainText ().left (m_jttyLiveEntryCommitted);
  ui->Tx_Message->setCurrentCharFormat (QTextCharFormat {});
}

// Reverts any edit that reached into the locked/committed prefix, however it got there (typing, backspace, paste, drag-drop).
void MainWindow::guardJttyLiveEntryLock()
{
  if (m_guardingJttyLiveEntryLock || m_mode != "JTTY"
      || m_jttyLiveEntryCommitted <= 0) return;
  if (ui->Tx_Message->toPlainText ().left (m_jttyLiveEntryCommitted)
      == m_jttyLiveEntryLockedText) return;
  m_guardingJttyLiveEntryLock = true;
  ui->Tx_Message->undo ();
  m_guardingJttyLiveEntryLock = false;
}

void MainWindow::execute_jtty_tx(qint64 requestId, QString message, bool isFinal)
{
  if (ui->cbLowerCase->isChecked()) message = message.toLower();
  auto encoded = Jtty::encodeTransmitText(message, jttyExchangeProfile(m_config), isFinal);
  switch (encoded.status) {
  case Jtty::TransmitTextStatus::Empty:
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::Empty);
    return;
  case Jtty::TransmitTextStatus::EncodingFailed:
    showStatusMessage(tr("JTTY text not sent: a line does not fit in one message once encoded; shorten it and send again"));
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  case Jtty::TransmitTextStatus::Encoded:
    break;
  }

  QVector<Jtty::TransmitSegment> segments;
  segments.reserve(encoded.segments.size());
  for (auto& segment : encoded.segments) {
    segment.transmit.frequency = ui->TxFreqSpinBox_2->value();
    segments.append(std::move(segment.transmit));
  }
  m_jttyQueueNotice = encoded.substituted
    ? tr("Unsupported characters were replaced. Hover over Send to review the text.")
    : QString {};
  enqueueJttySegments(requestId, std::move(segments));
}

void MainWindow::enqueueJttySegments(
    qint64 requestId, QVector<Jtty::TransmitSegment> segments)
{
  if (m_mode != "JTTY" || m_closing || jttyDrainInProgress()
      || segments.isEmpty()) {
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::NotAvailable);
    return;
  }
  m_jttyTransmitQueue.append(requestId, std::move(segments));
  updateJttySendButton();
  feedJttyTransmitQueue();
}

void MainWindow::enqueueJttyToneSegment(qint64 requestId,
                                        QString const& message,
                                        int const tones[], int toneCount)
{
  Jtty::TransmitSegment segment;
  segment.text = message;
  segment.tones.reserve (toneCount);
  for (int i = 0; i < toneCount; ++i) segment.tones.append (tones[i]);
  segment.frequency = ui->TxFreqSpinBox_2->value ();
  enqueueJttySegments (requestId, {std::move (segment)});
}

void MainWindow::feedJttyTransmitQueue()
{
  if (m_feedingJttyTransmitQueue || m_closing || m_mode != "JTTY"
      || jttyDrainInProgress() || m_jttyTxLifecycle.hasPending ()
      || m_delayedJttyStopContext.backend != JttyTxLifecycle::Backend::None
      || ptt0Timer.isActive ()) return;
  auto const next = m_jttyTransmitQueue.nextSegment();
  if (!next) return;

  auto progress = m_jttyQueueProgress;
  if (m_jttyTxLifecycle.active ()
      && m_jttyTxLifecycle.backend () == JttyTxLifecycle::Backend::Local) {
    progress = m_jttyTxQueue->progress ();
  }
  if (m_jttyTxLifecycle.active ()
      && (progress.epoch != m_jttyTxLifecycle.epoch ()
          || progress.queued_samples + next->sampleCount ()
             > TxAudioQueue::defaultCapacity ())) return;
  if (next->sampleCount () > TxAudioQueue::defaultCapacity ()) {
    auto const cancelled = m_jttyTransmitQueue.cancel ();
    for (auto id : cancelled) {
      Q_EMIT jttyTextRejected (id, JttyTxRejectReason::QueueFull);
    }
    m_jttyQueueNotice = tr ("A JTTY segment exceeds the audio queue capacity.");
    updateJttySendButton ();
    return;
  }

  m_feedingJttyTransmitQueue = true;
  execute_jtty_tones (m_jttyTransmitQueue.nextRequestId (), next->text,
                      next->tones.constData (), next->tones.size (),
                      next->frequency);
  m_feedingJttyTransmitQueue = false;
}

void MainWindow::execute_jtty_tones(qint64 requestId, QString const& message,
                                    int const itone[], int nsym, int frequency)
{
  if (jttyDrainInProgress()) {
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::NotAvailable);
    return;
  }
  m_nsym_jtty=nsym;

  float const f0=(frequency >= 0 ? frequency : ui->TxFreqSpinBox_2->value ()) - m_XIT;
  auto const samples = Jtty::renderTransmitTones(itone, m_nsym_jtty, 48000, f0);
  if (samples.isEmpty()) {
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }

  if (samples.size () > TxAudioQueue::defaultCapacity ()) {
    LOG_WARN("JTTY transmit FIFO capacity precheck failed; rejecting PCM enqueue");
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::QueueFull);
    return;
  }

  bool const newSession = !m_jttyTxLifecycle.active ();
  if (newSession) {
    auto const backend = m_tci_audio ? JttyTxLifecycle::Backend::Tci
                                     : JttyTxLifecycle::Backend::Local;
    auto const epoch = m_jttyTxLifecycle.begin (backend);
    if (!epoch.isValid ()) {
      Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::NotAvailable);
      return;
    }
    if (backend == JttyTxLifecycle::Backend::Tci) {
      Q_EMIT m_config.transceiver_clear_jtty_pcm(epoch);
    } else {
      Q_EMIT clearJttyStream(epoch);
    }
    m_jttyQueueProgress = {};
    m_jttyQueueProgress.epoch = epoch;
    m_jttyDisplayedEndSample = 0;
  }

  QByteArray bytes(reinterpret_cast<char const *> (samples.constData ()),
                   samples.size () * int (sizeof (qint16)));
  auto const epoch = m_jttyTxLifecycle.epoch ();
  qint64 const enqueueId = ++m_jttyEnqueueId;
  bool const firstPending = !m_jttyTxLifecycle.hasPending ();
  if (!m_jttyTxLifecycle.addPending (enqueueId, requestId)) {
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::Aborted);
    return;
  }
  m_pendingJttyMessages.append(PendingJttyMessage {
    epoch, enqueueId, requestId, samples.size (), message, newSession
  });
  m_jttyTxWatchdog.stop ();
  if (firstPending) {
    m_jttyEnqueueWatchdog.start (
      int (m_jttyTxLifecycle.preacceptanceTimeout ().count ()));
  }
  updateModeControlLock ();

  if (m_jttyTxLifecycle.backend () == JttyTxLifecycle::Backend::Tci) {
    Q_EMIT m_config.transceiver_enqueue_jtty_pcm(bytes, epoch, enqueueId);
  } else {
    Q_EMIT enqueueJttyStream(bytes, epoch, enqueueId);
  }
}

qint64 MainWindow::jttyTxCommittedSamples() const
{
  return m_jttyTxLifecycle.committedTotal ();
}

void MainWindow::completeJttyTxEnqueue(qint64 requestId, QString const& message,
                                       TxAudioQueueProgress progress,
                                       bool newSession, bool useTciAudio)
{
  if (newSession) {
    beginTxEvidenceSession ();
  }
  bool const firstSegment = m_jttyTransmitQueue.commitNext (progress.total_samples);
  m_jttyQueueProgress = progress;
  if (m_txEvidenceGeneration.isValid () &&
      m_txEvidenceSourceSession == m_txEvidenceSession) {
    m_txPlaybackDiagnostics.commitTarget (m_txEvidenceSession,
                                           m_txEvidenceGeneration,
                                           progress.total_samples - 1, false);
    auto const sessionId = m_txEvidenceSession;
    auto const generation = m_txEvidenceGeneration;
    auto const totalSamples = progress.total_samples;
    QTimer::singleShot (0, this, [this, sessionId, generation, totalSamples] {
      LOG_INFO (QString ("TX playout evidence JTTY target commit session=%1 generation=%2 total=%3\n%4")
                .arg (sessionId.value ()).arg (generation.value ())
                .arg (totalSamples).arg (m_txPlaybackDiagnostics.diagnosticDump ()));
    });
  }
  updateModeControlLock ();
  if (!m_transmitting) {
    m_transmitting = true;
    transmitDisplay (true);
  }
  updateJttyTransmitDisplay (progress.served_samples);
  updateJttySendButton ();
  if (firstSegment) {
    handleJttyContestSerial (message);
    Q_EMIT jttyTextAccepted (requestId);
  }

  // Fault-detector watchdog: generous margin over all audio still to play (the
  // whole queued session, not just this message). The happy path completes via
  // the backend drain signal well before this fires.
  qint64 const pendingSamples = useTciAudio
    ? progress.total_samples : progress.queued_samples;
  int pendingMs = int(pendingSamples / 48);
  if (!m_jttyTxLifecycle.hasPending ()) {
    startJttyTxWatchdog(pendingMs + 1000 * m_config.txDelay() + 10000);
  }

  if (m_monitoring && !ui->actionFull_Duplex_Mode->isChecked())
    monitor(false);

#ifdef WIN32
  if (m_mmttyif) {
    m_mmttyif->report_ptt_state(true);
  }
#endif

  // Only a new session starts transmit; a message appended to an already-active
  // session chains gaplessly (soundcard) via the enqueue above. When PTT is not
  // yet up, guiUpdate keys it and ptt1Timer -> startTx2 -> transmit starts the
  // stream with the normal lead.
  if (newSession && g_iptt == 1 && !m_modulator->isActive()) {
    startTx2();
  }
  feedJttyTransmitQueue ();
}

void MainWindow::onJttyBackendProgress(TxAudioQueueProgress progress)
{
  if (!m_jttyTxLifecycle.active ()
      || progress.epoch != m_jttyTxLifecycle.epoch ()
      || progress.served_samples < m_jttyQueueProgress.served_samples
      || progress.total_samples != m_jttyTxLifecycle.committedTotal ()) return;
  m_jttyQueueProgress = progress;
  updateJttyTransmitDisplay (progress.served_samples);
  feedJttyTransmitQueue ();
  updateJttySendButton ();
  if (m_mode == "JTTY" && !m_jttyLiveEntryPending.isEmpty ()) {
    m_guardingJttyLiveEntryLock = true;
    applyJttyLiveEntryFormatting ();
    m_guardingJttyLiveEntryLock = false;
    updateJttyLiveEntryFrameLabel ();
  }
}

void MainWindow::updateJttyTransmitDisplay(qint64 servedSamples)
{
  for (auto const& request : m_jttyTransmitQueue.requests ()) {
    for (auto const& segment : request.segments) {
      if (!segment.endSample || segment.endSample <= m_jttyDisplayedEndSample
          || segment.endSample - segment.sampleCount () >= servedSamples) continue;
      m_jttyDisplayedEndSample = segment.endSample;
      if (Jtty::isJttyFillerText (segment.text)) continue;   // idle-padding; never shown
      m_currentMessage = segment.text;
      write_all ("Tx", segment.text);
      auto const resolved = DecodeHighlightingModel::resolve_colors (
        m_config.decode_highlighting ().items (),
        {DecodeHighlightingModel::Highlight::Tx}, QColor (Qt::yellow),
        QColor (Qt::black));
      JttyReceiveLine::Presentation const presentation {
        0, request.id, QDateTime::currentDateTimeUtc (), 0.0,
        segment.frequency, segment.text, -10, true,
        resolved.background_, resolved.foreground_};
      JttyReceiveLine::Options const options {
        ui->cbLowerCase->isChecked (), ui->cbIncludeTime->isChecked ()};
      JttyReceiveLine txLine;
      auto const cursor = txLine.render (*ui->decodedTextBrowser2->document (),
        presentation, options, ui->decodedTextBrowser2->contentFont ());
      if (!cursor.isNull ()) {
        ui->decodedTextBrowser2->setTextCursor (cursor);
        ui->decodedTextBrowser2->ensureCursorVisible ();
      }
#ifdef WIN32
      if (m_mmttyif) m_mmttyif->echo_message_to_n1mm (append_separator (segment.text));
#endif
    }
  }
}

void MainWindow::updateJttySendButton()
{
  int const left = m_jttyTransmitQueue.remainingSegments (
    m_jttyQueueProgress.served_samples);
  QString text = tr ("Send message");
  QString tooltip = tr ("Ctrl+K sends now (one-shot); Alt+J starts a live session until Alt+K signs off. Ctrl+Shift+K clears the box. Halt Tx or Esc cancels pending text.");
  if (!m_jttyTransmitQueue.empty ()) {
    text = left ? tr ("Send (%1 left)").arg (left) : tr ("Send (finishing)");
    tooltip += "\n\n" + tr ("Pending text:") + "\n"
      + m_jttyTransmitQueue.pendingText (m_jttyQueueProgress.served_samples);
  }
  if (!m_jttyQueueNotice.isEmpty ()) tooltip += "\n\n" + m_jttyQueueNotice;
  if (ui->pbSendMessage->text () != text) ui->pbSendMessage->setText (text);
  if (ui->pbSendMessage->toolTip () != tooltip) {
    ui->pbSendMessage->setToolTip (tooltip);
    ui->pbSendMessage->setAccessibleDescription (tooltip);
  }
  updateModeControlLock ();
}

void MainWindow::handleJttyContestSerial(QString const& message)
{
  if(message.left(3).compare("TU ", Qt::CaseInsensitive) == 0) {
    logQSOTimer.start(0);
    int nr = ui->sbSerialNumber_2->value();
    m_xSent = QString::number(nr);
    ui->sbSerialNumber_2->setValue(nr+1);
  }
}

void MainWindow::abort_jtty_tx()
{
   noteTxStopReason (TxEvidence::TxStopReason::UserHalt);
   stopTx();

#ifdef WIN32
   if (m_mmttyif) {
       m_mmttyif->report_ptt_state(false);
   }
   finalizeMmttyExternalAbort ();
#endif
}

void MainWindow::interruptJttyTx()
{
  auto const stop = m_jttyTxLifecycle.stopContext ();
  if (!stop) {
    return;
  }
  // Halt Tx/Esc is a decisive stop -- don't let a still-armed session silently resurrect a new transmission later; resuming needs another explicit Alt+J or Ctrl+K.
  m_jttyLiveEntryArmed = false;
  m_jttyAutoAdvanceIdleTimer.stop ();

  if (stop->backend == JttyTxLifecycle::Backend::Local) {
    auto const progress = m_jttyTxQueue->progress ();
    captureJttyTxEvidenceTotals (progress.served_samples,
                                 stop->progress.total_samples,
                                 QStringLiteral ("JTTY source totals captured before abort"));
  } else {
    captureJttyTxEvidenceTotals (-1, stop->progress.total_samples,
                                 QStringLiteral ("TCI JTTY committed total captured before abort"));
  }
  rejectPendingJttyMessages(JttyTxRejectReason::Aborted);
  auto const cancelled = m_jttyTransmitQueue.cancel ();
  for (auto requestId : cancelled) {
    bool const awaitingBackend = std::any_of (
      m_pendingJttyMessages.cbegin (), m_pendingJttyMessages.cend (),
      [requestId] (PendingJttyMessage const& pending) {
        return pending.requestId == requestId;
      });
    if (!awaitingBackend) {
      Q_EMIT jttyTextRejected (requestId, JttyTxRejectReason::Aborted);
    }
  }
  m_jttyQueueNotice = tr ("JTTY transmission cancelled; some text may already have transmitted.");
  updateJttySendButton ();
  m_jttyTxLifecycle.failAll ();
  m_pendingJttyMessages.clear();
  m_jttyEnqueueWatchdog.stop ();
  auto const resetEpoch = TxAudioQueueEpoch {stop->epoch.value () + 1};
  if (stop->backend == JttyTxLifecycle::Backend::Tci) {
    Q_EMIT m_config.transceiver_clear_jtty_pcm(resetEpoch);
  } else {
    Q_EMIT clearJttyStream(resetEpoch);
  }
}

void MainWindow::onJttyBackendDrained(TxAudioQueueDrainState drain)
{
  feedJttyTransmitQueue ();
  if (m_jttyTxLifecycle.hasPending ()
      || m_jttyTransmitQueue.hasUncommitted ()) return;
  auto const ready = m_jttyTxLifecycle.observeDrain (drain.epoch,
                                                       drain.total_at_drain);
  if (ready) finishJttyDrain (*ready);
}

void MainWindow::finishJttyDrain(JttyTxLifecycle::Drain const& drain)
{
  auto const backend = m_jttyTxLifecycle.backend ();
  qint64 const servedAtDrain = backend == JttyTxLifecycle::Backend::Tci
    ? drain.total : m_jttyTxQueue->progress ().served_samples;
  captureJttyTxEvidenceTotals (servedAtDrain, drain.total,
                               QStringLiteral ("JTTY source totals captured at drain"));

  updateJttyTransmitDisplay (drain.total);
  auto const completedRequestIds = m_jttyTransmitQueue.complete (drain.total);
  for (auto requestId : completedRequestIds) {
    Q_EMIT jttyTextCompleted (requestId);
  }
  if (!m_jttyTransmitQueue.empty ()) {
    feedJttyTransmitQueue ();
    updateJttySendButton ();
    return;
  }
  // Still armed: top up with more filler instead of dropping PTT, keeping the same transmission going rather than ending it and re-keying whenever real content does arrive.
  if (m_mode == "JTTY" && m_jttyLiveEntryArmed) {
    sendJttyFillerPadding ();
    updateJttySendButton ();
    return;
  }
  stopTx();
  m_jttyQueueNotice = tr ("JTTY transmission complete.");
  updateJttySendButton ();
  Q_EMIT jttySessionDrained(drain.epoch.value ());
}

void MainWindow::onJttyBackendEnqueueAccepted(qint64 enqueueId, qint64 sampleCount,
                                              TxAudioQueueProgress progress)
{
  for (int i = 0; i < m_pendingJttyMessages.size (); ++i) {
    auto const pending = m_pendingJttyMessages.at (i);
    if (pending.epoch != progress.epoch || pending.enqueueId != enqueueId) {
      continue;
    }

    auto const previousTotal = m_jttyTxLifecycle.committedTotal ();
    auto const resolved = m_jttyTxLifecycle.accept (progress.epoch, enqueueId,
                                                     progress);
    if (!resolved.pending) return;
    m_pendingJttyMessages.remove (i);
    if (!m_jttyTxLifecycle.hasPending ()) m_jttyEnqueueWatchdog.stop ();
    if (sampleCount != pending.sampleCount) {
      LOG_WARN("JTTY transmit backend accepted unexpected PCM sample count");
    }
    bool const startsSession = pending.newSession || previousTotal <= 0;
    completeJttyTxEnqueue(pending.requestId, pending.message, progress,
                          startsSession,
                          m_jttyTxLifecycle.backend () == JttyTxLifecycle::Backend::Tci);
    if (resolved.drain) finishJttyDrain (*resolved.drain);
    return;
  }
}

void MainWindow::onJttyBackendEnqueueFailed(TxAudioQueueEpoch epoch,
                                            qint64 enqueueId,
                                            TxAudioQueueEnqueueFailure failure)
{
  LOG_WARN("JTTY transmit backend rejected PCM enqueue");
  for (int i = 0; i < m_pendingJttyMessages.size (); ++i) {
    auto const pending = m_pendingJttyMessages.at (i);
    if (pending.epoch != epoch || pending.enqueueId != enqueueId) {
      continue;
    }
    auto const resolved = m_jttyTxLifecycle.fail (epoch, enqueueId);
    if (!resolved.pending) return;
    m_pendingJttyMessages.remove (i);
    if (!m_jttyTxLifecycle.hasPending ()) m_jttyEnqueueWatchdog.stop ();
    JttyTxRejectReason reason {JttyTxRejectReason::BackendRejected};
    switch (failure) {
    case TxAudioQueueEnqueueFailure::Capacity:
      reason = JttyTxRejectReason::QueueFull;
      break;
    case TxAudioQueueEnqueueFailure::BackendUnavailable:
      reason = JttyTxRejectReason::NotAvailable;
      break;
    case TxAudioQueueEnqueueFailure::StaleEpoch:
      reason = JttyTxRejectReason::Aborted;
      break;
    case TxAudioQueueEnqueueFailure::None:
      break;
    }
    Q_EMIT jttyTextRejected(pending.requestId, reason);
    auto const cancelled = m_jttyTransmitQueue.cancel ();
    for (auto requestId : cancelled) {
      if (requestId != pending.requestId) {
        Q_EMIT jttyTextRejected (requestId, JttyTxRejectReason::Aborted);
      }
    }
    m_jttyQueueNotice = tr ("The audio backend rejected a JTTY segment.");
    updateJttySendButton ();
    if (resolved.drain) {
      finishJttyDrain (*resolved.drain);
    } else if (!m_jttyTxLifecycle.hasPending ()
               && m_jttyTxLifecycle.committedTotal () <= 0) {
      stopTx ();
    } else if (!m_jttyTxLifecycle.hasPending ()) {
      auto progress = m_jttyTxLifecycle.progress ();
      if (m_jttyTxLifecycle.backend () == JttyTxLifecycle::Backend::Local) {
        progress = m_jttyTxQueue->progress ();
      }
      qint64 const pendingSamples =
        m_jttyTxLifecycle.backend () == JttyTxLifecycle::Backend::Tci
          ? progress.total_samples : progress.queued_samples;
      startJttyTxWatchdog (
        int (pendingSamples / 48) + 1000 * m_config.txDelay () + 10000);
    }
    return;
  }
}

void MainWindow::rejectPendingJttyMessages(JttyTxRejectReason reason)
{
  for (auto const& pending : m_pendingJttyMessages) {
    Q_EMIT jttyTextRejected(pending.requestId, reason);
  }
}

void MainWindow::handleJttyEnqueueTimeout()
{
  if (!m_jttyTxLifecycle.hasPending ()) return;

  LOG_WARN("JTTY transmit backend enqueue acknowledgement timed out");
  noteTxStopReason (TxEvidence::TxStopReason::Watchdog);
  QVector<qint64> timedOutRequests;
  for (auto const& pending : m_pendingJttyMessages) {
    if (!timedOutRequests.contains (pending.requestId)) {
      timedOutRequests.append (pending.requestId);
    }
  }
  rejectPendingJttyMessages(JttyTxRejectReason::BackendTimedOut);
  auto const cancelled = m_jttyTransmitQueue.cancel ();
  for (auto requestId : cancelled) {
    if (!timedOutRequests.contains (requestId)) {
      Q_EMIT jttyTextRejected (requestId, JttyTxRejectReason::Aborted);
    }
  }
  m_jttyQueueNotice = tr ("The JTTY audio backend timed out.");
  updateJttySendButton ();
  m_jttyTxLifecycle.failAll ();
  m_pendingJttyMessages.clear ();
  stopTx ();
#ifdef WIN32
  if (m_mmttyif) m_mmttyif->report_ptt_state(false);
  finalizeMmttyExternalAbort ();
#endif
}

void MainWindow::handleJttyTxWatchdog()
{
  if (!m_jttyTxLifecycle.active ()) {
    return;
  }

  LOG_WARN("JTTY transmit completion watchdog expired");
  noteTxStopReason (TxEvidence::TxStopReason::Watchdog);
  stopTx();
#ifdef WIN32
  if (m_mmttyif) {
    m_mmttyif->report_ptt_state(false);
  }
  finalizeMmttyExternalAbort ();
#endif
}

void MainWindow::startJttyTxWatchdog(int durationMs)
{
  if (durationMs > 0) {
    m_jttyTxWatchdog.start(durationMs);
  }
}

void MainWindow::jtty_again()
{
  startJttyReview(0, 0, false);
}

void MainWindow::jttyDecodeAgainAtSample(quint64 reception, qint64 sample)
{
  startJttyReview(reception, sample, true);
}

bool MainWindow::jtty_key_struck(QKeyEvent * e)
{
  if(e->key() == Qt::Key_Escape) {
    abort_jtty_tx();
    return true;
  }
  int const functionKey=e->key()-Qt::Key_F1+1;
  if(functionKey < 1 || functionKey > 8) return false;
  return sendJttyFunctionKey(functionKey);
}

bool MainWindow::sendJttyFunctionKey(int index)
{
  QString macro;
  switch(index) {
  case 1: macro=ui->msg1->text(); break;
  case 2: macro=ui->msg2->text(); break;
  case 3: macro=ui->msg3->text(); break;
  case 4: macro=ui->msg4->text(); break;
  case 5: macro=ui->msg5->text(); break;
  case 6: macro=ui->msg6->text(); break;
  case 7: macro=ui->msg7->text(); break;
  case 8: macro=ui->msg8->text(); break;
  default: return false;
  }
  if(macro.simplified().isEmpty()) return false;

  auto const context = jttyNativeMacroContext(
    m_config, m_hisCall, ui->sbSerialNumber_2->value(), m_jttyHisCallSnr);
  auto const compiled=Jtty::compileNativeMacro(macro,context);
  if(compiled.status == Jtty::NativeMacroStatus::LiteralFallback) {
    jtty_tx(compiled.text);
    return true;
  }

  qint64 const requestId=++m_jttyTxRequestId;
  if(compiled.status == Jtty::NativeMacroStatus::InvalidRuntime) {
    LOG_WARN(QStringLiteral("JTTY native macro rejected: %1").arg(compiled.error));
    Q_EMIT jttyTextRejected(requestId,JttyTxRejectReason::EncodingFailed);
    return true;
  }

  int itone[944];
  int nsym=0;
  int encodeStatus=static_cast<int>(Jtty::NativeEncodeStatus::InvalidDescriptor);
  genjtty_atoms_c(compiled.atoms.constData(),compiled.atoms.size(),itone,&nsym,
                  &encodeStatus);
  if(nsym <= 0) {
    LOG_WARN(QStringLiteral("JTTY native macro rejected: %1")
             .arg(jttyNativeEncodeError(encodeStatus)));
    Q_EMIT jttyTextRejected(requestId,JttyTxRejectReason::EncodingFailed);
    return true;
  }
  enqueueJttyToneSegment(requestId, compiled.text, itone, nsym);
  return true;
}

QString MainWindow::jtty_msg_expand(QString t)
{
  auto const context = jttyNativeMacroContext(
    m_config, m_hisCall, ui->sbSerialNumber_2->value(), m_jttyHisCallSnr);
  return Jtty::expandLiteralMacro(t, context);
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
  m_wideGraph->setTol(n);
}

void MainWindow::on_comboBoxJttyStyle_currentIndexChanged(int index)
{
  auto const previousStyle = static_cast<Jtty::MessageStyle>(m_jttyMessageStyle);
  auto const newStyle = static_cast<Jtty::MessageStyle>(index);
  QLineEdit* const msgFields[8] = {ui->msg1, ui->msg2, ui->msg3, ui->msg4,
                                    ui->msg5, ui->msg6, ui->msg7, ui->msg8};

  m_settings->beginGroup("MainWindow");
  for (int i = 0; i < 8; ++i) {
    // Save the outgoing style's currently displayed text before switching away,
    // so in-session edits aren't lost.
    m_settings->setValue(Jtty::messageStyleSettingsKey(previousStyle, i + 1),
                          msgFields[i]->text());
  }
  for (int i = 0; i < 8; ++i) {
    int const functionKey = i + 1;
    msgFields[i]->setText(m_settings->value(
      Jtty::messageStyleSettingsKey(newStyle, functionKey),
      Jtty::messageStyleDefaultTemplate(newStyle, functionKey)).toString());
  }
  m_settings->endGroup();
  m_jttyMessageStyle = index;
}

#ifdef WIN32
void MainWindow::logText(const QString &text) {
  LOG_INFO(text);
}

QString MainWindow::jttyRejectReasonText(JttyTxRejectReason reason) const
{
  switch (reason) {
  case JttyTxRejectReason::Empty: return QStringLiteral("empty");
  case JttyTxRejectReason::EncodingFailed: return QStringLiteral("encoding failed");
  case JttyTxRejectReason::QueueFull: return QStringLiteral("queue full");
  case JttyTxRejectReason::BackendRejected: return QStringLiteral("backend rejected");
  case JttyTxRejectReason::Aborted: return QStringLiteral("aborted");
  case JttyTxRejectReason::NotAvailable: return QStringLiteral("not available");
  case JttyTxRejectReason::BackendTimedOut: return QStringLiteral("backend timed out");
  }
  return QStringLiteral("unknown");
}

void MainWindow::handleMmttyTxString(QString message)
{
  if (m_mmttyJttyOutput.finishRequested()) {
    logText(QStringLiteral("MMTTY/N1MM JTTY text ignored after graceful OFF"));
    return;
  }

  auto const context = jttyNativeMacroContext(
    m_config, m_hisCall, ui->sbSerialNumber_2->value(), m_jttyHisCallSnr);
  auto const compiled = Jtty::compileN1mmMessage(message, context);
  qint64 const requestId = ++m_jttyTxRequestId;
  m_mmttyJttyOutput.submit(requestId);
  if (compiled.status == Jtty::N1mmCompileStatus::Error) {
    logText(QStringLiteral("MMTTY/N1MM tagged JTTY request %1 rejected: %2")
            .arg(requestId).arg(compiled.error));
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }

  QString transmitText = compiled.status == Jtty::N1mmCompileStatus::Literal
    ? compiled.literalText : compiled.canonicalText;
  if (compiled.status == Jtty::N1mmCompileStatus::Literal) {
    if (mmttyNeedsHandoff ()) {
      if (!m_mmttyHandoff.queue (requestId)) {
        Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::QueueFull);
        return;
      }
      m_pendingMmttyJttyMessages.append ({requestId, transmitText, {}, true});
      beginMmttyHandoff ();
      return;
    }
    if (m_mode != QStringLiteral("JTTY")) set_mode (QStringLiteral("JTTY"));
    execute_jtty_tx (requestId, transmitText);
    return;
  }
  int itone[944] {};
  int nsym = 0;
  int encodeStatus = static_cast<int>(Jtty::NativeEncodeStatus::InvalidDescriptor);
  genjtty_atoms_c(compiled.atoms.constData(), compiled.atoms.size(), itone, &nsym,
                  &encodeStatus);
  if (nsym <= 0) {
    logText(QStringLiteral("MMTTY/N1MM tagged JTTY request %1 rejected: %2")
            .arg(requestId)
            .arg(jttyNativeEncodeError(encodeStatus)));
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }

  QVector<int> tones;
  tones.reserve (nsym);
  for (int i = 0; i < nsym; ++i) tones.append (itone[i]);

  if (mmttyNeedsHandoff ()) {
    if (!m_mmttyHandoff.queue (requestId)) {
      Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::QueueFull);
      return;
    }
    m_pendingMmttyJttyMessages.append ({requestId, transmitText, tones});
    beginMmttyHandoff ();
    return;
  }

  if (m_mode != QStringLiteral("JTTY")) set_mode (QStringLiteral("JTTY"));
  enqueueJttyToneSegment(requestId, transmitText, tones.constData (), tones.size ());
}

void MainWindow::handleMmttyStartTx()
{
  m_mmttyJttyOutput.start();
  if (mmttyNeedsHandoff ()) {
    beginMmttyHandoff ();
    return;
  }
  if (m_mode != QStringLiteral("JTTY")) set_mode (QStringLiteral("JTTY"));
  startPendingMmttyJttyTx();
}

void MainWindow::handleMmttyStopTx()
{
  if (m_mode != "JTTY" && !m_mmttyHandoff.active ()
      && !m_jttyTxLifecycle.active ()) {
    noteTxStopReason (TxEvidence::TxStopReason::UserHalt);
    stopTx ();
  }
  m_mmttyJttyOutput.finish();
  logText(QStringLiteral("MMTTY/N1MM JTTY OFF requested; waiting for backend drain"));
  completeMmttyJttyOutput();
}

void MainWindow::handleMmttyAbortTx()
{
  bool const handoffWasActive = m_mmttyHandoff.active ();
  m_mmttyHandoffWatchdog.stop ();
  m_pendingMmttyJttyMessages.clear ();
  m_mmttyHandoff.abort ();
  m_preserveMmttyOutputDuringStop = false;
  m_mmttyJttyOutput.abort();
  if (!handoffWasActive || m_jttyTxLifecycle.active ()) abort_jtty_tx();
  updateModeControlLock ();
}

void MainWindow::handleMmttyJttyAccepted(qint64 requestId)
{
  if (!m_mmttyJttyOutput.accept(requestId)) return;

  logText(QStringLiteral("MMTTY/N1MM JTTY request %1 accepted").arg(requestId));
  m_mmttyHandoff.submitted (requestId);
  if (m_mmttyHandoff.empty () && !m_mmttyHandoff.active ()) {
    m_mmttyHandoffWatchdog.stop ();
  }
  startPendingMmttyJttyTx();
}

void MainWindow::handleMmttyJttyRejected(qint64 requestId, JttyTxRejectReason reason)
{
  if (!m_mmttyJttyOutput.resolve(requestId)) return;

  logText(QStringLiteral("MMTTY/N1MM JTTY request %1 rejected: %2")
          .arg(requestId)
          .arg(jttyRejectReasonText(reason)));
  m_mmttyHandoff.submitted (requestId);
  if (m_mmttyHandoff.empty () && !m_mmttyHandoff.active ()) {
    m_mmttyHandoffWatchdog.stop ();
  }
  // Backend rejection may reset the audio session after emitting this signal.
  QTimer::singleShot(0, this, [this] { completeMmttyJttyOutput(); });
}

void MainWindow::handleMmttyJttyCompleted(qint64 requestId)
{
  if (!m_mmttyJttyOutput.resolve(requestId)) return;

  logText(QStringLiteral("MMTTY/N1MM JTTY request %1 completed").arg(requestId));
}

void MainWindow::handleMmttyJttySessionDrained(qint64 sessionId)
{
  Q_UNUSED(sessionId)
  completeMmttyJttyOutput(true);
}

void MainWindow::completeMmttyJttyOutput(bool drained)
{
  if (m_mmttyJttyOutput.takeCompletion(m_jttyTxLifecycle.active (), drained)
      && m_mmttyif) {
    m_mmttyif->report_output_complete();
  }
}

void MainWindow::startPendingMmttyJttyTx()
{
  if (m_mode != "JTTY" || !m_mmttyJttyOutput.startRequested()
      || m_mmttyHandoff.active ()) return;

  if (!m_jttyTxLifecycle.active () || jttyTxCommittedSamples () <= 0) {
    logText(QStringLiteral("MMTTY/N1MM JTTY start deferred until text is accepted"));
    return;
  }

  if (g_iptt == 1) {
    logText(QStringLiteral("MMTTY/N1MM JTTY start ignored; transmitter is already keyed"));
    m_mmttyJttyOutput.started();
    return;
  }

  m_mmttyJttyOutput.started();
  startTx2();
}

bool MainWindow::mmttyNeedsHandoff() const
{
  if (m_mmttyHandoff.active ()) return true;
  bool const busy = m_transmitting || m_tune || g_iptt == 1
    || ptt0Timer.isActive ();
  return Jtty::MmttyHandoff::SubmitAction::StopThenSubmit
    == Jtty::MmttyHandoff::planSubmission (
      true, m_jttyTxLifecycle.active (), busy);
}

void MainWindow::beginMmttyHandoff()
{
  if (m_mmttyHandoff.active ()) return;
  m_mmttyHandoff.waitForStop ();
  updateModeControlLock ();
  if (!m_mmttyHandoffWatchdog.isActive ()) {
    m_mmttyHandoffWatchdog.start (m_mmttyHandoff.timeoutMs ());
  }
  m_preserveMmttyOutputDuringStop = true;
  noteTxStopReason (TxEvidence::TxStopReason::ModeChange);
  if (m_tune) stop_tuning ();
  if (m_transmitting || g_iptt == 1 || m_jttyTxLifecycle.active ()) {
    stopTx ();
  } else if (!ptt0Timer.isActive ()) {
    QTimer::singleShot (0, this, &MainWindow::resumeMmttyHandoff);
  }
}

void MainWindow::resumeMmttyHandoff()
{
  if (!m_mmttyHandoff.stopCompleted ()) return;
  m_preserveMmttyOutputDuringStop = false;
  if (m_mode != QStringLiteral("JTTY")) set_mode (QStringLiteral("JTTY"));

  if (m_pendingMmttyJttyMessages.isEmpty ()) {
    m_mmttyHandoffWatchdog.stop ();
  }

  auto const queued = m_pendingMmttyJttyMessages;
  m_pendingMmttyJttyMessages.clear ();
  for (auto const& pending : queued) {
    if (pending.literal) execute_jtty_tx (pending.requestId, pending.message);
    else enqueueJttyToneSegment (pending.requestId, pending.message,
                                 pending.tones.constData (), pending.tones.size ());
  }
  startPendingMmttyJttyTx ();
  updateModeControlLock ();
}

void MainWindow::handleMmttyHandoffTimeout()
{
  auto const notSubmitted = m_pendingMmttyJttyMessages;
  m_pendingMmttyJttyMessages.clear ();
  m_mmttyHandoff.expire ();
  m_preserveMmttyOutputDuringStop = false;
  for (auto const& pending : notSubmitted) {
    Q_EMIT jttyTextRejected (pending.requestId,
                             JttyTxRejectReason::BackendTimedOut);
  }
  if (m_jttyTxLifecycle.hasPending ()) handleJttyEnqueueTimeout ();
  finalizeMmttyExternalAbort ();
}

void MainWindow::finalizeMmttyExternalAbort()
{
  m_mmttyHandoffWatchdog.stop ();
  m_pendingMmttyJttyMessages.clear ();
  m_mmttyHandoff.abort ();
  m_preserveMmttyOutputDuringStop = false;
  updateModeControlLock ();
  if (!m_mmttyJttyOutput.pending ()) return;
  m_mmttyJttyOutput.externalAbort ();
  completeMmttyJttyOutput (true);
}

void MainWindow::initMMTTY(quint16 port) {
    if (!m_mmttyif) {
        m_mmttyif = new MMTTYIF(this);
    }

    m_mmttyif->initialize(port);

    connect(m_mmttyif, &MMTTYIF::log_message, this, &MainWindow::logText);
    connect(m_mmttyif, &MMTTYIF::app_tx_string, this, &MainWindow::handleMmttyTxString);
    connect(m_mmttyif, &MMTTYIF::app_start_tx, this, &MainWindow::handleMmttyStartTx);
    connect(m_mmttyif, &MMTTYIF::app_stop_tx, this, &MainWindow::handleMmttyStopTx);
    connect(m_mmttyif, &MMTTYIF::app_abort_tx, this, &MainWindow::handleMmttyAbortTx);
    connect(this, &MainWindow::jttyTextAccepted, this, &MainWindow::handleMmttyJttyAccepted);
    connect(this, &MainWindow::jttyTextRejected, this, &MainWindow::handleMmttyJttyRejected);
    connect(this, &MainWindow::jttyTextCompleted, this, &MainWindow::handleMmttyJttyCompleted);
    connect(this, &MainWindow::jttySessionDrained, this, &MainWindow::handleMmttyJttySessionDrained);

    connect(m_mmttyif, &MMTTYIF::inactivity_timeout, qApp, &QCoreApplication::quit);
    connect(m_mmttyif, &MMTTYIF::app_is_quitting, this, [this]() {
        abort_jtty_tx();
        close();
    });

    QTimer::singleShot(3000, this, [this]() {
         bool const idle = !m_transmitting && !m_tune && g_iptt != 1
           && !ptt0Timer.isActive () && !m_jttyTxLifecycle.active ()
           && !m_mmttyHandoff.active ();
         if (idle && m_mode != "JTTY") set_mode("JTTY");
    });
}

MMTTYIF *MainWindow::getMmttyIf() const {
    return m_mmttyif;
}
#endif
