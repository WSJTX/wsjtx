#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "widegraph.h"
#include "commons.h"
#include "JttyMessages.hpp"
#include "JttyN1mm.hpp"
#include "JttyReplay.hpp"
#include "Logger.hpp"
#include <QByteArray>
#include <QDateTime>
#include "Modulator/Modulator.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <vector>
#ifdef WIN32
#include "MMTTYIF.hpp"
#undef MessageBox
#endif


extern dec_data_t& dec_data;
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
      Configuration const& configuration, QString const& hisCall, int serialNumber)
  {
    Jtty::NativeMacroContext context;
    context.myCall = configuration.my_callsign();
    context.hisCall = hisCall;
    context.serialNumber = serialNumber;
    context.grid = configuration.my_grid();
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

#define FCL fortran_charlen_t

namespace
{
  constexpr int jttyMaxUpdates = 30;
  constexpr int jttyMessageSize = Jtty::maxMessageLength;
  constexpr int jttyUpdateBufferSize = jttyMaxUpdates * jttyMessageSize;

  QString formatJttyDecodeLine (float frequency, QString const& message)
  {
    QString const frequencyText = QStringLiteral("%1").arg(qRound(frequency), 4);
    return message.isEmpty() ? frequencyText : frequencyText + QStringLiteral("  ") + message;
  }
}

extern "C" {
  void rjtty_sub_(short int d2[], int* k, int* nsps, int* nfa, int*nfb,
                  float* f0, float* ftol);

  // Bounds the scan to [istart0,istop] (sample indices into d2) instead of
  // the whole buffer -- see MainWindow::jtty_decode_windowed().
  void rjtty_sub_windowed_(short int d2[], int* k, int* nsps, int* nfa, int*nfb,
                  float* f0, float* ftol, int* istart0, int* istop);

  void jtty_get_updates_(char text_blocks[], qint64 message_ids[],
                         float frequencies[], float start_tsync[], bool eom[],
                         int* count, fortran_charlen_t);

  void genjtty_profile_(char * msg, int const* exchange_profile,
                       int itone[], int* nsym, fortran_charlen_t);
  void genjtty_atoms_c(Jtty::NativeAtomDescriptor const atoms[], int natoms,
                       int itone[], int* nsym, int* status);

  void gen_jttywave_(int itone[], int* nsym, int* nsps, float* bt, float* fsample, float* f0,
                    float xjunk[], float wave[], int* icmplx, int* nwave);
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

void MainWindow::jtty_save_wav()
{
  // Reject callers that arrive before a real JTTY capture exists; m_k0 is
  // still the initial sentinel, or is stale from an earlier interval.
  if (!Jtty::wavCaptureValid (m_k0)) return;
  if (m_k0 == m_jttyLastSavedWavK0) return;  //Guard against re-saving same audio under a new timestamp
  m_jttyLastSavedWavK0 = m_k0;

  //Save JTTY data to a .wav file
  QDateTime now {QDateTime::currentDateTimeUtc ()};
  qint64 ms = m_k0/12;
  auto const& tstart=now.addMSecs(-ms);
  m_fnameWE=m_config.save_directory().absoluteFilePath (tstart.toString("yyMMdd_hhmmss"));
  int samples=m_k0;
  QString dgrd = "jtty";
  save_wave_file (m_fnameWE, samples, m_freqNominalPeriod, dgrd);
  // "Save decoded" keeps the file only if something was decoded; give the
  // decoder a further 3 seconds to finish before killWaveFile() decides.
  if (m_saveDecoded) killFileTimer.start (3000);
}

void MainWindow::updateJttyDecodeHeadings()
{
  QString const prefix = ui->cbIncludeTime->isChecked()
    ? QStringLiteral("  UTC  Freq  ") : QStringLiteral("Freq  ");
  ui->lh_decodes_headings_label->setText(prefix + tr ("Message"));
  ui->rh_decodes_headings_label->setText(prefix + tr ("Message"));
}

void MainWindow::on_cbIncludeTime_toggled(bool)
{
  if (m_mode == "JTTY") {
    updateJttyDecodeHeadings();
    renderJttyAllFreqLines();
    renderJttyQsoLines();
  }
}

void MainWindow::renderJttyAllFreqLines()
{
  if (m_jttyAllFreqLines.isEmpty()) return;

  QStringList displayLines;
  for (auto const& line : m_jttyAllFreqLines) {
    QString displayLine = formatJttyDecodeLine (
      line.frequency, Jtty::wrapMessage (line.text));
    if (ui->cbLowerCase->isChecked ()) displayLine = displayLine.toLower ();
    if (ui->cbIncludeTime->isChecked ()) {
      QString const time = Jtty::jttyLineTimeLabel (line.messageStartUtc);
      if (!time.isEmpty ()) displayLine = time + " " + displayLine;
    }
    displayLines.append (displayLine);
  }

  QTextCharFormat format;
  format.setFont (ui->decodedTextBrowser->contentFont ());

  QTextCursor cursor = ui->decodedTextBrowser->textCursor ();
  if (m_jttyAllFreqsGroupStart.isValid ()) {
    cursor.setPosition (m_jttyAllFreqsGroupStart.position ());
    cursor.movePosition (QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText ();
  } else {
    cursor.movePosition (QTextCursor::End);
    if (cursor.position () > 0) cursor.insertBlock ();
  }
  m_jttyAllFreqsGroupStart = cursor.block ();
  cursor.insertText (displayLines.join (QChar {'\n'}), format);
  ui->decodedTextBrowser->setTextCursor (cursor);
  ui->decodedTextBrowser->ensureCursorVisible ();
}

void MainWindow::renderJttyQsoLines()
{
  if (m_jttyQsoLines.isEmpty ()) {
    m_jttyQsoRenderedLowerCase = ui->cbLowerCase->isChecked ();
    m_jttyQsoRenderedIncludeTime = ui->cbIncludeTime->isChecked ();
    return;
  }

  QTextCursor cursor = ui->decodedTextBrowser2->textCursor ();
  if (m_jttyQsoGroupStart.isValid () && m_jttyQsoGroupEnd.isValid ()
      && m_jttyQsoGroupEndPosition >= m_jttyQsoGroupStart.position ()) {
    cursor.setPosition (m_jttyQsoGroupStart.position ());
    cursor.setPosition (m_jttyQsoGroupEndPosition, QTextCursor::KeepAnchor);
    cursor.removeSelectedText ();
  } else {
    cursor.movePosition (QTextCursor::End);
    if (cursor.position () > 0) cursor.insertBlock ();
  }

  QTextCharFormat format;
  format.setFont (ui->decodedTextBrowser2->contentFont ());
  m_jttyQsoGroupStart = cursor.block ();
  QStringList renderedLines;
  for (auto const& line : m_jttyQsoLines) {
    QString display = formatJttyDecodeLine (
      line.frequency, Jtty::wrapMessage (line.text));
    if (ui->cbLowerCase->isChecked ()) display = display.toLower ();
    if (ui->cbIncludeTime->isChecked ()) {
      QString const time = Jtty::jttyLineTimeLabel (line.messageStartUtc);
      if (!time.isEmpty ()) display = time + " " + display;
    }
    renderedLines.append (display);
  }
  cursor.insertText (renderedLines.join (QChar {'\n'}), format);
  m_jttyQsoGroupEnd = cursor.block ();
  m_jttyQsoGroupEndPosition = cursor.position ();
  ui->decodedTextBrowser2->setTextCursor (cursor);
  m_jttyQsoRenderedLowerCase = ui->cbLowerCase->isChecked ();
  m_jttyQsoRenderedIncludeTime = ui->cbIncludeTime->isChecked ();
}

bool MainWindow::jtty_decode(int k, int istart0, int istop)
{
  auto jttyLineDateTimeUtc = [this, k] (float tsync) -> QDateTime {
    if (m_diskData && m_UTCdiskDateTime.isValid()) {
      return m_UTCdiskDateTime.addMSecs(qRound64(1000.0 * tsync)).toUTC();
    }
    double const elapsed = qMax(0.0, double(k) / 12000.0 - double(tsync));
    return QDateTime::currentDateTimeUtc().addMSecs(-qRound64(1000.0 * elapsed));
  };
  auto jttyLineDisplayDateTimeUtc = [this, &jttyLineDateTimeUtc] (float tsync) -> QDateTime {
    if (m_diskData) {
      return Jtty::jttyLineStartTimeUtc (m_UTCdiskDateTime, m_UTCdisk, tsync);
    }
    return jttyLineDateTimeUtc (tsync);
  };
  int nsps=384;
  // A non-advancing sample position starts a distinct displayed decode session.
  bool const newAllFreqsSession = (k <= m_jttyLastAllFreqsK);
  m_jttyLastAllFreqsK = k;
  if (newAllFreqsSession) {
      flushJttyDecodeLines();
      m_jttyAllFreqsGroupStart = QTextBlock();
      m_jttyQsoGroupStart = QTextBlock();
      m_jttyQsoGroupEnd = QTextBlock();
      m_jttyQsoGroupEndPosition = -1;
      m_jttyQsoLines.clear();
      m_jttyAllFreqLines.clear();
      m_bDecoded = false;
      m_jttyLastSavedWavK0 = -1;
  }
  float f0 = ui->RxFreqSpinBox_2->value();
  float ftol = ui->sbFtol_2->value();
  int nfa = m_wideGraph->nStartFreq();
  int nfb = m_wideGraph->Fmax();

  if (istart0 < 0) {
    rjtty_sub_(dec_data.d2,&k,&nsps,&nfa,&nfb,&f0,&ftol);
  } else {
    rjtty_sub_windowed_(dec_data.d2,&k,&nsps,&nfa,&nfb,&f0,&ftol,&istart0,&istop);
  }

  QVector<Jtty::MessageUpdate> updates;
  int updateCount {0};
  do {
      std::array<char, jttyUpdateBufferSize> textBlocks {};
      std::array<qint64, jttyMaxUpdates> messageIds {};
      std::array<float, jttyMaxUpdates> frequencies {};
      std::array<float, jttyMaxUpdates> sequenceStarts {};
      std::array<bool, jttyMaxUpdates> complete {};
      jtty_get_updates_(textBlocks.data(), messageIds.data(), frequencies.data(),
                        sequenceStarts.data(), complete.data(), &updateCount,
                        (FCL)jttyUpdateBufferSize);
      for (int i = 0; i < updateCount; ++i) {
          QString const text = QString::fromLatin1(
              textBlocks.data() + i * jttyMessageSize, jttyMessageSize).trimmed();
          if (messageIds[i] <= 0) continue;
          updates.append({messageIds[i], frequencies[i], text,
                          sequenceStarts[i], complete[i]});
      }
  } while (updateCount == jttyMaxUpdates);

  bool const allHistoryChanged = Jtty::mergeMessageUpdates(
      m_jttyAllFreqLines, updates,
      [this, &jttyLineDateTimeUtc, &jttyLineDisplayDateTimeUtc] (Jtty::MessageUpdate const& update) {
          JttyDecodeLine decodeLine;
          decodeLine.messageId = update.messageId;
          decodeLine.frequency = update.frequency;
          decodeLine.text = update.text;
          decodeLine.sequenceStart = update.sequenceStart;
          decodeLine.messageStartUtc = jttyLineDisplayDateTimeUtc (update.sequenceStart);
          decodeLine.complete = update.complete;
          decodeLine.context = currentDecodeOperatingContext();
          decodeLine.context.sequenceStart = jttyLineDateTimeUtc(update.sequenceStart);
          return decodeLine;
      });

  if (allHistoryChanged) renderJttyAllFreqLines ();

  for (auto& known : m_jttyAllFreqLines) {
      if (known.complete && !known.written) {
          write_all("Rx", formatJttyDecodeLine(known.frequency, known.text), &known.context);
          known.written = true;
      }
  }

  bool const qsoDisplayOptionsChanged = !m_jttyQsoLines.isEmpty()
      && (m_jttyQsoRenderedLowerCase != ui->cbLowerCase->isChecked()
          || m_jttyQsoRenderedIncludeTime != ui->cbIncludeTime->isChecked());
  bool anyEom {false};
  bool anyLineChanged {false};
  for (auto const& update : updates) {
      auto known = std::find_if(m_jttyQsoLines.begin(), m_jttyQsoLines.end(),
                                [&update] (JttyQsoLine const& line) {
                                  return line.messageId == update.messageId;
                                });
      bool const alreadyPresent = known != m_jttyQsoLines.end();
      if (!Jtty::shouldApplyToQsoHistory(
              alreadyPresent, update.frequency, f0, ftol)) {
          continue;
      }
      if (update.complete) anyEom = true;

      QString delta;
#ifdef WIN32
      bool startNew{false};       //Set to "true" when N1MM should start display of text on a new line
#endif
      if (alreadyPresent) {
          auto const change = Jtty::compareMessages(known->text, update.text);
          bool const frequencyChanged = known->frequency != update.frequency;
          if (!change.messageChanged && !frequencyChanged) continue;
          if (change.messageChanged) {
              if (change.extendsMessage) {
                  delta = change.appendedText;
#ifdef WIN32
                  startNew = change.startsMessage;
#endif
              } else {
                  delta = update.text;
#ifdef WIN32
                  startNew = true;
#endif
              }
          }
          known->frequency = update.frequency;
          known->text = update.text;
      } else {
#ifdef WIN32
          startNew = true;
#endif
          delta = update.text;
          auto const allLine = std::find_if (
            m_jttyAllFreqLines.cbegin (), m_jttyAllFreqLines.cend (),
            [&update] (JttyDecodeLine const& line) {
              return line.messageId == update.messageId;
            });
          QDateTime const messageStartUtc = allLine != m_jttyAllFreqLines.cend ()
            ? allLine->messageStartUtc
            : jttyLineDisplayDateTimeUtc (update.sequenceStart);
          m_jttyQsoLines.append({update.messageId, update.frequency, update.text,
                                 update.sequenceStart, messageStartUtc});
      }
      anyLineChanged = true;
      m_bDecoded = true;

#ifdef WIN32
      if (m_mmttyif && !delta.isEmpty()) {
//            m_mmttyif->echo_message_to_n1mm(append_separator(delta));
        if(ui->cbLowerCase->isChecked()) delta = delta.toLower();
        if(startNew) delta = "\r\n" + delta;
        m_mmttyif->echo_message_to_n1mm(delta);
      }
#endif
  }

  if (anyLineChanged || qsoDisplayOptionsChanged) renderJttyQsoLines ();
  return anyEom;
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

void MainWindow::submitJttyDraft(QString message)
{
  if (m_jttyDraftAcceptanceTracker.hasPendingSubmissionForCurrentDraft ()) {
    ui->Tx_Message->selectAll ();
    return;
  }

  qint64 const requestId = ++m_jttyTxRequestId;
  m_jttyDraftAcceptanceTracker.trackSubmission (requestId);
  execute_jtty_tx (requestId, message);
  if (m_jttyDraftAcceptanceTracker.isPending (requestId)) {
    ui->Tx_Message->selectAll ();
  }
}

void MainWindow::execute_jtty_tx(qint64 requestId, QString message)
{
  int itone[944];
  // Captured before anything below can change m_jttyTxActive: true means
  // this message is being queued behind one still transmitting, not
  // starting a fresh session.
  bool const isChainedMessage = m_jttyTxActive;
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

  // Keep message as the logical text; the chained leading space is only
  // transport spacing and must not leak into logging, display, or the contest
  // serial check in completeJttyTxEnqueue.
  auto transmitFrame = Jtty::transmitFrame(message, isChainedMessage).toLatin1();

  int nsym=0;
  int const exchangeProfile = static_cast<int>(jttyExchangeProfile(m_config));
  genjtty_profile_(transmitFrame.data(), &exchangeProfile,
                   &itone[0], &nsym, (FCL)80);
  if (nsym <= 0) {
    LOG_WARN("JTTY transmit message could not be encoded");
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }

  message = QString::fromLatin1(transmitFrame).trimmed();
  execute_jtty_tones(requestId, message, itone, nsym);
}

void MainWindow::execute_jtty_tones(qint64 requestId, QString const& message,
                                    int const itone[], int nsym)
{
  m_nsym_jtty=nsym;

  int nsps4=4*384;
  float bt=2.0;
  float fsample=48000.0;
  float f0=ui->TxFreqSpinBox_2->value ();
  int icmplx=0;
  int nwave=nsps4*m_nsym_jtty;

  bool const newSession = !m_jttyTxActive;
  if (newSession) {
    advanceJttyTxQueueEpoch();
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
    // natural drain, so drain totals remain session-relative.
    if (useTciAudio) {
      Q_EMIT m_config.transceiver_clear_jtty_pcm(m_jttyTxQueueEpoch);
    } else {
      m_jttyTxQueue->clear(m_jttyTxQueueEpoch);
    }
  }

  bool enqueued {false};
  TxAudioQueueProgress enqueueProgress;
  if(useTciAudio) {
    // TCI enqueue is asynchronous. MainWindow can reject a message that can
    // never fit; backend occupancy failures reject only the submitted enqueue.
    if (samples.size () <= TxAudioQueue::defaultCapacity ()) {
      QByteArray bytes(reinterpret_cast<char const *> (samples.constData ()),
                       samples.size () * int (sizeof (qint16)));
      qint64 const enqueueId = ++m_jttyTciEnqueueId;
      m_pendingJttyTciMessages.append(PendingJttyTciMessage {
        m_jttyTxQueueEpoch,
        enqueueId,
        requestId,
        samples.size (),
        message,
        newSession
      });
      m_jttyTxActive = true;
      Q_EMIT m_config.transceiver_enqueue_jtty_pcm(bytes, m_jttyTxQueueEpoch,
                                                   enqueueId);
      return;
    } else {
      LOG_WARN("JTTY TCI transmit FIFO capacity precheck failed; rejecting PCM enqueue");
      Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::QueueFull);
    }
  } else {
    auto const result = m_jttyTxQueue->enqueue(samples, m_jttyTxQueueEpoch);
    enqueued = result.accepted;
    enqueueProgress = result.progress;
    if (!enqueued) {
      LOG_WARN("JTTY transmit FIFO overflow; rejecting PCM enqueue");
    }
  }

  if (!enqueued) {
    if (!useTciAudio) {
      Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::QueueFull);
    }
    if (newSession) {
      advanceJttyTxQueueEpoch();
    }
    return;
  }

  completeJttyTxEnqueue(requestId, message, enqueueProgress, newSession, useTciAudio);
}

void MainWindow::advanceJttyTxQueueEpoch()
{
  m_jttyTxQueueEpoch = TxAudioQueueEpoch {m_jttyTxQueueEpoch.value () + 1};
  m_jttyTxQueueProgress = {};
  m_jttyTxQueueProgress.epoch = m_jttyTxQueueEpoch;
}

qint64 MainWindow::jttyTxCommittedSamples() const
{
  return m_jttyTxQueueProgress.total_samples;
}

void MainWindow::completeJttyTxEnqueue(qint64 requestId, QString const& message,
                                       TxAudioQueueProgress progress,
                                       bool newSession, bool useTciAudio)
{
  if (newSession) {
    beginTxEvidenceSession ();
  }
  m_currentMessage = message;
  qint64 const endSample = progress.total_samples;
  recordAcceptedJttyTextRequest(requestId, endSample);
  m_jttyTxQueueProgress = progress;
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
  m_jttyTxActive = true;
  m_transmitting = true;
  write_all("Tx", message);
  Q_EMIT jttyTextAccepted(requestId);

  ui->decodedTextBrowser2->insertText(" ");
  QTextCursor cursor = ui->decodedTextBrowser2->textCursor();
  QTextCharFormat format = cursor.charFormat();
  format.setBackground(QBrush(QColor(Qt::yellow)));
  cursor.setCharFormat(format);
  cursor.insertText(Jtty::wrapMessage(message));
  format.setBackground(QBrush(QColor(Qt::white)));
  cursor.setCharFormat(format);

  handleJttyContestSerial(message);

  // Fault-detector watchdog: generous margin over all audio still to play (the
  // whole queued session, not just this message). The happy path completes via
  // the backend drain signal well before this fires.
  qint64 const pendingSamples = useTciAudio
    ? progress.total_samples : progress.queued_samples;
  int pendingMs = int(pendingSamples / 48);
  startJttyTxWatchdog(pendingMs + 1000 * m_config.txDelay() + 10000);

  monitor(false);
  if(!m_diskData && (m_saveAll || m_saveDecoded) && Jtty::wavCaptureValid (m_k0)) {
    jtty_save_wav();
  }

#ifdef WIN32
  if (m_mmttyif) {
    m_mmttyif->report_ptt_state(true);
  }
#endif

#ifdef WIN32
  if (m_mmttyif) {
    m_mmttyif->echo_message_to_n1mm(append_separator(message));
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
    m_jttyTxQueueEpoch,
    requestId,
    endSample
  });
}

QVector<qint64> MainWindow::takeCompletedJttyTextRequests(
  TxAudioQueueEpoch epoch, qint64 totalAtDrain)
{
  // Backends report only final drain, so per-text completion is observed when
  // the accepted text's containing JTTY session has drained.
  QVector<qint64> completedRequestIds;
  for (int i = 0; i < m_acceptedJttyTxRequests.size ();) {
    auto const accepted = m_acceptedJttyTxRequests.at (i);
    if (accepted.epoch == epoch && accepted.endSample <= totalAtDrain) {
      completedRequestIds.append(accepted.requestId);
      m_acceptedJttyTxRequests.remove (i);
    } else {
      ++i;
    }
  }
  return completedRequestIds;
}

void MainWindow::clearAcceptedJttyTextRequests(TxAudioQueueEpoch epoch)
{
  for (int i = 0; i < m_acceptedJttyTxRequests.size ();) {
    if (m_acceptedJttyTxRequests.at (i).epoch == epoch) {
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
   noteTxStopReason (TxEvidence::TxStopReason::UserHalt);
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

  auto const interruptedEpoch = m_jttyTxQueueEpoch;
  if (!m_jttyTxUsesTciAudio) {
    auto const progress = m_jttyTxQueue->progress ();
    captureJttyTxEvidenceTotals (progress.served_samples,
                                 progress.total_samples,
                                 QStringLiteral ("JTTY source totals captured before abort"));
  } else {
    captureJttyTxEvidenceTotals (-1, m_jttyTxQueueProgress.total_samples,
                                 QStringLiteral ("TCI JTTY committed total captured before abort"));
  }
  advanceJttyTxQueueEpoch();
  clearAcceptedJttyTextRequests(interruptedEpoch);
  rejectPendingJttyTciMessages(JttyTxRejectReason::Aborted);
  m_pendingJttyTciMessages.clear();
  if (m_jttyTxUsesTciAudio) {
    Q_EMIT m_config.transceiver_clear_jtty_pcm(m_jttyTxQueueEpoch);
  } else {
    m_jttyTxQueue->clear(m_jttyTxQueueEpoch);
  }
  resetJttyTxState();
}

void MainWindow::onJttyBackendDrained(TxAudioQueueDrainState drain)
{
  if (m_mode != "JTTY" || !m_jttyTxActive) {
    return;
  }

  if (drain.epoch != m_jttyTxQueueEpoch
      || drain.total_at_drain != jttyTxCommittedSamples ()) {
    return;
  }

  qint64 const servedAtDrain = m_jttyTxUsesTciAudio
    ? drain.total_at_drain : m_jttyTxQueue->progress ().served_samples;
  captureJttyTxEvidenceTotals (servedAtDrain, drain.total_at_drain,
                               QStringLiteral ("JTTY source totals captured at drain"));

  auto const completedRequestIds = takeCompletedJttyTextRequests(
    drain.epoch, drain.total_at_drain);
  resetJttyTxState();
  stopTx();
  for (auto const requestId : completedRequestIds) {
    Q_EMIT jttyTextCompleted(requestId);
  }
  Q_EMIT jttySessionDrained(drain.epoch.value ());
}

void MainWindow::onJttyBackendEnqueueAccepted(qint64 enqueueId, qint64 sampleCount,
                                              TxAudioQueueProgress progress)
{
  if (m_mode != "JTTY" || !m_jttyTxActive
      || progress.epoch != m_jttyTxQueueEpoch) {
    return;
  }

  for (int i = 0; i < m_pendingJttyTciMessages.size (); ++i) {
    auto const pending = m_pendingJttyTciMessages.at (i);
    if (pending.epoch != progress.epoch || pending.enqueueId != enqueueId) {
      continue;
    }

    m_pendingJttyTciMessages.remove (i);
    if (sampleCount != pending.sampleCount) {
      LOG_WARN("JTTY transmit backend accepted unexpected PCM sample count");
    }
    bool const startsSession = pending.newSession
      || m_jttyTxQueueProgress.total_samples <= 0;
    completeJttyTxEnqueue(pending.requestId, pending.message, progress,
                          startsSession, true);
    return;
  }
}

void MainWindow::onJttyBackendEnqueueFailed(TxAudioQueueEpoch epoch,
                                            qint64 enqueueId)
{
  if (m_mode != "JTTY" || !m_jttyTxActive
      || epoch != m_jttyTxQueueEpoch) {
    return;
  }

  LOG_WARN("JTTY transmit backend rejected PCM enqueue");
  for (int i = 0; i < m_pendingJttyTciMessages.size (); ++i) {
    auto const pending = m_pendingJttyTciMessages.at (i);
    if (pending.epoch != epoch || pending.enqueueId != enqueueId) {
      continue;
    }
    m_pendingJttyTciMessages.remove (i);
    Q_EMIT jttyTextRejected(pending.requestId, JttyTxRejectReason::QueueFull);
    if (pending.newSession && m_jttyTxQueueProgress.total_samples <= 0) {
      for (int j = 0; j < m_pendingJttyTciMessages.size (); ++j) {
        if (m_pendingJttyTciMessages[j].epoch == epoch) {
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
  noteTxStopReason (TxEvidence::TxStopReason::Watchdog);
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
  m_jttyTxQueueProgress = {};
  m_jttyTxQueueProgress.epoch = m_jttyTxQueueEpoch;
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
  ui->DecodeButton->setChecked (true);
  qApp->processEvents();                                //Update the DecodeButton highlight
  replayJttyFrames (dec_data.params.kin, [this] (int k) {
    jtty_decode(k);
    return false;
  });
  flushJttyDecodeLines();
  finishDecodeUi();
}

// Triggered by double-clicking WideGraph's waterfall in JTTY mode: starts
// the rescan jttyPickLookbackSecs before the clicked time (a message can
// start just before the click) and stops as soon as jtty_decode reports a
// completed (EOM) message, rather than scanning a fixed window. A safety
// cap (jttyPickSafetyCapSecs forward of istart0) bounds how long it keeps
// looking if nothing ever completes -- e.g. the click landed on noise, or
// sync was lost partway through. secondsAgo is relative to m_k0 (the
// buffer position at the last processed block, i.e. "now"). Calls where k
// hasn't yet reached istart0 are cheap no-ops on the Fortran side, so the
// outer loop doesn't need to special-case its own starting point.
void MainWindow::jttyDecodeAgainAt(float secondsAgo)
{
  constexpr int jttyPickLookbackSecs = 5;
  constexpr int jttyPickSafetyCapSecs = 40;
  qint64 const center = qint64(m_k0) - qint64(qMax(0.0f, secondsAgo) * 12000.0f);
  int const istart0 = int(qMax(qint64(1), center - qint64(jttyPickLookbackSecs) * 12000));
  int const frames = snapshotJttyFrames (dec_data.params.kin);
  int const istop = int(qMin(qint64(frames),
                             qint64(istart0) + qint64(jttyPickSafetyCapSecs) * 12000));
  if (istop < istart0) return;   // clicked time is no longer in the buffer at all

  ui->DecodeButton->setChecked (true);
  qApp->processEvents();                                //Update the DecodeButton highlight
  replayJttyFrames (frames, [this, istart0, istop] (int k) {
    bool const eom = jtty_decode(k, istart0, istop);
    return eom || k >= istop;
  });
  flushJttyDecodeLines();
  finishDecodeUi();
}

void MainWindow::flushJttyDecodeLines()
{
  // Called at a definite session end (new session starting, or true end of file); unconditional.
  for (auto& line : m_jttyAllFreqLines) {
    if (line.written) continue;
    QString const text = line.text.trimmed();
    if (text.isEmpty()) continue;
    write_all("Rx", formatJttyDecodeLine(line.frequency, text), &line.context);
    line.written = true;
  }
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
    m_config, m_hisCall, ui->sbSerialNumber_2->value());
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
  execute_jtty_tones(requestId,compiled.text,itone,nsym);
  return true;
}

QString MainWindow::jtty_msg_expand(QString t)
{
  auto const context = jttyNativeMacroContext(
    m_config, m_hisCall, ui->sbSerialNumber_2->value());
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
  }
  return QStringLiteral("unknown");
}

void MainWindow::handleMmttyTxString(QString message)
{
  auto const context = jttyNativeMacroContext(
    m_config, m_hisCall, ui->sbSerialNumber_2->value());
  auto const compiled = Jtty::compileN1mmMessage(message, context);
  if (m_mode != "JTTY") {
    if (compiled.status == Jtty::N1mmCompileStatus::Literal) {
      jtty_tx(compiled.literalText);
      return;
    }

    qint64 const requestId = ++m_jttyTxRequestId;
    m_mmttyJttyOutput.submit(requestId);
    QString const reason = compiled.status == Jtty::N1mmCompileStatus::Error
      ? compiled.error : QStringLiteral("tagged JTTY actions require JTTY mode");
    logText(QStringLiteral("MMTTY/N1MM tagged JTTY request %1 rejected: %2")
            .arg(requestId).arg(reason));
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::NotAvailable);
    return;
  }

  if (m_mmttyJttyOutput.finishRequested()) {
    logText(QStringLiteral("MMTTY/N1MM JTTY text ignored after graceful OFF"));
    return;
  }

  qint64 const requestId = ++m_jttyTxRequestId;
  m_mmttyJttyOutput.submit(requestId);
  if (compiled.status == Jtty::N1mmCompileStatus::Literal) {
    execute_jtty_tx(requestId, compiled.literalText);
    return;
  }
  if (compiled.status == Jtty::N1mmCompileStatus::Error) {
    logText(QStringLiteral("MMTTY/N1MM tagged JTTY request %1 rejected: %2")
            .arg(requestId).arg(compiled.error));
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }

  int itone[944];
  int nsym = 0;
  int encodeStatus = static_cast<int>(Jtty::NativeEncodeStatus::InvalidDescriptor);
  genjtty_atoms_c(compiled.atoms.constData(), compiled.atoms.size(), itone, &nsym,
                  &encodeStatus);
  if (nsym <= 0) {
    logText(QStringLiteral("MMTTY/N1MM tagged JTTY request %1 rejected: %2")
            .arg(requestId).arg(jttyNativeEncodeError(encodeStatus)));
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }
  execute_jtty_tones(requestId, compiled.canonicalText, itone, nsym);
}

void MainWindow::handleMmttyStartTx()
{
  if (m_mode != "JTTY" && !m_mmttyJttyOutput.pending()) {
    startTx2();
    return;
  }

  m_mmttyJttyOutput.start();
  startPendingMmttyJttyTx();
}

void MainWindow::handleMmttyStopTx()
{
  if (m_mode != "JTTY") {
    noteTxStopReason (TxEvidence::TxStopReason::UserHalt);
    stopTx();
    if (!m_mmttyJttyOutput.pending()) return;
  }

  m_mmttyJttyOutput.finish();
  logText(QStringLiteral("MMTTY/N1MM JTTY OFF requested; waiting for backend drain"));
  completeMmttyJttyOutput();
}

void MainWindow::handleMmttyAbortTx()
{
  m_mmttyJttyOutput.abort();
  abort_jtty_tx();
}

void MainWindow::handleMmttyJttyAccepted(qint64 requestId)
{
  if (!m_mmttyJttyOutput.accept(requestId)) return;

  logText(QStringLiteral("MMTTY/N1MM JTTY request %1 accepted").arg(requestId));
  startPendingMmttyJttyTx();
}

void MainWindow::handleMmttyJttyRejected(qint64 requestId, JttyTxRejectReason reason)
{
  if (!m_mmttyJttyOutput.resolve(requestId)) return;

  logText(QStringLiteral("MMTTY/N1MM JTTY request %1 rejected: %2")
          .arg(requestId)
          .arg(jttyRejectReasonText(reason)));
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
  if (m_mmttyJttyOutput.takeCompletion(m_jttyTxActive, drained) && m_mmttyif) {
    m_mmttyif->report_output_complete();
  }
}

void MainWindow::startPendingMmttyJttyTx()
{
  if (m_mode != "JTTY" || !m_mmttyJttyOutput.startRequested()) return;

  if (!m_jttyTxActive || jttyTxCommittedSamples () <= 0) {
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

    // Auto-switch to JTTY mode after MMTTY connects
    QTimer::singleShot(3000, this, [this]() {
         set_mode("JTTY");
    });
}

MMTTYIF *MainWindow::getMmttyIf() const {
    return m_mmttyif;
}
#endif
