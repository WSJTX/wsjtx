#include "LiveAudioTestController.hpp"

#include "Audio/FixtureAudioInput.hpp"
#include "Audio/BWFFile.hpp"
#include "DecoderIpc.hpp"
#include "Decoder/decodedtext.h"
#include "widgets/JttyMessages.hpp"
#include "widgets/mainwindow.h"

extern dec_data_t& dec_data;

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <utility>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QAudioFormat>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QLabel>
#include <QProgressBar>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStatusBar>
#include <QEvent>
#include <QMetaObject>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>

#include "moc_LiveAudioTestController.cpp"

namespace
{
  QTextBlock findJttyMessageBlock (QTextDocument const& document, QString const& message)
  {
    // Display wrapping inserts paragraph breaks that QTextDocument::find cannot cross.
    auto const offset = document.toPlainText ().indexOf (
      Jtty::wrapMessage (message), 0, Qt::CaseInsensitive);
    return offset < 0 ? QTextBlock {} : document.findBlock (offset);
  }

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
  if (Mode::Jtty != m_mode)
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
  m_timeout.setInterval (Mode::Jtty != m_mode ? 110000 : 200000);
  connect (&m_timeout, &QTimer::timeout, this, [this] {
    fail (Mode::Jtty != m_mode
          ? tr ("Timed out after 110 seconds.")
          : tr ("Timed out waiting for JTTY audio or decoded text."));
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

  m_jttyStageTimer.setSingleShot (true);
  m_jttyStageTimer.setInterval (20000);
  connect (&m_jttyStageTimer, &QTimer::timeout, this, [this] {
    if (m_window->decoderBusy ())
      {
        m_jttyStageTimer.start ();
        return;
      }
    auto const stage = m_jttyCheckStage;
    if (stage == JttyCheckStage::Live)
      maybeFinishJtty ();
    else
      checkJttyReviewAndStatus ();
    if (!m_finished && m_jttyCheckStage == stage)
      fail (tr ("JTTY post-input checks did not advance for 20 seconds."));
  });

  connect (m_window, &MainWindow::decoderBackendStarted,
           this, &LiveAudioTestController::prepareWhenReady);
  connect (m_window, &MainWindow::decoderBackendStarted,
           this, [this] {
             if (m_submittedGeneration || m_ft8LastPublication)
               fail (tr ("Decoder backend restarted after FT8 publication."));
           });
  connect (m_window, &MainWindow::decodeCycleStarted,
           this, [this] (quint64 cycle) {
             if (schedulingScenario ())
               {
                 if (!m_armed || m_ft8ActiveCycle || m_completedCycles != m_ft8Invocations.size ())
                   fail (tr ("FT8 scheduling started an overlapping or unexpected cycle."));
                 m_ft8ActiveCycle = cycle;
               }
             else if (m_submittedGeneration)
               fail (tr ("Unexpected decoder cycle %1 after FT8 publication %2.")
                     .arg (cycle).arg (m_submittedGeneration));
           });
  connect (m_window, &MainWindow::decoderBackendFailed,
           this, [this] (QString const& reason) {
             if (Mode::Jtty != m_mode)
               {
                 fail (tr ("Decoder backend failed: %1").arg (reason));
               }
           });
  connect (m_window, &MainWindow::decodedMessageProcessed,
           this, [this] (QString const& message) {
             if (message.isEmpty ()) return;
             auto const normalized = message.simplified ();
             if (auto * invocation = activeFt8Invocation ())
               invocation->processed.insert (normalized);
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
             if (message.isEmpty ()) return;
             auto const normalized = message.simplified ();
             if (auto * invocation = activeFt8Invocation ())
               invocation->displayed.insert (normalized);
             m_displayed.insert (normalized);
           });
  connect (m_window, &MainWindow::decodeCycleCompleted,
           this, [this] (quint64 cycle) {
             ++m_completedCycles;
             if (schedulingScenario ())
               {
                 if (!m_ft8ActiveCycle || cycle != m_ft8ActiveCycle
                     || m_ft8Invocations.size () != m_completedCycles
                     || m_ft8CompletedCycles.contains (cycle))
                   {
                     fail (tr ("FT8 scheduling completion identity/count mismatch."));
                     return;
                   }
                 m_ft8CompletedCycles.insert (cycle);
                 auto const& invocation = m_ft8Invocations.constLast ();
                 auto logMessages = [&invocation, cycle] (char const * layer, QSet<QString> const& values)
                   {
                     auto messages = values.values ();
                     messages.sort ();
                     std::cerr << "WSJT-X FT8 scheduling: half_symbols=" << invocation.halfSymbols
                               << " cycle=" << cycle << " " << layer << "="
                               << messages.join (" | ").toStdString () << std::endl;
                   };
                 logMessages ("raw", invocation.raw);
                 logMessages ("processed", invocation.processed);
                 logMessages ("displayed", invocation.displayed);
                 m_ft8ActiveCycle = 0;
                 std::cerr << "WSJT-X FT8 scheduling: completed_cycle=" << cycle
                           << " half_symbols=" << m_window->liveAudioTestHalfSymbols ()
                           << " received_samples=" << m_ft8AcceptedEnd << std::endl;
               }
             if (m_submittedGeneration)
               {
                 if (!m_rolloverVerified || cycle != m_submittedCycle
                     || ++m_submittedCompletions != 1)
                   {
                     fail (tr ("Submitted FT8 completion mismatch: cycle=%1 expected=%2 count=%3 rollover=%4.")
                           .arg (cycle).arg (m_submittedCycle)
                           .arg (m_submittedCompletions).arg (m_rolloverVerified));
                     return;
                   }
               }
             if (m_decoderStage == DecoderStage::Multithreaded)
               {
                 m_completedMultithreadedDecode = true;
               }
             maybeFinish ();
             m_decoderStage = DecoderStage::None;
           });
  connect (m_window, &MainWindow::decodeCycleAborted,
           this, [this] {
             fail (QStringLiteral ("Decoder generation aborted before completion."));
           });
  connect (m_window, &MainWindow::decoderOutputLine,
           this, [this] (QByteArray const& line) {
             auto const message = messageFromDecoderLine (line);
             if (message.isEmpty ()) return;
             if (auto * invocation = activeFt8Invocation ())
               invocation->raw.insert (message);
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
             if (schedulingScenario ()) return;
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
             if (multithreaded) exerciseSubmittedFt8Rollover ();
           });
  connect (m_window, &MainWindow::liveAudioTestFt8SchedulingPoint,
           this, &LiveAudioTestController::observeFt8SchedulingPoint, Qt::DirectConnection);
  connect (m_window, &MainWindow::liveAudioTestFt8RequestPublished,
           this, [this] (decoder_params_t const& params,
                         decoder_input_metadata_t const& metadata,
                         short const * samples, qint64 period) {
             observeFt8SchedulingRequest (params, metadata, samples, period, false);
           }, Qt::DirectConnection);
  connect (m_window, &MainWindow::liveAudioTestFt8RequestCaptured,
           this, [this] (decoder_params_t const& params,
                         decoder_input_metadata_t const& metadata,
                         short const * samples, qint64 period) {
             observeFt8SchedulingRequest (params, metadata, samples, period, true);
           }, Qt::DirectConnection);
  connect (m_window, &MainWindow::liveAudioTestReceiveRange,
           this, [this] (quint64 epoch, int start, int end, bool accepted) {
             if (schedulingScenario ())
               {
                 if (!m_ft8Epoch) m_ft8Epoch = epoch;
                 if (!accepted || epoch != m_ft8Epoch || start != m_ft8AcceptedEnd)
                   fail (tr ("FT8 scheduling received a rejected, gapped, or wrong-period block."));
                 m_ft8AcceptedEnd = end;
                 return;
               }
             if (!m_submittedGeneration || m_finished) return;
             if (!accepted)
               {
                 ++m_rolloverRejectedBlocks;
                 fail (tr ("Submitted FT8 rollover rejected epoch=%1 range=[%2,%3).")
                       .arg (epoch).arg (start).arg (end));
                 return;
               }
             if (epoch != m_rolloverEpoch || start != m_rolloverAcceptedEnd
                 || end > int (m_rolloverSamples.size ())
                 || !std::equal (m_rolloverSamples.begin () + start,
                                  m_rolloverSamples.begin () + end, dec_data.d2 + start)
                 || m_window->liveAudioTestPublishedDecoderGeneration () != m_submittedGeneration)
               {
                 fail (tr ("Submitted FT8 rollover mismatch: epoch=%1 expected_epoch=%2 range=[%3,%4) expected_start=%5 generation=%6.")
                       .arg (epoch).arg (m_rolloverEpoch).arg (start).arg (end)
                       .arg (m_rolloverAcceptedEnd).arg (m_submittedGeneration));
                 return;
               }
             m_rolloverAcceptedEnd = end;
             ++m_rolloverAcceptedBlocks;
           });
  connect (m_window, &MainWindow::liveAudioTestReceiveBlock,
           this, [this] (ReceiveAudio const& block) {
             if (!m_submittedGeneration || m_finished
                 || block->epoch <= m_preRolloverEpoch) return;
             if (!m_rolloverEpoch) m_rolloverEpoch = block->epoch;
             if (block->epoch != m_rolloverEpoch
                 || block->start != int (m_rolloverSamples.size ())) return;
             m_rolloverSamples.insert (m_rolloverSamples.end (),
                                       block->samples.begin (), block->samples.end ());
           });
  connect (m_fixture, &FixtureAudioInput::emissionStarted,
           this, [this] (qint64 utcStartMilliseconds) {
             m_captureStartMs = utcStartMilliseconds;
             if (Mode::Jtty == m_mode
                 && utcStartMilliseconds % 180000 != FixtureAudioInput::jttyCaptureOffsetMs ())
               {
                 fail (tr ("JTTY fixture did not begin at its boundary-crossing capture offset."));
                 return;
               }
             // Allow long fixtures their full playback time after any startup wait.
             if (Mode::Jtty == m_mode) m_timeout.start(180000);
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
             if (Mode::Jtty == m_mode && !m_finished)
               {
                 m_jttyStageTimer.start ();
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

void LiveAudioTestController::exerciseSubmittedFt8Rollover ()
{
  if (m_submittedGeneration || m_window->liveAudioTestReceivingAudio ())
    {
      fail (tr ("FT8 rollover requires one final publication outside receive DSP."));
      return;
    }
  m_submittedGeneration = m_window->liveAudioTestPublishedDecoderGeneration ();
  m_submittedCycle = m_window->liveAudioTestDecodeCycleGeneration ();
  auto const previousEpoch = m_window->liveAudioTestReceiveEpoch ();
  m_preRolloverEpoch = previousEpoch;
  if (!m_submittedGeneration || !m_window->decoderBusy ())
    {
      fail (tr ("FT8 final invocation has no committed decoder publication."));
      return;
    }

  struct Acknowledgement
  {
    std::mutex mutex;
    std::condition_variable ready;
    bool complete {false};
    QString error;
  };
  auto acknowledgement = std::make_shared<Acknowledgement> ();
  auto * fixture = m_fixture;
  if (!QMetaObject::invokeMethod (fixture, [fixture, acknowledgement] {
        auto const error = fixture->emitFt8RolloverPrefix ();
        {
          std::lock_guard<std::mutex> lock {acknowledgement->mutex};
          acknowledgement->error = error;
          acknowledgement->complete = true;
        }
        acknowledgement->ready.notify_one ();
      }, Qt::QueuedConnection))
    {
      fail (tr ("Unable to queue the FT8 rollover prefix."));
      return;
    }
  {
    std::unique_lock<std::mutex> lock {acknowledgement->mutex};
    if (!acknowledgement->ready.wait_for (lock, std::chrono::seconds {10},
          [&] {return acknowledgement->complete;}))
      {
        lock.unlock ();
        fail (tr ("Timed out awaiting FT8 rollover: generation=%1 previous_epoch=%2.")
              .arg (m_submittedGeneration).arg (previousEpoch));
        return;
      }
    auto const error = acknowledgement->error;
    lock.unlock ();
    if (!error.isEmpty ())
      {
        fail (error);
        return;
      }
  }
  // Deliver Detector's queued blocks while decoder readiness events remain pending.
  QCoreApplication::sendPostedEvents (m_window, QEvent::MetaCall);
  if (m_finished) return;
  auto const lastSample = m_rolloverSamples.empty () ? 0 : m_rolloverSamples.back ();
  m_rolloverVerified = m_rolloverAcceptedBlocks > 0
    && m_rolloverEpoch > previousEpoch
    && m_rolloverSamples.size () == FixtureAudioInput::ft8RolloverFrames ()
    && lastSample < 0
    && m_rolloverAcceptedEnd == FixtureAudioInput::ft8RolloverFrames ()
    && m_window->liveAudioTestPublishedDecoderGeneration () == m_submittedGeneration
    && m_window->decoderBusy () && !m_submittedCompletions;
  if (!m_rolloverVerified)
    {
      fail (tr ("FT8 rollover did not preserve active publication: epoch=%1 accepted=%2 end=%3 rejected=%4 generation=%5 active=%6.")
            .arg (m_rolloverEpoch).arg (m_rolloverAcceptedBlocks)
            .arg (m_rolloverAcceptedEnd).arg (m_rolloverRejectedBlocks)
            .arg (m_submittedGeneration)
            .arg (m_window->liveAudioTestPublishedDecoderGeneration ()));
    }
}

void LiveAudioTestController::observeFt8SchedulingPoint (int halfSymbols, bool available)
{
  if (!schedulingScenario () || m_finished) return;
  if (!m_ft8SchedulingPoints.isEmpty () && halfSymbols > m_ft8SchedulingPoints.last ().halfSymbols
      && !verifyFt8SchedulingPoint ()) return;
  if (halfSymbols == 41 || halfSymbols == finalFt8HalfSymbols ()
      || (m_mode == Mode::Ft8ThreeStage && halfSymbols == 46))
    {
      auto const index = m_ft8SchedulingPoints.size ();
      auto const expected = index == 0 ? 41
        : index == expectedFt8Stages () - 1 ? finalFt8HalfSymbols () : 46;
      if (index >= expectedFt8Stages () || halfSymbols != expected
          || (halfSymbols == 41 && !available)
          || (!available && halfSymbols == 46
              && (!m_ft8ActiveCycle || m_ft8Invocations.isEmpty ()
                  || m_ft8Invocations.last ().halfSymbols != 41)))
        {
          fail (tr ("FT8 scheduling point %1 has an unexpected order or decoder owner.")
                .arg (halfSymbols));
          return;
        }
      m_ft8SchedulingPoints.append (Ft8SchedulingPoint {halfSymbols, available});
      std::cerr << "WSJT-X FT8 scheduling: trigger=" << halfSymbols
                << " available=" << available << " active_cycle=" << m_ft8ActiveCycle
                << std::endl;
    }
  maybeFinish ();
}

bool LiveAudioTestController::verifyFt8SchedulingPoint ()
{
  auto& point = m_ft8SchedulingPoints.last ();
  if (point.verified) return true;
  auto const final = point.halfSymbols == finalFt8HalfSymbols ();
  if ((final && !point.captured) || (point.available && !point.published)
      || (!final && !point.available && point.published))
    {
      fail (tr ("FT8 half-symbol %1 violated scheduling: available=%2 captured=%3 published=%4.")
            .arg (point.halfSymbols).arg (point.available).arg (point.captured).arg (point.published));
      return false;
    }
  point.verified = true;
  if (!point.available)
    std::cerr << "WSJT-X FT8 scheduling: verified backpressure at half_symbols="
              << point.halfSymbols << (final ? " deferred_final" : " skipped_early") << std::endl;
  return true;
}

void LiveAudioTestController::observeFt8SchedulingRequest (
  decoder_params_t const& params, decoder_input_metadata_t const& metadata,
  short const * samples, qint64 period, bool pending)
{
  if (!schedulingScenario () || m_finished) return;
  auto point = std::find_if (m_ft8SchedulingPoints.begin (), m_ft8SchedulingPoints.end (),
                            [&params] (Ft8SchedulingPoint const& value) {
                              return value.halfSymbols == params.nzhsym;
                            });
  if (point == m_ft8SchedulingPoints.end ())
    {
      fail (tr ("FT8 request has no scheduled trigger at half-symbol %1.").arg (params.nzhsym));
      return;
    }
  auto const final = params.nzhsym == finalFt8HalfSymbols ();
  auto const available = params.nzhsym * 3456;
  auto const publication = m_window->liveAudioTestPublishedDecoderGeneration ();
  auto const expectedTime = QDateTime::fromMSecsSinceEpoch (m_captureStartMs, Qt::UTC).time ();
  auto const expectedUtc = expectedTime.hour () * 10000 + expectedTime.minute () * 100 + expectedTime.second ();
  if (!m_ft8InputId && !pending)
    {
      m_ft8InputId = metadata.input_id;
      m_ft8AnalysisId = metadata.analysis_id;
    }
  bool const identityMatches = m_captureStartMs > 0 && m_captureStartMs % 15000 == 0
    && period == m_captureStartMs / 15000 && params.nutc == expectedUtc
    && m_ft8Epoch && metadata.input_id > 0 && metadata.input_id == m_ft8InputId
    && metadata.analysis_id > 0 && metadata.analysis_id == m_ft8AnalysisId
    && metadata.attempt_no == m_ft8LastAttempt + 1
    && (pending || (publication > m_ft8LastPublication && m_ft8ActiveCycle
                    && m_ft8ActiveCycle == m_window->liveAudioTestDecodeCycleGeneration ()));
  bool const configurationMatches = params.nmode == 8 && !params.ndiskdat
    && params.newdat && !params.nagain && params.ntrperiod == 15
    && params.lmultift8 == final && params.nmt == ft8TestThreadCount
    && (params.ndepth & 7) == 2 && params.nft8cycles == (final ? 1 : 2)
    && !params.lft8subpass && !params.lft8lowth
    && params.ndecoderstart == expectedFt8Stages () - 2
    && params.nfa == MainWindow::liveAudioTestDecodeLowFrequency ()
    && params.nfb == MainWindow::liveAudioTestDecodeHighFrequency ();
  auto const atTrigger = !point->captured;
  bool const schedulingMatches = !point->published
    && (pending ? final && !point->available && atTrigger
                : (point->available || (final && point->captured))
                  && m_completedCycles == m_ft8Invocations.size ())
    && (!atTrigger || (point == m_ft8SchedulingPoints.end () - 1
                       && m_window->liveAudioTestHalfSymbols () == params.nzhsym
                       && m_window->liveAudioTestReceivingAudio ()
                       && m_window->liveAudioTestReceiveEpoch () == m_ft8Epoch
                       && m_ft8AcceptedEnd == available))
    && (!final || atTrigger || (metadata.input_id == m_ft8FinalMetadata.input_id
                               && metadata.analysis_id == m_ft8FinalMetadata.analysis_id
                               && metadata.attempt_no == m_ft8FinalMetadata.attempt_no
                               && metadata.valid_samples == m_ft8FinalMetadata.valid_samples));
  bool const audioMatches = m_ft8FixtureSamples.size () == DecoderIpc::Ft8SampleCount
    && params.kin == available && metadata.valid_samples == available && samples
    && available < int (DecoderIpc::Ft8SampleCount)
    && std::equal (m_ft8FixtureSamples.begin (), m_ft8FixtureSamples.begin () + available, samples)
    && std::all_of (samples + available, samples + DecoderIpc::Ft8SampleCount,
                    [] (short value) {return value == 0;});
  std::cerr << "WSJT-X FT8 scheduling: " << (pending ? "capture" : "invocation")
            << " MTD=" << params.lmultift8 << " half_symbols=" << params.nzhsym
            << " available_samples=" << metadata.valid_samples
            << " window_samples=" << DecoderIpc::Ft8SampleCount
            << " period=" << period << " input=" << metadata.input_id
            << " analysis=" << metadata.analysis_id << " attempt=" << metadata.attempt_no
            << " generation=" << publication << " cycle=" << m_ft8ActiveCycle
            << " completed=" << m_completedCycles
            << " fixture_finished=" << m_fixtureFinished << " identity=" << identityMatches
            << " configuration=" << configurationMatches << " scheduling=" << schedulingMatches
            << " audio=" << audioMatches << std::endl;
  if (!identityMatches || !configurationMatches || !schedulingMatches || !audioMatches)
    {
      fail (tr ("FT8 half-symbol %1 request failed identity, configuration, scheduling, or audio validation.")
            .arg (params.nzhsym));
      return;
    }
  point->captured = true;
  if (final && atTrigger) m_ft8FinalMetadata = metadata;
  if (pending) return;
  point->published = true;
  m_ft8Invocations.append (Ft8Invocation {params.nzhsym, m_ft8ActiveCycle, {}, {}, {}});
  m_ft8LastPublication = publication;
  m_ft8LastAttempt = metadata.attempt_no;
  m_decoderStage = final ? DecoderStage::Multithreaded : DecoderStage::EarlyStandard;
  if (final) m_sawConfiguredMultithreadedDecode = true;
  else m_sawEarlyStandardDecode = true;
}

LiveAudioTestController::Ft8Invocation * LiveAudioTestController::activeFt8Invocation ()
{
  if (!schedulingScenario () || m_finished) return nullptr;
  if (m_ft8Invocations.isEmpty () || !m_ft8ActiveCycle
      || m_ft8Invocations.last ().cycle != m_ft8ActiveCycle)
    {
      fail (tr ("FT8 scheduling output arrived without an active invocation."));
      return nullptr;
    }
  return &m_ft8Invocations.last ();
}

bool LiveAudioTestController::verifyFt8SchedulingOutput ()
{
  auto requireMessage = [this] (Ft8Invocation const& invocation, QString const& message, bool gui) {
    QStringList missing;
    if (!invocation.raw.contains (message)) missing.append (QStringLiteral ("raw output"));
    if (gui && !invocation.processed.contains (message)) missing.append (QStringLiteral ("GUI processing"));
    if (gui && !invocation.displayed.contains (message)) missing.append (QStringLiteral ("GUI display"));
    if (missing.isEmpty ()) return true;
    fail (tr ("FT8 half-symbol %1 cycle %2 reference %3 missing from %4.")
          .arg (invocation.halfSymbols).arg (invocation.cycle)
          .arg (message, missing.join (QStringLiteral (", "))));
    return false;
  };
  auto const& first = m_ft8Invocations.constFirst ();
  auto const& final = m_ft8Invocations.constLast ();
  auto const usefulEarly = std::any_of (m_expected.begin (), m_expected.end (),
                                       [&first] (QString const& message) {
                                         return first.raw.contains (message)
                                           && first.processed.contains (message)
                                           && first.displayed.contains (message);
                                       });
  if (!usefulEarly)
    {
      fail (tr ("FT8 stage 41 delivered no reference message through raw output, GUI processing, and display."));
      return false;
    }
  for (auto const& message : m_expected)
    if (!requireMessage (final, message, false)) return false;
  auto const laterMessage = QStringLiteral ("N1PJT HB9CQK -10");
  if (first.raw.contains (laterMessage))
    {
      fail (tr ("FT8 half-symbol 41 unexpectedly recovered the later-stage reference %1.")
            .arg (laterMessage));
      return false;
    }
  // The second invocation is stage 46 when eligible, otherwise the final MTD stage.
  return requireMessage (m_ft8Invocations.at (1), laterMessage, true)
    && requireMessage (final, laterMessage, false);
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
  auto * deepAction = m_window->findChild<QAction *> (
    schedulingScenario () ? "actionMediumDecode" : "actionDeepestDecode");
  auto * multithreadedAction =
    m_window->findChild<QAction *> ("actionUse_multithreaded_FT8_decoder");
  auto * threadCountAction = m_window->findChild<QAction *> (
    QStringLiteral ("actionMT%1").arg (ft8TestThreadCount));
  auto * cycleCountAction = m_window->findChild<QAction *> (
    QStringLiteral ("actionDecFT8cycles%1").arg (schedulingScenario () ? 1 : ft8TestCycleCount));
  auto * subpassAction = m_window->findChild<QAction *> (
    schedulingScenario () ? "actionFT8SensMin" : "actionFT8subpass");
  auto * twoStageAction =
    m_window->findChild<QAction *> (m_mode == Mode::Ft8ThreeStage
                                   ? "actionStartThreeStage" : "actionStartTwoStage");
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
  if (!schedulingScenario () && !m_window->prepareLiveAudioTestFt8InputCompletion ())
    {
      fail (tr ("Unable to prepare the complete FT8 fixture decode."));
      return;
    }

  bool const decoderConfigurationMatches =
    m_window->liveAudioTestMultithreadedFt8Enabled ()
    && m_window->liveAudioTestFt8ThreadCount () == ft8TestThreadCount
    && ft8Action->isChecked () && deepAction->isChecked () && twoStageAction->isChecked ()
    && m_window->liveAudioTestDecodeDepth () == (schedulingScenario () ? 2 : 3)
    && m_window->liveAudioTestFt8Cycles () == (schedulingScenario () ? 1 : ft8TestCycleCount)
    && m_window->liveAudioTestFt8Sensitivity () == (schedulingScenario () ? 1 : 3)
    && m_window->liveAudioTestFt8DecoderStart () == (m_mode == Mode::Ft8ThreeStage ? 1 : 0);
  if (!decoderConfigurationMatches)
    {
      fail (tr ("Unable to configure the FT8 multithreaded decoder as requested."));
      return;
    }

  if (schedulingScenario ())
    {
      BWFFile file {QAudioFormat {}, m_fixture->fixturePath ()};
      if (!file.open (QIODevice::ReadOnly) || file.format ().sampleRate () != 12000
          || file.size () != qint64 (DecoderIpc::Ft8SampleCount * sizeof (short)))
        {
          fail (tr ("FT8 scheduling requires the 12 kHz reference fixture."));
          return;
        }
      auto const pcm = file.readAll ();
      m_ft8FixtureSamples.resize (DecoderIpc::Ft8SampleCount);
      if (pcm.size () != file.size ())
        {
          fail (tr ("Cannot read the FT8 scheduling reference PCM."));
          return;
        }
      std::memcpy (m_ft8FixtureSamples.data (), pcm.constData (), pcm.size ());
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
            << " depth=" << m_window->liveAudioTestDecodeDepth ()
            << " cycles=" << m_window->liveAudioTestFt8Cycles ()
            << " sensitivity=" << m_window->liveAudioTestFt8Sensitivity ()
            << " start=" << m_window->liveAudioTestFt8DecoderStart () << " decode_range="
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

  auto * savePath = m_window->findChild<QLabel *> ("save_path_display_label");
  auto * saveDecoded = m_window->findChild<QAction *> ("actionSave_decoded");
  auto * unsplitLog = m_window->findChild<QAction *> ("actionDon_t_split_ALL_TXT");
  QDir const dataDirectory {QStandardPaths::writableLocation (QStandardPaths::DataLocation)};
  if (!savePath || !saveDecoded || !unsplitLog) {
    fail (tr ("JTTY recording or logging controls were not found."));
    return;
  }
  m_jttySaveDirectory = QDir::fromNativeSeparators (
    QFileInfo (savePath->text ()).canonicalFilePath ());
  auto const dataRoot = QDir::fromNativeSeparators (
    QFileInfo (dataDirectory.absolutePath ()).canonicalFilePath ());
  if (!QCoreApplication::applicationName ().endsWith (" - test") || dataRoot.isEmpty ()
      || !m_jttySaveDirectory.startsWith (dataRoot + QLatin1Char ('/'))) {
    fail (tr ("JTTY recording validation requires a save directory inside the isolated test data directory: save=%1 data=%2.")
          .arg (m_jttySaveDirectory, dataRoot));
    return;
  }
  for (auto const& name : QDir (m_jttySaveDirectory).entryList ({"*.wav"}, QDir::Files))
    m_jttyInitialRecordings.insert (name);
  m_jttyLogPath = dataDirectory.absoluteFilePath ("ALL.TXT");
  m_jttyInitialLogSize = QFileInfo (m_jttyLogPath).size ();
  unsplitLog->trigger ();
  saveDecoded->trigger ();

  auto const frequencyError = m_window->checkLiveAudioTestJttyFrequencyChanges ();
  if (!frequencyError.isEmpty ()) {
    fail (frequencyError);
    return;
  }
  auto const mailboxError = m_window->checkLiveAudioTestJttyMailboxOverflow ();
  if (!mailboxError.isEmpty ()) { fail (mailboxError); return; }
  auto * includeTime = m_window->findChild<QAbstractButton *> ("cbIncludeTime");
  auto * lowerCase = m_window->findChild<QAbstractButton *> ("cbLowerCase");
  if (!includeTime || !lowerCase) { fail (tr ("JTTY display options were not found.")); return; }
  includeTime->setChecked (true);
  lowerCase->setChecked (false);

  connect (m_jttyAllDecodes, &QTextEdit::textChanged,
           this, &LiveAudioTestController::observeJttyDisplay);
  connect (m_jttyQsoFrequency, &QTextEdit::textChanged,
           this, &LiveAudioTestController::observeJttyDisplay);
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

  if (schedulingScenario ())
    {
      if (!m_ft8SchedulingPoints.isEmpty () && !m_ft8SchedulingPoints.last ().verified) return;
      auto const expectedInvocations = std::count_if (
        m_ft8SchedulingPoints.begin (), m_ft8SchedulingPoints.end (),
        [this] (Ft8SchedulingPoint const& point) {
          return point.available || point.halfSymbols == finalFt8HalfSymbols ();
        });
      if (m_ft8SchedulingPoints.size () != expectedFt8Stages ()
          || m_ft8Invocations.size () != expectedInvocations
          || m_completedCycles != expectedInvocations || m_ft8ActiveCycle
          || m_ft8CompletedCycles.size () != expectedInvocations
          || m_emittedFrames != qint64 (DecoderIpc::Ft8SampleCount))
        {
          fail (tr ("FT8 scheduling did not complete exactly one cycle per eligible invocation."));
          return;
        }
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
  if (schedulingScenario () && !verifyFt8SchedulingOutput ()) return;

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
            << " rollover_epoch=" << m_rolloverEpoch
            << " rollover_end=" << m_rolloverAcceptedEnd
            << " rollover_accepted=" << m_rolloverAcceptedBlocks
            << " rollover_rejected=" << m_rolloverRejectedBlocks
            << " submitted_generation=" << m_submittedGeneration
            << " submitted_completions=" << m_submittedCompletions
            << " decode_cycles=" << m_completedCycles << std::endl;
  m_window->close ();
  QCoreApplication::exit (EXIT_SUCCESS);
}

void LiveAudioTestController::observeJttyDisplay ()
{
  if (m_finished || m_jttyCheckStage != JttyCheckStage::Live
      || !m_jttyAllDecodes || !m_jttyQsoFrequency) return;

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
      if (!m_jttyAllPrefixBlock.isValid ())
        m_jttyAllPrefixBlock = findJttyMessageBlock (*m_jttyAllDecodes->document (), m_expectedJttyPrefix);
    }
  if (m_jttyQsoFinals.isEmpty () && qsoText.contains (m_expectedJttyPrefix))
    {
      m_jttyQsoSawPrefix = true;
      if (!m_jttyQsoPrefixBlock.isValid ())
        m_jttyQsoPrefixBlock = findJttyMessageBlock (*m_jttyQsoFrequency->document (), m_expectedJttyPrefix);
    }
}

void LiveAudioTestController::pollJttyDisplay ()
{
  observeJttyDisplay ();
  // GUI actions must run after text-change notifications finish updating both panes.
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
  if (m_jttyCheckStage != JttyCheckStage::Live) {
    checkJttyReviewAndStatus ();
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

  for (auto const& message : m_expectedJtty)
    {
      if (allText.count (message) != 1 || qsoText.count (message) != 1)
        {
          fail (tr ("A JTTY message appeared more than once across the receive boundary."));
          return;
        }
      auto const block = findJttyMessageBlock (*m_jttyAllDecodes->document (), message);
      if (!block.isValid ()) {
        fail (tr ("The displayed JTTY message could not be located: %1").arg (message));
        return;
      }
      auto const line = block.text ();
      if (!QRegularExpression {QStringLiteral ("^[0-9]{6} +[0-9]{3,4}  ")}.match (line).hasMatch ()) {
        fail (tr ("JTTY timestamps do not match the six-digit UTC column."));
        return;
      }
    }
  if (m_expectedJtty.size () == 1)
    {
      auto const& message = m_expectedJtty.constFirst ();
      if (!m_jttyAllPrefixBlock.isValid () || !m_jttyQsoPrefixBlock.isValid ()
          || findJttyMessageBlock (*m_jttyAllDecodes->document (), message) != m_jttyAllPrefixBlock
          || findJttyMessageBlock (*m_jttyQsoFrequency->document (), message) != m_jttyQsoPrefixBlock)
        {
          fail (tr ("A growing JTTY message changed display lines across the receive boundary."));
          return;
        }
    }
  auto const sampleRate = m_fixture->streamDescriptor ().sample_rate_hz;
  if (sampleRate <= 0 || m_captureStartMs <= 0
      || m_captureStartMs % 180000 + m_emittedFrames * 1000 / sampleRate <= 180000)
    {
      fail (tr ("The JTTY fixture did not cross the artificial receive boundary."));
      return;
    }

  auto const progress = m_window->statusBar ()->findChildren<QProgressBar *> ();
  if (progress.size () != 1 || !progress.front ()->isHidden ()) {
    fail (tr ("JTTY still displays a cyclic progress indicator during reception."));
    return;
  }
  QFile log {m_jttyLogPath};
  if (!log.open (QIODevice::ReadOnly)) {
    fail (tr ("JTTY live reception did not create a readable ALL.TXT."));
    return;
  }
  m_jttyLogBeforeReview = log.readAll ();
  auto const liveLog = QString::fromUtf8 (m_jttyLogBeforeReview.mid (m_jttyInitialLogSize)).toUpper ();
  for (auto const& message : m_expectedJtty) {
    if (liveLog.count (message) != 1) {
      fail (tr ("JTTY live reception did not log each expected message exactly once."));
      return;
    }
    auto const allBlock = findJttyMessageBlock (*m_jttyAllDecodes->document (), message);
    auto const qsoBlock = findJttyMessageBlock (*m_jttyQsoFrequency->document (), message);
    if (!allBlock.isValid () || !qsoBlock.isValid ()) {
      fail (tr ("The displayed JTTY message could not be located: %1").arg (message));
      return;
    }
    m_jttyAllLiveBlocks.append (allBlock);
    m_jttyQsoLiveBlocks.append (qsoBlock);
  }
  m_jttyAllBeforeReview = m_jttyAllDecodes->toPlainText ();
  m_jttyQsoBeforeReview = m_jttyQsoFrequency->toPlainText ();
  auto * decode = m_window->findChild<QAbstractButton *> ("DecodeButton");
  if (!decode || !decode->isEnabled ()) {
    fail (tr ("JTTY historical review is not available after live reception."));
    return;
  }
  m_jttyCheckStage = JttyCheckStage::Review;
  std::cerr << "WSJT-X JTTY live audio test: stage=Review remaining_ms=" << m_timeout.remainingTime () << std::endl;
  m_jttyCheckElapsed.start ();
  m_jttyStageTimer.start ();
  decode->click ();
}

void LiveAudioTestController::checkJttyReviewAndStatus ()
{
  if (m_jttyCheckStage == JttyCheckStage::Drain) return;
  auto const progress = m_window->statusBar ()->findChildren<QProgressBar *> ();
  if (progress.size () != 1) {
    fail (tr ("The status progress control was not found."));
    return;
  }
  if (m_jttyCheckStage == JttyCheckStage::Review) {
    auto * decode = m_window->findChild<QAbstractButton *> ("DecodeButton");
    if (!decode || decode->isChecked () || m_window->decoderBusy ()) return;
    auto const allText = m_jttyAllDecodes->toPlainText ();
    auto const qsoText = m_jttyQsoFrequency->toPlainText ();
    auto const heading = QCoreApplication::translate ("MainWindow", "JTTY review");
    auto const allReview = allText.mid (m_jttyAllBeforeReview.size ()).simplified ().toUpper ();
    auto const qsoReview = qsoText.mid (m_jttyQsoBeforeReview.size ()).simplified ().toUpper ();
    if (!allText.startsWith (m_jttyAllBeforeReview) || !qsoText.startsWith (m_jttyQsoBeforeReview)
        || !allReview.contains (heading.toUpper ()) || !qsoReview.contains (heading.toUpper ())) {
      fail (tr ("Historical JTTY review did not preserve live history in a separate review group."));
      return;
    }
    for (int i = 0; i < m_expectedJtty.size (); ++i) {
      auto const& message = m_expectedJtty[i];
      if (allReview.count (message) != 1 || qsoReview.count (message) != 1
          || findJttyMessageBlock (*m_jttyAllDecodes->document (), message) != m_jttyAllLiveBlocks[i]
          || findJttyMessageBlock (*m_jttyQsoFrequency->document (), message) != m_jttyQsoLiveBlocks[i]) {
        fail (tr ("Historical JTTY review altered live message identity or produced duplicate review text."));
        return;
      }
    }
    QFile log {m_jttyLogPath};
    if (!log.open (QIODevice::ReadOnly) || log.readAll () != m_jttyLogBeforeReview) {
      fail (tr ("Historical JTTY review changed ALL.TXT."));
      return;
    }
    auto * monitor = m_window->findChild<QAbstractButton *> ("monitorButton");
    if (!monitor || !monitor->isEnabled ()) {
      fail (tr ("JTTY monitoring cannot be stopped after historical review."));
      return;
    }
    m_jttyCheckStage = JttyCheckStage::Stopped;
    std::cerr << "WSJT-X JTTY live audio test: stage=Stopped remaining_ms=" << m_timeout.remainingTime () << std::endl;
    m_jttyCheckElapsed.restart ();
    m_jttyStageTimer.start ();
    if (monitor->isChecked ()) monitor->click ();
    return;
  }
  if (m_jttyCheckElapsed.elapsed () < 1200) return;
  if (m_jttyCheckStage == JttyCheckStage::Stopped) {
    bool stopped = false;
    for (auto * label : m_window->statusBar ()->findChildren<QLabel *> ()) {
      if (label->accessibleName () == QCoreApplication::translate ("MainWindow", "Transmit status"))
        stopped = label->text () == QCoreApplication::translate ("MainWindow", "Stopped");
    }
    if (m_window->monitoringActive () || !stopped || !progress.front ()->isHidden ()) {
      fail (tr ("Stopped JTTY reception did not retain its stopped status and hidden progress indicator."));
      return;
    }
    QString saved;
    QDir const directory {m_jttySaveDirectory};
    for (auto const& name : directory.entryList ({"*.wav"}, QDir::Files)) {
      if (m_jttyInitialRecordings.contains (name)) continue;
      BWFFile file {QAudioFormat {}, directory.absoluteFilePath (name)};
      if (file.open (QIODevice::ReadOnly) && file.size () > 12000 * 2)
        saved = directory.absoluteFilePath (name);
    }
    if (saved.isEmpty ()) return;
    m_jttyCheckStage = JttyCheckStage::Wav;
    std::cerr << "WSJT-X JTTY live audio test: stage=Wav remaining_ms=" << m_timeout.remainingTime () << std::endl;
    m_jttyStageTimer.start ();
    if (!m_window->startLiveAudioTestJttyWav (saved))
      fail (tr ("Unable to decode the isolated JTTY recording."));
    return;
  }
  if (m_jttyCheckStage == JttyCheckStage::Wav) {
    if (m_window->decoderBusy ()) return;
    QFile log {m_jttyLogPath};
    if (!log.open (QIODevice::ReadOnly)) { fail (tr ("Cannot read JTTY replay log.")); return; }
    auto const contents = log.readAll ();
    if (contents.size () == m_jttyLogBeforeReview.size ()) return;
    auto const replayLog = QString::fromUtf8 (contents.mid (m_jttyLogBeforeReview.size ())).toUpper ();
    for (auto const& message : m_expectedJtty) {
      if (replayLog.count (message) != 1) {
        fail (tr ("Automatic JTTY WAV decoding did not log each message exactly once."));
        return;
      }
    }
    auto * includeTime = m_window->findChild<QAbstractButton *> ("cbIncludeTime");
    auto * lowerCase = m_window->findChild<QAbstractButton *> ("cbLowerCase");
    if (!includeTime || !lowerCase) { fail (tr ("JTTY display options were not found.")); return; }
    QStringList const before {m_jttyAllDecodes->toPlainText (), m_jttyQsoFrequency->toPlainText ()};
    QList<QTextEdit *> const panes {m_jttyAllDecodes, m_jttyQsoFrequency};
    QRegularExpression const timestamp {QStringLiteral ("^[0-9]{6} "),
                                        QRegularExpression::MultilineOption};
    includeTime->setChecked (false);
    for (int i = 0; i < panes.size (); ++i) {
      auto expected = before[i];
      expected.remove (timestamp);
      if (panes[i]->toPlainText () != expected) {
        fail (tr ("Include Time did not immediately refresh retained JTTY history."));
        return;
      }
    }
    lowerCase->setChecked (true);
    for (int i = 0; i < panes.size (); ++i) {
      auto const text = panes[i]->toPlainText ().simplified ();
      for (auto const& message : m_expectedJtty) {
        auto const count = before[i].simplified ().count (message);
        if (count < 3 || text.count (message.toLower ()) != count || text.contains (message)) {
          fail (tr ("Lowercase did not immediately refresh live, reviewed, and WAV JTTY history."));
          return;
        }
      }
    }
    includeTime->setChecked (true);
    lowerCase->setChecked (false);
    if (panes[0]->toPlainText () != before[0] || panes[1]->toPlainText () != before[1]
        || !log.seek (0) || log.readAll () != contents) {
      fail (tr ("Refreshing JTTY display options changed message history or ALL.TXT."));
      return;
    }
    auto * ft8 = m_window->findChild<QAction *> ("actionFT8");
    if (!ft8 || !ft8->isEnabled ()
        || !QMetaObject::invokeMethod (m_fixture, "stop", Qt::BlockingQueuedConnection)) {
      fail (tr ("Cannot prepare the isolated fixture for the timed-mode UI check."));
      return;
    }
    m_jttyCheckStage = JttyCheckStage::Drain;
    std::cerr << "WSJT-X JTTY live audio test: stage=Drain remaining_ms=" << m_timeout.remainingTime () << std::endl;
    auto const drainError = m_window->checkLiveAudioTestJttyDrain ();
    if (m_finished) return;
    if (!drainError.isEmpty ()) { fail (drainError); return; }
    auto * decode = m_window->findChild<QAbstractButton *> ("DecodeButton");
    if (!decode || !decode->isEnabled ()) {
      fail (tr ("JTTY review is unavailable for cancellation validation."));
      return;
    }
    decode->click ();
    if (!decode->isChecked () || !m_window->decoderBusy ()) {
      fail (tr ("JTTY review did not start before mode-switch cancellation."));
      return;
    }
    m_jttyCheckStage = JttyCheckStage::TimedMode;
    std::cerr << "WSJT-X JTTY live audio test: stage=TimedMode remaining_ms=" << m_timeout.remainingTime () << std::endl;
    m_jttyCheckElapsed.restart ();
    m_jttyStageTimer.start ();
    ft8->trigger ();
    if (decode->isChecked () || m_window->decoderBusy ()) {
      fail (tr ("Switching modes did not cancel JTTY review and clear its Decode state."));
      return;
    }
    return;
  }
  if (progress.front ()->isHidden ()) {
    fail (tr ("Switching from JTTY to FT8 did not restore timed-mode progress."));
    return;
  }
  completeJttyTest ();
}

void LiveAudioTestController::completeJttyTest ()
{
  m_finished = true;
  m_succeeded = true;
  m_timeout.stop ();
  m_modalTimer.stop ();
  m_jttyPollTimer.stop ();
  m_jttyStageTimer.stop ();
  std::cout << "WSJT-X JTTY live audio test passed: expected="
            << m_expectedJtty.size ()
            << " all_prefix=" << m_jttyAllSawPrefix
            << " qso_prefix=" << m_jttyQsoSawPrefix
            << " capture_offset_ms=" << m_captureStartMs % 180000
            << " boundary_crossed=1"
            << " review_isolated=1 recording_saved=1 wav_logged=1 frequency_policy=1 status_verified=1"
            << " retained_options_refreshed=1 review_cancelled=1 drain_verified=1"
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
  m_jttyPollTimer.stop ();
  m_jttyStageTimer.stop ();
  std::cerr << "WSJT-X live audio test failed: " << reason.toStdString ()
            << " expected=" << (Mode::Jtty == m_mode ? m_expectedJtty.size () : m_expected.size ())
            << " observed=" << (Mode::Jtty == m_mode ? m_jttyAllFinals.size () : m_observed.size ())
            << " displayed=" << (Mode::Jtty == m_mode ? m_jttyQsoFinals.size () : m_displayed.size ())
            << " frames=" << m_emittedFrames
            << " decode_cycles=" << m_completedCycles << std::endl;
  if (Mode::Jtty != m_mode)
    {
      std::cerr << "WSJT-X live audio test: submitted rollover epoch=" << m_rolloverEpoch
                << " accepted=" << m_rolloverAcceptedBlocks
                << " end=" << m_rolloverAcceptedEnd
                << " rejected=" << m_rolloverRejectedBlocks
                << " generation=" << m_submittedGeneration
                << " active_generation=" << m_window->liveAudioTestPublishedDecoderGeneration ()
                << " completions=" << m_submittedCompletions << std::endl;
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
      char const* stage = "Live";
      switch (m_jttyCheckStage) {
      case JttyCheckStage::Live: break;
      case JttyCheckStage::Review: stage = "Review"; break;
      case JttyCheckStage::Stopped: stage = "Stopped"; break;
      case JttyCheckStage::Wav: stage = "Wav"; break;
      case JttyCheckStage::Drain: stage = "Drain"; break;
      case JttyCheckStage::TimedMode: stage = "TimedMode"; break;
      }
      std::cerr << "WSJT-X JTTY live audio test: stage=" << stage
                << " busy=" << m_window->decoderBusy ()
                << " fixture_finished=" << m_fixtureFinished << std::endl;
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
