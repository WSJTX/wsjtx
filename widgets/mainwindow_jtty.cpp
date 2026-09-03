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
#undef MessageBox
#endif


extern dec_data_t& dec_data;
extern qint32 g_iptt;

namespace
{
  Jtty::NativeMacroContext jttyNativeMacroContext(
      Configuration const& configuration, QString const& hisCall, int serialNumber)
  {
    Jtty::NativeMacroContext context;
    context.myCall = configuration.my_callsign();
    context.hisCall = hisCall;
    context.serialNumber = serialNumber;
    context.grid = configuration.my_grid();

    switch (configuration.special_op_id()) {
    case Configuration::SpecialOperatingActivity::FIELD_DAY:
      context.exchangeProfile = Jtty::NativeExchangeProfile::FieldDay;
      context.configuredExchange = configuration.Field_Day_Exchange();
      break;
    case Configuration::SpecialOperatingActivity::RTTY:
      context.exchangeProfile = Jtty::NativeExchangeProfile::RttyRoundup;
      context.configuredExchange = configuration.RTTY_Exchange();
      break;
    default:
      break;
    }
    return context;
  }
}

#if QT_VERSION >= QT_VERSION_CHECK (5, 13, 0)
#define SkipEmptyParts Qt::SkipEmptyParts
#else
#define SkipEmptyParts QString::SkipEmptyParts
#endif

#define FCL fortran_charlen_t

extern "C" {
  void rjtty_sub_(short int d2[], int* k, int* nsps, int* nfa, int*nfb,
                  float* f0, float* ftol);

  // Bounds the scan to [istart0,istop] (sample indices into d2) instead of
  // the whole buffer -- see MainWindow::jtty_decode_windowed().
  void rjtty_sub_windowed_(short int d2[], int* k, int* nsps, int* nfa, int*nfb,
                  float* f0, float* ftol, int* istart0, int* istop);

   // Parallel metadata arrays follow the display order of their corresponding
   // all_freqs or qso_freq snapshot and must match jtty_mdec's MAX_SLOTS.
   void jtty_get_msgs_(float* f0, float* ftol, bool* all_new, bool* qso_new,
    char all_freqs[], char line[], bool qso_eom[], float all_tsync[], float qso_tsync[],
    bool all_eom[], int all_slot_ids[],
    fortran_charlen_t, fortran_charlen_t);

  void genjtty_(char const * msg, int itone[], int* nsym, fortran_charlen_t);
  void genjtty_atoms_c(Jtty::NativeAtomDescriptor const atoms[], int natoms,
                       int itone[], int* nsym);

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
  if (m_mode == "JTTY") updateJttyDecodeHeadings();
}

