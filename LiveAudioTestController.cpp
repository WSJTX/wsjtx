#include "LiveAudioTestController.hpp"

#include "Audio/FixtureAudioInput.hpp"
#include "DecoderIpc.hpp"
#include "Decoder/decodedtext.h"
#include "widgets/mainwindow.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMetaObject>
#include <QTextEdit>
#include <QTextStream>

#include "moc_LiveAudioTestController.cpp"

namespace
{
#if defined(WSJT_TSAN_TEST_PROFILE)
  constexpr int ft8TestThreadCount = 2;
  constexpr int ft8TestCycleCount = 1;
#else
  constexpr int ft8TestThreadCount = 4;
  constexpr int ft8TestCycleCount = 3;
#endif
}

LiveAudioTestController::LiveAudioTestController (
  MainWindow * window, FixtureAudioInput * fixture, QString expectedPath,
  Mode mode, QObject * parent)
  : QObject {parent}
  , m_window {window}
  , m_fixture {fixture}
  , m_expectedPath {std::move (expectedPath)}
  , m_mode {mode}
{
  if (Mode::Ft8 == m_mode)
    {
      m_expected = readExpectedMessages (m_expectedPath, &m_initializationError);
    }
  else
    {
      m_expectedJtty = readExpectedJttyMessages (
        m_expectedPath, &m_initializationError);
      for (auto& message : m_expectedJtty)
        {
          message = message.simplified ().toUpper ();
        }
      if (!m_expectedJtty.isEmpty ())
        {
          auto const& first = m_expectedJtty.constFirst ();
          auto const prefixLength = std::max (4, first.size () / 2);
          m_expectedJttyPrefix = first.left (
            std::min (prefixLength, std::max (0, first.size () - 1)));
        }
    }

  m_timeout.setSingleShot (true);
  m_timeout.setInterval (Mode::Ft8 == m_mode ? 110000 : 100000);
  connect (&m_timeout, &QTimer::timeout, this, [this] {
    fail (Mode::Ft8 == m_mode
          ? tr ("Timed out after 110 seconds.")
          : tr ("Timed out after 100 seconds."));
  });

  m_prepareTimer.setSingleShot (true);
  connect (&m_prepareTimer, &QTimer::timeout,
           this, &LiveAudioTestController::prepareWhenReady);

  m_modalTimer.setInterval (100);
  connect (&m_modalTimer, &QTimer::timeout,
           this, &LiveAudioTestController::checkForUnexpectedModal);

  m_jttyPollTimer.setInterval (50);
  connect (&m_jttyPollTimer, &QTimer::timeout,
           this, &LiveAudioTestController::pollJttyDisplay);

  connect (m_window, &MainWindow::decoderBackendStarted,
           this, &LiveAudioTestController::prepareWhenReady);
  connect (m_window, &MainWindow::decoderBackendFailed,
           this, [this] (QString const& reason) {
             if (Mode::Ft8 == m_mode)
               {
                 fail (tr ("Decoder backend failed: %1").arg (reason));
               }
           });
  connect (m_window, &MainWindow::decodedMessageProcessed,
           this, [this] (QString const& message) {
             if (message.isEmpty ()) return;
             auto const normalized = message.simplified ();
             m_observed.insert (normalized);
             if (m_decoderStage == DecoderStage::EarlyStandard)
               {
                 m_earlyObserved.insert (normalized);
               }
             else if (m_decoderStage == DecoderStage::Multithreaded)
               {
                 m_multithreadedObserved.insert (normalized);
               }
           });
  connect (m_window, &MainWindow::decodedMessageDisplayed,
           this, [this] (QString const& message) {
             if (!message.isEmpty ()) m_displayed.insert (message.simplified ());
           });
  connect (m_window, &MainWindow::decodeCycleCompleted,
           this, [this] (quint64) {
             ++m_completedCycles;
             if (m_decoderStage == DecoderStage::Multithreaded)
               {
                 m_completedMultithreadedDecode = true;
               }
             maybeFinish ();
             m_decoderStage = DecoderStage::None;
           });
  connect (m_window, &MainWindow::decoderOutputLine,
           this, [this] (QByteArray const& line) {
             auto const message = messageFromDecoderLine (line);
             if (message.isEmpty ()) return;
             if (m_decoderStage == DecoderStage::EarlyStandard)
               {
                 m_earlyRaw.insert (message);
               }
             else if (m_decoderStage == DecoderStage::Multithreaded)
               {
                 m_multithreadedRaw.insert (message);
               }
           });
  connect (m_window, &MainWindow::ft8DecoderInvocation,
           this, [this] (bool multithreaded, int threadCount, int depth,
                         int cycles, bool subpass, int decoderStart,
                         int halfSymbols, int sampleCount,
                         int lowFrequency, int highFrequency) {
             m_decoderStage = multithreaded
               ? DecoderStage::Multithreaded : DecoderStage::EarlyStandard;
             if (!multithreaded && halfSymbols == 41)
               {
                 m_sawEarlyStandardDecode = true;
               }
             if (multithreaded && threadCount == ft8TestThreadCount && depth == 3
                 && cycles == ft8TestCycleCount && subpass && decoderStart == 0
                 && halfSymbols == 49
                 && sampleCount == DecoderIpc::Ft8SampleCount
                 && lowFrequency == MainWindow::liveAudioTestDecodeLowFrequency ()
                 && highFrequency == MainWindow::liveAudioTestDecodeHighFrequency ())
               {
                 m_sawConfiguredMultithreadedDecode = true;
               }
             std::cerr << "WSJT-X live audio test: decoder invocation MTD="
                       << multithreaded << " threads=" << threadCount
                       << " depth=" << depth << " cycles=" << cycles
                       << " subpass=" << subpass << " start=" << decoderStart
                       << " half_symbols=" << halfSymbols
                       << " samples=" << sampleCount
                       << " nfa=" << lowFrequency
                       << " nfb=" << highFrequency << std::endl;
           });
  connect (m_fixture, &FixtureAudioInput::emissionStarted,
           this, [] (qint64 utcStartMilliseconds) {
             std::cerr << "WSJT-X live audio test: PCM emission started at UTC epoch "
                       << utcStartMilliseconds << " ms" << std::endl;
           });
  connect (m_fixture, &FixtureAudioInput::emissionFinished,
           this, [this] (qint64 frames) {
             m_emittedFrames = frames;
             auto const completionError = Mode::Ft8 == m_mode
               ? m_window->completeLiveAudioTestFt8Input (frames) : QString {};
             if (!completionError.isEmpty ())
               {
                 fail (completionError);
                 return;
               }
             m_fixtureFinished = true;
             maybeFinish ();
             if (Mode::Jtty == m_mode)
               {
                 QTimer::singleShot (8000, this, [this] {
                   if (!m_finished)
                     {
                       fail (tr ("JTTY display did not settle to the expected text after input ended."));
                     }
                 });
               }
           });
  if (Mode::Jtty == m_mode)
    {
      connect (m_window, &MainWindow::liveAudioTestJttyFramesConsumed,
               m_fixture, &FixtureAudioInput::acknowledgeJttyFrames,
               Qt::QueuedConnection);
    }
  connect (m_fixture, &AudioInputSource::error,
           this, [this] (QString const& reason) {
             fail (tr ("Synthetic audio source failed: %1").arg (reason));
           });
}

