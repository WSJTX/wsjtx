#include "JttyTxLoopbackTestController.hpp"

#include "Audio/BWFFile.hpp"
#include "Audio/FixtureSoundOutput.hpp"
#include "widgets/mainwindow.h"
#include "wsjtx_config.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>

#include <QAction>
#include <QApplication>
#include <QAudioFormat>
#include <QByteArray>
#include <QFileInfo>
#include <QMessageBox>
#include <QSysInfo>

#include "moc_JttyTxLoopbackTestController.cpp"

extern "C" void genjtty_ (char const * message, int tones[], int * symbols,
                            fortran_charlen_t);

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
  m_timeout.setInterval (60000);
  connect (&m_timeout, &QTimer::timeout, this, [this] {
    fail (tr ("Timed out after 60 seconds."));
  });

  m_prepareTimer.setSingleShot (true);
  connect (&m_prepareTimer, &QTimer::timeout,
           this, &JttyTxLoopbackTestController::prepareWhenReady);

  m_modalTimer.setInterval (100);
  connect (&m_modalTimer, &QTimer::timeout,
           this, &JttyTxLoopbackTestController::checkForUnexpectedModal);

  connect (m_window, &MainWindow::jttyTextAccepted,
           this, [this] (qint64 requestId) {
             if (m_acceptedRequests.contains (requestId))
               {
                 fail (tr ("JTTY request %1 was accepted more than once.")
                       .arg (requestId));
                 return;
             }
             m_acceptedRequests.insert (requestId);
             m_acceptedOrder.append (requestId);
             maybeFinish ();
           });
  connect (m_window, &MainWindow::jttyTextRejected,
           this, [this] (qint64 requestId, MainWindow::JttyTxRejectReason reason) {
             fail (tr ("JTTY request %1 was rejected with reason %2.")
                   .arg (requestId).arg (static_cast<int> (reason)));
           });
  connect (m_window, &MainWindow::jttyTextCompleted,
           this, [this] (qint64 requestId) {
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
             QTimer::singleShot (1500, this, [this] {
               submitSecondMessage ();
             });
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

QString JttyTxLoopbackTestController::adjacentStructuredFramesMessage ()
{
  return QStringLiteral ("WB9XYZ TU CQ KA1ABC CQ");
}

qint64 JttyTxLoopbackTestController::encodedSampleFrames (QString const& message)
{
  int tones[2048];
  int symbols {0};
  QByteArray field (80, ' ');
  auto const bytes = message.toLatin1 ();
  std::copy (bytes.cbegin (), bytes.cend (), field.begin ());
  genjtty_ (field.constData (), tones, &symbols, 80);
  return qint64 (symbols) * 384 * 4;
}

void JttyTxLoopbackTestController::begin ()
{
  m_timeout.start ();
  m_modalTimer.start ();
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

  jttyAction->trigger ();
  if (!jttyAction->isChecked ())
    {
      fail (tr ("The application did not enter JTTY mode."));
      return;
    }

  m_prepared = true;
  m_expectedAudioFrames = encodedSampleFrames (contestExchangeMessage ())
    + encodedSampleFrames (adjacentStructuredFramesMessage ());
  if (m_expectedAudioFrames <= 0)
    {
      fail (tr ("The JTTY encoder did not produce a valid test waveform extent."));
      return;
    }
  m_firstRequestId = m_window->submitJttyText (contestExchangeMessage ());
  if (m_firstRequestId <= 0 || !m_acceptedRequests.contains (m_firstRequestId))
    {
      fail (tr ("The first JTTY text request was not accepted synchronously."));
      return;
    }

  std::cerr << "WSJT-X JTTY TX loopback test: first request accepted, "
               "waiting for real audio consumption"
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
  if (m_output->capturedFrames () - m_firstNonSilentFrame < sampleRate)
    {
      fail (tr ("The gapless append occurred before one second of real playback."));
      return;
    }

  m_secondRequestId = m_window->submitJttyText (
    adjacentStructuredFramesMessage ());
  if (m_secondRequestId <= 0 || m_secondRequestId == m_firstRequestId
      || !m_acceptedRequests.contains (m_secondRequestId))
    {
      fail (tr ("The second JTTY text request was not accepted during playback."));
      return;
    }
  if (m_output->restartCount () != 1 || m_output->stopCount () != 0)
    {
      fail (tr ("Appending the second JTTY message restarted or stopped playback."));
      return;
    }

  std::cerr << "WSJT-X JTTY TX loopback test: second request accepted after "
               "non-silent playback began"
            << std::endl;
}

void JttyTxLoopbackTestController::maybeFinish ()
{
  if (m_finished || !m_secondRequestId || m_sessionDrainCount != 1
      || m_captureStopCount != 1)
    {
      return;
    }

  QSet<qint64> const expectedRequests {m_firstRequestId, m_secondRequestId};
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
            << " frames=" << m_capturedFrames
            << " capture=" << m_capturePath.toStdString () << std::endl;
  m_window->close ();
  QCoreApplication::exit (EXIT_SUCCESS);
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
  if (auto * modal = QApplication::activeModalWidget ()) modal->close ();
  m_window->close ();
  QCoreApplication::exit (EXIT_FAILURE);
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