bool MainWindow::jtty_decode(int k, int istart0, int istop)
{
  auto boundedLatin1 = [] (char const *data, int size) {
    QByteArray bytes {QByteArray::fromRawData(data, size)};
    int const nul = bytes.indexOf('\0');
    if (nul >= 0) bytes.truncate(nul);
    return QString::fromLatin1(bytes.constData(), bytes.size());
  };
  auto jttyLineDateTimeUtc = [this, k] (float tsync) -> QDateTime {
    if (m_diskData && m_UTCdiskDateTime.isValid()) {
      return m_UTCdiskDateTime.addMSecs(qRound64(1000.0 * tsync)).toUTC();
    }
    double const elapsed = qMax(0.0, double(k) / 12000.0 - double(tsync));
    return QDateTime::currentDateTimeUtc().addMSecs(-qRound64(1000.0 * elapsed));
  };
  auto jttyLineTimeUtc = [this] (float tsync) -> QString {
    if (!m_diskData) return {};
    return Jtty::jttyLineTimeLabel(m_UTCdiskDateTime, m_UTCdisk, tsync);
  };
  auto messageBody = [] (QString const& line) {
      return Jtty::parseDecodeLine(line).message;
  };
  int nsps=384;
  // rjtty_sub_ restarts its own internal slot table whenever k stops
  // advancing (a fresh WAV, or a "decode again" replay of the current one).
  // Mirror that here: freeze whatever's already shown in both panes rather
  // than matching new growth against a frozen line from an earlier session
  // (a replayed WAV can regrow to the exact same final text, which would
  // otherwise match -- and silently stop updating -- the old, completed
  // line instead of the new, still-growing one).
  bool const newAllFreqsSession = (k <= m_jttyLastAllFreqsK);
  m_jttyLastAllFreqsK = k;
  if (newAllFreqsSession) {
      flushJttyDecodeLines();  // old session's slot table is gone Fortran-side too; log what's left
      m_jttyAllFreqsGroupStart = QTextBlock();
      m_jttyQsoGroupStart = QTextBlock();
      m_jttyQsoGroupEnd = QTextBlock();
      m_jttyQsoLines.clear();
      m_jttyAllFreqLines.clear();
      m_bDecoded = false;
      m_jttyLastSavedWavK0 = -1;
  }
  char qso_freq[800];
  char all_freqs[2400];
  bool qso_eom[30];    // must match jtty_mdec's MAX_SLOTS
  bool all_eom[30];    // ditto
  int all_slot_ids[30]; // ditto
  float all_tsync[30]; // ditto
  float qso_tsync[30]; // ditto
  float f0 = ui->RxFreqSpinBox_2->value();
  float ftol = ui->sbFtol_2->value();
  bool all_new = true;
  bool qso_new = true;
  int nfa = m_wideGraph->nStartFreq();
  int nfb = m_wideGraph->Fmax();

  if (istart0 < 0) {
    rjtty_sub_(dec_data.d2,&k,&nsps,&nfa,&nfb,&f0,&ftol);
  } else {
    rjtty_sub_windowed_(dec_data.d2,&k,&nsps,&nfa,&nfb,&f0,&ftol,&istart0,&istop);
  }

  // Each call returns frequency-sorted snapshots plus stable decoder-slot
  // identity and completion metadata for every all-frequency line.
  jtty_get_msgs_(&f0, &ftol, &all_new, &qso_new, &all_freqs[0],
                 &qso_freq[0], &qso_eom[0], &all_tsync[0], &qso_tsync[0],
                 &all_eom[0], &all_slot_ids[0],
                 (FCL)2400, (FCL)800);

  QString allMsgs {boundedLatin1(all_freqs, sizeof all_freqs)};
  if(ui->cbLowerCase->isChecked()) allMsgs = allMsgs.toLower();
  if(all_new) {
      QString const trimmedAll = allMsgs.trimmed();
      if (!trimmedAll.isEmpty()) {
          QStringList const snapshotLines = trimmedAll.split(QChar('\n'));
          int const lineCount = qMin(snapshotLines.size(), 30);
          QVector<float> startTimes;
          QVector<int> slotIds;
          startTimes.reserve(lineCount);
          slotIds.reserve(lineCount);
          for (int i = 0; i < lineCount; ++i) {
              startTimes.append(all_tsync[i]);
              slotIds.append(all_slot_ids[i]);
          }
          auto const lineOrder = Jtty::decodeLineOrder(startTimes, slotIds);

          QStringList lines;
          for (int const sourceIndex : lineOrder) {
              auto const parsed = Jtty::parseDecodeLine(snapshotLines.at(sourceIndex));
              QString line = parsed.valid
                  ? QStringLiteral("%1  %2").arg(parsed.frequency, 4)
                        .arg(Jtty::wrapMessage(parsed.message))
                  : Jtty::wrapMessage(snapshotLines.at(sourceIndex));
              if (ui->cbIncludeTime->isChecked()) {
                  QString const t = jttyLineTimeUtc(all_tsync[sourceIndex]);
                  if (!t.isEmpty()) line = t + " " + line;
              }
              lines.append(line);
          }
          QString const textToInsert = lines.join(QChar('\n'));
          // insertText() (below, via a plain QTextCursor) doesn't stamp the
          // app-configured content font the way DisplayText::insertText()
          // does, so pin it explicitly to match the rest of the pane.
          QTextCharFormat format;
          format.setFont(ui->decodedTextBrowser->contentFont());

          QTextCursor cursor = ui->decodedTextBrowser->textCursor();
          if (m_jttyAllFreqsGroupStart.isValid()) {
              // Same group still growing: replace just its own text with the
              // freshly resorted snapshot, leaving earlier groups untouched.
              cursor.setPosition(m_jttyAllFreqsGroupStart.position());
              cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
              cursor.removeSelectedText();
          } else {
              // First group ever, or first group of a new session: start a
              // new block below any earlier group instead of clearing them.
              cursor.movePosition(QTextCursor::End);
              if (cursor.position() > 0) {
                  cursor.insertBlock();
              }
          }
          m_jttyAllFreqsGroupStart = cursor.block();
          cursor.insertText(textToInsert, format);
          ui->decodedTextBrowser->setTextCursor(cursor);
          ui->decodedTextBrowser->ensureCursorVisible();
      }

//For now, at least, we're sending only the "on-frequency" decodes to N1MM
//#ifdef WIN32
//      if (m_mmttyif) {
//          m_mmttyif->echo_message_to_n1mm(append_separator(allMsgs));
//      }
//#endif

  }

  QStringList const allLines = allMsgs.split(QChar('\n'), SkipEmptyParts);
  for (int lineIdx = 0; lineIdx < allLines.size() && lineIdx < 30; ++lineIdx) {
      QString const newLine = allLines.at(lineIdx).trimmed();
      int const slotId = all_slot_ids[lineIdx];
      if (newLine.isEmpty() || slotId <= 0) continue;

      int matchIndex = -1;
      for (int i = 0; i < m_jttyAllFreqLines.size(); ++i) {
          if (m_jttyAllFreqLines.at(i).slotId == slotId) {
              matchIndex = i;
              break;
          }
      }

      if (matchIndex < 0) {
          JttyDecodeLine decodeLine;
          decodeLine.slotId = slotId;
          decodeLine.text = newLine;
          decodeLine.context = currentDecodeOperatingContext();
          decodeLine.context.sequenceStart = jttyLineDateTimeUtc(all_tsync[lineIdx]);
          m_jttyAllFreqLines.append(decodeLine);
          matchIndex = m_jttyAllFreqLines.size() - 1;
      } else {
          m_jttyAllFreqLines[matchIndex].text = newLine;
      }

      auto& known = m_jttyAllFreqLines[matchIndex];
      if (all_eom[lineIdx] && !known.written) {
          write_all("Rx", known.text, &known.context);
          known.written = true;
      }
  }

  bool const qsoDisplayOptionsChanged = !m_jttyQsoLines.isEmpty()
      && (m_jttyQsoRenderedLowerCase != ui->cbLowerCase->isChecked()
          || m_jttyQsoRenderedIncludeTime != ui->cbIncludeTime->isChecked());
  if(qso_new || qsoDisplayOptionsChanged) {
      QString message_qso_freq {boundedLatin1(qso_freq, sizeof qso_freq)};

      auto wrappedDisplayFor = [this, &jttyLineTimeUtc] (JttyQsoLine const& line) {
          auto const parsed = Jtty::parseDecodeLine(line.text);
          QString display = parsed.valid
              ? QStringLiteral("%1  %2").arg(parsed.frequency, 4)
                    .arg(Jtty::wrapMessage(parsed.message))
              : Jtty::wrapMessage(line.text);
          if (ui->cbLowerCase->isChecked()) display = display.toLower();
          if (ui->cbIncludeTime->isChecked()) {
              QString const t = jttyLineTimeUtc(line.tsync);
              if (!t.isEmpty()) display = t + " " + display;
          }
          return display;
      };

      QStringList const newLines = qso_new
          ? message_qso_freq.split(QChar('\n'), SkipEmptyParts) : QStringList {};
      bool anyLineChanged = false;
      for (int lineIdx = 0; lineIdx < newLines.size(); ++lineIdx) {
          QString const newLine = newLines.at(lineIdx).trimmed();
          if (newLine.isEmpty()) continue;
          QString const newBody = messageBody(newLine);

          int matchIndex = -1;
          for (int i = 0; i < m_jttyQsoLines.size(); ++i) {
              if (newBody.startsWith(messageBody(m_jttyQsoLines.at(i).text))) {
                  matchIndex = i;
                  break;
              }
          }

          QString delta;
#ifdef WIN32
          bool startNew{false};       //Set to "true" when N1MM should start display of text on a new line
#endif
          if (matchIndex >= 0) {
              auto& known = m_jttyQsoLines[matchIndex];
              QString const knownBody = messageBody(known.text);
              if (newBody.length() <= knownBody.length()) continue;   // unchanged this call
              delta = newBody.mid(knownBody.length());
              known.text = newLine;
          } else {
#ifdef WIN32
              startNew = true;
#endif
              delta = newLine;
              m_jttyQsoLines.append({newLine, lineIdx < 30 ? qso_tsync[lineIdx] : 0.0f});
          }
          anyLineChanged = true;
          m_bDecoded = true;

#ifdef WIN32
          if (m_mmttyif) {
//            m_mmttyif->echo_message_to_n1mm(append_separator(delta));
            if(startNew) delta = "\r\n" + delta.mid(8,-1); //Insert CRLF at start and delete the Freq and SNR info
            m_mmttyif->echo_message_to_n1mm(delta);
          }
#endif
      }

      if (anyLineChanged || qsoDisplayOptionsChanged) {
          QTextCursor cursor = ui->decodedTextBrowser2->textCursor();
          if (m_jttyQsoGroupStart.isValid() && m_jttyQsoGroupEnd.isValid()) {
              cursor.setPosition(m_jttyQsoGroupStart.position());
              QTextCursor endCursor(m_jttyQsoGroupEnd);
              endCursor.movePosition(QTextCursor::EndOfBlock);
              cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);
              cursor.removeSelectedText();
          } else {
              cursor.movePosition(QTextCursor::End);
              if (cursor.position() > 0) cursor.insertBlock();
          }
          QTextCharFormat format;
          format.setFont(ui->decodedTextBrowser2->contentFont());
          m_jttyQsoGroupStart = cursor.block();
          QStringList renderedLines;
          for (auto const& line : m_jttyQsoLines) {
              renderedLines.append(wrappedDisplayFor(line));
          }
          cursor.insertText(renderedLines.join(QChar('\n')), format);
          m_jttyQsoGroupEnd = cursor.block();
          ui->decodedTextBrowser2->setTextCursor(cursor);
      }
      m_jttyQsoRenderedLowerCase = ui->cbLowerCase->isChecked();
      m_jttyQsoRenderedIncludeTime = ui->cbIncludeTime->isChecked();
  }
  // Only meaningful to a windowed caller (jttyDecodeAgainAt): whether this
  // snapshot's qso_freq slot table includes a completed message, so a
  // click-driven decode can stop as soon as its message is done instead of
  // scanning all the way to the safety cap.
  bool anyEom = false;
  for (bool b : qso_eom) if (b) { anyEom = true; break; }
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

  message = Jtty::withChainedSpacing(message, isChainedMessage);

  int n=message.length();
  QString t = " ";
  t = message + t.repeated(80-n);
  int nsym=0;
  genjtty_(t.toLatin1().constData(), &itone[0], &nsym, (FCL)80);
  if (nsym <= 0) {
    LOG_WARN("JTTY transmit message could not be encoded");
    Q_EMIT jttyTextRejected(requestId, JttyTxRejectReason::EncodingFailed);
    return;
  }

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
  cursor.insertText(message);
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
  if(!m_diskData && (m_saveAll || m_saveDecoded) && (m_k0 > 59*384) && (m_k0 < 9999999)) {
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
  for(int k=3456; k<dec_data.params.kin; k+=3456) {
    jtty_decode(k);
  }
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
  int const istop = int(qMin(qint64(dec_data.params.kin),
                             qint64(istart0) + qint64(jttyPickSafetyCapSecs) * 12000));
  if (istop < istart0) return;   // clicked time is no longer in the buffer at all

  ui->DecodeButton->setChecked (true);
  qApp->processEvents();                                //Update the DecodeButton highlight
  for(int k=3456; k<dec_data.params.kin; k+=3456) {
    bool const eom = jtty_decode(k, istart0, istop);
    if (eom || k >= istop) break;
  }
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
    write_all("Rx", text, &line.context);
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
  sendJttyFunctionKey(functionKey);
  return true;
}

