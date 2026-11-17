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
#include <QRegularExpression>
#include <QUdpSocket>
#include <algorithm>
#include <limits>

extern bool blocked;
extern bool programStart;

QString MainWindow::checkQsoCompletionTest (QString const& requestedProfile)
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
  auto waitUntil = [] (auto const& ready, int timeout = 5000) {
    if (ready ()) return true;
    QEventLoop loop;
    QTimer poll;
    QTimer watchdog;
    poll.setInterval (5);
    watchdog.setSingleShot (true);
    connect (&poll, &QTimer::timeout, &loop, [&] {if (ready ()) loop.quit ();});
    connect (&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start ();
    watchdog.start (timeout);
    loop.exec ();
    return ready ();
  };
  auto settleLogs = [&] {
    return waitUntil ([&] {
      return !logQSOTimer.isActive () && !m_processingLogRequests
        && std::none_of (m_pendingLogQsos.begin (), m_pendingLogQsos.end (),
          [] (QsoLogRequest const& request) {return request.stage == QsoLogRequest::Stage::Queued;})
        && (m_openLogQso ? !m_logDlg->isHidden () : m_pendingLogQsos.empty ());
    });
  };
  auto expireRepeat = [&] {
    QMetaObject::invokeMethod (&m_repeatDeadline, "timeout", Qt::DirectConnection);
  };

  if (!m_config.transceiver_online ()) return "Cannot open the QSO test's dummy radio.";
  auto radioReady = [&] {
    auto const state = m_operatingFrequency.snapshot ();
    return !programStart && m_rigState.online () && state.rx
      && !state.pendingRx && !state.pendingTx && !state.recoveryRequired
      && !m_modeEntries.active () && !m_pendingModeRetune.pending ()
      && !m_transmitting && !m_rigState.ptt () && !m_tune
      && !tuneButtonTimer.isActive () && !ptt0Timer.isActive () && !ptt1Timer.isActive ();
  };
  if (!waitUntil (radioReady, 10000))
    return "Startup radio and mode activity did not settle before the QSO test.";
  if (!requestNominalFrequencyChange (14074000, FrequencyRequestOrigin::User)
      || !waitUntil ([&] {
        return radioReady () && m_operatingFrequency.rx () == 14074000
          && nominalRxBand () == "20m"
          && (!m_monitoring || m_rigState.frequency () == m_operatingFrequency.snapshot ().correctedRx);
      })) return "The QSO test could not establish its initial 20m radio context.";

  QUdpSocket notifications;
  if (!notifications.bind (QHostAddress {QHostAddress::LocalHost}, 0))
    return "Unable to listen for local QSO notifications.";
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
  // Drain status traffic too, so it cannot crowd out the log notifications.
  connect (&notifications, &QUdpSocket::readyRead, &notifications, collectNotifications);
  m_messageClient->set_server ("127.0.0.1", {});
  m_messageClient->set_server_port (notifications.localPort ());
  if (!settleLogs ()) return "Queued QSO logging did not settle.";
  auto adifRecords = [&] {
    QFile file {m_logBook.path ()};
    return file.open (QIODevice::ReadOnly) ? file.readAll ().toUpper ().count ("<EOR>") : 0;
  };
  auto const initialRecords = adifRecords ();
  // Repeated contacts may already be worked; logging must append a new record.
  auto lastLoggedCall = [&] {
    QFile file {m_logBook.path ()};
    if (!file.open (QIODevice::ReadOnly)) return QString {};
    auto const text = QString::fromUtf8 (file.readAll ());
    QRegularExpression const field {"<call:(\\d+)>", QRegularExpression::CaseInsensitiveOption};
    QString call;
    for (auto it = field.globalMatch (text); it.hasNext ();)
      {
        auto const m = it.next ();
        call = text.mid (m.capturedEnd (), m.captured (1).toInt ()).trimmed ().toUpper ();
      }
    return call;
  };
  auto loggedAndWorked = [&] (QString const& call, int recordsBefore) {
    return adifRecords () == recordsBefore + 1 && lastLoggedCall () == call
      && m_logBook.call_worked_on_band (call, {}, {});
  };
  bool const logging = m_config.autoLog () || m_config.prompt_to_log ();
  bool const prompting = m_config.prompt_to_log ();
  bool const clearCall = m_config.clear_DXcall ();
  bool const clearGrid = m_config.clear_DXgrid ();
  if (m_config.autoLog () && prompting)
    return "The QSO fixture enabled both automatic and prompted logging.";
  QString loadedProfile = prompting ? "prompt" : logging ? "automatic" : "disabled";
  if (!clearCall && !clearGrid) loadedProfile += "-keep";
  else if (!clearCall) loadedProfile += "-keep-call";
  else if (!clearGrid) loadedProfile += "-keep-grid";
  if (loadedProfile != requestedProfile)
    return QString {"Requested QSO profile %1, loaded %2."}.arg (requestedProfile, loadedProfile);
  auto fieldsMatch = [&] (bool logged) {
    return m_hisCall == (logged && clearCall ? QString {} : QString {"W1AW"})
      && ui->dxGridEntry->text () == (logged && clearGrid ? QString {} : QString {"FN31"});
  };
  auto describe = [&] (std::shared_ptr<QsoCompletion> const& context) {
    return QString {" auto=%1 call=%2 ntx=%3 message=[%4] tx4=[%5] band=%6/%7 "
                    "period=%8/%9 phase=%10 clear=%11 open=%12 automatic=%13 log=%14 current=%15"}
      .arg (m_auto).arg (m_hisCall).arg (m_ntx).arg (m_currentMessage)
      .arg (ui->tx4->text ()).arg (nominalRxBand ()).arg (context ? context->band : "none")
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
  QString logFailure;
  auto logFirst = [&] {
    auto const context = m_qsoCompletion;
    logFailure.clear ();
    if (!context) {
      logFailure = " No QSO context was prepared.";
      return false;
    }
    if (!logging) return true;
    scheduleLogQso ();
    if (!settleLogs ()) return false;
    if (context != m_qsoCompletion || !repeatContextIsCurrent (context)) {
      logFailure = " Logging replaced or invalidated the prepared QSO." + describe (context);
      return false;
    }
    if (prompting) {
      if (!m_openLogQso) return false;
      m_logDlg->accept ();
      if (!settleLogs ()) return false;
    }
    if (context != m_qsoCompletion || !repeatContextIsCurrent (context)) {
      logFailure = " Accepting the log replaced or invalidated the prepared QSO." + describe (context);
      return false;
    }
    return true;
  };
  auto receive73 = [&] (QString const& mode, QString const& call) {
    DecodedText message {QString {"060500 -10  0.3 1500 %1  K1ABC %2 73"}
      .arg (mode == "MSK144" ? "&" : ":").arg (call)};
    auto_sequence (message, 25, 50);
  };

  for (auto const& mode : {QString {"MSK144"}, QString {"Q65"}}) {
    for (bool autoSequence : {false, true}) {
      if (!prepare (mode, "W1AW")) return mode + ": RR73 did not start a repeat.";
      ui->cbAutoSeq->setVisible (true);
      ui->cbAutoSeq->setEnabled (true);
      ui->cbAutoSeq->setChecked (autoSequence);
      auto const context = m_qsoCompletion;
      auto const deadline = m_repeatDeadline.timerId ();
      auto const interval = m_repeatDeadline.interval ();
      auto const before = accepted;
      if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
      if (!m_auto || m_hisCall != "W1AW" || ui->dxGridEntry->text () != "FN31"
          || context != m_qsoCompletion || context->phase != QsoCompletion::Phase::AwaitingFinal73
          || context->logState != (logging ? QsoCompletion::LogState::Accepted : QsoCompletion::LogState::None)
          || context->clearDxPending != (logging && clearCall)
          || context->clearGridPending != (logging && clearGrid)
          || m_ntx != 4 || !ui->txrb4->isChecked () || !m_repeatDeadline.isActive ()
          || m_repeatDeadline.timerId () != deadline || m_repeatDeadline.interval () != interval)
        return mode + ": first RR73 logging changed the active repeat." + describe (context);
      if (accepted != before + (logging ? 1 : 0))
        return mode + ": first RR73 was not logged exactly once.";
      if (!updateRepeatTransmission (m_currentMessage.split (' ', SkipEmptyParts))
          || context != m_qsoCompletion || m_repeatDeadline.timerId () != deadline)
        return mode + ": repeated RR73 extended or replaced the deadline.";
      m_sentFirst73 = false;
      receive73 (mode, "W1AW");
      receive73 (mode, "W1AW");
      if (mode == "MSK144") {
        if (context->phase != QsoCompletion::Phase::FinalReplyPending || !m_auto)
          return mode + ": confirmation did not defer the final transmit stop.";
        DecodedMessageReaction::QsoReactionEffect finish;
        finish.kind = DecodedMessageReaction::QsoReactionEffect::Kind::ScheduleRepeatFinish;
        finish.intValue = 0;
        DecodedText finalReply {"060500 -10  0.3 1500 &  K1ABC W1AW 73"};
        applyQsoReactionEffect (finish, finalReply, nullptr);
        if (!waitUntil ([&] {return !m_auto;}))
          return mode + ": the queued final-reply callback did not finish the repeat.";
      }
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
    if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
    auto const stoppedContext = m_qsoCompletion;
    on_stopTxButton_clicked ();
    if (m_auto || !fieldsMatch (logging))
      return mode + ": explicit Stop did not end the wait and clear a logged contact."
        + describe (stoppedContext);

    if (!prepare (mode, "W1AW")) return mode + ": disabled-Enable-Tx setup failed.";
    if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
    auto const disabledContext = m_qsoCompletion;
    on_autoButton_clicked (false);
    if (m_auto || !fieldsMatch (logging))
      return mode + ": disabling Enable Tx did not release the deferred clear."
        + describe (disabledContext);

    if (!prepare (mode, "W1AW")) return mode + ": fault-stop setup failed.";
    if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
    auto const faultContext = m_qsoCompletion;
    requestTxStop (TxEvidence::TxStopReason::Error);
    if (m_auto || m_hisCall != "W1AW" || faultContext->clearDxPending
        || m_repeatDeadline.isActive ())
      return mode + ": a fault stop applied a deferred clear or left a deadline active.";

    if (!prepare (mode, "W1AW")) return mode + ": delayed-auto-off setup failed.";
    if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
    auto const flagContext = m_qsoCompletion;
    DecodedMessageReaction::QsoReactionEffect autoOff;
    autoOff.kind = DecodedMessageReaction::QsoReactionEffect::Kind::ScheduleAutoFlagOff;
    DecodedText signoff {"060500 -10  0.3 1500 &  K1ABC W1AW 73"};
    applyQsoReactionEffect (autoOff, signoff, nullptr);
    if (!waitUntil ([&] {return !m_auto;}))
      return mode + ": delayed Auto Tx shutdown did not run.";
    if (m_auto || m_hisCall != "W1AW" || flagContext->clearDxPending
        || m_repeatDeadline.isActive ())
      return mode + ": delayed Auto Tx shutdown did not cancel the repeat cleanly.";

    if (!prepare (mode, "W1AW")) return mode + ": Tune setup failed.";
    if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
    auto const tuneContext = m_qsoCompletion;
    if (!waitUntil ([] {return !blocked;})) return mode + ": logged-contact UI block did not end.";
    if (tuneContext != m_qsoCompletion || !repeatContextIsCurrent (tuneContext))
      return mode + ": waiting for the UI block changed the Tune setup contact." + describe (tuneContext);
    on_tuneButton_clicked (true);
    end_tuning ();
    if (!waitUntil ([&] {return !m_tune && !tuneButtonTimer.isActive ();}))
      return mode + ": Tune did not finish shutting down.";
    if (m_hisCall != "W1AW" || tuneContext->clearDxPending
        || m_repeatDeadline.isActive ())
      return mode + ": tuning retained a repeat deadline or cleared its contact.";

    if (!prepare (mode, "W1AW")) return mode + ": timeout setup failed.";
    if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
    auto const timeoutContext = m_qsoCompletion;
    if (m_repeatDeadline.interval () != 300000)
      return mode + ": the repeat budget is not twenty TR periods.";
    expireRepeat ();
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
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (accepted != loggedBeforeRetry || m_openLogQso)
      return mode + ": retry requested another log for the same contact.";
    expireRepeat ();
    receive73 (mode, "W1AW");
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (m_auto || !fieldsMatch (logging) || m_ntx != 6 || accepted != loggedBeforeRetry)
      return mode + ": late confirmation failed to complete the timed-out contact exactly once.";

    if (!prepare (mode, "W1AW")) return mode + ": Stop-after-timeout setup failed.";
    if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
    expireRepeat ();
    on_stopTxButton_clicked ();
    if (m_auto || !fieldsMatch (logging))
      return mode + ": explicit Stop after timeout did not release the deferred clear.";

    if (logging) {
      if (!prepare (mode, "W1AW")) return mode + ": manual log after timeout setup failed.";
      auto const manualTimeout = m_qsoCompletion;
      expireRepeat ();
      auto const before = accepted;
      scheduleLogQso (false);
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
      if (prompting) {
        if (!m_openLogQso) return mode + ": manual log after timeout did not open its prompt.";
        m_logDlg->accept ();
        if (!settleLogs ()) return "Queued QSO logging did not settle.";
      }
      if (m_auto || manualTimeout->phase != QsoCompletion::Phase::TimedOut
          || !fieldsMatch (false) || m_ntx != 4 || accepted != before + 1)
        return mode + ": manual logging canceled or cleared the timed-out contact.";
      receive73 (mode, "W1AW");
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
      if (m_auto || !fieldsMatch (true) || m_ntx != 6 || accepted != before + 1)
        return mode + ": late confirmation after manual logging failed to complete exactly once.";
    }

    if (!prepare (mode, "W1AW")) return mode + ": changed-message setup failed.";
    if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
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
      if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
      auto const stale = m_qsoCompletion;
      auto const previousFrequency = m_operatingFrequency.rx ();
      if (change == 0) ui->dxCallEntry->setText ("W2XYZ");
      if (change == 1) {
        auto const changedFrequency = nominalRxBand () == "2m" ? Frequency {14074000} : Frequency {144174000};
        if (!m_operatingFrequency.requestNominal (changedFrequency,
              [] (RigFrequency::Request const&) {return true;}, false)
            || nominalRxBand () == stale->band)
          return mode + ": stale-timeout setup did not change the QSO band." + describe (stale);
      }
      if (change == 2) m_TRperiod = 30.0;
      if (change == 3) m_mode = "FT8";
      expireRepeat ();
      if (!m_auto || m_hisCall != (change == 0 ? "W2XYZ" : "W1AW")
          || stale->clearDxPending)
        return mode + QString {": a stale deadline affected a changed QSO context (%1)."}.arg (change)
          + describe (stale);
      if (change == 1
          && !m_operatingFrequency.requestNominal (previousFrequency,
            [] (RigFrequency::Request const&) {return true;}, false))
        return mode + ": stale-timeout cleanup did not restore the preceding frequency.";
    }
  }

  auto_tx_mode (false);
  if (!waitUntil ([&] {
        auto const state = m_operatingFrequency.snapshot ();
        return !state.pendingRx && !state.pendingTx && !m_transmitting && !m_rigState.ptt ();
      })) return "The preceding radio activity did not settle before the external-band repeat check.";
  if (!m_operatingFrequency.requestNominal (14074000,
        [] (RigFrequency::Request const&) {return true;}, false))
    return "External-band repeat setup could not establish its initial RX frequency.";
  synchronizeOperatingBandState ();
  if (!prepare ("Q65", "W1AW")) return "External-band repeat setup failed.";
  auto const externalBandContext = m_qsoCompletion;
  if (!m_repeatDeadline.isActive ()) return "External-band repeat deadline was not armed.";
  Q_EMIT m_config.frequency_observation (
    {m_config.frequency_generation (), 0, RigFrequency::Path::Rx,
     7074000, RigFrequency::Basis::Readback, {}});
  if (m_operatingFrequency.rx () != 7074000 || m_qsoCompletion
      || m_repeatDeadline.isActive () || externalBandContext->waiting ())
    return QString {"An external RX-band change did not cancel the previous band's repeat: "
                    "rx=%1 context=%2 deadline=%3 waiting=%4 pending=%5/%6 online=%7."}
      .arg (m_operatingFrequency.rx ()).arg (bool (m_qsoCompletion))
      .arg (m_repeatDeadline.isActive ()).arg (externalBandContext->waiting ())
      .arg (m_operatingFrequency.snapshot ().pendingRx).arg (m_operatingFrequency.snapshot ().pendingTx)
      .arg (m_config.is_transceiver_online ());
  auto_tx_mode (false);
  if (!requestNominalFrequencyChange (14074000, FrequencyRequestOrigin::User)
      || !waitUntil ([&] {
           auto const state = m_operatingFrequency.snapshot ();
           return !state.pendingRx && !state.pendingTx
             && (!m_monitoring || m_rigState.frequency () == state.correctedRx);
         })) return "The external-band repeat check did not restore its radio context.";

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
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (prompting) {
      if (!m_openLogQso) return "Q65 RRR confirmation failed to offer logging.";
      m_logDlg->accept ();
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
    }
    if (!fieldsMatch (logging) || accepted != before + (logging ? 1 : 0))
      return "Q65 RRR confirmation lost the log or applied the wrong clear preference.";
    receive73 ("Q65", "W1AW");
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (m_auto || m_openLogQso || accepted != before + (logging ? 1 : 0))
      return "Repeated Q65 RRR confirmation restarted transmission or logging.";
  }

  if (prompting) {
    prepare ("FT8", "W1AW");
    ui->cbAutoSeq->setVisible (true);
    ui->cbAutoSeq->setEnabled (true);
    ui->cbAutoSeq->setChecked (true);
    scheduleLogQso ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
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
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (m_auto || !m_inQSOwith.isEmpty () || stopWRTimer.isActive () || stopWCTimer.isActive ()
        || !m_openLogQso || m_openLogQso->context != firstPrompt)
      return "An open log prompt delayed the next contact's completion effects.";

    prepare ("FT8", "W3NEW");
    m_inQSOwith = "W3NEW";
    stopWRTimer.start (std::numeric_limits<int>::max ());
    stopWCTimer.start (std::numeric_limits<int>::max ());
    m_logDlg->accept ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (!m_openLogQso || m_openLogQso->context != queuedContact
        || m_openLogQso->data.call != "W2XYZ" || !m_auto || m_inQSOwith != "W3NEW"
        || !stopWRTimer.isActive () || !stopWCTimer.isActive ())
      return "Displaying a queued log replayed completion effects against a new contact.";
    m_logDlg->accept ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
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
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
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
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
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
    auto const recordsBeforeEdited = adifRecords ();
    m_logDlg->accept ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (!m_auto || m_hisCall != "W2XYZ" || newer->clearDxPending != clearCall
        || acceptedCalls.last () != "W2XYA" || !loggedAndWorked ("W2XYA", recordsBeforeEdited))
      return "Editing the logged callsign lost the repeat's ownership.";
    on_stopTxButton_clicked ();
    if (m_hisCall != (clearCall ? QString {} : QString {"W2XYZ"}))
      return "Stop did not honor the clear preference for the edited log's original contact.";

    if (!prepare ("MSK144", "W1AW")) return "Delayed-accept setup failed.";
    scheduleLogQso ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    on_stopTxButton_clicked ();
    m_logDlg->accept ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (!fieldsMatch (true)) return "Accepting after Stop did not honor the clear preferences.";

    if (!prepare ("MSK144", "W1AW")) return "Changed-selection logging setup failed.";
    auto const changedSelection = m_qsoCompletion;
    scheduleLogQso ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (changedSelection != m_qsoCompletion || !repeatContextIsCurrent (changedSelection))
      return "Logging changed the selection-test contact." + describe (changedSelection);
    ui->txrb6->setChecked (true);
    m_ntx = 6;
    ui->tx6->setText ("CQ K1ABC FN42");
    ui->cbAutoSeq->setChecked (true);
    m_sentFirst73 = false;
    receive73 ("MSK144", "W1AW");
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (!m_auto) return "An old final reply stopped a newly selected message.";
    m_dateTimeQSOOn = QDateTime::currentDateTimeUtc ().addSecs (-17);
    auto const selectedStart = m_dateTimeQSOOn;
    m_xSent = "next sent";
    m_xRcvd = "next received";
    m_logDlg->accept ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (!m_auto || m_hisCall != "W1AW" || m_hisGrid != "FN31"
        || m_dateTimeQSOOn != selectedStart || m_xSent != "next sent"
        || m_xRcvd != "next received" || changedSelection->waiting ()
        || changedSelection->clearDxPending)
      return "Accepting a log cleared new activity selected before its first transmission.";
    receive73 ("MSK144", "W1AW");
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (!m_auto || m_openLogQso || !m_pendingLogQsos.empty ())
      return "A late final reply resumed a canceled repeat after accepting its log.";

    if (!prepare ("MSK144", "W1AW")) return "Accepted-log rejection setup failed.";
    if (!logFirst ()) return "First RR73 logging did not settle." + logFailure;
    auto const previouslyAccepted = m_qsoCompletion;
    scheduleLogQso (false);
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (!m_openLogQso) return "Manual logging after an accepted QSO was unavailable.";
    m_logDlg->reject ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (previouslyAccepted->logState != QsoCompletion::LogState::Accepted)
      return "Rejecting a later manual log lost the QSO's accepted status.";

    if (!prepare ("Q65", "W3OLD")) return "Earlier Q65 logging setup failed.";
    scheduleLogQso ();
    if (!prepare ("Q65", "W4NEW")) return "Later Q65 logging setup failed.";
    auto const nextQsoStart = m_dateTimeQSOOn;
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    auto const recordsBeforeW3OLD = adifRecords ();
    m_logDlg->accept ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (!loggedAndWorked ("W3OLD", recordsBeforeW3OLD) || m_hisCall != "W4NEW"
        || m_dateTimeQSOOn != nextQsoStart || !m_auto)
      return "An older same-mode Q65 log lost its worked status or changed the new contact.";

    if (!prepare ("Q65", "W5OLD")) return "Earlier Q65 activity setup failed.";
    scheduleLogQso ();
    if (!prepare ("MSK144", "W6NEW")) return "Changed-mode logging setup failed.";
    auto const scoreBeforeAccept = m_score;
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    auto const recordsBeforeW5OLD = adifRecords ();
    m_logDlg->accept ();
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (!loggedAndWorked ("W5OLD", recordsBeforeW5OLD) || m_score != scoreBeforeAccept
        || m_hisCall != "W6NEW" || !m_auto)
      return "An older Q65 log lost its worked status or changed the new activity.";

    for (auto const& mode : {QString {"MSK144"}, QString {"Q65"}}) {
      for (bool leavePromptOpen : {false, true}) {
        for (bool acceptConfirmed : {false, true}) {
          if (!prepare (mode, "W1AW")) return mode + ": confirmation prompt setup failed.";
          auto const context = m_qsoCompletion;
          ui->cbAutoSeq->setChecked (true);
          auto const start = m_dateTimeQSOOn;
          auto const before = accepted;
          scheduleLogQso ();
          if (!settleLogs ()) return "Queued QSO logging did not settle.";
          if (context != m_qsoCompletion || !repeatContextIsCurrent (context))
            return mode + ": opening the prompt changed its contact." + describe (context);
          if (!m_openLogQso) return mode + ": first RR73 prompt did not open.";
          dialogReport->setText ("-08");
          if (!leavePromptOpen) {
            m_logDlg->reject ();
            if (!settleLogs ()) return "Queued QSO logging did not settle.";
            if (context->logState != QsoCompletion::LogState::None || !m_auto)
              return mode + ": rejecting before confirmation did not preserve the repeat.";
          }
          receive73 (mode, "W1AW");
          auto const confirmation = context->confirmedAt;
          receive73 (mode, "W1AW");
          if (mode == "MSK144") {
            if (context->phase != QsoCompletion::Phase::FinalReplyPending)
              return mode + ": confirmation did not enter its pending phase.";
            expireRepeat ();
          }
          if (m_auto || m_ntx != 6 || !fieldsMatch (false))
            return mode + ": completion depended on accepting the log prompt.";
          if (!settleLogs ()) return "The confirmed QSO log prompt did not settle.";
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
          if (!settleLogs ()) return "Queued QSO logging did not settle.";
          if (accepted != before + int (acceptConfirmed) || !fieldsMatch (acceptConfirmed))
            return mode + ": accepting or declining the confirmed log applied the wrong effects.";
          if (!acceptConfirmed && context->logState != QsoCompletion::LogState::Declined)
            return mode + ": declining a confirmed contact was not final.";
          receive73 (mode, "W1AW");
          if (!settleLogs ()) return "Queued QSO logging did not settle.";
          if (m_auto || m_openLogQso || !m_pendingLogQsos.empty ()
              || accepted != before + int (acceptConfirmed))
            return mode + ": another 73 reopened a completed contact's log prompt.";
          if (!acceptConfirmed) {
            scheduleLogQso (false);
            if (!settleLogs ()) return "Queued QSO logging did not settle.";
            if (!m_openLogQso) return mode + ": declining automatic logging disabled manual logging.";
            m_logDlg->reject ();
            if (!settleLogs ()) return "Queued QSO logging did not settle.";
          }
        }
      }

      if (!prepare (mode, "W1AW")) return mode + ": delayed timeout acceptance setup failed.";
      auto const timeoutContext = m_qsoCompletion;
      scheduleLogQso ();
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
      if (timeoutContext != m_qsoCompletion || !repeatContextIsCurrent (timeoutContext))
        return mode + ": opening the timeout prompt changed its contact." + describe (timeoutContext);
      expireRepeat ();
      m_logDlg->accept ();
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
      if (m_auto || !fieldsMatch (false) || m_ntx != 4
          || timeoutContext->phase != QsoCompletion::Phase::TimedOut)
        return mode + ": accepting after timeout destroyed the retry fields.";
      auto const beforeLateReply = accepted;
      receive73 (mode, "W1AW");
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
      if (m_auto || !fieldsMatch (true) || m_ntx != 6
          || m_openLogQso || accepted != beforeLateReply)
        return mode + ": late confirmation after log acceptance did not complete exactly once.";

      if (!prepare (mode, "W1AW")) return mode + ": declined timeout setup failed.";
      scheduleLogQso ();
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
      m_logDlg->reject ();
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
      expireRepeat ();
      receive73 (mode, "W1AW");
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
      if (!m_openLogQso || m_auto || !fieldsMatch (false))
        return mode + ": late confirmation failed to offer the unlogged contact once.";
      m_logDlg->reject ();
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
      receive73 (mode, "W1AW");
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
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
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (prompting) {
      if (!m_openLogQso) return "Ordinary FT8 logging without Auto Seq did not open its prompt.";
      m_logDlg->accept ();
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
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
    if (!settleLogs ()) return "Queued QSO logging did not settle.";
    if (m_auto || stopWRTimer.isActive ())
      return "Queued ordinary logging did not stop the Auto Tx state armed by its outer plan.";
    if (prompting) {
      m_logDlg->accept ();
      if (!settleLogs ()) return "Queued QSO logging did not settle.";
    }
  }

  if (!settleLogs ()) return "Queued QSO logging did not settle.";
  if (!waitUntil ([&] {
        collectNotifications ();
        return loggedNotifications >= accepted && adifNotifications >= accepted;
      }) || loggedNotifications != accepted || adifNotifications != accepted)
    return QString {"QSO notifications were duplicated or lost: accepted=%1 QSO=%2 ADIF=%3"}
      .arg (accepted).arg (loggedNotifications).arg (adifNotifications);
  if (adifRecords () != initialRecords + accepted)
    return "The ADIF record count did not match accepted QSO requests.";
  return {};
}