void LiveAudioTestController::begin ()
{
  m_timeout.start ();
  m_modalTimer.start ();
  if (!m_initializationError.isEmpty ())
    {
      fail (m_initializationError);
      return;
    }
  prepareWhenReady ();
}

QSet<QString> LiveAudioTestController::readExpectedMessages (
  QString const& path, QString * error)
{
  QFile file {path};
  if (!file.open (QIODevice::ReadOnly | QIODevice::Text))
    {
      *error = tr ("Unable to open expected decode file %1: %2")
        .arg (path, file.errorString ());
      return {};
    }

  QSet<QString> messages;
  QTextStream stream {&file};
  while (!stream.atEnd ())
    {
      auto const message = messageFromDecoderLine (stream.readLine ().toUtf8 ());
      if (!message.isEmpty ()) messages.insert (message);
    }

  if (messages.isEmpty ())
    {
      *error = tr ("Expected decode file %1 contains no messages.").arg (path);
    }
  return messages;
}

QStringList LiveAudioTestController::readExpectedJttyMessages (
  QString const& path, QString * error)
{
  QFile file {path};
  if (!file.open (QIODevice::ReadOnly | QIODevice::Text))
    {
      *error = tr ("Unable to open expected JTTY text file %1: %2")
        .arg (path, file.errorString ());
      return {};
    }

  QStringList messages;
  QTextStream stream {&file};
  while (!stream.atEnd ())
    {
      auto const line = stream.readLine ().simplified ();
      if (!line.isEmpty () && !line.startsWith ('#')) messages.append (line);
    }
  if (messages.isEmpty ())
    {
      *error = tr ("Expected JTTY text file %1 contains no messages.")
        .arg (path);
    }
  return messages;
}