void MainWindow::sendJttyFunctionKey(int index)
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
  default: return;
  }

  auto const context = jttyNativeMacroContext(
    m_config, m_hisCall, ui->sbSerialNumber_2->value());
  auto const compiled=Jtty::compileNativeMacro(macro,context);
  if(compiled.status == Jtty::NativeMacroStatus::LiteralFallback) {
    jtty_tx(compiled.text);
    return;
  }

  qint64 const requestId=++m_jttyTxRequestId;
  if(compiled.status == Jtty::NativeMacroStatus::InvalidRuntime) {
    LOG_WARN(QStringLiteral("JTTY native macro rejected: %1").arg(compiled.error));
    Q_EMIT jttyTextRejected(requestId,JttyTxRejectReason::EncodingFailed);
    return;
  }

  int itone[944];
  int nsym=0;
  genjtty_atoms_c(compiled.atoms.constData(),compiled.atoms.size(),itone,&nsym);
  if(nsym <= 0) {
    LOG_WARN("JTTY native macro could not be encoded");
    Q_EMIT jttyTextRejected(requestId,JttyTxRejectReason::EncodingFailed);
    return;
  }
  execute_jtty_tones(requestId,compiled.text,itone,nsym);
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
  if (m_mode != "JTTY") {
    jtty_tx(message);
    return;
  }

  if (m_mmttyJttyFinishRequested) {
    logText(QStringLiteral("MMTTY/N1MM JTTY text ignored after graceful OFF"));
    return;
  }

  qint64 const requestId = ++m_jttyTxRequestId;
  m_mmttyJttyRequests.insert(requestId, message);
  execute_jtty_tx(requestId, message);
}

