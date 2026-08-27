#include "Ft8TxLoopbackTestController.hpp"

#include "Audio/BWFFile.hpp"
#include "Audio/FixtureSoundOutput.hpp"
#include "widgets/mainwindow.h"

#include <cstdlib>
#include <iostream>
#include <utility>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QAudioFormat>
#include <QDateTime>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>

#include "moc_Ft8TxLoopbackTestController.cpp"

namespace
{
  constexpr int sampleRate = 48000;
  constexpr qint64 periodFrames = 15 * sampleRate;
  constexpr qint64 expectedFirstAudioFrame = sampleRate / 2;
  // Raised-cosine shaping can delay the first nonzero sample slightly.
  constexpr qint64 firstAudioToleranceFrames = sampleRate / 100;
  constexpr int bytesPerFrame = 2;
  constexpr qint64 periodMs = 15000;
  constexpr qint64 startTriggerOffsetMs = 25;
}

Ft8TxLoopbackTestController::Ft8TxLoopbackTestController (
  MainWindow * window, FixtureSoundOutput * output, QString capturePath,
  QObject * parent)
  : QObject {parent}
  , m_window {window}
  , m_output {output}
  , m_capturePath {std::move (capturePath)}
{
  m_timeout.setSingleShot (true);
  m_timeout.setInterval (60000);
  connect (&m_timeout, &QTimer::timeout, this, [this] {
    fail (tr ("Timed out after 60 seconds."));
  });

  m_prepareTimer.setSingleShot (true);
  connect (&m_prepareTimer, &QTimer::timeout,
           this, &Ft8TxLoopbackTestController::prepareWhenReady);

  m_startTimer.setSingleShot (true);
  m_startTimer.setTimerType (Qt::PreciseTimer);
  connect (&m_startTimer, &QTimer::timeout,
           this, &Ft8TxLoopbackTestController::startWhenScheduled);
  connect (m_window, &MainWindow::liveAudioTestFt8TransmitStartDecided,
           this, &Ft8TxLoopbackTestController::handleStartDecision);

  m_modalTimer.setInterval (100);
  connect (&m_modalTimer, &QTimer::timeout,
           this, &Ft8TxLoopbackTestController::checkForUnexpectedModal);

  connect (m_output, &FixtureSoundOutput::captureStarted,
           this, [this] (QString const& path) {
             ++m_captureStartCount;
             if (path != m_capturePath)
               {
                 fail (tr ("Synthetic output opened the wrong capture path: %1")
                       .arg (path));
                 return;
               }
             if (m_captureStartCount != 1 || m_output->restartCount () != 1)
               {
                 fail (tr ("The FT8 output stream did not start exactly once."));
                 return;
               }
             auto * autoButton =
               m_window->findChild<QAbstractButton *> ("autoButton");
             if (!autoButton)
               {
                 fail (tr ("The FT8 Auto control was not found."));
                 return;
               }
             if (autoButton->isChecked ()) autoButton->click ();
             m_autoDisabledDuringTransmit = !autoButton->isChecked ()
               && m_output->stopCount () == 0;
             if (!m_autoDisabledDuringTransmit)
               {
                 fail (tr ("Disabling Auto stopped the active FT8 transmission."));
               }
           });
  connect (m_output, &FixtureSoundOutput::nonSilentAudioStarted,
           this, [this] (qint64 frame) {
             if (m_firstNonSilentFrame >= 0)
               {
                 fail (tr ("Synthetic output reported non-silent FT8 audio more than once."));
                 return;
               }
             m_firstNonSilentFrame = frame;
           });
  connect (m_output, &FixtureSoundOutput::captureStopped,
           this, [this] (QString const& path, qint64 frames) {
             ++m_captureStopCount;
             m_capturedFrames = frames;
             if (path != m_capturePath || m_captureStopCount != 1)
               {
                 fail (tr ("The FT8 capture did not stop exactly once at the requested path."));
                 return;
               }
             maybeFinish ();
           });
  connect (m_output, &FixtureSoundOutput::captureFailed,
           this, [this] (QString const& reason) {
             fail (tr ("Synthetic audio output failed: %1").arg (reason));
           });
}

void Ft8TxLoopbackTestController::begin ()
{
  m_timeout.start ();
  m_modalTimer.start ();
  prepareWhenReady ();
}