QString LiveAudioTestController::messageFromDecoderLine (QByteArray const& rawLine)
{
  auto const line = QString::fromUtf8 (rawLine).simplified ();
  if (line.isEmpty () || line.startsWith ('<')) return {};
  auto const fields = line.split (' ', Qt::SkipEmptyParts);
  if (fields.size () < 5) return {};
  bool timeOk {false};
  bool snrOk {false};
  bool dtOk {false};
  bool frequencyOk {false};
  fields.at (0).toInt (&timeOk);
  fields.at (1).toInt (&snrOk);
  fields.at (2).toDouble (&dtOk);
  fields.at (3).toInt (&frequencyOk);
  if (!timeOk || !snrOk || !dtOk || !frequencyOk) return {};
  return DecodedText {QString::fromUtf8 (rawLine)}.message ().simplified ();
}

void LiveAudioTestController::prepareWhenReady ()
{
  if (Mode::Jtty == m_mode)
    {
      prepareJttyWhenReady ();
    }
  else
    {
      prepareFt8WhenReady ();
    }
}

void LiveAudioTestController::prepareFt8WhenReady ()
{
  if (m_finished || m_armed) return;
  if (!m_window->decoderBackendRunning ())
    {
      m_prepareTimer.start (50);
      return;
    }

  auto * ft8Action = m_window->findChild<QAction *> ("actionFT8");
  auto * deepAction = m_window->findChild<QAction *> ("actionDeepestDecode");
  auto * multithreadedAction =
    m_window->findChild<QAction *> ("actionUse_multithreaded_FT8_decoder");
  auto * threadCountAction = m_window->findChild<QAction *> (
    QStringLiteral ("actionMT%1").arg (ft8TestThreadCount));
  auto * cycleCountAction = m_window->findChild<QAction *> (
    QStringLiteral ("actionDecFT8cycles%1").arg (ft8TestCycleCount));
  auto * subpassAction = m_window->findChild<QAction *> ("actionFT8subpass");
  auto * twoStageAction =
    m_window->findChild<QAction *> ("actionStartTwoStage");
  auto * monitorButton = m_window->findChild<QAbstractButton *> ("monitorButton");
  auto * autoButton = m_window->findChild<QAbstractButton *> ("autoButton");
  if (!ft8Action || !deepAction || !multithreadedAction
      || !threadCountAction || !cycleCountAction || !subpassAction
      || !twoStageAction || !monitorButton || !autoButton)
    {
      fail (tr ("A required FT8 decoder or monitoring GUI control was not found."));
      return;
    }
  if (!monitorButton->isEnabled ())
    {
      m_prepareTimer.start (50);
      return;
    }

  ft8Action->trigger ();
  deepAction->setChecked (true);
  if (!multithreadedAction->isChecked ()) multithreadedAction->trigger ();
  threadCountAction->trigger ();
  cycleCountAction->trigger ();
  subpassAction->setChecked (true);
  twoStageAction->setChecked (true);
  if (!m_window->configureLiveAudioTestDecodeRange ())
    {
      fail (tr ("Unable to configure the %1-%2 Hz waterfall decode range.")
            .arg (MainWindow::liveAudioTestDecodeLowFrequency ())
            .arg (MainWindow::liveAudioTestDecodeHighFrequency ()));
      return;
    }
  if (!m_window->prepareLiveAudioTestFt8InputCompletion ())
    {
      fail (tr ("Unable to prepare the complete FT8 fixture decode."));
      return;
    }

  bool const decoderConfigurationMatches =
    m_window->liveAudioTestMultithreadedFt8Enabled ()
    && m_window->liveAudioTestFt8ThreadCount () == ft8TestThreadCount
    && m_window->liveAudioTestDecodeDepth () == 3
    && m_window->liveAudioTestFt8Cycles () == ft8TestCycleCount
    && m_window->liveAudioTestFt8Sensitivity () == 3
    && m_window->liveAudioTestFt8DecoderStart () == 0;
  if (!decoderConfigurationMatches)
    {
      fail (tr ("Unable to configure the FT8 multithreaded decoder as requested."));
      return;
    }

  if (autoButton->isChecked ()) autoButton->click ();
  if (!monitorButton->isChecked ()) monitorButton->click ();
  if (!m_window->monitoringActive ())
    {
      fail (tr ("Monitor did not enter the active state."));
      return;
    }
  if (m_window->diskDataActive ())
    {
      fail (tr ("Synthetic input unexpectedly selected the disk-data path."));
      return;
    }

  m_armed = QMetaObject::invokeMethod (
    m_fixture, "arm", Qt::QueuedConnection);
  if (!m_armed)
    {
      fail (tr ("Unable to arm the synthetic audio source."));
      return;
    }
  std::cerr << "WSJT-X live audio test: GUI ready, FT8 monitoring active, "
            << "MTD=1 threads=" << ft8TestThreadCount
            << " depth=3 cycles=" << ft8TestCycleCount
            << " sensitivity=3 start=0 decode_range="
            << MainWindow::liveAudioTestDecodeLowFrequency () << '-'
            << MainWindow::liveAudioTestDecodeHighFrequency ()
            << std::endl;
}