void MainWindow::handleMmttyStartTx()
{
  if (m_mode != "JTTY") {
    startTx2();
    return;
  }

  m_mmttyJttyFinishRequested = false;
  m_mmttyJttyStartRequested = true;
  startPendingMmttyJttyTx();
}

void MainWindow::handleMmttyStopTx()
{
  if (m_mode != "JTTY") {
    noteTxStopReason (TxEvidence::TxStopReason::UserHalt);
    stopTx();
    return;
  }

  m_mmttyJttyFinishRequested = true;
  m_mmttyJttyStartRequested = false;
  logText(QStringLiteral("MMTTY/N1MM JTTY OFF requested; waiting for backend drain"));
}

void MainWindow::handleMmttyAbortTx()
{
  m_mmttyJttyStartRequested = false;
  m_mmttyJttyFinishRequested = false;
  m_mmttyJttyOutputPending = false;
  m_mmttyJttyRequests.clear();
  abort_jtty_tx();
}

void MainWindow::handleMmttyJttyAccepted(qint64 requestId)
{
  if (!m_mmttyJttyRequests.contains(requestId)) return;

  logText(QStringLiteral("MMTTY/N1MM JTTY request %1 accepted").arg(requestId));
  m_mmttyJttyOutputPending = true;
  startPendingMmttyJttyTx();
}

