#include "WavDecodeTestController.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextEdit>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include "Audio/WavFile.hpp"

#include "widgets/mainwindow.h"

WavDecodeTestController::WavDecodeTestController (
    MainWindow * window, QString wavPath, QString expectedMessage, Mode mode, QObject * parent)
  : QObject {parent}
  , m_window {window}
  , m_mode {mode}
  , m_period {mode == Mode::Fst4 ? 15 : mode == Mode::Fst4w ? 120 : 60}
  , m_modeName {mode == Mode::Jt4 ? QStringLiteral ("JT4")
                : mode == Mode::Jt9 ? QStringLiteral ("JT9")
                : mode == Mode::Jt65 ? QStringLiteral ("JT65")
                : mode == Mode::Q65 ? QStringLiteral ("Q65")
                : mode == Mode::Fst4 ? QStringLiteral ("FST4") : QStringLiteral ("FST4W")}
  , m_wavPath {std::move (wavPath)}
  , m_expectedMessage {expectedMessage.simplified ()}
{
  m_timeout.setSingleShot (true);
  m_timeout.setInterval (m_mode == Mode::Fst4 || m_mode == Mode::Fst4w ? 360000 : 60000);
  connect (&m_timeout, &QTimer::timeout, this, [this] {
    finish (tr ("Timed out waiting for %1 WAV decoding.").arg (m_modeName));
  });
  // Windows dispatches coarse timers separately from the mode restore's precise timers.
  m_prepareTimer.setTimerType (Qt::PreciseTimer);
  m_prepareTimer.setSingleShot (true);
  connect (&m_prepareTimer, &QTimer::timeout,
           this, &WavDecodeTestController::prepareWhenReady);
  m_modalTimer.setInterval (100);
  connect (&m_modalTimer, &QTimer::timeout, this, [this] {
    if (auto * modal = QApplication::activeModalWidget ())
      finish (tr ("Unexpected modal window: %1").arg (modal->windowTitle ()));
  });
  connect (m_window, &MainWindow::decoderBackendFailed, this,
           [this] (QString const& reason) {
    finish (tr ("Decoder backend failed: %1").arg (reason));
  });
  connect (m_window, &MainWindow::decoderBackendStarted, this, [this] {
    if (m_stage == Stage::Restart && m_restartAborted)
      {
        m_stage = Stage::AfterRestart;
        QTimer::singleShot (0, this, [this] {
          if (m_finished) return;
          auto * decode = m_window->findChild<QAbstractButton *> ("DecodeButton");
          if (!decode || !decode->isEnabled () || m_window->decoderBusy ())
            {
              finish (tr ("%1 decoding did not resume after the decoder restart.").arg (m_modeName));
              return;
            }
          m_observed = false;
          m_awaitingCycle = true;
          decode->click ();
          if (!m_finished && !m_activeGeneration)
            finish (tr ("Manual decoding did not start after the decoder restart."));
        });
        return;
      }
    if (m_started) finish (tr ("Decoder backend restarted during the test."));
  });
  connect (m_window, &MainWindow::decodeCycleAborted, this, [this] (quint64 generation) {
    if (m_stage == Stage::Restart && !m_restartAborted && generation == m_activeGeneration)
      {
        m_restartAborted = true;
        m_lastGeneration = generation;
        m_activeGeneration = 0;
        return;
      }
    finish (tr ("%1 decode cycle was aborted.").arg (m_modeName));
  });
  connect (m_window, &MainWindow::decodeCycleStarted, this,
           [this] (quint64 generation) {
    if (m_finished) return;
    if (!m_awaitingCycle || m_activeGeneration || generation <= m_lastGeneration)
      {
        finish (tr ("Unexpected %1 decode cycle %2.").arg (m_modeName).arg (generation));
        return;
      }
    m_awaitingCycle = false;
    m_activeGeneration = generation;
    m_observed = false;
  });
  connect (m_window, &MainWindow::decodedMessageDisplayed, this,
           [this] (QString const& message) {
    if (m_activeGeneration && !expectsMessage ())
      {
        finish (tr ("A short silent %1 WAV displayed a stale decode: %2").arg (m_modeName, message));
        return;
      }
    auto const expectedDisplayedMessage = m_mode == Mode::Jt4
      ? m_expectedMessage + QStringLiteral (" f") : m_expectedMessage;
    if (m_activeGeneration && message.simplified () == expectedDisplayedMessage)
      {
        if (m_mode != Mode::Jt4 && m_mode != Mode::Jt9 && m_mode != Mode::Jt65)
          {
            auto * display = m_window->findChild<QTextEdit *> ("decodedTextBrowser");
            bool found = false;
            if (display)
              for (auto const& line : display->toPlainText ().split ('\n'))
                {
                  auto const text = line.simplified ();
                  if (!text.contains (m_expectedMessage)) continue;
                  if (text.section (' ', 0, 0) != (m_period < 60 ? QStringLiteral ("001500") : QStringLiteral ("0015")))
                    {
                      finish (tr ("Incorrect %1 UTC in displayed line: %2").arg (m_modeName, text));
                      return;
                    }
                  found = true;
                }
            if (!found)
              {
                finish (tr ("The %1 message was absent from the decode display.").arg (m_modeName));
                return;
              }
          }
        m_observed = true;
      }
  });
  connect (m_window, &MainWindow::decodeCycleCompleted,
           this, &WavDecodeTestController::completeCycle);
}

