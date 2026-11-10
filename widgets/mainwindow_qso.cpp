#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "logqso.h"
#include "MessageBox.hpp"
#include "DecodedMessageReaction.hpp"
#include "qt_helpers.hpp"

#include <cmath>
#include <algorithm>
#include <QScopedValueRollback>

extern bool pounce;
extern bool filtered;
extern bool blocked;

namespace
{
  bool signoffPayload (QStringList const& words, QString const& hisCall, QString const& myCall,
                       QString const& signoff = "RR73")
  {
    auto baseCall = [] (QString call) {
      return Radio::base_callsign (call.remove ('<').remove ('>'));
    };
    return words.size () == 3 && words.at (2) == signoff
      && !hisCall.isEmpty () && !myCall.isEmpty ()
      && baseCall (words.at (0)) == baseCall (hisCall)
      && baseCall (words.at (1)) == baseCall (myCall);
  }
}

std::shared_ptr<QsoCompletion> MainWindow::currentQsoCompletion ()
{
  if (m_qsoCompletion && !m_qsoCompletion->matches (
        m_hisCall, m_mode, m_currentBand, m_TRperiod)) endQsoRepeat (QsoRepeatEnd::Superseded);
  if (!m_qsoCompletion) {
    m_qsoCompletion = std::make_shared<QsoCompletion> ();
    m_qsoCompletion->call = m_hisCall;
    m_qsoCompletion->mode = m_mode;
    m_qsoCompletion->band = m_currentBand;
    m_qsoCompletion->trPeriod = m_TRperiod;
  }
  return m_qsoCompletion;
}

bool MainWindow::repeatIsCurrent (std::shared_ptr<QsoCompletion> const& context) const
{
  return m_auto && repeatContextIsCurrent (context);
}

bool MainWindow::repeatContextIsCurrent (std::shared_ptr<QsoCompletion> const& context) const
{
  auto const signoff = m_mode == "Q65" && !send_rr73_for_tx4 () ? "RRR" : "RR73";
  return context && context == m_qsoCompletion
    && context->matches (m_hisCall, m_mode, m_currentBand, m_TRperiod)
    && !m_tune && m_config.repeat_Tx () && m_ntx == 4
    && signoffPayload (m_currentMessage.split (' ', SkipEmptyParts), m_hisCall, m_baseCall, signoff)
    && signoffPayload (ui->tx4->text ().split (' ', SkipEmptyParts), m_hisCall, m_baseCall, signoff);
}

bool MainWindow::updateRepeatTransmission (QStringList const& words)
{
  bool const eligible = (m_mode == "MSK144" || m_mode == "Q65")
    && m_config.repeat_Tx () && m_auto && !m_tune && !m_hisCall.isEmpty ()
    && m_ntx == 4 && signoffPayload (words, m_hisCall, m_baseCall);
  if (m_qsoCompletion && (!m_qsoCompletion->matches (
        m_hisCall, m_mode, m_currentBand, m_TRperiod)
      || (!eligible && (m_qsoCompletion->phase != QsoCompletion::Phase::Ordinary
          || (m_ntx <= 3 && m_qsoCompletion->logState != QsoCompletion::LogState::None)))))
    endQsoRepeat (QsoRepeatEnd::Superseded);
  if (!eligible) return false;

  if (m_qsoCompletion && !m_qsoCompletion->defersClear ()) endQsoRepeat (QsoRepeatEnd::Superseded);
  auto const context = currentQsoCompletion ();
  if (context->waiting ()) return true;
  context->phase = QsoCompletion::Phase::AwaitingFinal73;
  m_repeatDeadline.start (int (20000.0 * m_TRperiod));
  return true;
}

void MainWindow::recordFinalConfirmation ()
{
  auto const context = currentQsoCompletion ();
  if (!repeatContextIsCurrent (context) || context->phase == QsoCompletion::Phase::Cancelled
      || context->confirmedAt.isValid ()) return;
  context->confirmedAt = QDateTime::currentDateTimeUtc ();
  for (auto& request : m_pendingLogQsos) {
    if (request.context == context)
      request.data.dateTimeOff = qMax (request.data.dateTimeOn, context->confirmedAt);
  }
}

void MainWindow::applyPendingQsoClear (std::shared_ptr<QsoCompletion> const& context)
{
  if (context != m_qsoCompletion || context->defersClear ()
      || context->phase == QsoCompletion::Phase::Cancelled
      || context->logState != QsoCompletion::LogState::Accepted
      || !context->matches (m_hisCall, m_mode, m_currentBand, m_TRperiod)) return;
  bool const clearCall = context->clearDxPending;
  bool const clearGrid = context->clearGridPending;
  context->clearDxPending = false;
  context->clearGridPending = false;
  if (clearGrid) ui->dxGridEntry->clear ();
  if (clearCall) {
    // A logged-call clear must respect the independent grid-clear preference.
    QScopedValueRollback<bool> clearingLoggedContact {blocked, true};
    clearDX ();
  }
}

