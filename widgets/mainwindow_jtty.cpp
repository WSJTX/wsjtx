#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "commons.h"
#include "JttyKeyRouter.hpp"
#include "JttyMessages.hpp"
#include "JttyN1mm.hpp"
#include "JttyReceiveLine.hpp"
#include "JttyTransmitText.hpp"
#include "Logger.hpp"
#include "models/DecodeHighlightingModel.hpp"
#include <QApplication>
#include <QByteArray>
#include <QDateTime>
#include <QKeyEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <functional>
#include <QScopedValueRollback>
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
    auto const profile = jttyExchangeProfile(configuration);
    return Jtty::nativeMacroContext(
      configuration.my_callsign(), hisCall, serialNumber, configuration.my_grid(), snr,
      profile, profile == Jtty::NativeExchangeProfile::FieldDay
        ? configuration.Field_Day_Exchange() : configuration.RTTY_Exchange());
  }
}

constexpr qint64 kJttySamplesPerFrame = Jtty::samplesPerFrame;

// Lead time for queueing the next diddle; covers the enqueue round trip.
constexpr qint64 kJttyFillerLeadSamples = kJttySamplesPerFrame / 2;

// Pause after which a trailing unterminated word is released.
constexpr int kJttyAutoAdvanceIdleMs = 1500;

// Unbroken diddle frames tolerated before PTT drops on its own, about 60 s.
// Each diddle re-arms the enqueue watchdog, so nothing else would end an over
// the operator has walked away from until the Runaway Tx watchdog, minutes on.
constexpr int kJttyMaxDiddleFrames = 32;

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
  QString const prefix = QStringLiteral("  UTC  Freq  dB  ");
  ui->lh_decodes_headings_label->setText(prefix + tr ("Message"));
  ui->rh_decodes_headings_label->setText(prefix + tr ("Message"));
}