void LiveAudioTestController::prepareJttyWhenReady ()
{
  if (m_finished || m_armed) return;

  auto * jttyAction = m_window->findChild<QAction *> ("actionJTTY");
  auto * monitorButton = m_window->findChild<QAbstractButton *> ("monitorButton");
  m_jttyAllDecodes = m_window->findChild<QTextEdit *> ("decodedTextBrowser");
  m_jttyQsoFrequency = m_window->findChild<QTextEdit *> ("decodedTextBrowser2");
  if (!jttyAction || !monitorButton || !m_jttyAllDecodes || !m_jttyQsoFrequency)
    {
      fail (tr ("A required JTTY mode, monitoring, or decode display control was not found."));
      return;
    }
  if (!jttyAction->isEnabled () || !monitorButton->isEnabled ())
    {
      m_prepareTimer.start (50);
      return;
    }

  jttyAction->trigger ();
  if (!jttyAction->isChecked ())
    {
      fail (tr ("The JTTY GUI action did not select JTTY mode."));
      return;
    }
  if (!monitorButton->isChecked ()) monitorButton->click ();
  if (!m_window->monitoringActive ())
    {
      fail (tr ("Monitor did not enter the active state for JTTY."));
      return;
    }
  if (m_window->diskDataActive ())
    {
      fail (tr ("Synthetic JTTY input unexpectedly selected the disk-data path."));
      return;
    }

  m_armed = QMetaObject::invokeMethod (
    m_fixture, "arm", Qt::QueuedConnection);
  if (!m_armed)
    {
      fail (tr ("Unable to arm the synthetic JTTY audio source."));
      return;
    }
  m_jttyPollTimer.start ();
  std::cerr << "WSJT-X JTTY live audio test: GUI ready, monitoring active, "
            << "expected=\"" << m_expectedJtty.join (QStringLiteral (" | ")).toStdString () << "\""
            << std::endl;
}

void LiveAudioTestController::maybeFinish ()
{
  if (Mode::Jtty == m_mode)
    {
      maybeFinishJtty ();
    }
  else
    {
      maybeFinishFt8 ();
    }
}