void MainWindow::handleMmttyJttyRejected(qint64 requestId, JttyTxRejectReason reason)
{
  if (!m_mmttyJttyRequests.remove(requestId)) return;

  logText(QStringLiteral("MMTTY/N1MM JTTY request %1 rejected: %2")
          .arg(requestId)
          .arg(jttyRejectReasonText(reason)));
  if (m_mmttyJttyRequests.isEmpty()) {
    m_mmttyJttyStartRequested = false;
  }
}

void MainWindow::handleMmttyJttyCompleted(qint64 requestId)
{
  if (!m_mmttyJttyRequests.remove(requestId)) return;

  logText(QStringLiteral("MMTTY/N1MM JTTY request %1 completed").arg(requestId));
}

void MainWindow::handleMmttyJttySessionDrained(qint64 sessionId)
{
  Q_UNUSED(sessionId)
  m_mmttyJttyStartRequested = false;
  m_mmttyJttyFinishRequested = false;
  bool const reportOutputComplete = m_mmttyJttyOutputPending;
  m_mmttyJttyOutputPending = false;
  if (m_mmttyif && reportOutputComplete) {
    m_mmttyif->report_output_complete();
  }
}

void MainWindow::startPendingMmttyJttyTx()
{
  if (m_mode != "JTTY" || !m_mmttyJttyStartRequested) return;

  if (!m_jttyTxActive || jttyTxCommittedSamples () <= 0) {
    logText(QStringLiteral("MMTTY/N1MM JTTY start deferred until text is accepted"));
    return;
  }

  if (g_iptt == 1) {
    logText(QStringLiteral("MMTTY/N1MM JTTY start ignored; transmitter is already keyed"));
    m_mmttyJttyStartRequested = false;
    return;
  }

  m_mmttyJttyStartRequested = false;
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
