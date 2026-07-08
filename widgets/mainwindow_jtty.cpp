#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "commons.h"
#include "JttyMessages.hpp"
#include "Logger.hpp"
#include <QByteArray>
#include <QDateTime>
#include "Modulator/Modulator.hpp"
#include <iostream>
#include <vector>

#ifdef WIN32
#include "MMTTYIF.hpp"
#include "MMTTY_Messages.hpp"
#undef MessageBox
#endif


extern dec_data_t& dec_data;
extern qint32 g_iptt;

#if QT_VERSION >= QT_VERSION_CHECK (5, 13, 0)
#define SkipEmptyParts Qt::SkipEmptyParts
#else
#define SkipEmptyParts QString::SkipEmptyParts
#endif

#define FCL fortran_charlen_t

extern "C" {
  void rjtty_sub_(short int d2[], int* k, int* nsps, float* f0, float* ftol);

void jtty_get_msgs_(float* f0, float* ftol, bool* all_new, bool* qso_new,
    char all_freqs[], char line[], fortran_charlen_t, fortran_charlen_t);

  void genjtty_(char const * msg, int itone[], int* nsym, fortran_charlen_t);

  void gen_jttywave_(int itone[], int* nsym, int* nsps, float* bt, float* fsample, float* f0,
                    float xjunk[], float wave[], int* icmplx, int* nwave);
}

void MainWindow::jtty_save_wav()
{
  //Save JTTY data to a .wav file
  QDateTime now {QDateTime::currentDateTimeUtc ()};
  qint64 ms = m_k0/12;
  auto const& tstart=now.addMSecs(-ms);
  m_fnameWE=m_config.save_directory().absoluteFilePath (tstart.toString("yyMMdd_hhmmss"));
  int samples=m_k0;
  QString dgrd = "jtty";
  save_wave_file (m_fnameWE, samples, m_freqNominalPeriod, dgrd);
}