void MainWindow::jtty_tx(QString message)
{
  if (!m_config.prepare_radio_mode ()) return;
  setXIT (ui->TxFreqSpinBox->value ());
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

// The one PTT control. Off stops gracefully; Esc aborts.
// finalOver: this call both opens and closes the over, so the text committed
// here carries EOM. Nothing is left for a following PTT-off to mark final.
void MainWindow::setJttyPttEnabled(bool on, bool finalOver)
{
  if (m_mode != "JTTY" || on == m_jttyLiveEntryArmed) return;

  if (on) {
    if (!m_config.prepare_radio_mode ()) {
      updateRadioModeStatus ();
      return;
    }
    setXIT (ui->TxFreqSpinBox->value ());
    m_jttyLiveEntryArmed = true;
    m_jttyDiddling = false;   // stale from the previous session's last filler
    m_jttyDiddleFrames = 0;
    // Nothing committable (empty box or half-typed word): key with diddle.
    if (!commitJttyLiveEntryPlan (/*forceFlush=*/false, /*holdbackWords=*/0,
                                  /*isFinal=*/finalOver)) {
      sendJttyFillerPadding ();
    }
  } else {
    // Disarm first so finishJttyDrain stops topping up with filler.
    m_jttyLiveEntryArmed = false;
    m_jttyAutoAdvanceIdleTimer.stop ();
    commitJttyLiveEntryPlan (/*forceFlush=*/true, /*holdbackWords=*/0,
                             /*isFinal=*/true);
    // Only filler left: nothing to be graceful about, so stop now.
    if (jttyPendingIsOnlyFiller ()) abort_jtty_tx ();
  }
  updateJttySendButton ();
}

// Commits the next slice once the backlog falls below one frame.
void MainWindow::maybeCommitJttyLiveEntry()
{
  if (m_mode != "JTTY" || !m_jttyLiveEntryArmed || m_guardingJttyLiveEntryLock) return;
  if (ui->Tx_Message->toPlainText ().mid (m_jttyLiveEntryCommitted)
      .trimmed ().isEmpty ()) return;
  if (!Jtty::shouldCommitForQueueDepth (jttyQueuedSamples ())) return;
  commitJttyLiveEntryPlan (/*forceFlush=*/false, /*holdbackWords=*/0, /*isFinal=*/false);
}

// Filler owns no live-entry span, so cancelling it can't unlock committed text.
bool MainWindow::jttyPendingIsOnlyFiller() const
{
  // Text that has played but not yet completed still owns a span; aborting now
  // would reject it and un-commit text that was actually sent.
  if (!m_jttyLiveEntryPending.isEmpty ()) return false;
  for (auto const& request : m_jttyTransmitQueue.requests ()) {
    for (auto const& segment : request.segments) {
      if (segment.endSample
          && segment.endSample <= m_jttyQueueProgress.served_samples) continue;
      if (!Jtty::isJttyFillerText (segment.text)) return false;
    }
  }
  return true;
}

// Queues the next diddle before the current one drains; waiting for the drain
// callback leaves the backend dry for the enqueue round trip, gapping the tone.
void MainWindow::maybeTopUpJttyFiller()
{
  if (m_mode != "JTTY" || !m_jttyLiveEntryArmed) return;
  if (!m_jttyTxLifecycle.active ()) return;
  // More than the playing segment left: text or an earlier top-up is waiting.
  if (m_jttyTransmitQueue.remainingSegments (
        m_jttyQueueProgress.served_samples) > 1) return;
  if (jttyQueuedSamples () > kJttyFillerLeadSamples) return;
  if (m_jttyDiddleFrames >= kJttyMaxDiddleFrames) {
    showStatusMessage (tr ("PTT dropped: JTTY was sending diddle with nothing "
                           "to transmit. Press F9 to go back on the air."));
    setJttyPttEnabled (false);
    return;
  }
  sendJttyFillerPadding ();
}

// Backlog still to play; prefers the queue's own figure on the local backend.
qint64 MainWindow::jttyQueuedSamples() const
{
  if (m_jttyTxLifecycle.active ()
      && m_jttyTxLifecycle.backend () == JttyTxLifecycle::Backend::Local
      && m_jttyTxQueue) {
    return m_jttyTxQueue->progress ().queued_samples;
  }
  return m_jttyQueueProgress.queued_samples;
}

// Commit now if the queue is low; else restart the trailing-word pause timer.
void MainWindow::autoAdvanceJttyLiveEntry()
{
  if (m_mode != "JTTY" || !m_jttyLiveEntryArmed || m_guardingJttyLiveEntryLock) return;
  QString const uncommitted = ui->Tx_Message->toPlainText ().mid (m_jttyLiveEntryCommitted);
  if (uncommitted.trimmed ().isEmpty ()) {
    m_jttyAutoAdvanceIdleTimer.stop ();
    return;
  }
  maybeCommitJttyLiveEntry ();
  m_jttyAutoAdvanceIdleTimer.start (kJttyAutoAdvanceIdleMs);
}

// A pause means no keystroke is coming to finish the trailing word.
void MainWindow::idleFlushJttyLiveEntry()
{
  if (m_mode != "JTTY" || !m_jttyLiveEntryArmed || m_guardingJttyLiveEntryLock) return;
  commitJttyLiveEntryPlan (/*forceFlush=*/true, /*holdbackWords=*/0, /*isFinal=*/false);
}

// Deprecated alias (Ctrl+K / Send button); the graceful stop is now F9.
void MainWindow::commitJttyLiveEntry()
{
  if (m_mode != "JTTY") return;
  if (!m_jttyLiveEntryArmed) {
    // Nothing streaming: send the box once, as this shortcut always did.
    if (!ui->Tx_Message->toPlainText ().mid (m_jttyLiveEntryCommitted).trimmed ().isEmpty ()) {
      if (!m_config.prepare_radio_mode ()) return;
      setXIT (ui->TxFreqSpinBox->value ());
    }
    m_jttyAutoAdvanceIdleTimer.stop ();
    commitJttyLiveEntryPlan (/*forceFlush=*/true, /*holdbackWords=*/0, /*isFinal=*/true);
    return;
  }
  setJttyPttEnabled (false);
}

// Deprecated alias (Alt+J); PTT on is now F9.
void MainWindow::startJttyLiveStream()
{
  setJttyPttEnabled (true);
}

void MainWindow::sendJttyFillerPadding()
{
  qint64 const requestId = ++m_jttyTxRequestId;
  ++m_jttyDiddleFrames;
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
  m_jttyDiddleFrames = 0;   // real text: the unbroken diddle run ends here
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

// Esc (and the Ctrl+Shift+K alias): clears the whole box, cancelling Tx.
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
  m_jttyDiddling = false;
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

// Deprecated alias (Alt+K): appends the sign-off, then stops gracefully.
void MainWindow::insertJttyChatSignoff()
{
  if (m_mode != "JTTY" || !m_config.prepare_radio_mode ()) return;
  setXIT (ui->TxFreqSpinBox->value ());
  insertJttyLiveEntryText (QStringLiteral ("DE %M K "));
  commitJttyLiveEntry ();
}

// Fraction (0..1) of a pending commit that should show as sent, stepping one JTTY frame at a time using pack_jtty's own frame_starts boundaries (segment.frameCharStarts) rather than an even split, which put the boundary mid-atom; falls back to an even split if a segment's boundaries weren't captured. Each frame is a fixed 59*1536 samples (genjtty_frames, lib/jtty/genjtty.f90). Returns 0 for a requestId not yet enqueued (not started, never "finished").
double MainWindow::jttyRequestSentFraction(qint64 requestId) const
{
  qint64 const samplesPerFrame = kJttySamplesPerFrame;
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
  qint64 const samplesPerFrame = kJttySamplesPerFrame;
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
      || m_config.radio_mode_yielded () || segments.isEmpty()) {
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

  float const f0=(frequency >= 0 ? frequency : ui->TxFreqSpinBox_2->value ()) - m_operatingFrequency.audioOffset ();
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
    endJttyTxEcho ();
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

void MainWindow::completeJttyTxEnqueue(qint64 requestId,
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
  // Release the next slice first, so text goes out instead of diddle.
  maybeCommitJttyLiveEntry ();
  maybeTopUpJttyFiller ();
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
  // The label follows what is playing; the echo, what has been sent.
  for (auto const& request : m_jttyTransmitQueue.requests ()) {
    for (auto const& segment : request.segments) {
      if (segment.endSample && segment.endSample > servedSamples
          && segment.endSample - segment.sampleCount () < servedSamples) {
        m_jttyDiddling = Jtty::isJttyFillerText (segment.text);
      }
    }
  }

  // Echoed a frame at a time, as the far end decodes it, into one growing
  // line per over; echoing ahead of the audio would show text Esc never sent.
  auto const update = m_jttyTxEcho.update (
    m_jttyTransmitQueue.requests (), servedSamples,
    [] (QString const& text) { return Jtty::isJttyFillerText (text); });
  for (auto const& text : update.completedSegments) {
    m_currentMessage = text;
#ifdef WIN32
    if (m_mmttyif) m_mmttyif->echo_message_to_n1mm (append_separator (text));
#endif
  }
  if (!update.grew || m_jttyTxEcho.isEmpty ()) return;
  if (!m_jttyTxEchoLine) {
    m_jttyTxEchoLine = std::make_shared<JttyReceiveLine> ();
    m_jttyTxEchoStart = QDateTime::currentDateTimeUtc ();
  }
  auto const resolved = DecodeHighlightingModel::resolve_colors (
    m_config.decode_highlighting ().items (),
    {DecodeHighlightingModel::Highlight::Tx}, QColor (Qt::yellow),
    QColor (Qt::black));
  JttyReceiveLine::Presentation const presentation {
    0, m_jttyTxEcho.firstRequestId (), m_jttyTxEchoStart, 0.0,
    m_jttyTxEcho.frequency (), m_jttyTxEcho.text (), -10, true,
    resolved.background_, resolved.foreground_};
  JttyReceiveLine::Options const options {ui->cbLowerCase->isChecked ()};
  auto const cursor = m_jttyTxEchoLine->render (*ui->decodedTextBrowser2->document (),
    presentation, options, ui->decodedTextBrowser2->contentFont ());
  if (!cursor.isNull ()) {
    ui->decodedTextBrowser2->setTextCursor (cursor);
    ui->decodedTextBrowser2->ensureCursorVisible ();
  }
}

// Ctrl+Up / Ctrl+Down: replace the unsent part of the Tx box with an earlier
// over, ready to edit or send again.
void MainWindow::recallJttyTransmission(int step)
{
  int const unsentFrom = m_jttyLiveEntryCommitted;
  QString const draft = ui->Tx_Message->toPlainText ().mid (unsentFrom);
  QString text;
  if (!m_jttyTxHistory.recall (step, draft, &text)) return;
  QScopedValueRollback<bool> recalling {m_jttyRecalling, true};
  QTextCursor cursor = ui->Tx_Message->textCursor ();
  cursor.setPosition (unsentFrom);
  cursor.movePosition (QTextCursor::End, QTextCursor::KeepAnchor);
  cursor.insertText (text, QTextCharFormat {});
  ui->Tx_Message->setTextCursor (cursor);
  ui->Tx_Message->ensureCursorVisible ();
}

// PTT has dropped: the over is echoed in the QSO Frequency pane and Ctrl+Up
// recalls it, so its text leaves the Tx box. Unsent type-ahead stays.
void MainWindow::clearJttySentText()
{
  if (m_mode != "JTTY") return;
  int const cut = m_jttyLiveEntryPending.isEmpty ()
    ? m_jttyLiveEntryCommitted : m_jttyLiveEntryPending.constFirst ().start;
  if (cut <= 0) return;
  m_guardingJttyLiveEntryLock = true;
  QTextCursor cursor (ui->Tx_Message->document ());
  cursor.setPosition (cut, QTextCursor::KeepAnchor);
  cursor.removeSelectedText ();
  for (auto& pending : m_jttyLiveEntryPending) {
    pending.start -= cut;
    pending.end -= cut;
  }
  m_jttyLiveEntryCommitted -= cut;
  if (m_jttyLiveEntryPending.isEmpty ()) m_jttyLiveEntryFramesSent = 0;
  applyJttyLiveEntryFormatting ();
  // The lock guard reverts bad edits with undo(), so the removal mustn't be undoable.
  ui->Tx_Message->document ()->clearUndoRedoStacks ();
  m_guardingJttyLiveEntryLock = false;
  updateJttyLiveEntryFrameLabel ();
}

QFont MainWindow::jttyDisplayFont() const
{
  QFont font = m_config.decoded_text_font ();
  if (m_mode == "JTTY" && m_jttyFontZoom && font.pointSize () > 0) {
    font.setPointSize (qMax (6, font.pointSize () + m_jttyFontZoom));
  }
  return font;
}

// Ctrl+= / Ctrl+- / Ctrl+0: JTTY's own offset from the Decoded Text Font size.
void MainWindow::zoomJttyFont(int step)
{
  int const base = m_config.decoded_text_font ().pointSize ();
  if (base <= 0) return;
  int const zoom = step ? qBound (6 - base, m_jttyFontZoom + step, 40 - base) : 0;
  if (zoom != m_jttyFontZoom) {
    m_jttyFontZoom = zoom;
    setDecodedTextFont (jttyDisplayFont ());
  }
  showStatusMessage (tr ("JTTY font size %1 pt").arg (base + zoom));
}

// PTT has dropped: the over is complete, and ALL.TXT gets it as one line.
void MainWindow::endJttyTxEcho()
{
  if (!m_jttyTxEcho.isEmpty ()) {
    write_all ("Tx", m_jttyTxEcho.text ());
    m_jttyTxHistory.add (m_jttyTxEcho.text ());
  }
  m_jttyTxEcho.reset ();
  m_jttyTxEchoLine.reset ();
}

void MainWindow::updateJttySendButton()
{
  int const left = m_jttyTransmitQueue.remainingSegments (
    m_jttyQueueProgress.served_samples);
  QString text = tr ("Send message");
  QString tooltip = tr ("F9 turns PTT on and off: on, the message is sent as you type and diddle fills any gap; off finishes what is queued, then drops PTT. Esc stops immediately and clears the box.");
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
  updateJttyPttStateLabel ();
  updateModeControlLock ();
}

void MainWindow::updateJttyPttStateLabel()
{
  if (m_mode != "JTTY") {
    ui->labelJttyPttState->clear ();
    ui->labelJttyPttState->setStyleSheet (QString {});
    return;
  }

  // Large and colour-coded, readable at a glance from across the shack.
  auto const style = [] (char const * background) {
    return QStringLiteral ("QLabel{background-color: %1; color: black; font-weight: bold;"
                           " font-size: 14pt; border-radius: 4px; padding: 2px 8px}")
      .arg (QLatin1String {background});
  };
  QString text;
  QString sheet;
  if (m_jttyLiveEntryArmed) {
    text = m_jttyDiddling ? tr ("Diddle") : tr ("Tx");
    sheet = style (m_jttyDiddling ? "#ffb266" : "#ff6666");
  } else if (m_jttyTxLifecycle.active ()) {
    // PTT is off (or a one-shot send) and queued text is still going out.
    text = tr ("Tx → Rx");
    sheet = style ("#ff9999");
  } else if (m_monitoring) {
    text = tr ("Rx");
    sheet = style ("#99dd99");
  }
  if (ui->labelJttyPttState->text () != text) {
    ui->labelJttyPttState->setText (text);
    ui->labelJttyPttState->setStyleSheet (sheet);
  }
}

// %L: log the QSO with the exchange just sent, then bump the serial number.
void MainWindow::logJttyQso()
{
  // The log reads the report from the first word, except for Field Day,
  // whose exchange has none.
  bool const report =
    jttyExchangeProfile (m_config) != Jtty::NativeExchangeProfile::FieldDay;
  auto const logged = [report] (QString const& text) {
    QString const exchange = text.simplified ().toUpper ();
    if (!report || exchange.isEmpty () || exchange.startsWith (QStringLiteral ("599 "))) {
      return exchange;
    }
    return QStringLiteral ("599 ") + exchange;
  };
  m_xSent = logged (ui->jttySentEntry->text ());
  m_xRcvd = logged (ui->jttyReceivedEntry->text ());
  scheduleLogQso (false);
  ui->sbSerialNumber_2->setValue (ui->sbSerialNumber_2->value () + 1);
}

void MainWindow::jttyEnterSendsMessage()
{
  auto const role = m_jttyMacros.current ().enterRole;
  // Text the operator typed is what Enter sends, in every set. Appending the
  // sequence's next macro to it put words on the air nobody asked for, and a
  // TU macro's %L logged the QSO as a side effect of sending a sentence.
  bool const typed = !ui->Tx_Message->toPlainText ().mid (m_jttyLiveEntryCommitted)
    .trimmed ().isEmpty ();
  if (role == Jtty::EnterRole::Off || typed) {
    commitJttyLiveEntry ();  // as Send message: send the box, or end the over
    return;
  }
  bool const haveDxCall = !m_hisCall.trimmed ().isEmpty ();
  int const key = Jtty::enterSendsKey (role, m_jttyEnterStep, haveDxCall);
  if (!key) {
    showStatusWarning (haveDxCall ? tr ("QSO already logged. Enter a new DX Call.")
                                  : tr ("Enter a DX Call first."));
    return;
  }
  // After Enter in a QSO field, typing should go to the Tx box again.
  auto * const focus = QApplication::focusWidget ();
  if (focus == ui->dxCallEntry || focus == ui->jttyCallEntry || focus == ui->jttySentEntry
      || focus == ui->jttyReceivedEntry) {
    ui->Tx_Message->setFocus ();
  }
  sendJttyFunctionKey (key);
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
  // Cleared before the early return: PTT can be on with nothing committed.
  m_jttyLiveEntryArmed = false;
  m_jttyAutoAdvanceIdleTimer.stop ();

  auto const stop = m_jttyTxLifecycle.stopContext ();
  if (!stop) {
    return;
  }

  if (stop->backend == JttyTxLifecycle::Backend::Local) {
    auto const progress = m_jttyTxQueue->progress ();
    captureJttyTxEvidenceTotals (progress.served_samples,
                                 stop->progress.total_samples,
                                 QStringLiteral ("JTTY source totals captured before abort"));
  } else {
    captureJttyTxEvidenceTotals (-1, stop->progress.total_samples,
                                 QStringLiteral ("TCI JTTY committed total captured before abort"));
  }
  auto const served = stop->backend == JttyTxLifecycle::Backend::Local
    ? m_jttyTxQueue->progress ().served_samples
    : m_jttyQueueProgress.served_samples;
  updateJttyTransmitDisplay (served);
  // Noted while the queue still holds its segments; cancel() empties it below.
  m_jttyRequestsOnAir = m_jttyTransmitQueue.requestsOnAir (served);
  endJttyTxEcho ();
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
  m_jttyRequestsOnAir.clear ();
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
    // Typed text beats diddle when the queue is empty.
    maybeCommitJttyLiveEntry ();
    if (!m_jttyTransmitQueue.empty ()) {
      feedJttyTransmitQueue ();
    } else {
      sendJttyFillerPadding ();
    }
    updateJttySendButton ();
    return;
  }
  endJttyTxEcho ();
  clearJttySentText ();
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
    completeJttyTxEnqueue(pending.requestId, progress,
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

// Called for every key press in the main window and the Wide Graph, before
// the focused widget sees it. Returns true when the key has been consumed.
bool MainWindow::jtty_key_struck(QKeyEvent * e, QObject * receiver)
{
  if (m_mode != "JTTY") return false;
  // Child dialogs (Log QSO, message boxes) also pass through MainWindow's
  // filter. Judge by the receiver: focus and active window can both be null.
  auto * const target = qobject_cast<QWidget *> (receiver);
  auto * const window = target ? target->window () : nullptr;
  if (window != this && window != m_wideGraph.data ()) return false;
  auto * const focusWidget = QApplication::focusWidget ();
  auto const focus = Jtty::classifyKeyFocus (
    focusWidget ? focusWidget : target, ui->Tx_Message, {ui->dxCallEntry, ui->jttyCallEntry, ui->jttySentEntry, ui->jttyReceivedEntry});
  // A set without a QSO sequence (Chat) sends the Tx box, so Enter in DX Call
  // keeps its usual meaning there.
  bool const enterSends = ui->cbJttyEnterSends->isChecked ()
    && (m_jttyMacros.current ().enterRole != Jtty::EnterRole::Off
        || focus != Jtty::KeyFocus::QsoField);
  auto const route = Jtty::routeKey (e->key (), e->modifiers (), e->text (), focus, enterSends);
  using Action = Jtty::KeyRoute::Action;
  bool const repeat = e->isAutoRepeat ();
  switch (route.action) {
  case Action::None:
    return false;
  case Action::AppendToTxBox:
    appendJttyTxBoxText (route.text);
    return true;
  case Action::BackspaceTxBox:
    backspaceJttyTxBox ();
    return true;
  case Action::Abort:
    // Hard abort: stop mid-frame and clear the box, unlike F9's graceful stop.
    if (!repeat) clearJttyLiveEntry ();
    return true;
  case Action::TogglePtt:
    if (!repeat) setJttyPttEnabled (!m_jttyLiveEntryArmed);
    return true;
  case Action::FocusDxCall:
    if (!repeat) focusJttyDxCall ();
    return true;
  case Action::FunctionKey:
    if (!repeat) sendJttyFunctionKey (route.functionKey);
    return true;
  case Action::StepTxFrequency:
    ui->TxFreqSpinBox_2->setValue (ui->TxFreqSpinBox_2->value () + route.txStepHz);
    return true;
  case Action::EnterSendsMessage:
    if (!repeat) jttyEnterSendsMessage ();
    return true;
  case Action::RecallHistory:
    recallJttyTransmission (route.recallStep);
    return true;
  case Action::ZoomFont:
    zoomJttyFont (route.zoomStep);
    return true;
  }
  return false;
}

void MainWindow::appendJttyTxBoxText(QString const& text)
{
  QTextCursor cursor = ui->Tx_Message->textCursor ();
  cursor.movePosition (QTextCursor::End);
  cursor.insertText (text, QTextCharFormat {});
  ui->Tx_Message->setTextCursor (cursor);
  ui->Tx_Message->ensureCursorVisible ();
}

// Only unsent text can be deleted; the committed prefix is locked.
void MainWindow::backspaceJttyTxBox()
{
  if (ui->Tx_Message->document ()->characterCount () - 1 <= m_jttyLiveEntryCommitted) return;
  QTextCursor cursor = ui->Tx_Message->textCursor ();
  cursor.movePosition (QTextCursor::End);
  cursor.deletePreviousChar ();
  ui->Tx_Message->setTextCursor (cursor);
}

void MainWindow::focusJttyDxCall()
{
  activateWindow ();
  raise ();
  auto * const field = ui->jttyQsoFieldsWidget->isVisible () ? ui->jttyCallEntry : ui->dxCallEntry;
  field->setFocus (Qt::ShortcutFocusReason);
  field->selectAll ();
}

// What %E sends. JTTY sends no report; logJttyQso() adds 599 for the log.
void MainWindow::updateJttySentExchange()
{
  if (m_mode != "JTTY") return;
  ui->jttySentEntry->setText (jtty_msg_expand (QStringLiteral ("%E")));
}

// Call, Sent and Received are for contest QSOs: shown for Run and S&P sets.
void MainWindow::updateJttyQsoFieldsVisibility()
{
  ui->jttyQsoFieldsWidget->setVisible (
    m_mode == "JTTY" && m_jttyMacros.current ().enterRole != Jtty::EnterRole::Off);
}

void MainWindow::appendJttyReceivedExchange(QString const& word)
{
  if (word.isEmpty ()) return;
  QString const text = ui->jttyReceivedEntry->text ().simplified ();
  ui->jttyReceivedEntry->setText (text.isEmpty () ? word.toUpper ()
                                                  : text + QStringLiteral (" ") + word.toUpper ());
}

namespace
{
  class JttyKeyForwarder final
    : public QObject
  {
  public:
    JttyKeyForwarder (std::function<bool (QKeyEvent *, QObject *)> handler, QObject * parent)
      : QObject {parent}
      , m_handler {std::move (handler)}
    {
    }

  protected:
    bool eventFilter (QObject * object, QEvent * event) override
    {
      if (event->type () == QEvent::KeyPress
          && m_handler (static_cast<QKeyEvent *> (event), object)) return true;
      return QObject::eventFilter (object, event);
    }

  private:
    std::function<bool (QKeyEvent *, QObject *)> m_handler;
  };
}

// The Wide Graph is a separate top-level window: operators click the
// waterfall to tune, then keep typing, so its keys must reach the Tx box too.
void MainWindow::installJttyWideGraphKeyFilter()
{
  auto * forwarder = new JttyKeyForwarder {
    [this] (QKeyEvent * e, QObject * receiver) { return jtty_key_struck (e, receiver); },
    m_wideGraph.data ()};
  m_wideGraph->installEventFilter (forwarder);
  for (auto * child : m_wideGraph->findChildren<QWidget *> ()) {
    child->installEventFilter (forwarder);
  }
}

bool MainWindow::sendJttyFunctionKey(int index)
{
  if (index < 1 || index > Jtty::macroKeyCount) return false;
  auto const control = Jtty::parseMacroControl (m_jttyMacros.macro (index).text);
  if (control.text.isEmpty () && !control.pttOn && !control.pttOff) return false;
  if ((control.text.contains (QStringLiteral ("%H"), Qt::CaseInsensitive) || control.logQso)
      && m_hisCall.trimmed ().isEmpty ()) {
    showStatusWarning (tr ("Enter a DX Call before sending this macro."));
    return true;
  }
  QString const nextCall = ui->jttyNextCallEntry->text ().trimmed ().toUpper ();
  if (control.text.contains (QStringLiteral ("%Q")) && nextCall.isEmpty ()) {
    showStatusWarning (tr ("Enter a Call next before sending this macro."));
    return true;
  }
  bool const startsNextQso = control.startsNextQso ();

  // Expanded now, so the operator sees exactly what will go out. The trailing
  // space completes the last word, so the whole macro commits in one piece:
  // split word by word it can cost up to 2.5 times as many frames.
  QString macro = control.text;
  macro.replace (QStringLiteral ("%Q"), nextCall);
  QString const text = jtty_msg_expand (
    macro, ui->sbSerialNumber_2->value () + (startsNextQso ? 1 : 0));
  if (!text.isEmpty ()) {
    QString const box = ui->Tx_Message->toPlainText ();
    bool const needsSeparator = !box.isEmpty () && !box.at (box.size () - 1).isSpace ();
    appendJttyTxBoxText ((needsSeparator ? QStringLiteral (" ") : QString {}) + text
                         + QStringLiteral (" "));
  }
  if (control.pttOn || control.pttOff) {
    // A macro that ends the over has to key it too: pressed from receive, %]
    // alone would otherwise queue text that nothing ever sends. The keying
    // commit is final whenever the macro closes the over, so it carries EOM.
    setJttyPttEnabled (true, control.pttOff);
    // The radio refused the mode change, so nothing went out: no step, no log.
    if (!m_jttyLiveEntryArmed) return true;
    if (control.pttOff) setJttyPttEnabled (false);
  }
  m_jttyEnterStep = Jtty::stepAfterKey (m_jttyMacros.current ().enterRole, index, m_jttyEnterStep);
  // Last: the Log QSO dialog may take focus, and logging may clear DX Call.
  if (control.logQso) logJttyQso ();
  if (startsNextQso) {
    // Logged after its data was captured, so the old QSO keeps its call.
    ui->dxCallEntry->setText (nextCall);
    ui->jttyNextCallEntry->clear ();
    if (control.text.contains (QStringLiteral ("%E"))) m_jttyEnterStep = Jtty::EnterStep::ExchangeSent;
  }
  return true;
}

QString MainWindow::jtty_msg_expand(QString t, int serialNumber)
{
  auto const context = jttyNativeMacroContext(
    m_config, m_hisCall, serialNumber < 0 ? ui->sbSerialNumber_2->value() : serialNumber,
    m_jttyHisCallSnr);
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
  if (index < 0 || index == m_jttyMacros.currentIndex ()) return;
  m_jttyMacros.setCurrentIndex (index);
  refreshJttyMacroControls ();
}

void MainWindow::refreshJttyMacroControls()
{
  {
    QSignalBlocker const blocker {ui->comboBoxJttyStyle};
    ui->comboBoxJttyStyle->clear ();
    for (auto const& set : m_jttyMacros.sets ()) ui->comboBoxJttyStyle->addItem (set.name);
    for (int i = 0; i < ui->comboBoxJttyStyle->count (); ++i) {
      ui->comboBoxJttyStyle->setItemData (i, Qt::AlignCenter, Qt::TextAlignmentRole);
    }
    ui->comboBoxJttyStyle->setCurrentIndex (m_jttyMacros.currentIndex ());
  }
  QPushButton * const buttons[Jtty::macroKeyCount] {
    ui->pbF1, ui->pbF2, ui->pbF3, ui->pbF4, ui->pbF5, ui->pbF6, ui->pbF7, ui->pbF8,
    ui->pbSF1, ui->pbSF2, ui->pbSF3, ui->pbSF4, ui->pbSF5, ui->pbSF6, ui->pbSF7, ui->pbSF8};
  for (int key = 1; key <= Jtty::macroKeyCount; ++key) {
    auto const macro = m_jttyMacros.macro (key);
    QString const name = Jtty::macroKeyName (key);
    QString const shortName = key <= 8 ? name : QStringLiteral ("S-F%1").arg (key - 8);
    auto * const button = buttons[key - 1];
    button->setText (macro.label.isEmpty () ? shortName
                                            : QStringLiteral ("%1: %2").arg (shortName, macro.label));
    button->setToolTip (macro.text.isEmpty () ? tr ("%1: empty").arg (name)
                                              : QStringLiteral ("%1: %2").arg (name, macro.text));
    button->setEnabled (!macro.text.isEmpty ());
  }
  updateJttyQsoFieldsVisibility ();
}

void MainWindow::switchToJttyAfterMmttyStartup()
{
  if (m_modeLocked || m_closing) return;
  bool const idle = !m_transmitting && !m_tune && g_iptt != 1
    && !ptt0Timer.isActive () && !m_jttyTxLifecycle.active ();
#ifdef WIN32
  if (m_mmttyHandoff.active ()) return;
#endif
  if (idle && m_mode != "JTTY") set_mode("JTTY");
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
  if (m_config.radio_mode_yielded ()) return;
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
  auto const encoded = Jtty::encodeNativeAtoms (compiled.atoms);
  if (encoded.tones.isEmpty ()) {
    logText(QStringLiteral("MMTTY/N1MM tagged JTTY request %1 rejected: %2")
            .arg(requestId)
            .arg(encoded.error));
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }
  auto const& tones = encoded.tones;

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
  if (m_config.radio_mode_yielded ()) return;
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

    QTimer::singleShot(3000, this, &MainWindow::switchToJttyAfterMmttyStartup);
}

MMTTYIF *MainWindow::getMmttyIf() const {
    return m_mmttyif;
}
#endif