void MainWindow::endQsoRepeat (QsoRepeatEnd reason, std::shared_ptr<QsoCompletion> context)
{
  if (!context) context = m_qsoCompletion;
  if (!context) return;
  bool const current = context == m_qsoCompletion;
  switch (reason) {
  case QsoRepeatEnd::Superseded:
    context->cancel ();
    if (current) {
      m_repeatDeadline.stop ();
      m_qsoCompletion.reset ();
    }
    return;
  case QsoRepeatEnd::ActivityChanged:
  case QsoRepeatEnd::Fault:
  case QsoRepeatEnd::AutoDisabled:
    if (context->defersClear ()) {
      context->cancel ();
      if (current) m_repeatDeadline.stop ();
    }
    return;
  case QsoRepeatEnd::FinalReply:
  case QsoRepeatEnd::Deadline:
  case QsoRepeatEnd::OperatorStop:
    break;
  }
  bool const q65Rrr = reason == QsoRepeatEnd::FinalReply
    && context->phase == QsoCompletion::Phase::Ordinary && m_mode == "Q65"
    && !send_rr73_for_tx4 () && context->confirmedAt.isValid ();
  if (!context->defersClear () && !q65Rrr) return;
  bool const timedOut = context->phase == QsoCompletion::Phase::TimedOut;
  if (!(timedOut ? repeatContextIsCurrent (context) : repeatIsCurrent (context))) {
    context->cancel ();
    if (current) m_repeatDeadline.stop ();
    return;
  }
  m_repeatDeadline.stop ();
  context->phase = QsoCompletion::Phase::Finished;
  if (reason != QsoRepeatEnd::OperatorStop) auto_tx_mode (false);
  if (context != m_qsoCompletion) return;
  if (reason == QsoRepeatEnd::Deadline && !context->confirmedAt.isValid ()) {
    context->phase = QsoCompletion::Phase::TimedOut;
    return;
  }
  if (context->confirmedAt.isValid () && reason != QsoRepeatEnd::OperatorStop
      && ui->cbAutoSeq->isVisible () && ui->cbAutoSeq->isEnabled () && ui->cbAutoSeq->isChecked ()) {
    m_ntx = 6;
    ui->txrb6->setChecked (true);
    m_QSOProgress = CALLING;
  }
  applyPendingQsoClear (context);
}

QsoLogData MainWindow::captureLogQso ()
{
  if (!m_dateTimeQSOOn.isValid ()) {
    auto const now = QDateTime::currentDateTimeUtc ();
    m_dateTimeQSOOn = now.addSecs (-(m_ntx - 2) * int (m_TRperiod)
      - int (std::fmod (double (now.time ().second ()), m_TRperiod)));
  }
  auto grid = m_hisGrid == "...." ? QString {} : m_hisGrid;
  if (m_config.ZZ00 () && m_hisGrid.isEmpty () && m_specOp != SpecOp::NONE
      && m_specOp != SpecOp::FOX && m_specOp != SpecOp::HOUND) m_hisGrid = "ZZ00";
  switch (m_specOp) {
  case SpecOp::NA_VHF:
  case SpecOp::WW_DIGI:
  case SpecOp::ARRL_DIGI:
    m_xSent = m_config.my_grid ().left (4);
    m_xRcvd = m_hisGrid.left (4);
    break;
  case SpecOp::EU_VHF: {
    m_rptSent = m_xSent.section (' ', 0, 0).left (2);
    m_rptRcvd = m_xRcvd.section (' ', 0, 0).left (2);
    auto const fields = m_xRcvd.split (' ', SkipEmptyParts);
    if (fields.size () >= 2) m_hisGrid = fields.at (1);
    grid = m_hisGrid;
    ui->dxGridEntry->setText (grid);
    break;
  }
  case SpecOp::FIELD_DAY:
  case SpecOp::RTTY:
    m_rptSent = m_xSent.section (' ', 0, 0);
    m_rptRcvd = m_xRcvd.section (' ', 0, 0);
    break;
  case SpecOp::Q65_PILEUP:
    m_xSent = m_config.my_grid ().left (4);
    m_xRcvd = m_hisGrid;
    break;
  default: break;
  }
  QsoLogData data;
  data.call = m_hisCall;
  data.grid = grid;
  data.mode = m_mode;
  data.rptSent = m_rptSent;
  data.rptRcvd = m_rptRcvd;
  data.dateTimeOn = m_dateTimeQSOOn;
  data.dateTimeOff = qMax (data.dateTimeOn, QDateTime::currentDateTimeUtc ());
  data.dialFreq = m_operatingFrequency.rx () + ui->TxFreqSpinBox->value ();
  data.noSuffix = m_noSuffix;
  data.xSent = m_xSent;
  data.xRcvd = m_xRcvd;
  data.specOp = m_specOp;
  data.myCall = m_config.my_callsign ();
  data.myGrid = m_config.my_grid ();
  data.operatorCall = m_config.opCall ();
  data.contestName = m_config.Contest_Name ();
  data.individualContestName = m_config.Individual_Contest_Name ();
  data.superFox = m_config.superFox ();
  return data;
}