void MainWindow::jtty_decode(int k)
{
  auto boundedLatin1 = [] (char const *data, int size) {
    QByteArray bytes {QByteArray::fromRawData(data, size)};
    int const nul = bytes.indexOf('\0');
    if (nul >= 0) bytes.truncate(nul);
    return QString::fromLatin1(bytes.constData(), bytes.size());
  };
  int nsps=384;
  char qso_freq[800];
  char all_freqs[2400];
  float f0 = ui->RxFreqSpinBox_2->value();
  float ftol = ui->sbFtol_2->value();
  bool all_new = true;
  bool qso_new = true;

  rjtty_sub_(dec_data.d2,&k,&nsps,&f0,&ftol);

  jtty_get_msgs_(&f0, &ftol, &all_new, &qso_new, &all_freqs[0],
                 &qso_freq[0], (FCL)2400, (FCL)800);

  QString allMsgs {boundedLatin1(all_freqs, sizeof all_freqs)};
  if(ui->cbLowerCase->isChecked()) allMsgs = allMsgs.toLower();
  if(all_new) {
      ui->decodedTextBrowser->clear();
      if(allMsgs.left(1) == " ") {
          ui->decodedTextBrowser->insertText(" " + allMsgs.trimmed());
      } else {
          ui->decodedTextBrowser->insertText(allMsgs.trimmed());
      }
#ifdef WIN32
      if (m_mmttyif) {
          m_mmttyif->echo_message_to_n1mm(allMsgs);
      }
#endif
  }
  if(qso_new) {
      QString message2 {boundedLatin1(qso_freq, sizeof qso_freq)};
      if(ui->cbLowerCase->isChecked()) message2 = message2.toLower();
      int n2=message2.length();
      if(n2 > 0) {
        ui->decodedTextBrowser2->clear();
        ui->decodedTextBrowser2->insertText(message2.trimmed());
#ifdef WIN32
        if (m_mmttyif) {
            m_mmttyif->echo_message_to_n1mm(message2);
        }
#endif
      }
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

void MainWindow::execute_jtty_tx(qint64 requestId, QString message)
{
  int itone[848];
  if(ui->cbLowerCase->isChecked()) message = message.toLower();

  auto const preparedMessage = Jtty::prepareTransmitText(message);
  if (preparedMessage.changed()) {
    LOG_WARN("JTTY transmit message was normalized or shortened before encoding");
  }
  message = preparedMessage.text;
  if (message.isEmpty()) {
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::Empty);
    return;
  }

  int n=message.length();
  QString t = " ";
  t = message + t.repeated(80-n);
  genjtty_(t.toLatin1().constData(), &itone[0], &m_nsym_jtty, (FCL)80);
  if (m_nsym_jtty <= 0) {
    LOG_WARN("JTTY transmit message could not be encoded");
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }

  int nsps4=4*384;
  float bt=2.0;
  float fsample=48000.0;
  float f0=ui->TxFreqSpinBox_2->value ();
  int icmplx=0;
  int nwave=nsps4*m_nsym_jtty;

  bool const newSession = !m_jttyTxActive;
  if (newSession) {
    ++m_jttyTxSessionId;
    m_jttyQueuedSamples = 0;
    m_jttyTxUsesTciAudio = m_tci_audio;
  }
  bool const useTciAudio = m_jttyTxUsesTciAudio;

  std::vector<float> wave(nwave > 0 ? nwave : 1);
  gen_jttywave_(const_cast<int *>(itone), &m_nsym_jtty, &nsps4, &bt, &fsample, &f0,
                wave.data(), wave.data(), &icmplx, &nwave);

  QVector<qint16> samples;
  samples.reserve(nwave);
  for(int i=0; i<nwave; ++i) {
    float v = wave[i] * 32767.0f;
    if(v >  32767.0f) v =  32767.0f;
    if(v < -32768.0f) v = -32768.0f;
    samples.append(static_cast<qint16>(qRound(v)));
  }
  if (samples.isEmpty()) {
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }

  if (newSession) {
    // A fresh JTTY session starts a new FIFO accounting baseline even after a
    // natural drain, so drain totals stay comparable to m_jttyQueuedSamples.
    if (useTciAudio) {
      Q_EMIT m_config.transceiver_clear_jtty_pcm(m_jttyTxSessionId);
    } else {
      m_jttyTxBuffer->clear(m_jttyTxSessionId);
    }
  }

  bool enqueued {false};
  if(useTciAudio) {
    // TCI enqueue is asynchronous. MainWindow can reject a message that can
    // never fit; backend occupancy failures reject only the submitted enqueue.
    if (jttyPcmEnqueueFits (JTTY_PCM_FIFO_DEFAULT_CAPACITY, 0, samples.size ())) {
      QByteArray bytes(reinterpret_cast<char const *> (samples.constData ()),
                       samples.size () * int (sizeof (qint16)));
      qint64 const enqueueId = ++m_jttyTciEnqueueId;
      m_pendingJttyTciMessages.append(PendingJttyTciMessage {
        m_jttyTxSessionId,
        enqueueId,
        requestId,
        samples.size (),
        message,
        newSession
      });
      m_jttyTxActive = true;
      Q_EMIT m_config.transceiver_enqueue_jtty_pcm(bytes, m_jttyTxSessionId, enqueueId);
      return;
    } else {
      LOG_WARN("JTTY TCI transmit FIFO capacity precheck failed; rejecting PCM enqueue");
      Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::QueueFull);
    }
  } else {
    enqueued = m_jttyTxBuffer->enqueueMessage(samples, m_jttyTxSessionId);
  }

  if (!enqueued) {
    if (!useTciAudio) {
      Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::QueueFull);
    }
    if (newSession) {
      ++m_jttyTxSessionId;
      m_jttyQueuedSamples = 0;
    }
    return;
  }

  completeJttyTxEnqueue(requestId, message, samples.size (), newSession, useTciAudio);
}

