#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "logqso.h"
#include "DecodedMessageReaction.hpp"
#include "qt_helpers.hpp"
#include "Audio/FixtureAudioInput.hpp"
#include "Audio/FixtureSoundOutput.hpp"
#include "Decoder/decodedtext.h"
#include "Network/MessageClient.hpp"
#include "Network/NetworkMessage.hpp"
#include "models/Bands.hpp"

#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QUdpSocket>

QString MainWindow::checkQsoCompletionTest ()
{
  if (!m_automated_test || !m_config.is_dummy_rig ()
      || !qobject_cast<FixtureAudioInput *> (m_soundInput)
      || !qobject_cast<FixtureSoundOutput *> (m_soundOutput))
    return "QSO completion checks require the isolated dummy-rig audio fixtures.";

  m_guiTimer.stop ();
  QObject scope;
  connect (&scope, &QObject::destroyed, this, [this] {
    logQSOTimer.stop ();
    m_pendingLogQsos.clear ();
    endQsoRepeat (QsoRepeatEnd::Superseded);
    if (!m_logDlg->isHidden ()) m_logDlg->reject ();
    auto_tx_mode (false);
  });
  auto drain = [] (int milliseconds = 20) {
    QEventLoop loop;
    QTimer::singleShot (milliseconds, &loop, &QEventLoop::quit);
    loop.exec ();
  };

  QUdpSocket notifications;
  if (!notifications.bind (QHostAddress {QHostAddress::LocalHost}, 0))
    return "Unable to listen for local QSO notifications.";
  m_messageClient->set_server ("127.0.0.1", {});
  m_messageClient->set_server_port (notifications.localPort ());
  drain ();
  int accepted = 0;
  int loggedNotifications = 0;
  int adifNotifications = 0;
  QStringList acceptedCalls;
  connect (m_logDlg.data (), &LogQSO::acceptQSO, &scope,
           [&] (QDateTime const&, QString const& call) {
    ++accepted;
    acceptedCalls.append (call);
  });
  auto collectNotifications = [&] {
    while (notifications.hasPendingDatagrams ()) {
      NetworkMessage::Reader message {notifications.receiveDatagram ().data ()};
      if (message.type () == NetworkMessage::QSOLogged) ++loggedNotifications;
      if (message.type () == NetworkMessage::LoggedADIF) ++adifNotifications;
    }
  };
  auto adifRecords = [&] {
    QFile file {m_logBook.path ()};
    return file.open (QIODevice::ReadOnly) ? file.readAll ().toUpper ().count ("<EOR>") : 0;
  };
  auto const initialRecords = adifRecords ();
  bool const logging = m_config.autoLog () || m_config.prompt_to_log ();
  bool const prompting = m_config.prompt_to_log ();
  bool const clearCall = m_config.clear_DXcall ();
  bool const clearGrid = m_config.clear_DXgrid ();
  auto fieldsMatch = [&] (bool logged) {
    return m_hisCall == (logged && clearCall ? QString {} : QString {"W1AW"})
      && ui->dxGridEntry->text () == (logged && clearGrid ? QString {} : QString {"FN31"});
  };
  auto describe = [&] (std::shared_ptr<QsoCompletion> const& context) {
    return QString {" auto=%1 call=%2 ntx=%3 message=[%4] tx4=[%5] band=%6/%7 "
                    "period=%8/%9 phase=%10 clear=%11 open=%12 automatic=%13 log=%14 current=%15"}
      .arg (m_auto).arg (m_hisCall).arg (m_ntx).arg (m_currentMessage)
      .arg (ui->tx4->text ()).arg (m_currentBand).arg (context ? context->band : "none")
      .arg (m_TRperiod).arg (context ? context->trPeriod : -1)
      .arg (context ? int (context->phase) : -1).arg (context && context->clearDxPending)
      .arg (bool (m_openLogQso)).arg (m_openLogQso && m_openLogQso->automatic)
      .arg (context ? int (context->logState) : -1).arg (context == m_qsoCompletion);
  };
  auto prepare = [&] (QString const& mode, QString const& call, double period = 15.0) {
    endQsoRepeat (QsoRepeatEnd::Superseded);
    auto_tx_mode (false);
    m_mode = mode;
    m_specOp = SpecOp::NONE;
    m_currentBand = m_config.bands ()->find (m_operatingFrequency.rx ());
    m_TRperiod = period;
    m_baseCall = "K1ABC";
    m_bShMsgs = false;
    m_send_RR73 = true;
    m_tune = false;
    ui->dxCallEntry->setText (call);
    ui->dxGridEntry->setText ("FN31");
    genStdMsgs ("-10");
    ui->txrb4->setChecked (true);
    m_ntx = 4;
    m_QSOProgress = ROGERS;
    m_currentMessage = call + " K1ABC RR73";
    ui->tx4->setText (m_currentMessage);
    m_rptSent = "-10";
    m_rptRcvd = "-12";
    m_xSent = "sent exchange";
    m_xRcvd = "received exchange";
    m_dateTimeQSOOn = QDateTime::currentDateTimeUtc ().addSecs (-60);
    auto_tx_mode (true);
    return updateRepeatTransmission (m_currentMessage.split (' ', SkipEmptyParts));
  };
  auto logFirst = [&] {
    if (!logging) return;
    scheduleLogQso ();
    drain ();
    if (prompting) {
      m_logDlg->accept ();
      drain (60);
    }
  };
  auto receive73 = [&] (QString const& mode, QString const& call) {
    DecodedText message {QString {"060500 -10  0.3 1500 %1  K1ABC %2 73"}
      .arg (mode == "MSK144" ? "&" : ":").arg (call)};
    auto_sequence (message, 25, 50);
  };

  for (auto const& mode : {QString {"MSK144"}, QString {"Q65"}}) {
    for (bool autoSequence : {false, true}) {
      if (!prepare (mode, "W1AW", 0.02)) return mode + ": RR73 did not start a repeat.";
      ui->cbAutoSeq->setVisible (true);
      ui->cbAutoSeq->setEnabled (true);
      ui->cbAutoSeq->setChecked (autoSequence);
      auto const context = m_qsoCompletion;
      auto const deadline = m_repeatDeadline.timerId ();
      auto const before = accepted;
      logFirst ();
      if (m_auto != true || m_hisCall != "W1AW" || ui->dxGridEntry->text () != "FN31")
        return mode + ": first RR73 logging stopped or cleared the repeat.";
      if (accepted != before + (logging ? 1 : 0))
        return mode + ": first RR73 was not logged exactly once.";
      if (!updateRepeatTransmission (m_currentMessage.split (' ', SkipEmptyParts))
          || context != m_qsoCompletion || m_repeatDeadline.timerId () != deadline)
        return mode + ": repeated RR73 extended or replaced the deadline.";
      m_sentFirst73 = false;
      receive73 (mode, "W1AW");
      receive73 (mode, "W1AW");
      drain (40);
      if (m_auto || context->waiting () || m_repeatDeadline.isActive ())
        return mode + ": partner 73 did not finish the repeat.";
      if (!fieldsMatch (logging))
        return mode + ": natural finish did not honor the log and clear preferences.";
      int const expectedMessage = autoSequence || (logging && clearCall) ? 6 : 4;
      if (m_ntx != expectedMessage || (expectedMessage == 6 && m_QSOProgress != CALLING))
        return mode + ": confirmed completion selected the wrong next message or progress.";
      if (accepted != before + (logging ? 1 : 0))
        return mode + ": later partner 73 logged the QSO a second time.";
    }

    if (!prepare (mode, "W1AW")) return mode + ": Stop setup failed.";
    logFirst ();
    auto const stoppedContext = m_qsoCompletion;
    on_stopTxButton_clicked ();
    if (m_auto || !fieldsMatch (logging))
      return mode + ": explicit Stop did not end the wait and clear a logged contact."
        + describe (stoppedContext);

    if (!prepare (mode, "W1AW")) return mode + ": disabled-Enable-Tx setup failed.";
    logFirst ();
    auto const disabledContext = m_qsoCompletion;
    on_autoButton_clicked (false);
    if (m_auto || !fieldsMatch (logging))
      return mode + ": disabling Enable Tx did not release the deferred clear."
        + describe (disabledContext);

    if (!prepare (mode, "W1AW")) return mode + ": fault-stop setup failed.";
    logFirst ();
    auto const faultContext = m_qsoCompletion;
    requestTxStop (TxEvidence::TxStopReason::Error);
    if (m_auto || m_hisCall != "W1AW" || faultContext->clearDxPending
        || m_repeatDeadline.isActive ())
      return mode + ": a fault stop applied a deferred clear or left a deadline active.";

    if (!prepare (mode, "W1AW")) return mode + ": delayed-auto-off setup failed.";
    logFirst ();
    auto const flagContext = m_qsoCompletion;
    DecodedMessageReaction::QsoReactionEffect autoOff;
    autoOff.kind = DecodedMessageReaction::QsoReactionEffect::Kind::ScheduleAutoFlagOff;
    DecodedText signoff {"060500 -10  0.3 1500 &  K1ABC W1AW 73"};
    applyQsoReactionEffect (autoOff, signoff, nullptr);
    drain ();
    if (m_auto || m_hisCall != "W1AW" || flagContext->clearDxPending
        || m_repeatDeadline.isActive ())
      return mode + ": delayed Auto Tx shutdown did not cancel the repeat cleanly.";

    if (!prepare (mode, "W1AW")) return mode + ": Tune setup failed.";
    logFirst ();
    drain (60);
    auto const tuneContext = m_qsoCompletion;
    on_tuneButton_clicked (true);
    end_tuning ();
    drain (300);
    if (m_hisCall != "W1AW" || tuneContext->clearDxPending
        || m_repeatDeadline.isActive ())
      return mode + ": tuning retained a repeat deadline or cleared its contact.";

    if (!prepare (mode, "W1AW")) return mode + ": timeout setup failed.";
    logFirst ();
    auto const timeoutContext = m_qsoCompletion;
    if (m_repeatDeadline.interval () != 300000)
      return mode + ": the repeat budget is not twenty TR periods.";
    m_repeatDeadline.start (10);
    drain (30);
    if (m_auto || timeoutContext->phase != QsoCompletion::Phase::TimedOut
        || m_repeatDeadline.isActive () || !fieldsMatch (false) || m_ntx != 4)
      return mode + ": deadline did not retain the unconfirmed contact for retry.";
    auto const loggedBeforeRetry = accepted;
    on_autoButton_clicked (true);
    if (!updateRepeatTransmission (m_currentMessage.split (' ', SkipEmptyParts))
        || m_qsoCompletion != timeoutContext || !timeoutContext->waiting ()
        || !m_repeatDeadline.isActive ())
      return mode + ": retry replaced the contact or failed to restart its deadline.";
    if (logging) scheduleLogQso ();
    drain ();
    if (accepted != loggedBeforeRetry || m_openLogQso)
      return mode + ": retry requested another log for the same contact.";
    m_repeatDeadline.start (10);
    drain (30);
    receive73 (mode, "W1AW");
    drain (30);
    if (m_auto || !fieldsMatch (logging) || m_ntx != 6 || accepted != loggedBeforeRetry)
      return mode + ": late confirmation failed to complete the timed-out contact exactly once.";

    if (!prepare (mode, "W1AW")) return mode + ": Stop-after-timeout setup failed.";
    logFirst ();
    m_repeatDeadline.start (10);
    drain (30);
    on_stopTxButton_clicked ();
    if (m_auto || !fieldsMatch (logging))
      return mode + ": explicit Stop after timeout did not release the deferred clear.";

    if (logging) {
      if (!prepare (mode, "W1AW")) return mode + ": manual log after timeout setup failed.";
      auto const manualTimeout = m_qsoCompletion;
      m_repeatDeadline.start (10);
      drain (30);
      auto const before = accepted;
      scheduleLogQso (false);
      drain ();
      if (prompting) {
        if (!m_openLogQso) return mode + ": manual log after timeout did not open its prompt.";
        m_logDlg->accept ();
        drain ();
      }
      if (m_auto || manualTimeout->phase != QsoCompletion::Phase::TimedOut
          || !fieldsMatch (false) || m_ntx != 4 || accepted != before + 1)
        return mode + ": manual logging canceled or cleared the timed-out contact.";
      receive73 (mode, "W1AW");
      drain (30);
      if (m_auto || !fieldsMatch (true) || m_ntx != 6 || accepted != before + 1)
        return mode + ": late confirmation after manual logging failed to complete exactly once.";
    }

    if (!prepare (mode, "W1AW")) return mode + ": changed-message setup failed.";
    logFirst ();
    auto const oldContext = m_qsoCompletion;
    m_ntx = 6;
    m_currentMessage = "CQ K1ABC FN42";
    if (updateRepeatTransmission (m_currentMessage.split (' ', SkipEmptyParts)))
      return mode + ": CQ was treated as a repeat transmission.";
    endQsoRepeat (QsoRepeatEnd::Deadline, oldContext);
    if (!m_auto || m_hisCall != "W1AW" || m_repeatDeadline.isActive ()
        || oldContext->clearDxPending)
      return mode + ": canceling a changed transmission affected the new activity.";

    for (int change = 0; change != 4; ++change) {
      if (!prepare (mode, "W1AW")) return mode + ": stale-timeout setup failed.";
      logFirst ();
      auto const stale = m_qsoCompletion;
      if (change == 0) ui->dxCallEntry->setText ("W2XYZ");
      if (change == 1) m_currentBand = "2m";
      if (change == 2) m_TRperiod = 30.0;
      if (change == 3) m_mode = "FT8";
      m_repeatDeadline.start (10);
      drain (30);
      if (!m_auto || m_hisCall != (change == 0 ? "W2XYZ" : "W1AW")
          || stale->clearDxPending)
        return mode + QString {": a stale deadline affected a changed QSO context (%1)."}.arg (change)
          + describe (stale);
    }
  }

  for (bool autoSequence : {false, true}) {
    if (!prepare ("Q65", "W1AW")) return "Q65 RRR setup failed.";
    endQsoRepeat (QsoRepeatEnd::Superseded);
    m_send_RR73 = false;
    m_currentMessage = "W1AW K1ABC RRR";
    ui->tx4->setText (m_currentMessage);
    ui->cbAutoSeq->setChecked (autoSequence);
    auto const before = accepted;
    receive73 ("Q65", "W1AW");
    if (m_auto || m_ntx != (autoSequence ? 6 : 4))
      return "Q65 RRR confirmation did not stop Auto Tx independently of Auto Seq and logging.";
    drain ();
    if (prompting) {
      if (!m_openLogQso) return "Q65 RRR confirmation failed to offer logging.";
      m_logDlg->accept ();
      drain ();
    }
    if (!fieldsMatch (logging) || accepted != before + (logging ? 1 : 0))
      return "Q65 RRR confirmation lost the log or applied the wrong clear preference.";
    receive73 ("Q65", "W1AW");
    drain ();
    if (m_auto || m_openLogQso || accepted != before + (logging ? 1 : 0))
      return "Repeated Q65 RRR confirmation restarted transmission or logging.";
  }

  if (prompting) {
    prepare ("FT8", "W1AW");
    ui->cbAutoSeq->setVisible (true);
    ui->cbAutoSeq->setEnabled (true);
    ui->cbAutoSeq->setChecked (true);
    scheduleLogQso ();
    drain ();
    if (!m_openLogQso) return "The first ordinary FT8 log prompt did not open.";
    auto const firstPrompt = m_openLogQso->context;
    prepare ("FT8", "W2XYZ");
    m_send_RR73 = false;
    m_currentMessage = "W2XYZ K1ABC RRR";
    ui->tx4->setText (m_currentMessage);
    m_sentFirst73 = false;
    m_inQSOwith = "W2XYZ";
    stopWRTimer.start (10000);
    stopWCTimer.start (10000);
    DecodedText finalReply {"060500 -10  0.3 1500 ~  K1ABC W2XYZ 73"};
    auto_sequence (finalReply, 25, 50);
    if (m_pendingLogQsos.size () != 1)
      return "The ordinary FT8 final reply did not queue its log behind the open prompt.";
    auto const queuedContact = m_pendingLogQsos.front ().context;
    drain ();
    if (m_auto || !m_inQSOwith.isEmpty () || stopWRTimer.isActive () || stopWCTimer.isActive ()
        || !m_openLogQso || m_openLogQso->context != firstPrompt)
      return "An open log prompt delayed the next contact's completion effects.";

    prepare ("FT8", "W3NEW");
    m_inQSOwith = "W3NEW";
    stopWRTimer.start (10000);
    stopWCTimer.start (10000);
    m_logDlg->accept ();
    drain ();
    if (!m_openLogQso || m_openLogQso->context != queuedContact
        || m_openLogQso->data.call != "W2XYZ" || !m_auto || m_inQSOwith != "W3NEW"
        || !stopWRTimer.isActive () || !stopWCTimer.isActive ())
      return "Displaying a queued log replayed completion effects against a new contact.";
    m_logDlg->accept ();
    drain ();
    if (!m_auto || m_hisCall != "W3NEW")
      return "Accepting a queued ordinary log affected a newer contact.";
    stopWRTimer.stop ();
    stopWCTimer.stop ();

    if (!prepare ("MSK144", "W1AW")) return "Queued-log setup failed.";
    m_rptSent = "-07";
    auto const originalStart = m_dateTimeQSOOn;
    scheduleLogQso ();
    auto const original = m_qsoCompletion;
    if (!prepare ("Q65", "W2XYZ")) return "New-contact setup failed.";
    m_rptSent = "-20";
    scheduleLogQso ();
    auto const newer = m_qsoCompletion;
    drain ();
    auto * dialogCall = m_logDlg->findChild<QLineEdit *> ("call");
    auto * dialogReport = m_logDlg->findChild<QLineEdit *> ("sent");
    if (!m_openLogQso || m_openLogQso->context != original
        || !dialogCall || !dialogReport || dialogCall->text () != "W1AW"
        || dialogReport->text () != "-07"
        || m_openLogQso->data.dateTimeOn != originalStart)
      return "Queued logging did not retain the original QSO data.";
    auto const newerStart = m_dateTimeQSOOn;
    m_xSent = "new sent";
    m_xRcvd = "new received";
    m_logDlg->accept ();
    drain ();
    if (m_hisCall != "W2XYZ" || m_hisGrid != "FN31" || !m_auto
        || m_dateTimeQSOOn != newerStart || m_xSent != "new sent" || m_xRcvd != "new received"
        || !m_openLogQso || m_openLogQso->context != newer
        || dialogCall->text () != "W2XYZ")
      return "Accepting an older log modified the newer QSO or lost its queued dialog.";
    dialogCall->setText ("W2XYA");
    auto * dialogMode = m_logDlg->findChild<QLineEdit *> ("mode");
    if (!dialogMode) return "The log dialog mode editor was not found.";
    dialogMode->setText ("RTTY");
    on_actionActiveStations_triggered ();
    m_EMEworked.remove ("W2XYA");
    m_logDlg->accept ();
    drain ();
    if (!m_auto || m_hisCall != "W2XYZ" || newer->clearDxPending != clearCall
        || acceptedCalls.last () != "W2XYA" || !m_EMEworked.value ("W2XYA"))
      return "Editing the logged callsign lost the repeat's ownership.";
    on_stopTxButton_clicked ();
    if (m_hisCall != (clearCall ? QString {} : QString {"W2XYZ"}))
      return "Stop did not honor the clear preference for the edited log's original contact.";

    if (!prepare ("MSK144", "W1AW")) return "Delayed-accept setup failed.";
    scheduleLogQso ();
    drain ();
    on_stopTxButton_clicked ();
    m_logDlg->accept ();
    drain ();
    if (!fieldsMatch (true)) return "Accepting after Stop did not honor the clear preferences.";

    if (!prepare ("MSK144", "W1AW")) return "Changed-selection logging setup failed.";
    scheduleLogQso ();
    drain ();
    auto const changedSelection = m_qsoCompletion;
    ui->txrb6->setChecked (true);
    m_ntx = 6;
    ui->tx6->setText ("CQ K1ABC FN42");
    ui->cbAutoSeq->setChecked (true);
    m_sentFirst73 = false;
    receive73 ("MSK144", "W1AW");
    drain ();
    if (!m_auto) return "An old final reply stopped a newly selected message.";
    m_dateTimeQSOOn = QDateTime::currentDateTimeUtc ().addSecs (-17);
    auto const selectedStart = m_dateTimeQSOOn;
    m_xSent = "next sent";
    m_xRcvd = "next received";
    m_logDlg->accept ();
    drain ();
    if (!m_auto || m_hisCall != "W1AW" || m_hisGrid != "FN31"
        || m_dateTimeQSOOn != selectedStart || m_xSent != "next sent"
        || m_xRcvd != "next received" || changedSelection->waiting ()
        || changedSelection->clearDxPending)
      return "Accepting a log cleared new activity selected before its first transmission.";
    receive73 ("MSK144", "W1AW");
    drain ();
    if (!m_auto || m_openLogQso || !m_pendingLogQsos.empty ())
      return "A late final reply resumed a canceled repeat after accepting its log.";

    if (!prepare ("MSK144", "W1AW")) return "Accepted-log rejection setup failed.";
    logFirst ();
    auto const previouslyAccepted = m_qsoCompletion;
    scheduleLogQso (false);
    drain ();
    if (!m_openLogQso) return "Manual logging after an accepted QSO was unavailable.";
    m_logDlg->reject ();
    drain ();
    if (previouslyAccepted->logState != QsoCompletion::LogState::Accepted)
      return "Rejecting a later manual log lost the QSO's accepted status.";

    if (!prepare ("Q65", "W3OLD")) return "Earlier Q65 logging setup failed.";
    scheduleLogQso ();
    if (!prepare ("Q65", "W4NEW")) return "Later Q65 logging setup failed.";
    auto const nextQsoStart = m_dateTimeQSOOn;
    m_EMEworked.remove ("W3OLD");
    drain ();
    m_logDlg->accept ();
    drain ();
    if (!m_EMEworked.value ("W3OLD") || m_hisCall != "W4NEW"
        || m_dateTimeQSOOn != nextQsoStart || !m_auto)
      return "An older same-mode Q65 log lost its worked status or changed the new contact.";

    if (!prepare ("Q65", "W5OLD")) return "Earlier Q65 activity setup failed.";
    scheduleLogQso ();
    if (!prepare ("MSK144", "W6NEW")) return "Changed-mode logging setup failed.";
    auto const scoreBeforeAccept = m_score;
    m_EMEworked.remove ("W5OLD");
    drain ();
    m_logDlg->accept ();
    drain ();
    if (!m_EMEworked.value ("W5OLD") || m_score != scoreBeforeAccept
        || m_hisCall != "W6NEW" || !m_auto)
      return "An older Q65 log lost its worked status or changed the new activity.";

    for (auto const& mode : {QString {"MSK144"}, QString {"Q65"}}) {
      for (bool leavePromptOpen : {false, true}) {
        for (bool acceptConfirmed : {false, true}) {
          if (!prepare (mode, "W1AW", 0.02)) return mode + ": confirmation prompt setup failed.";
          ui->cbAutoSeq->setChecked (true);
          auto const start = m_dateTimeQSOOn;
          auto const before = accepted;
          scheduleLogQso ();
          drain ();
          auto const context = m_qsoCompletion;
          if (!m_openLogQso) return mode + ": first RR73 prompt did not open.";
          dialogReport->setText ("-08");
          if (!leavePromptOpen) {
            m_logDlg->reject ();
            drain ();
            if (context->logState != QsoCompletion::LogState::None || !m_auto)
              return mode + ": rejecting before confirmation did not preserve the repeat.";
          }
          receive73 (mode, "W1AW");
          auto const confirmation = context->confirmedAt;
          receive73 (mode, "W1AW");
          if (mode == "MSK144") m_repeatDeadline.start (1);
          drain (40);
          if (m_auto || m_ntx != 6 || !fieldsMatch (false))
            return mode + ": completion depended on accepting the log prompt.";
          if (!confirmation.isValid () || context->confirmedAt != confirmation
              || !m_openLogQso || m_openLogQso->context != context || !m_pendingLogQsos.empty ())
            return mode + ": confirmation was lost or opened more than one log prompt.";
          if (m_openLogQso->data.dateTimeOn != start
              || (!leavePromptOpen && m_openLogQso->data.dateTimeOff != confirmation))
            return mode + ": renewed logging lost the original start or confirmation end time.";
          if (leavePromptOpen && dialogReport->text () != "-08")
            return mode + ": confirmation overwrote an operator edit in the open prompt.";
          if (acceptConfirmed) m_logDlg->accept ();
          else m_logDlg->reject ();
          drain ();
          if (accepted != before + int (acceptConfirmed) || !fieldsMatch (acceptConfirmed))
            return mode + ": accepting or declining the confirmed log applied the wrong effects.";
          if (!acceptConfirmed && context->logState != QsoCompletion::LogState::Declined)
            return mode + ": declining a confirmed contact was not final.";
          receive73 (mode, "W1AW");
          drain ();
          if (m_auto || m_openLogQso || !m_pendingLogQsos.empty ()
              || accepted != before + int (acceptConfirmed))
            return mode + ": another 73 reopened a completed contact's log prompt.";
          if (!acceptConfirmed) {
            scheduleLogQso (false);
            drain ();
            if (!m_openLogQso) return mode + ": declining automatic logging disabled manual logging.";
            m_logDlg->reject ();
            drain ();
          }
        }
      }

      if (!prepare (mode, "W1AW")) return mode + ": delayed timeout acceptance setup failed.";
      scheduleLogQso ();
      drain ();
      auto const timeoutContext = m_qsoCompletion;
      m_repeatDeadline.start (10);
      drain (30);
      m_logDlg->accept ();
      drain ();
      if (m_auto || !fieldsMatch (false) || m_ntx != 4
          || timeoutContext->phase != QsoCompletion::Phase::TimedOut)
        return mode + ": accepting after timeout destroyed the retry fields.";
      auto const beforeLateReply = accepted;
      receive73 (mode, "W1AW");
      drain (30);
      if (m_auto || !fieldsMatch (true) || m_ntx != 6
          || m_openLogQso || accepted != beforeLateReply)
        return mode + ": late confirmation after log acceptance did not complete exactly once.";

      if (!prepare (mode, "W1AW")) return mode + ": declined timeout setup failed.";
      scheduleLogQso ();
      drain ();
      m_logDlg->reject ();
      drain ();
      m_repeatDeadline.start (10);
      drain (30);
      receive73 (mode, "W1AW");
      drain ();
      if (!m_openLogQso || m_auto || !fieldsMatch (false))
        return mode + ": late confirmation failed to offer the unlogged contact once.";
      m_logDlg->reject ();
      drain ();
      receive73 (mode, "W1AW");
      drain ();
      if (m_openLogQso || m_auto)
        return mode + ": repeated late confirmation reopened a declined prompt.";
    }
  }

  if (logging) {
    prepare ("FT8", "W1AW");
    ui->cbAutoSeq->setVisible (true);
    ui->cbAutoSeq->setEnabled (true);
    ui->cbAutoSeq->setChecked (false);
    scheduleLogQso ();
    drain ();
    if (prompting) {
      if (!m_openLogQso) return "Ordinary FT8 logging without Auto Seq did not open its prompt.";
      m_logDlg->accept ();
      drain ();
    }
    if (m_auto == clearCall || !fieldsMatch (true))
      return "Ordinary FT8 log acceptance without Auto Seq changed the clear-and-stop policy.";

    prepare ("FT8", "W1AW");
    ui->cbAutoSeq->setVisible (true);
    ui->cbAutoSeq->setEnabled (true);
    ui->cbAutoSeq->setChecked (true);
    scheduleLogQso ();
    auto_tx_mode (true);
    stopWRTimer.start (10000);
    drain ();
    if (m_auto || stopWRTimer.isActive ())
      return "Queued ordinary logging did not stop the Auto Tx state armed by its outer plan.";
    if (prompting) {
      m_logDlg->accept ();
      drain ();
    }
  }

  drain ();
  collectNotifications ();
  if (loggedNotifications != accepted || adifNotifications != accepted)
    return QString {"QSO notifications were duplicated or lost: accepted=%1 QSO=%2 ADIF=%3"}
      .arg (accepted).arg (loggedNotifications).arg (adifNotifications);
  if (adifRecords () != initialRecords + accepted)
    return "The ADIF record count did not match accepted QSO requests.";
  return {};
}