void LiveAudioTestController::maybeFinishFt8 ()
{
  if (m_finished || !m_fixtureFinished || m_window->decoderBusy ()
      || !m_completedMultithreadedDecode)
    {
      return;
    }

  if (!m_sawEarlyStandardDecode || !m_sawConfiguredMultithreadedDecode)
    {
      fail (tr ("The expected early standard and final configured MTD decoder "
                "invocations were not both observed."));
      return;
    }
  if (m_multithreadedRaw.isEmpty ())
    {
      fail (tr ("The configured MTD invocation produced no decoder output."));
      return;
    }

  QStringList missingFromMultithreadedDecode;
  QStringList missingAfterProcessing;
  QStringList missingFromDisplay;
  for (auto const& message : m_expected)
    {
      if (!m_multithreadedRaw.contains (message))
        {
          missingFromMultithreadedDecode.append (message);
        }
      if (!m_observed.contains (message)) missingAfterProcessing.append (message);
      if (!m_displayed.contains (message)) missingFromDisplay.append (message);
    }
  missingFromMultithreadedDecode.sort ();
  missingAfterProcessing.sort ();
  missingFromDisplay.sort ();
  if (!missingFromMultithreadedDecode.isEmpty ()
      || !missingAfterProcessing.isEmpty () || !missingFromDisplay.isEmpty ())
    {
      QStringList failures;
      if (!missingFromMultithreadedDecode.isEmpty ())
        {
          failures.append (tr ("live MTD missing: %1")
                           .arg (missingFromMultithreadedDecode.join (" | ")));
        }
      if (!missingAfterProcessing.isEmpty ())
        {
          failures.append (tr ("processed missing: %1")
                           .arg (missingAfterProcessing.join (" | ")));
        }
      if (!missingFromDisplay.isEmpty ())
        {
          failures.append (tr ("display missing: %1")
                           .arg (missingFromDisplay.join (" | ")));
        }
      auto multithreaded = m_multithreadedRaw.values ();
      auto observed = m_observed.values ();
      multithreaded.sort ();
      observed.sort ();
      fail (tr ("Reference-message validation failed: %1; live MTD output: %2; "
                "processed output: %3")
            .arg (failures.join ("; "))
            .arg (multithreaded.join (" | "))
            .arg (observed.join (" | ")));
      return;
    }
  if (m_displayed.isEmpty ())
    {
      fail (tr ("Decoder messages were processed but none reached the GUI display path."));
      return;
    }

  m_finished = true;
  m_succeeded = true;
  m_timeout.stop ();
  m_modalTimer.stop ();
  std::cout << "WSJT-X live audio test passed: expected=" << m_expected.size ()
            << " matched_expected=" << m_expected.size ()
            << " observed=" << m_observed.size ()
            << " displayed=" << m_displayed.size ()
            << " raw_early=" << m_earlyRaw.size ()
            << " raw_mtd=" << m_multithreadedRaw.size ()
            << " frames=" << m_emittedFrames
            << " decode_cycles=" << m_completedCycles << std::endl;
  m_window->close ();
  QCoreApplication::exit (EXIT_SUCCESS);
}

void LiveAudioTestController::pollJttyDisplay ()
{
  if (m_finished || !m_jttyAllDecodes || !m_jttyQsoFrequency) return;

  auto const allText = m_jttyAllDecodes->toPlainText ().simplified ().toUpper ();
  auto const qsoText = m_jttyQsoFrequency->toPlainText ().simplified ().toUpper ();
  for (auto const& message : m_expectedJtty)
    {
      if (allText.contains (message)) m_jttyAllFinals.insert (message);
      if (qsoText.contains (message)) m_jttyQsoFinals.insert (message);
    }

  if (m_jttyAllFinals.isEmpty () && allText.contains (m_expectedJttyPrefix))
    {
      m_jttyAllSawPrefix = true;
    }
  if (m_jttyQsoFinals.isEmpty () && qsoText.contains (m_expectedJttyPrefix))
    {
      m_jttyQsoSawPrefix = true;
    }
  maybeFinishJtty ();
}