void MainWindow::completeJttyTxEnqueue(qint64 requestId, QString const& message, qint64 sampleCount, bool newSession, bool useTciAudio)
{
  m_currentMessage = message;
  qint64 const endSample = m_jttyQueuedSamples + sampleCount;
  recordAcceptedJttyTextRequest(requestId, endSample);
  m_jttyQueuedSamples = endSample;
  m_jttyTxActive = true;
  m_transmitting = true;
  Q_EMIT jttyTextAccepted(requestId);

  ui->decodedTextBrowser2->insertText(" ");
  QTextCursor cursor = ui->decodedTextBrowser2->textCursor();
  QTextCharFormat format = cursor.charFormat();
  format.setBackground(QBrush(QColor(Qt::yellow)));
  cursor.setCharFormat(format);
  cursor.insertText(message);
  format.setBackground(QBrush(QColor(Qt::white)));
  cursor.setCharFormat(format);

  handleJttyContestSerial(message);

  // Fault-detector watchdog: generous margin over all audio still to play (the
  // whole queued session, not just this message). The happy path completes via
  // the backend drain signal well before this fires.
  int pendingMs = useTciAudio
      ? int(m_jttyQueuedSamples / 48)
      : int((m_jttyTxBuffer->totalReal() - m_jttyTxBuffer->servedReal()) / 48);
  startJttyTxWatchdog(pendingMs + 1000 * m_config.txDelay() + 10000);

  monitor(false);
  if(!m_diskData && m_saveAll && (m_k0 > 53*384) && (m_k0 < 9999999)) {
    jtty_save_wav();
  }

#ifdef WIN32
  if (m_mmttyif) {
    m_mmttyif->report_ptt_state(true);
  }
#endif

#ifdef WIN32
  if (m_mmttyif) {
    m_mmttyif->echo_message_to_n1mm(message);
  }
#endif

  // Only a new session starts transmit; a message appended to an already-active
  // session chains gaplessly (soundcard) via the enqueue above. When PTT is not
  // yet up, guiUpdate keys it and ptt1Timer -> startTx2 -> transmit starts the
  // stream with the normal lead.
  if (newSession && g_iptt == 1 && !m_modulator->isActive()) {
    startTx2();
  }
}

void MainWindow::recordAcceptedJttyTextRequest(qint64 requestId, qint64 endSample)
{
  m_acceptedJttyTxRequests.append(AcceptedJttyTxRequest {
    m_jttyTxSessionId,
    requestId,
    endSample
  });
}

QVector<qint64> MainWindow::takeCompletedJttyTextRequests(qint64 sessionId, qint64 totalAtDrain)
{
  // Backends report only final drain, so per-text completion is observed when
  // the accepted text's containing JTTY session has drained.
  QVector<qint64> completedRequestIds;
  for (int i = 0; i < m_acceptedJttyTxRequests.size ();) {
    auto const accepted = m_acceptedJttyTxRequests.at (i);
    if (accepted.sessionId == sessionId && accepted.endSample <= totalAtDrain) {
      completedRequestIds.append(accepted.requestId);
      m_acceptedJttyTxRequests.remove (i);
    } else {
      ++i;
    }
  }
  return completedRequestIds;
}

void MainWindow::clearAcceptedJttyTextRequests(qint64 sessionId)
{
  for (int i = 0; i < m_acceptedJttyTxRequests.size ();) {
    if (m_acceptedJttyTxRequests.at (i).sessionId == sessionId) {
      m_acceptedJttyTxRequests.remove (i);
    } else {
      ++i;
    }
  }
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
   interruptJttyTx();

#ifdef WIN32
   if (m_mmttyif) {
       m_mmttyif->report_ptt_state(false);
   }
#endif

   stopTx();
}

void MainWindow::interruptJttyTx()
{
  if (m_mode != "JTTY" || !m_jttyTxActive) {
    return;
  }

  qint64 const interruptedSessionId = m_jttyTxSessionId;
  ++m_jttyTxSessionId;
  clearAcceptedJttyTextRequests(interruptedSessionId);
  rejectPendingJttyTciMessages(JttyTxRejectReason::Aborted);
  m_pendingJttyTciMessages.clear();
  if (m_jttyTxUsesTciAudio) {
    Q_EMIT m_config.transceiver_clear_jtty_pcm(m_jttyTxSessionId);
  } else {
    m_jttyTxBuffer->clear(m_jttyTxSessionId);
  }
  resetJttyTxState();
}

void MainWindow::onJttyBackendDrained(qint64 sessionId, qint64 totalAtDrain)
{
  if (m_mode != "JTTY" || !m_jttyTxActive) {
    return;
  }

  if (sessionId != m_jttyTxSessionId || totalAtDrain != m_jttyQueuedSamples) {
    return;
  }

  auto const completedRequestIds = takeCompletedJttyTextRequests(sessionId, totalAtDrain);
  resetJttyTxState();
  stopTx();
  for (auto const requestId : completedRequestIds) {
    Q_EMIT jttyTextCompleted(requestId);
  }
  Q_EMIT jttySessionDrained(sessionId);
}

