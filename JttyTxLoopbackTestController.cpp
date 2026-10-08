#include "JttyTxLoopbackTestController.hpp"

#include "Audio/BWFFile.hpp"
#include "Audio/FixtureSoundOutput.hpp"
#include "lib/jtty/JttyTransmit.hpp"
#include "widgets/mainwindow.h"
#include "wsjtx_config.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QAudioFormat>
#include <QByteArray>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMetaObject>
#include <QSysInfo>
#include <QTextCursor>
#include <QTextEdit>
#include <QWidget>

#include "moc_JttyTxLoopbackTestController.cpp"

namespace
{
  constexpr int sampleRate = 48000;
  constexpr int bytesPerFrame = 2;
}

JttyTxLoopbackTestController::JttyTxLoopbackTestController (
  MainWindow * window, FixtureSoundOutput * output, QString capturePath,
  QObject * parent)
  : QObject {parent}
  , m_window {window}
  , m_output {output}
  , m_capturePath {std::move (capturePath)}
{
  m_timeout.setSingleShot (true);
  m_timeout.setInterval (180000);
  connect (&m_timeout, &QTimer::timeout, this, [this] {
    fail (tr ("Timed out after 180 seconds."));
  });

  m_prepareTimer.setSingleShot (true);
  connect (&m_prepareTimer, &QTimer::timeout,
           this, &JttyTxLoopbackTestController::prepareWhenReady);

  m_modalTimer.setInterval (100);
  connect (&m_modalTimer, &QTimer::timeout,
           this, &JttyTxLoopbackTestController::checkForUnexpectedModal);

  connect (m_window, &MainWindow::jttyTextAccepted,
           this, [this] (qint64 requestId) {
             if (requestId == m_modeChangeProbeId)
               {
                 fail (tr ("The programmatic mode-change probe was accepted after abort."));
                 return;
               }
             if (m_sessionDrainCount)
               {
                 fail (tr ("A request submitted during PTT release started before cancellation."));
                 return;
               }
             if (m_acceptedRequests.contains (requestId))
               {
                 fail (tr ("JTTY request %1 was accepted more than once.")
                       .arg (requestId));
                 return;
             }
             m_acceptedRequests.insert (requestId);
             m_acceptedOrder.append (requestId);
             if (m_firstRequestId && requestId != m_firstRequestId) {
               m_secondRequestId = requestId;
             }
             maybeFinish ();
           });
  connect (m_window, &MainWindow::jttyTextRejected,
           this, [this] (qint64 requestId, MainWindow::JttyTxRejectReason reason) {
             if (m_submittingModeChangeProbe
                 && reason == MainWindow::JttyTxRejectReason::NotAvailable)
               {
                 m_modeChangeProbeUnavailable = true;
                 return;
               }
             if (requestId == m_modeChangeProbeId
                 && !m_modeChangeProbeComplete
                 && (reason == MainWindow::JttyTxRejectReason::Aborted
                     || (m_modeChangeProbeSwitching
                         && reason == MainWindow::JttyTxRejectReason::NotAvailable)))
               {
                 m_modeChangeProbeComplete = true;
                 m_prepareTimer.start (100);
                 return;
               }
             if (m_checkingCancellation
                 && reason == MainWindow::JttyTxRejectReason::Aborted
                 && !m_cancelledRequests.contains (requestId)
                 && !m_acceptedRequests.contains (requestId)
                 && !m_completedRequests.contains (requestId))
               {
                 m_cancelledRequests.insert (requestId);
                 return;
               }
             auto const send = m_window->findChild<QAbstractButton *> ("pbSendMessage");
             fail (tr ("JTTY request %1 was rejected with reason %2 (probe=%3, probe_started=%4, decoder_busy=%5). %6")
                   .arg (requestId).arg (static_cast<int> (reason))
                   .arg (m_modeChangeProbeId).arg (m_modeChangeProbeStarted)
                   .arg (m_window->decoderBusy ())
                   .arg (send ? send->toolTip () : QString {}));
           });
  connect (m_window, &MainWindow::jttyTextCompleted,
           this, [this] (qint64 requestId) {
             if (m_sessionDrainCount)
               {
                 fail (tr ("Cancelled JTTY work emitted a completion notification."));
                 return;
               }
             if (m_completedRequests.contains (requestId))
               {
                 fail (tr ("JTTY request %1 completed more than once.")
                       .arg (requestId));
                 return;
             }
             m_completedRequests.insert (requestId);
             m_completedOrder.append (requestId);
             maybeFinish ();
           });
  connect (m_window, &MainWindow::jttySessionDrained,
           this, [this] (qint64) {
             ++m_sessionDrainCount;
             if (m_sessionDrainCount > 1)
               {
                 fail (tr ("More than one JTTY transmit session drained."));
                 return;
               }
             if (!verifyCancellationPaths ()) return;
             maybeFinish ();
           });

  connect (m_output, &FixtureSoundOutput::captureStarted,
           this, [this] (QString const& path) {
             ++m_captureStartCount;
             if (path != m_capturePath)
               {
                 fail (tr ("Synthetic output opened the wrong capture path: %1")
                       .arg (path));
                 return;
               }
             if (m_captureStartCount > 1 || m_output->restartCount () != 1)
               {
                 fail (tr ("JTTY output stream restarted more than once."));
               }
           });
  connect (m_output, &FixtureSoundOutput::nonSilentAudioStarted,
           this, [this] (qint64 frame) {
             if (m_nonSilentAudioSeen)
               {
                 fail (tr ("Synthetic output reported non-silent playback more than once."));
                 return;
             }
             m_nonSilentAudioSeen = true;
             m_firstNonSilentFrame = frame;
           });
  connect (m_output, &FixtureSoundOutput::playbackCheckpoint,
           this, [this] (int captureNumber, qint64 frames) {
             if (m_finished) return;
             if (captureNumber != 1 || m_output->restartCount () != captureNumber
                 || m_secondRequestId || !m_nonSilentAudioSeen
                 || frames - m_firstNonSilentFrame < 72000)
               {
                 fail (tr ("Synthetic playback delivered a stale or invalid append checkpoint."));
                 return;
               }
             submitSecondMessage ();
           });
  connect (m_output, &FixtureSoundOutput::captureStopped,
           this, [this] (QString const& path, qint64 frames) {
             ++m_captureStopCount;
             m_capturedFrames = frames;
             if (path != m_capturePath)
               {
                 fail (tr ("Synthetic output stopped the wrong capture path: %1")
                       .arg (path));
                 return;
               }
             if (m_captureStopCount > 1)
               {
                 fail (tr ("JTTY output stream stopped more than once."));
                 return;
               }
             maybeFinish ();
           });
  connect (m_output, &FixtureSoundOutput::captureFailed,
           this, [this] (QString const& reason) {
             fail (tr ("Synthetic audio output failed: %1").arg (reason));
           });
}