void WavDecodeTestController::begin ()
{
  if (!QFileInfo {m_wavPath}.isFile () || m_expectedMessage.isEmpty ())
    {
      finish (tr ("A WAV file and an expected message are required."));
      return;
    }
  m_timeout.start ();
  m_modalTimer.start ();
  prepareWhenReady ();
}

void WavDecodeTestController::prepareWhenReady ()
{
  if (m_finished || m_started) return;
  auto * mode = m_window->findChild<QAction *> (
      QStringLiteral ("action") + m_modeName);
  auto * quick = m_window->findChild<QAction *> ("actionQuickDecode");
  auto * submode = m_window->findChild<QSpinBox *> ("sbSubmode");
  auto * fast = m_window->findChild<QAbstractButton *> ("cbFast9");
  auto * cqOnly = m_window->findChild<QAbstractButton *> ("cbCQonly");
  auto * frequency = m_window->findChild<QSpinBox *> ("RxFreqSpinBox");
  auto const fixtureFrequency = m_mode == Mode::Jt4 ? 1000 : 1500;
  auto * decode = m_window->findChild<QAbstractButton *> ("DecodeButton");
  auto * rigStatus = m_window->findChild<QAbstractButton *> ("readFreq");
  auto * period = m_window->findChild<QSpinBox *> (m_mode == Mode::Fst4w ? "sbTR_FST4W" : "sbTR");
  if (!mode || !quick || !submode || !fast || !cqOnly || !frequency || !decode || !rigStatus || !period)
    {
      finish (tr ("A required %1 GUI control was not found.").arg (m_modeName));
      return;
    }
  // Rig startup can change the dial frequency and resume monitoring.
  if (rigStatus->property ("state").toString () != QStringLiteral ("ok")
      || !m_window->decoderBackendRunning () || m_window->decoderBusy ()
      || !mode->isEnabled ()
      || (!decode->isEnabled () && !(m_configured && m_mode == Mode::Fst4w)))
    {
      m_prepareTimer.start (50);
      return;
    }
  if (!m_configured)
    {
      mode->trigger ();
      quick->trigger ();
      submode->setValue (0);
      if (m_mode != Mode::Jt4 && m_mode != Mode::Jt9 && m_mode != Mode::Jt65) period->setValue (m_period);
      if (fast->isChecked ()) fast->click ();
      cqOnly->setChecked (false);
      if (m_mode == Mode::Fst4 || m_mode == Mode::Fst4w)
        {
          auto * blanker = m_window->findChild<QSpinBox *> ("sbNB");
          if (!blanker) { finish (tr ("The noise blanker control was not found.")); return; }
          blanker->setValue (0);
        }
      if (m_mode == Mode::Fst4)
        {
          auto * low = m_window->findChild<QSpinBox *> ("sbF_Low");
          auto * high = m_window->findChild<QSpinBox *> ("sbF_High");
          if (!low || !high) { finish (tr ("The FST4 search controls were not found.")); return; }
          low->setValue (1300);
          high->setValue (1700);
        }
      if (m_mode == Mode::Fst4w)
        {
          auto * receive = m_window->findChild<QSpinBox *> ("sbFST4W_RxFreq");
          if (!receive) { finish (tr ("The FST4W frequency control was not found.")); return; }
          receive->setValue (fixtureFrequency);
        }
      m_configured = true;
      m_prepareTimer.start (100);
      return;
    }
  if (!mode->isChecked () || !quick->isChecked ()
      || submode->value () != 0 || fast->isChecked ()
      || (m_mode != Mode::Jt4 && m_mode != Mode::Jt9 && m_mode != Mode::Jt65 && period->value () != m_period)
      || !m_window->configureLiveAudioTestDecodeRange ())
    {
      finish (tr ("Unable to configure ordinary %1A decoding.").arg (m_modeName));
      return;
    }
  frequency->setValue (fixtureFrequency);
  m_started = true;
  loadShortWav ();
}