void MainWindow::onJttyBackendEnqueueAccepted(qint64 sessionId, qint64 enqueueId, qint64 sampleCount)
{
  if (m_mode != "JTTY" || !m_jttyTxActive || sessionId != m_jttyTxSessionId) {
    return;
  }

  for (int i = 0; i < m_pendingJttyTciMessages.size (); ++i) {
    auto const pending = m_pendingJttyTciMessages.at (i);
    if (pending.sessionId != sessionId || pending.enqueueId != enqueueId) {
      continue;
    }

    m_pendingJttyTciMessages.remove (i);
    if (sampleCount != pending.sampleCount) {
      LOG_WARN("JTTY transmit backend accepted unexpected PCM sample count");
    }
    bool const startsSession = pending.newSession || m_jttyQueuedSamples <= 0;
    completeJttyTxEnqueue(pending.requestId, pending.message, sampleCount, startsSession, true);
    return;
  }
}

void MainWindow::onJttyBackendEnqueueFailed(qint64 sessionId, qint64 enqueueId)
{
  if (m_mode != "JTTY" || !m_jttyTxActive || sessionId != m_jttyTxSessionId) {
    return;
  }

  LOG_WARN("JTTY transmit backend rejected PCM enqueue");
  for (int i = 0; i < m_pendingJttyTciMessages.size (); ++i) {
    auto const pending = m_pendingJttyTciMessages.at (i);
    if (pending.sessionId != sessionId || pending.enqueueId != enqueueId) {
      continue;
    }
    m_pendingJttyTciMessages.remove (i);
    Q_EMIT jttyTextRejected(pending.requestId, JttyTxRejectReason::QueueFull);
    if (pending.newSession && m_jttyQueuedSamples <= 0) {
      for (int j = 0; j < m_pendingJttyTciMessages.size (); ++j) {
        if (m_pendingJttyTciMessages[j].sessionId == sessionId) {
          m_pendingJttyTciMessages[j].newSession = true;
          return;
        }
      }
      resetJttyTxState();
    }
    return;
  }
}

void MainWindow::rejectPendingJttyTciMessages(JttyTxRejectReason reason)
{
  for (auto const& pending : m_pendingJttyTciMessages) {
    Q_EMIT jttyTextRejected(pending.requestId, reason);
  }
}

void MainWindow::handleJttyTxWatchdog()
{
  if (m_mode != "JTTY" || !m_jttyTxActive) {
    return;
  }

  LOG_WARN("JTTY transmit completion watchdog expired");
  interruptJttyTx();
#ifdef WIN32
  if (m_mmttyif) {
    m_mmttyif->report_ptt_state(false);
  }
#endif
  stopTx();
}

void MainWindow::resetJttyTxState()
{
  m_jttyTxWatchdog.stop();
  m_jttyTxActive = false;
  m_jttyQueuedSamples = 0;
  m_pendingJttyTciMessages.clear();
  m_acceptedJttyTxRequests.clear();
}

void MainWindow::startJttyTxWatchdog(int durationMs)
{
  if (durationMs > 0) {
    m_jttyTxWatchdog.start(durationMs);
  }
}

void MainWindow::jtty_again()
{
  for(int k=3456; k<dec_data.params.kin; k+=3456) {
    jtty_decode(k);
  }
  decodeDone();
}

bool MainWindow::jtty_key_struck(QKeyEvent * e)
{
  QString t{};
  if(e->key() == Qt::Key_Escape) {
    abort_jtty_tx();
    return true;
  }
  if(e->key() == Qt::Key_F1) t = ui->msg1->text();
  if(e->key() == Qt::Key_F2) t = ui->msg2->text();
  if(e->key() == Qt::Key_F3) t = ui->msg3->text();
  if(e->key() == Qt::Key_F4) t = ui->msg4->text();
  if(e->key() == Qt::Key_F5) t = ui->msg5->text();
  if(e->key() == Qt::Key_F6) t = ui->msg6->text();
  if(e->key() == Qt::Key_F7) t = ui->msg7->text();
  if(e->key() == Qt::Key_F8) t = ui->msg8->text();
  if(t=="") return false;
  t=jtty_msg_expand(t);
  jtty_tx(t);
  return true;
}