void LiveAudioTestController::maybeFinishJtty ()
{
  if (m_finished || !m_fixtureFinished
      || m_jttyAllFinals.size () != m_expectedJtty.size ()
      || m_jttyQsoFinals.size () != m_expectedJtty.size ())
    {
      return;
    }
  auto const messagesAppearInOrder = [this] (QString const& text) {
    int offset = 0;
    for (auto const& message : m_expectedJtty)
      {
        auto const index = text.indexOf (message, offset);
        if (index < 0) return false;
        offset = index + message.size ();
      }
    return true;
  };
  auto const allText = m_jttyAllDecodes->toPlainText ().simplified ().toUpper ();
  auto const qsoText = m_jttyQsoFrequency->toPlainText ().simplified ().toUpper ();
  if (!messagesAppearInOrder (allText) || !messagesAppearInOrder (qsoText))
    {
      fail (tr ("The expected JTTY messages did not reach both panes in FIFO order."));
      return;
    }
  if (m_expectedJtty.size () == 1
      && (!m_jttyAllSawPrefix || !m_jttyQsoSawPrefix))
    {
      fail (tr ("The expected JTTY message reached both panes without an observed growing prefix."));
      return;
    }

  m_finished = true;
  m_succeeded = true;
  m_timeout.stop ();
  m_modalTimer.stop ();
  m_jttyPollTimer.stop ();
  std::cout << "WSJT-X JTTY live audio test passed: expected="
            << m_expectedJtty.size ()
            << " all_prefix=" << m_jttyAllSawPrefix
            << " qso_prefix=" << m_jttyQsoSawPrefix
            << " frames=" << m_emittedFrames << std::endl;
  m_window->close ();
  QCoreApplication::exit (EXIT_SUCCESS);
}

void LiveAudioTestController::fail (QString const& reason)
{
  if (m_finished) return;
  m_finished = true;
  m_timeout.stop ();
  m_prepareTimer.stop ();
  m_modalTimer.stop ();
  std::cerr << "WSJT-X live audio test failed: " << reason.toStdString ()
            << " expected=" << m_expected.size ()
            << " observed=" << m_observed.size ()
            << " displayed=" << m_displayed.size ()
            << " frames=" << m_emittedFrames
            << " decode_cycles=" << m_completedCycles << std::endl;
  if (Mode::Ft8 == m_mode)
    {
      std::cerr << "WSJT-X live audio test: FT8 backpressure: "
                << m_window->liveAudioTestFt8BackpressureDiagnostics ().toStdString ()
                << std::endl;
    }
  auto early = m_earlyObserved.values ();
  auto multithreaded = m_multithreadedObserved.values ();
  auto earlyRaw = m_earlyRaw.values ();
  auto multithreadedRaw = m_multithreadedRaw.values ();
  early.sort ();
  multithreaded.sort ();
  earlyRaw.sort ();
  multithreadedRaw.sort ();
  std::cerr << "WSJT-X live audio test: early messages: "
            << early.join (" | ").toStdString () << std::endl;
  std::cerr << "WSJT-X live audio test: MTD messages: "
            << multithreaded.join (" | ").toStdString () << std::endl;
  std::cerr << "WSJT-X live audio test: raw early messages: "
            << earlyRaw.join (" | ").toStdString () << std::endl;
  std::cerr << "WSJT-X live audio test: raw MTD messages: "
            << multithreadedRaw.join (" | ").toStdString () << std::endl;
  if (Mode::Jtty == m_mode && m_jttyAllDecodes && m_jttyQsoFrequency)
    {
      std::cerr << "WSJT-X JTTY live audio test: All Decodes text: "
                << m_jttyAllDecodes->toPlainText ().simplified ().toStdString ()
                << std::endl;
      std::cerr << "WSJT-X JTTY live audio test: QSO Frequency text: "
                << m_jttyQsoFrequency->toPlainText ().simplified ().toStdString ()
                << std::endl;
    }
  if (auto * modal = QApplication::activeModalWidget ()) modal->close ();
  m_window->close ();
  QCoreApplication::exit (EXIT_FAILURE);
}

void LiveAudioTestController::checkForUnexpectedModal ()
{
  if (auto * modal = QApplication::activeModalWidget ())
    {
      fail (tr ("Unexpected modal window: %1").arg (modal->windowTitle ()));
    }
}