QString JttyTxLoopbackTestController::contestExchangeMessage ()
{
  return QStringLiteral ("WB9XYZ 599 0123");
}

QStringList JttyTxLoopbackTestController::longMessageSegments ()
{
  // A one-shot Send commits this as a single continuous message (only its
  // very last transmit segment gets EOM), so the receiver accumulates it in
  // jtty_mdecode.f90's fixed character(len=80) decode buffer -- this must
  // stay at or under that width, or later content is silently dropped on
  // decode rather than making it into the receiver's displayed text.
  // Widening that receive-side buffer for genuinely long continuous
  // messages is tracked as a separate follow-up.
  return {
    QStringLiteral ("SECOND MESSAGE QUEUED DURING FIRST STAYS PENDING UNTIL ITS OWN AIRTIME ARRIVES")
  };
}

QString JttyTxLoopbackTestController::unsentDraft ()
{
  // No embedded space, so auto-advance's zero holdback never releases it early.
  return QStringLiteral ("UNSENTDRAFTMUSTSURVIVE");
}

qint64 JttyTxLoopbackTestController::encodedSampleFrames (QString const& message)
{
  int tones[2048];
  int symbols {0};
  QByteArray field (80, ' ');
  auto const bytes = message.toLatin1 ();
  if (bytes.size () > field.size ()) return 0;
  std::copy (bytes.cbegin (), bytes.cend (), field.begin ());
  char frames[Jtty::maxTransmitFrames * Jtty::transmitFrameBits];
  int frameStarts[Jtty::maxTransmitFrames];
  int nframes {0};
  int status {0};
  genjtty_text_c (field.data (), static_cast<int> (Jtty::NativeExchangeProfile::None), 1, tones,
                  &symbols, frames, &nframes, frameStarts, &status);
  return qint64 (symbols) * 384 * 4;
}