void Ft8TxLoopbackTestController::prepareWhenReady ()
{
  if (m_finished || m_prepared) return;

  auto * ft8Action = m_window->findChild<QAction *> ("actionFT8");
  auto * txFrequency = m_window->findChild<QSpinBox *> ("TxFreqSpinBox");
  auto * tx6 = m_window->findChild<QLineEdit *> ("tx6");
  auto * txb6 = m_window->findChild<QAbstractButton *> ("txb6");
  auto * txFirst = m_window->findChild<QAbstractButton *> ("txFirstCheckBox");
  auto * autoButton = m_window->findChild<QAbstractButton *> ("autoButton");
  if (!ft8Action || !txFrequency || !tx6 || !txb6 || !txFirst || !autoButton)
    {
      fail (tr ("A required FT8 transmit GUI control was not found."));
      return;
    }
  if (!ft8Action->isEnabled () || !txb6->isEnabled () || !autoButton->isEnabled ())
    {
      m_prepareTimer.start (50);
      return;
    }

  ft8Action->trigger ();
  if (!ft8Action->isChecked ())
    {
      fail (tr ("The application did not enter FT8 mode."));
      return;
    }
  if (autoButton->isChecked ()) autoButton->click ();
  txFrequency->setValue (1500);
  tx6->setText (QStringLiteral ("CQ KA1ABC FN42"));
  QMetaObject::invokeMethod (tx6, "editingFinished", Qt::DirectConnection);
  txb6->click ();

  auto const scheduleNow = QDateTime::currentMSecsSinceEpoch ();
  auto const untilNextPeriod = periodMs - scheduleNow % periodMs;
  if (untilNextPeriod < 1000)
    {
      m_prepareTimer.start (static_cast<int> (untilNextPeriod + 50));
      return;
    }

  auto const nextPeriod = scheduleNow / periodMs + 1;
  txFirst->setChecked ((nextPeriod % 2) == 0);
  autoButton->click ();
  if (!autoButton->isChecked () || txFrequency->value () != 1500
      || tx6->text () != QStringLiteral ("CQ KA1ABC FN42"))
    {
      fail (tr ("Unable to configure the requested FT8 transmission."));
      return;
    }

  m_targetPeriodStartMs = nextPeriod * periodMs;
  auto const targetStart = m_targetPeriodStartMs + startTriggerOffsetMs;
  auto const startDelay = targetStart
    - QDateTime::currentMSecsSinceEpoch ();
  if (startDelay <= 0)
    {
      if (autoButton->isChecked ()) autoButton->click ();
      m_prepareTimer.start (50);
      return;
    }
  m_startTimer.start (static_cast<int> (startDelay));

  m_prepared = true;
  ++m_startAttemptCount;
  std::cerr << "WSJT-X FT8 TX loopback test: GUI ready, message=\"CQ KA1ABC FN42\" "
               "frequency=1500 next_period="
            << nextPeriod << " tx_first=" << txFirst->isChecked () << std::endl;
}

void Ft8TxLoopbackTestController::startWhenScheduled ()
{
  if (m_finished) return;

  m_startCallbackMs = QDateTime::currentMSecsSinceEpoch ();
  if (m_startCallbackMs < m_targetPeriodStartMs)
    {
      m_startTimer.start (static_cast<int> (
        m_targetPeriodStartMs - m_startCallbackMs));
      return;
    }

  auto * tx6 = m_window->findChild<QLineEdit *> ("tx6");
  auto * txb6 = m_window->findChild<QAbstractButton *> ("txb6");
  auto * autoButton = m_window->findChild<QAbstractButton *> ("autoButton");
  if (!tx6 || !txb6 || !autoButton)
    {
      fail (tr ("The FT8 transmit controls disappeared before transmission."));
      return;
    }
  tx6->setText (QStringLiteral ("CQ KA1ABC FN42"));
  QMetaObject::invokeMethod (tx6, "editingFinished", Qt::DirectConnection);
  txb6->click ();

  auto const request = m_window->startLiveAudioTestFt8Transmit (
    m_targetPeriodStartMs);
  if (request.session_id < 0 || request.generation < 0)
    {
      fail (tr ("Unable to start the synthetic FT8 transmission."));
      return;
    }
  m_startSessionId = request.session_id;
  m_startGeneration = request.generation;
}