QString MainWindow::jtty_msg_expand(QString t)
{
  if(!t.contains("%")) return t;
  for (int i=0; i<5; i++) {
    t=t.replace("%M",m_config.my_callsign());
    t=t.replace("%H",m_hisCall);
    t=t.replace("%Q",m_hisCall);
    if(t.contains("%N")) {
      int n=ui->sbSerialNumber_2->value();
      QString tn=Jtty::formatSerialNumber(n);
      t=t.replace("%N",tn);
      if(!t.contains("%")) return t;
    }
    if(!t.contains("%")) return t;
  }
  return t;
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
  Q_UNUSED(n);
}

void MainWindow::logText(const QString &text) {
  LOG_INFO(text);
}

#ifdef Q_OS_WIN

void MainWindow::initMMTTY(const QString& hexHandle) {
    if (!m_mmttyif) {
        m_mmttyif = new MMTTYIF(this);
    }
    connect(m_mmttyif, &MMTTYIF::log_message, this, &MainWindow::logText);

    // Register custom window message
    UINT MSG_MMTTY = ::RegisterWindowMessageA("MMTTY");

    // Print to logs the assigned custom message number for "MMTTY"
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    MMTTYIF::logText(QString("%1 [INIT] Registered MMTTY message: 0x%2")
                     .arg(timestamp)
                     .arg(MSG_MMTTY, 4, 16, QChar('0')));

    // Get the WId (Window ID) of the MainWindow instance
    WId winId = this->winId();
    HWND hwnd = reinterpret_cast<HWND>(winId);

    // Get our own thread ID
    DWORD threadId = GetCurrentThreadId();

    HWND targetHwnd = HWND_BROADCAST;
    QString targetName = "Broadcast";

    if (!hexHandle.isEmpty()) {
        bool ok;
        targetHwnd = reinterpret_cast<HWND>(hexHandle.toULongLong(&ok, 16));
        if (ok) {
            targetName = QString("0x%1").arg(hexHandle);
        } else {
            // Revert back to broadcast if parsing failed
            targetHwnd = HWND_BROADCAST;
        }
    }

    MMTTYIF::logMessage(QString("SENT (%1)").arg(targetName), MSG_MMTTY, TXM_THREAD, static_cast<LPARAM>(threadId));
    ::PostMessageA(targetHwnd, MSG_MMTTY, TXM_THREAD, static_cast<LPARAM>(threadId)); // Send Thread ID

    MMTTYIF::logMessage(QString("SENT (%1)").arg(targetName), MSG_MMTTY, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd));
    ::PostMessageA(targetHwnd, MSG_MMTTY, TXM_HANDLE, reinterpret_cast<LPARAM>(hwnd)); // Send Window Handle

    MMTTYIF::logMessage(QString("SENT (%1)").arg(targetName), MSG_MMTTY, TXM_START, 0x00000000);
    ::PostMessageA(targetHwnd, MSG_MMTTY, TXM_START, 0x00000000); // Send Start signal

    m_mmttyif->initialize(hexHandle, this->winId()); // Or however you get handles

    connect(m_mmttyif, &MMTTYIF::app_tx_string, this, &MainWindow::jtty_tx);
    // Graceful MMTTY/N1MM OFF should stop submitting text, not call stopTx();
    // accepted JTTY audio drains through onJttyBackendDrained().
    connect(m_mmttyif, &MMTTYIF::inactivity_timeout, qApp, &QCoreApplication::quit);
    connect(m_mmttyif, &MMTTYIF::app_is_quitting, qApp, &QCoreApplication::quit);

    // Auto-switch to JTTY mode after MMTTY connects
    QTimer::singleShot(3000, this, [this]() {
         set_mode("JTTY");
    });
}

MMTTYIF *MainWindow::getMmttyIf() const {
    return m_mmttyif;
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (m_mmttyif && msg->message == m_mmttyif->getMttyMsg()) {
            m_mmttyif->filterEvent(message);
            *result = 0; // Return 0 to indicate we handled the message
            return true; // Stop standard Qt processing for this message
        }
    }

    // Call base class method for unhandled messages
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