void JttyTxLoopbackTestController::begin ()
{
  m_timeout.start ();
  m_modalTimer.start ();
  // The startup splash screen installs an application-wide event filter that
  // swallows any Escape keypress for as long as it stays visible (up to a
  // real 20-second wall-clock timer, independent of AUDIO_SPEED), which would
  // intercept verifyCancellationPaths' synthetic Escape before it ever
  // reaches MainWindow::keyPressEvent. Close it immediately so this test's
  // correctness doesn't depend on outlasting that timer.
  QMetaObject::invokeMethod (m_window, "splash_done", Qt::DirectConnection);
  prepareWhenReady ();
}

void JttyTxLoopbackTestController::prepareWhenReady ()
{
  if (m_finished || m_prepared) return;

  auto * jttyAction = m_window->findChild<QAction *> ("actionJTTY");
  if (!jttyAction)
    {
      fail (tr ("The JTTY mode action was not found."));
      return;
    }
  if (!jttyAction->isEnabled ())
    {
      m_prepareTimer.start (50);
      return;
    }

  if (!m_modeChangeProbeStarted)
    {
      jttyAction->trigger ();
      m_submittingModeChangeProbe = true;
      auto const requestId = m_window->submitJttyText (
        QStringLiteral ("MODE CHANGE PROBE"));
      m_submittingModeChangeProbe = false;
      if (m_modeChangeProbeUnavailable)
        {
          m_modeChangeProbeUnavailable = false;
          m_prepareTimer.start (50);
          return;
        }
      m_modeChangeProbeId = requestId;
      m_modeChangeProbeStarted = true;
      QString controlsError;
      if (!verifyModeControlsEnabled (false, &controlsError))
        {
          fail (controlsError);
          return;
        }
      auto * ft8Action = m_window->findChild<QAction *> ("actionFT8");
      if (!ft8Action)
        {
          fail (tr ("The FT8 mode action was not found."));
          return;
        }
      m_modeChangeProbeSwitching = true;
      if (!QMetaObject::invokeMethod (
            m_window, "on_actionFT8_triggered", Qt::DirectConnection)
          || !ft8Action->isChecked ())
        {
          fail (tr ("The programmatic mode change did not enter FT8."));
          return;
        }
      return;
    }
  if (!m_modeChangeProbeComplete)
    {
      m_prepareTimer.start (50);
      return;
    }
  m_modeChangeProbeSwitching = false;
  if (m_window->decoderBusy ())
    {
      m_prepareTimer.start (50);
      return;
    }
  if (m_window->liveAudioTestJttyStreamActive ()
      || m_output->restartCount () != 0)
    {
      fail (tr ("The JTTY stream remained active after a programmatic mode change."));
      return;
    }

  jttyAction->trigger ();
  if (!jttyAction->isChecked ())
    {
      fail (tr ("The application did not enter JTTY mode."));
      return;
    }

  m_prepared = true;
  m_segmentEndFrames.append (encodedSampleFrames (contestExchangeMessage ()));
  qint64 longMessageFrames = 0;
  // Combined length is bounded by the receive-side decode buffer -- see the
  // comment on longMessageSegments().
  if (longMessageSegments ().join (' ').size () > 80)
    {
      fail (tr ("The long-message fixture exceeds the receive-side decode buffer width."));
      return;
    }
  for (auto const& segment : longMessageSegments ())
    {
      auto const frames = encodedSampleFrames (segment);
      if (segment.isEmpty () || segment.size () > 80 || frames <= 0)
        {
          fail (tr ("A long-message fixture segment has an invalid length or waveform extent."));
          return;
        }
      longMessageFrames += frames;
      m_segmentEndFrames.append (m_segmentEndFrames.constLast () + frames);
    }
  m_expectedAudioFrames = encodedSampleFrames (contestExchangeMessage ())
    + longMessageFrames;
  if (longMessageFrames <= 10 * sampleRate
      || m_expectedAudioFrames >= 150 * sampleRate)
    {
      fail (tr ("The JTTY encoder did not produce a valid test waveform extent."));
      return;
    }
  auto * display = m_window->findChild<QTextEdit *> ("decodedTextBrowser2");
  if (!display)
    {
      fail (tr ("The JTTY transmit display was not found."));
      return;
    }
  connect (display, &QTextEdit::textChanged,
           this, &JttyTxLoopbackTestController::observeTransmittedDisplay);
  if (!QMetaObject::invokeMethod (m_output, "configureJttyCapture", Qt::QueuedConnection,
                                 Q_ARG (qint64, m_expectedAudioFrames)))
    {
      fail (tr ("Unable to configure the synthetic JTTY capture extent."));
      return;
    }
  m_firstRequestId = m_window->submitJttyText (contestExchangeMessage ());
  if (m_firstRequestId <= 0)
    {
      fail (tr ("The first JTTY text request did not receive an identifier."));
      return;
    }

  QString controlsError;
  if (!verifyModeControlsEnabled (false, &controlsError))
    {
      fail (controlsError);
      return;
    }

  std::cerr << "WSJT-X JTTY TX loopback test: first request submitted, "
               "waiting for backend acceptance and real audio consumption"
            << std::endl;
}