void MainWindow::scheduleLogQso (bool automatic)
{
  auto const context = currentQsoCompletion ();
  if (automatic && context->phase != QsoCompletion::Phase::Ordinary
      && context->logState != QsoCompletion::LogState::None) return;
  if (!automatic && m_openLogQso && m_openLogQso->context == context) {
    m_logDlg->showNormal ();
    m_logDlg->raise ();
    m_logDlg->activateWindow ();
    return;
  }
  if (context->logState == QsoCompletion::LogState::Pending) return;
  if (std::any_of (m_pendingLogQsos.begin (), m_pendingLogQsos.end (),
      [&context] (QsoLogRequest const& request) {return request.context == context;})) return;
  if (m_hisCall.isEmpty ()) {
    MessageBox::warning_message (this, tr ("Warning:  DX Call field is empty."));
    if ((m_specOp == SpecOp::NA_VHF || m_specOp == SpecOp::WW_DIGI) && m_config.autoLog ()) return;
  }
  auto data = captureLogQso ();
  if (context->confirmedAt.isValid ())
    data.dateTimeOff = qMax (data.dateTimeOn, context->confirmedAt);
  if (context->logState != QsoCompletion::LogState::Accepted)
    context->logState = QsoCompletion::LogState::Pending;
  QsoLogRequest request;
  request.data = std::move (data);
  request.context = context;
  request.automatic = automatic;
  m_pendingLogQsos.push_back (std::move (request));
  logQSOTimer.start (0);
}

bool MainWindow::ownsQsoContact (QsoLogRequest const& request) const
{
  auto const& context = request.context;
  return context == m_qsoCompletion
    && context->matches (m_hisCall, m_mode, m_currentBand, m_TRperiod)
    && (!request.automatic || context->phase != QsoCompletion::Phase::Cancelled);
}

void MainWindow::applyLogCompletionEffects (QsoLogRequest const& request)
{
  auto const context = request.context;
  if (ownsQsoContact (request)) {
    if (!context->defersClear ()) {
      DecodedMessageReaction::applyAutoTxStopAfterLogging (
        m_mode, m_config.repeat_Tx (), send_rr73_for_tx4 (), [this] {
          if (m_specOp == SpecOp::NA_VHF && m_mode == "FT4" && m_config.NCCC_Sprint ())
            QTimer::singleShot (int (850.0 * m_TRperiod), this, [this] {cease_auto_Tx_after_QSO ();});
          else cease_auto_Tx_after_QSO ();
        });
    }
    m_inQSOwith.clear ();
    if (ui->respondComboBox->isVisible () && autoRespondPolicy () != AutoRespondPolicy::None)
      m_autoRespondScores.reset ();
    stopWRTimer.stop ();
    stopWCTimer.stop ();
  }
}

void MainWindow::processLogRequests ()
{
  if (m_processingLogRequests) return;
  {
    QScopedValueRollback<bool> processing {m_processingLogRequests, true};
    for (;;) {
      auto request = std::find_if (m_pendingLogQsos.begin (), m_pendingLogQsos.end (),
        [] (QsoLogRequest const& item) {return item.stage == QsoLogRequest::Stage::Queued;});
      if (request == m_pendingLogQsos.end ()) break;
      request->stage = QsoLogRequest::Stage::ReadyToDisplay;
      auto const captured = *request;
      applyLogCompletionEffects (captured);
    }
  }
  // Dialog initialization can enter a nested event loop that completes another QSO.
  showNextLogQso ();
}

void MainWindow::showNextLogQso ()
{
  if (m_openLogQso || m_pendingLogQsos.empty ()) return;
  Q_ASSERT (m_pendingLogQsos.front ().stage == QsoLogRequest::Stage::ReadyToDisplay);
  m_openLogQso = std::move (m_pendingLogQsos.front ());
  m_pendingLogQsos.pop_front ();
  auto const context = m_openLogQso->context;
  m_logDlg->initLogQSO (m_openLogQso->data);
  if (context == m_qsoCompletion && pounce
      && (!m_pounceSelectionTimer.isActive () || m_pounceSelectionTimer.remainingTime () > 2000))
    m_pounceSelectionTimer.start (2000);
  QTimer::singleShot (2000, this, [this, context] {
    if (context == m_qsoCompletion) {
      filtered = false;
      check_button_color ();
    }
    read_txLog ();
  });
  QTimer::singleShot (7000, this, [this] {read_txLog ();});
}

void MainWindow::on_logQSOButton_clicked ()
{
  scheduleLogQso (false);
}