void WavDecodeTestController::loadShortWav ()
{
  if (!m_fixtureDirectory.isValid ())
    {
      finish (tr ("Unable to create the short WAV fixture directory."));
      return;
    }
  auto const path = m_fixtureDirectory.filePath (m_stage == Stage::ShortFirst
      ? QStringLiteral ("261003_001000") : QStringLiteral ("261003_002000"));
  std::vector<short> const silence (12000, 0);
  auto const error = Radio::WavFile::save (path, silence.data (), int (silence.size ()),
      "K1ABC", "FN42", m_modeName, 0, 0, {}, {}, {});
  if (!error.isEmpty () || !m_window->startWavDecodeTest (path + ".wav"))
    {
      finish (tr ("Unable to load the short %1 WAV: %2").arg (m_modeName, error));
      return;
    }
  decodeShortWavWhenReady ();
}

void WavDecodeTestController::decodeShortWavWhenReady ()
{
  if (m_finished) return;
  auto * open = m_window->findChild<QAction *> ("actionOpen");
  auto * decode = m_window->findChild<QAbstractButton *> ("DecodeButton");
  if (!open || !decode)
    {
      finish (tr ("The WAV load controls were not found."));
      return;
    }
  if (!open->isEnabled ())
    {
      QTimer::singleShot (50, this, &WavDecodeTestController::decodeShortWavWhenReady);
      return;
    }
  if (!decode->isEnabled () || m_window->decoderBusy () || !m_window->diskDataActive ())
    {
      finish (tr ("The short %1 WAV did not become ready for manual decoding.").arg (m_modeName));
      return;
    }
  if (m_stage == Stage::ShortBeforeMonitoring)
    {
      if (!rejectsLiveRepeat ()) return;
      // A new WAV must not inherit the frequency of the previous live period.
      QKeyEvent qsy {QEvent::KeyPress, Qt::Key_F12,
                     Qt::ControlModifier | Qt::ShiftModifier};
      QApplication::sendEvent (m_window, &qsy);
      m_stage = Stage::ShortReplacement;
      loadShortWav ();
      return;
    }
  m_awaitingCycle = true;
  decode->click ();
  if (!m_finished && !m_activeGeneration)
    finish (tr ("Manual decoding did not start for the short %1 WAV.").arg (m_modeName));
}

bool WavDecodeTestController::rejectsLiveRepeat ()
{
  auto * monitor = m_window->findChild<QAbstractButton *> ("monitorButton");
  auto * decode = m_window->findChild<QAbstractButton *> ("DecodeButton");
  if (!monitor || !decode || !monitor->isEnabled () || monitor->isChecked ())
    {
      finish (tr ("%1 monitoring was not ready to resume after WAV loading.").arg (m_modeName));
      return false;
    }
  monitor->click ();
  if (!monitor->isChecked () || m_window->diskDataActive ()
      || (m_mode == Mode::Fst4w && decode->isEnabled ()))
    {
      finish (tr ("%1 monitoring did not restore live decode controls.").arg (m_modeName));
      return false;
    }
  m_window->statusBar ()->clearMessage ();
  QKeyEvent repeat {QEvent::KeyPress, Qt::Key_D, Qt::ShiftModifier};
  QApplication::sendEvent (m_window, &repeat);
  if (m_finished) return false;
  auto const expectedStatus = m_mode == Mode::Fst4w
    ? MainWindow::tr ("FST4W manual decoding is available only for WAV files.")
    : MainWindow::tr ("No completed %1 reception is available to decode again.").arg (m_modeName);
  if (m_window->decoderBusy () || m_activeGeneration || decode->isChecked ()
      || m_window->statusBar ()->currentMessage () != expectedStatus)
    {
      finish (tr ("%1 accepted a stale manual repeat after resuming live monitoring.").arg (m_modeName));
      return false;
    }
  monitor->click ();
  return true;
}