void JttyTxLoopbackTestController::submitSecondMessage ()
{
  if (m_finished) return;
  if (!m_prepared || m_firstRequestId <= 0
      || !m_acceptedRequests.contains (m_firstRequestId))
    {
      fail (tr ("Non-silent playback began before the first request was accepted."));
      return;
    }
  if (m_captureStartCount != 1 || m_output->restartCount () != 1
      || m_output->stopCount () != 0)
    {
      fail (tr ("The output stream restarted or stopped before the gapless append."));
      return;
    }
  auto const consumedFrames = m_output->capturedFrames ();
  if (consumedFrames - m_firstNonSilentFrame < 72000
      || consumedFrames >= encodedSampleFrames (contestExchangeMessage ())
      || m_completedRequests.contains (m_firstRequestId))
    {
      fail (tr ("The gapless append missed its playback interval within the first message."));
      return;
    }

  auto * input = m_window->findChild<QPlainTextEdit *> ("Tx_Message");
  auto * send = m_window->findChild<QAbstractButton *> ("pbSendMessage");
  auto * display = m_window->findChild<QTextEdit *> ("decodedTextBrowser2");
  if (!input || !send || !display)
    {
      fail (tr ("A required JTTY input, send button, or transmit display was not found."));
      return;
    }
  input->setPlainText (longMessageSegments ().join (' '));
  send->click ();
  // Committed text stays visible (locked, not editable) rather than clearing.
  if (input->toPlainText () != longMessageSegments ().join (' '))
    {
      fail (tr ("The validated long JTTY draft was altered by submission."));
      return;
    }
  if (!send->text ().contains ("left")
      || !send->toolTip ().contains (longMessageSegments ().constLast ()))
    {
      fail (tr ("The Send button did not expose pending long-message text."));
      return;
    }
  if (display->toPlainText ().contains ("FIRST SEGMENT")
      || display->toPlainText ().contains ("ZEBRA"))
    {
      fail (tr ("Queued long-message text appeared as transmitted before its audio began."));
      return;
    }
  input->moveCursor (QTextCursor::End);
  input->insertPlainText (unsentDraft ());
  if (m_output->restartCount () != 1 || m_output->stopCount () != 0)
    {
      fail (tr ("Appending the second JTTY message restarted or stopped playback."));
      return;
    }

  std::cerr << "WSJT-X JTTY TX loopback test: long draft accepted through Enter "
               "during playback; segments=" << longMessageSegments ().size ()
            << " expected_audio_frames=" << m_expectedAudioFrames
            << " append_frame=" << consumedFrames
            << std::endl;
}