void Ft8TxLoopbackTestController::handleStartDecision (
  qint64 sessionId, qint64 generation, qint64 targetPeriodStartMs,
  bool accepted, qint64 actualStartMs)
{
  if (m_finished || sessionId != m_startSessionId
      || generation != m_startGeneration
      || targetPeriodStartMs != m_targetPeriodStartMs)
    {
      return;
    }

  m_startDecidedMs = actualStartMs;
  if (accepted)
    {
      std::cerr << "WSJT-X FT8 TX loopback start: attempt="
                << m_startAttemptCount
                << " callback_offset_ms="
                << m_startCallbackMs - m_targetPeriodStartMs
                << " audio_decision_offset_ms="
                << m_startDecidedMs - m_targetPeriodStartMs << std::endl;
      return;
    }

  auto * autoButton = m_window->findChild<QAbstractButton *> ("autoButton");
  if (!autoButton)
    {
      fail (tr ("The FT8 Auto control disappeared before retry."));
      return;
    }
  if (autoButton->isChecked ()) autoButton->click ();
  std::cerr << "WSJT-X FT8 TX loopback retry: attempt="
            << m_startAttemptCount
            << " callback_offset_ms="
            << m_startCallbackMs - m_targetPeriodStartMs
            << " audio_decision_offset_ms="
            << m_startDecidedMs - m_targetPeriodStartMs << std::endl;
  m_prepared = false;
  m_prepareTimer.start (50);
}

void Ft8TxLoopbackTestController::maybeFinish ()
{
  if (m_finished || m_captureStopCount != 1) return;
  if (!m_prepared || m_captureStartCount != 1 || m_firstNonSilentFrame < 0
      || !m_autoDisabledDuringTransmit || m_output->restartCount () != 1
      || m_output->stopCount () != 1)
    {
      fail (tr ("The FT8 output lifecycle was not one start, non-silent transmission, and natural stop."));
      return;
    }
  if (qAbs (m_firstNonSilentFrame - expectedFirstAudioFrame)
      > firstAudioToleranceFrames)
    {
      fail (tr ("FT8 audio began at frame %1; expected %2 +/- %3 frames.")
            .arg (m_firstNonSilentFrame).arg (expectedFirstAudioFrame)
            .arg (firstAudioToleranceFrames));
      return;
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
  m_startTimer.stop ();
  m_modalTimer.stop ();
  std::cout << "WSJT-X FT8 TX loopback capture passed: restarts="
            << m_output->restartCount () << " stops=" << m_output->stopCount ()
            << " first_non_silent_frame=" << m_firstNonSilentFrame
            << " start_attempts=" << m_startAttemptCount
            << " callback_offset_ms="
            << m_startCallbackMs - m_targetPeriodStartMs
            << " audio_decision_offset_ms="
            << m_startDecidedMs - m_targetPeriodStartMs
            << " frames=" << m_capturedFrames
            << " capture=" << m_capturePath.toStdString () << std::endl;
  m_window->close ();
  QCoreApplication::exit (EXIT_SUCCESS);
}

bool Ft8TxLoopbackTestController::validateCapture (QString * error) const
{
  QFileInfo const info {m_capturePath};
  if (!info.isFile () || info.size () <= 0)
    {
      *error = tr ("The FT8 capture file was not created or is empty: %1")
        .arg (m_capturePath);
      return false;
    }
  BWFFile capture {QAudioFormat {}, m_capturePath};
  if (!capture.open (BWFFile::ReadOnly))
    {
      *error = tr ("Unable to reopen FT8 capture %1: %2")
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
      *error = tr ("The FT8 capture is not 48 kHz mono signed 16-bit little-endian PCM.");
      return false;
    }
  if (capture.size () != periodFrames * bytesPerFrame
      || m_capturedFrames != periodFrames)
    {
      *error = tr ("The FT8 capture must contain exactly %1 frames; file=%2 reported=%3.")
        .arg (periodFrames).arg (capture.size () / bytesPerFrame)
        .arg (m_capturedFrames);
      return false;
    }
  return true;
}

void Ft8TxLoopbackTestController::fail (QString const& reason)
{
  if (m_finished) return;
  m_finished = true;
  m_timeout.stop ();
  m_prepareTimer.stop ();
  m_startTimer.stop ();
  m_modalTimer.stop ();
  std::cerr << "WSJT-X FT8 TX loopback capture failed: " << reason.toStdString ()
            << " capture_starts=" << m_captureStartCount
            << " capture_stops=" << m_captureStopCount
            << " restarts=" << m_output->restartCount ()
            << " stops=" << m_output->stopCount ()
            << " start_attempts=" << m_startAttemptCount
            << " callback_offset_ms="
            << m_startCallbackMs - m_targetPeriodStartMs
            << " audio_decision_offset_ms="
            << m_startDecidedMs - m_targetPeriodStartMs
            << " frames=" << m_capturedFrames << std::endl;
  if (auto * modal = QApplication::activeModalWidget ()) modal->close ();
  m_window->close ();
  QCoreApplication::exit (EXIT_FAILURE);
}

void Ft8TxLoopbackTestController::checkForUnexpectedModal ()
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