void WavDecodeTestController::completeCycle (quint64 generation)
{
  if (m_finished) return;
  if (!m_activeGeneration || generation != m_activeGeneration
      || (expectsMessage () && !m_observed))
    {
      finish (tr ("Cycle %1 did not display the expected message: %2")
              .arg (generation).arg (m_expectedMessage));
      return;
    }
  m_lastGeneration = generation;
  m_activeGeneration = 0;
  ++m_completedCycles;
  // Completion is emitted before MainWindow enables the Decode button.
  QTimer::singleShot (0, this, [this] {
    if (m_finished) return;
    auto * decode = m_window->findChild<QAbstractButton *> ("DecodeButton");
    if (m_window->decoderBusy () || !m_window->diskDataActive ()
        || !decode || !decode->isEnabled ())
      {
        finish (tr ("%1 decoding did not return to an idle disk-data state.").arg (m_modeName));
        return;
      }
    if (m_stage == Stage::ShortFirst)
      {
        m_stage = Stage::Initial;
        m_awaitingCycle = true;
        if (!m_window->startWavDecodeTest (m_wavPath))
          finish (tr ("Unable to start the %1 WAV load.").arg (m_modeName));
        return;
      }
    if (m_stage == Stage::Repeat && restartsDecoder ())
      {
        restartDuringDecode ();
        return;
      }
    if (m_stage == Stage::Repeat || m_stage == Stage::AfterRestart)
      {
        m_stage = m_mode == Mode::Jt4 || m_mode == Mode::Jt9 || m_mode == Mode::Jt65
          ? Stage::ShortReplacement : Stage::ShortBeforeMonitoring;
        loadShortWav ();
        return;
      }
    if (m_stage == Stage::ShortReplacement)
      {
        m_stage = Stage::ShortRepeat;
        m_awaitingCycle = true;
        decode->click ();
        return;
      }
    if (m_stage == Stage::ShortRepeat)
      {
        if (m_mode == Mode::Fst4w && !rejectsLiveRepeat ()) return;
        finish ();
        return;
      }
    if (m_mode != Mode::Jt4 && m_mode != Mode::Jt9 && m_mode != Mode::Jt65)
      {
        auto * period = m_window->findChild<QSpinBox *> (m_mode == Mode::Fst4w ? "sbTR_FST4W" : "sbTR");
        period->setValue (m_mode == Mode::Fst4w ? 300 : m_period == 15 ? 30 : 15);
        m_window->statusBar ()->clearMessage ();
        decode->click ();
        if (m_finished) return;
        if (m_window->decoderBusy () || m_activeGeneration || decode->isChecked ()
            || m_window->statusBar ()->currentMessage ()
                 != MainWindow::tr ("No completed %1 reception is available to decode again.").arg (m_modeName))
          {
            finish (tr ("%1 accepted a repeat with an incompatible reception period.").arg (m_modeName));
            return;
          }
        period->setValue (m_period);
      }
    m_stage = Stage::Repeat;
    m_observed = false;
    m_awaitingCycle = true;
    decode->click ();
  });
}

// A restart while the worker decodes must abort that cycle, relaunch the
// decoder, and leave decoding usable.
void WavDecodeTestController::restartDuringDecode ()
{
  auto * decode = m_window->findChild<QAbstractButton *> ("DecodeButton");
  m_stage = Stage::Restart;
  m_awaitingCycle = true;
  decode->click ();
  if (m_finished) return;
  if (!m_activeGeneration)
    {
      finish (tr ("Manual decoding did not start before the decoder restart."));
      return;
    }
  if (!m_window->liveAudioTestStallDecoder ())
    {
      finish (tr ("The decoder restart was refused."));
      return;
    }
  if (m_finished) return;
  if (!m_restartAborted || m_window->decoderBusy () || decode->isEnabled ())
    finish (tr ("The decoder restart did not abort the active %1 cycle.").arg (m_modeName));
}

void WavDecodeTestController::finish (QString const& error)
{
  if (m_finished) return;
  m_finished = true;
  m_succeeded = error.isEmpty () && m_completedCycles == (restartsDecoder () ? 6 : 5)
    && m_restartAborted == restartsDecoder ();
  m_timeout.stop ();
  m_prepareTimer.stop ();
  m_modalTimer.stop ();
  if (m_succeeded)
    std::cerr << "WSJT-X " << m_modeName.toStdString () << " WAV test passed: initial and repeat decode displayed "
              << m_expectedMessage.toStdString () << "; short initial, replacement, and repeat WAV decodes were silent"
              << (restartsDecoder () ? "; decoding resumed after a decoder restart aborted a cycle" : "")
              << std::endl;
  else
    std::cerr << "WSJT-X " << m_modeName.toStdString () << " WAV test failed: " << error.toStdString ()
              << " completed_cycles=" << m_completedCycles << std::endl;
  if (auto * modal = QApplication::activeModalWidget ()) modal->close ();
  m_window->close ();
  QCoreApplication::exit (m_succeeded ? EXIT_SUCCESS : EXIT_FAILURE);
}