void JttyTxLoopbackTestController::observeTransmittedDisplay ()
{
  if (m_finished || !m_displayError.isEmpty ()) return;
  auto const * display = m_window->findChild<QTextEdit *> ("decodedTextBrowser2");
  auto const text = display->toPlainText ().simplified ();
  QStringList messages {contestExchangeMessage ()};
  messages.append (longMessageSegments ());
  qint64 startFrame = 0;
  for (int i = 0; i < messages.size (); ++i)
    {
      auto const endFrame = m_segmentEndFrames.at (i);
      if (i >= m_displayedFrames.size () && text.contains (messages.at (i)))
        {
          auto const frames = m_output->capturedFrames ();
          if (i != m_displayedFrames.size () || frames <= startFrame
              || frames >= endFrame)
            {
              m_displayError = tr ("JTTY transmit segment %1 appeared out of order or outside its playback interval (frame %2, interval %3-%4).")
                .arg (i).arg (frames).arg (startFrame).arg (endFrame);
              QTimer::singleShot (0, this, [this] { fail (m_displayError); });
              return;
            }
          m_displayedFrames.append (frames);
        }
      startFrame = endFrame;
    }
}

bool JttyTxLoopbackTestController::verifyCancellationPaths ()
{
  auto * input = m_window->findChild<QPlainTextEdit *> ("Tx_Message");
  auto * send = m_window->findChild<QAbstractButton *> ("pbSendMessage");
  if (!input || !send || !input->toPlainText ().endsWith (unsentDraft ()))
    {
      fail (tr ("The newer unsent draft was lost before cancellation checks."));
      return false;
    }

  for (bool const useEscape : {false, true})
    {
      input->moveCursor (QTextCursor::End);
      // Send is a one-shot commit that disarms auto-advance, so unlike the
      // armed (Alt+J) path, an explicit click is needed here to queue this chunk.
      QString const chunk = QStringLiteral (" ") + longMessageSegments ().join (' ')
        + QStringLiteral (" ");
      input->insertPlainText (chunk);
      send->click ();
      if (m_finished || !input->toPlainText ().endsWith (chunk)
          || !send->text ().contains ("left"))
        {
          fail (tr ("A new long draft was not retained while PTT release was pending."));
          return false;
        }
      input->moveCursor (QTextCursor::End);
      input->insertPlainText (unsentDraft ());
      auto const cancelledBefore = m_cancelledRequests.size ();
      m_checkingCancellation = true;
      if (useEscape)
        {
          QKeyEvent escape {QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
          QCoreApplication::sendEvent (m_window, &escape);
        }
      else if (!QMetaObject::invokeMethod (
                 m_window, "on_stopTxButton_clicked", Qt::DirectConnection))
        {
          fail (tr ("Unable to invoke Stop Tx for pending JTTY text."));
        }
      m_checkingCancellation = false;
      if (m_finished) return false;
      if (m_cancelledRequests.size () != cancelledBefore + 1
          || m_completedRequests.size () != 2 || m_acceptedRequests.size () != 2
          || !input->toPlainText ().endsWith (unsentDraft ())
          || send->text () != QStringLiteral ("Send message")
          || !send->toolTip ().contains ("cancelled"))
        {
          fail (tr ("%1 did not cancel pending text exactly once while preserving the newer draft.")
                .arg (useEscape ? QStringLiteral ("Esc") : QStringLiteral ("Stop Tx")));
          return false;
        }
    }
  if (m_output->restartCount () != 1)
    {
      fail (tr ("Cancelling deferred text restarted the captured audio stream."));
      return false;
    }
  m_cancellationChecked = true;
  return true;
}

void JttyTxLoopbackTestController::maybeFinish ()
{
  if (m_finished || !m_secondRequestId || m_sessionDrainCount != 1
      || m_captureStopCount != 1 || !m_cancellationChecked)
    {
      return;
    }

  QSet<qint64> const expectedRequests {m_firstRequestId, m_secondRequestId};
  if (!m_displayError.isEmpty ()
      || m_displayedFrames.size () != 1 + longMessageSegments ().size ())
    {
      fail (m_displayError.isEmpty ()
            ? tr ("The transmit display did not expose every segment during playback.")
            : m_displayError);
      return;
    }
  QVector<qint64> const expectedOrder {m_firstRequestId, m_secondRequestId};
  if (m_acceptedRequests != expectedRequests)
    {
      fail (tr ("The accepted-request set did not contain exactly both submitted messages."));
      return;
    }
  if (m_acceptedOrder != expectedOrder)
    {
      fail (tr ("JTTY text requests were not accepted in submission order."));
      return;
    }
  if (m_completedRequests != expectedRequests)
    {
      fail (tr ("The completed-request set did not contain exactly both submitted messages."));
      return;
    }
  if (m_completedOrder != expectedOrder)
    {
      fail (tr ("JTTY text requests did not complete in FIFO order."));
      return;
    }
  if (!m_nonSilentAudioSeen || m_captureStartCount != 1
      || m_output->restartCount () != 1 || m_output->stopCount () != 1)
    {
      fail (tr ("The output stream lifecycle was not one start, one drain, and one stop."));
      return;
    }
  if (m_output->maxInternalSilentFrames () > sampleRate / 100)
    {
      fail (tr ("The captured JTTY session contains an unexpected internal audio gap."));
      return;
    }

  auto const * input = m_window->findChild<QPlainTextEdit *> ("Tx_Message");
  auto const * send = m_window->findChild<QAbstractButton *> ("pbSendMessage");
  auto const * display = m_window->findChild<QTextEdit *> ("decodedTextBrowser2");
  if (!input || !input->toPlainText ().endsWith (unsentDraft ()))
    {
      fail (tr ("Finishing queued transmission changed the newer unsent draft."));
      return;
    }
  if (!send || send->text () != QStringLiteral ("Send message"))
    {
      fail (tr ("The Send button still reports pending text after the session drained."));
      return;
    }
  auto const displayed = display ? display->toPlainText ().simplified () : QString {};
  for (auto const& segment : longMessageSegments ())
    {
      if (!displayed.contains (segment))
        {
          fail (tr ("The transmit display omitted completed segment: %1").arg (segment));
          return;
        }
    }

  QString captureError;
  if (!validateCapture (&captureError))
    {
      fail (captureError);
      return;
    }

  m_finished = true;
  m_succeeded = true;
  m_timeout.stop ();
  m_prepareTimer.stop ();
  m_modalTimer.stop ();
  std::cout << "WSJT-X JTTY TX loopback capture passed: requests=2 restarts="
            << m_output->restartCount () << " stops=" << m_output->stopCount ()
            << " drains=" << m_sessionDrainCount
            << " cancellations=" << m_cancelledRequests.size ()
            << " frames=" << m_capturedFrames
            << " displayed_segments=" << m_displayedFrames.size ()
            << " capture=" << m_capturePath.toStdString () << std::endl;
  m_window->close ();
  QCoreApplication::exit (EXIT_SUCCESS);
}

bool JttyTxLoopbackTestController::verifyModeControlsEnabled (
  bool expected, QString * error) const
{
  QStringList const controlNames {
    QStringLiteral ("menuMode"), QStringLiteral ("houndButton"),
    QStringLiteral ("ft8Button"), QStringLiteral ("ft4Button"),
    QStringLiteral ("msk144Button"), QStringLiteral ("q65Button"),
    QStringLiteral ("jt65Button"), QStringLiteral ("echoButton")
  };
  for (auto const& name : controlNames)
    {
      auto * control = m_window->findChild<QWidget *> (name);
      if (!control)
        {
          *error = tr ("Mode control %1 was not found.").arg (name);
          return false;
        }
      if (control->isEnabled () != expected)
        {
          *error = tr ("Mode control %1 was unexpectedly %2.")
            .arg (name, control->isEnabled () ? tr ("enabled") : tr ("disabled"));
          return false;
        }
    }
  return true;
}

bool JttyTxLoopbackTestController::validateCapture (QString * error) const
{
  QFileInfo const info {m_capturePath};
  if (!info.isFile () || info.size () <= 0)
    {
      *error = tr ("The JTTY capture file was not created or is empty: %1")
        .arg (m_capturePath);
      return false;
    }

  BWFFile capture {QAudioFormat {}, m_capturePath};
  if (!capture.open (BWFFile::ReadOnly))
    {
      *error = tr ("Unable to reopen JTTY capture %1: %2")
        .arg (m_capturePath, capture.errorString ());
      return false;
    }
  auto const& format = capture.format ();
  if (format.codec () != QStringLiteral ("audio/pcm")
      || format.sampleRate () != sampleRate || format.channelCount () != 1
      || format.sampleSize () != 16
      || format.sampleType () != QAudioFormat::SignedInt
      || format.byteOrder () != QAudioFormat::LittleEndian)
    {
      *error = tr ("The JTTY capture is not 48 kHz mono signed 16-bit little-endian PCM.");
      return false;
    }
  qint64 const minimumCapturedFrames = m_expectedAudioFrames + sampleRate / 5;
  qint64 const maximumCapturedFrames = m_expectedAudioFrames + sampleRate / 2;
  if (capture.size () % bytesPerFrame
      || capture.size () / bytesPerFrame < minimumCapturedFrames
      || capture.size () / bytesPerFrame > maximumCapturedFrames
      || capture.size () / bytesPerFrame != m_capturedFrames)
    {
      *error = tr ("The JTTY capture has an invalid PCM extent: file frames=%1, expected audio frames=%2, reported frames=%3.")
        .arg (capture.size () / bytesPerFrame)
        .arg (m_expectedAudioFrames)
        .arg (m_capturedFrames);
      return false;
    }
  return true;
}

void JttyTxLoopbackTestController::fail (QString const& reason)
{
  if (m_finished) return;
  m_finished = true;
  m_timeout.stop ();
  m_prepareTimer.stop ();
  m_modalTimer.stop ();
  std::cerr << "WSJT-X JTTY TX loopback capture failed: "
            << reason.toStdString ()
            << " first_request=" << m_firstRequestId
            << " second_request=" << m_secondRequestId
            << " accepted=" << m_acceptedRequests.size ()
            << " completed=" << m_completedRequests.size ()
            << " capture_starts=" << m_captureStartCount
            << " capture_stops=" << m_captureStopCount
            << " restarts=" << m_output->restartCount ()
            << " stops=" << m_output->stopCount ()
            << " drains=" << m_sessionDrainCount
            << " frames=" << m_capturedFrames << std::endl;
  QTimer::singleShot (0, this, [this] {
    if (auto * modal = QApplication::activeModalWidget ()) modal->close ();
    m_window->close ();
    QCoreApplication::exit (EXIT_FAILURE);
  });
}

void JttyTxLoopbackTestController::checkForUnexpectedModal ()
{
  if (auto * modal = QApplication::activeModalWidget ())
    {
      auto detail = modal->windowTitle ();
      if (auto const * messageBox = qobject_cast<QMessageBox const *> (modal))
        {
          detail = messageBox->text ();
          if (!messageBox->informativeText ().isEmpty ())
            {
              detail += QStringLiteral (" ") + messageBox->informativeText ();
            }
        }
      fail (tr ("Unexpected modal window: %1").arg (detail));
    }
}
