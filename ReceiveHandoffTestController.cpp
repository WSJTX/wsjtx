#include "ReceiveHandoffTestController.hpp"

#include "Audio/FixtureAudioInput.hpp"
#include "commons.h"
#include "widgets/mainwindow.h"

extern dec_data_t& dec_data;

#include <chrono>
#include <cstdlib>
#include <iostream>

#include <QAbstractButton>
#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>

#include "moc_ReceiveHandoffTestController.cpp"

namespace
{
constexpr qint64 periodFrames = 15 * 12000;
}

ReceiveHandoffTestController::ReceiveHandoffTestController (
  MainWindow * window, FixtureAudioInput * fixture, QObject * parent)
  : QObject {parent}
  , window_ {window}
  , fixture_ {fixture}
{
  retry_.setSingleShot (true);
  retry_.setInterval (50);
  connect (&retry_, &QTimer::timeout, this, &ReceiveHandoffTestController::prepare);
  timeout_.setSingleShot (true);
  timeout_.setInterval (20000);
  connect (&timeout_, &QTimer::timeout, this, [this] {
    finish (false, tr ("Timed out waiting for the full receive handoff. ") + diagnostics ());
  });

  auto const producer = producer_;
  connect (fixture_, &FixtureAudioInput::handoffCheckpoint, this,
           [producer] (qint64 frames) {
             std::lock_guard<std::mutex> lock {producer->mutex};
             producer->frames = frames;
             producer->finished = true;
             producer->changed.notify_all ();
           }, Qt::DirectConnection);
  connect (fixture_, &AudioInputSource::error, this,
           [producer] (QString const& error) {
             std::lock_guard<std::mutex> lock {producer->mutex};
             producer->error = error;
             producer->changed.notify_all ();
           }, Qt::DirectConnection);
  connect (window_, &MainWindow::liveAudioTestReceiveRange,
           this, &ReceiveHandoffTestController::observe);
  connect (window_, &MainWindow::ft8DecoderInvocation, this,
           [this] (bool, int, int, int, bool, int, int halfSymbols, int, int, int) {
             if (halfSymbols == 49) ++final_submissions_;
           });
  connect (window_, &MainWindow::decodeCycleStarted, this,
           [this] (quint64 generation) { decoder_generation_ = generation; });
}

void ReceiveHandoffTestController::begin ()
{
  timeout_.start ();
  prepare ();
}

void ReceiveHandoffTestController::prepare ()
{
  if (finished_) return;
  auto * ft8 = window_->findChild<QAction *> ("actionFT8");
  auto * monitor = window_->findChild<QAbstractButton *> ("monitorButton");
  if (!ft8 || !monitor)
    {
      finish (false, tr ("Required FT8 or monitoring control was not found."));
      return;
    }
  if (!monitor->isEnabled () || !window_->decoderBackendRunning ())
    {
      retry_.start ();
      return;
    }

  ft8->trigger ();
  if (!monitor->isChecked ()) monitor->click ();
  if (!window_->monitoringActive ())
    {
      retry_.start ();
      return;
    }
  if (!window_->configureLiveAudioTestHandoff ())
    {
      finish (false, tr ("Unable to configure ordinary FT8 final start."));
      return;
    }

  resetScenario (QStringLiteral ("within-period one-second backlog"));
  if (!advance (0, true) || !drain ()) return;
  for (qint64 offset = 3456; offset < 153600; offset += 3456)
    if (!advance (offset) || !drain ()) return;
  if (!advance (153600) || !drain ()) return;
  auto const beforeStall = committed_;
  if (!advance (165600)) return;
  if (!require (committed_ == beforeStall,
                tr ("GUI consumed audio during the withheld second.")) || !drain ()) return;
  if (!require (!rejected_ && committed_ > beforeStall && committed_ < 165600,
                tr ("Backlog did not commit losslessly with a partial tail."))) return;
  auto const beforeFlush = accepted_;
  if (!advance (165600, false, true) || !drain ()) return;
  if (!require (committed_ == 165600 && accepted_ == beforeFlush + 1,
                tr ("Partial tail was not delivered exactly once."))) return;
  if (!advance (165600, false, true) || !drain ()) return;
  if (!require (accepted_ == beforeFlush + 1,
                tr ("Repeated flush duplicated the partial tail."))) return;
  std::cerr << "WSJT-X receive handoff stage passed: "
            << diagnostics ().toStdString () << std::endl;

  resetScenario (QStringLiteral ("one-second backlog across rollover"));
  if (!advance (0, true) || !drain ()) return;
  for (qint64 offset = 3456; offset < 168600; offset += 3456)
    if (!advance (offset) || !drain ()) return;
  if (!advance (168600) || !drain ()) return;
  if (!require (!final_submissions_ && !rejected_,
                tr ("A was rejected or a final was submitted by 14.05 seconds."))) return;
  obsolete_epoch_ = epoch_;
  rejected_end_ = committed_;
  auto const acceptedA = accepted_;
  if (!advance (180600) || !drain ()) return;
  if (!require (rejected_ == 4 && rejected_end_ == 179712
                && accepted_ == acceptedA && !final_submissions_,
                tr ("Obsolete A entered DSP or submitted a final pass."))) return;
  sample_period_ = 1;
  epoch_ = 0;
  committed_ = 0;
  if (!advance (183456) || !drain ()) return;
  if (!require (committed_ == 3456 && epoch_ != obsolete_epoch_,
                tr ("B did not begin with its first complete block at zero."))) return;
  for (qint64 offset = 186912; offset <= 193824; offset += 3456)
    if (!advance (offset) || !drain ()) return;
  if (!require (committed_ == 13824 && accepted_ == acceptedA + 4
                && !final_submissions_, tr ("B stopped advancing after rollover."))) return;
  std::cerr << "WSJT-X receive handoff stage passed: "
            << diagnostics ().toStdString () << std::endl;

  resetScenario (QStringLiteral ("delayed complete-period handoff"));
  sample_period_ = 1;
  if (!advance (2 * periodFrames, true) || !drain ()) return;
  if (!require (rejected_ > 0 && accepted_ > 0 && committed_ == 179712,
                tr ("Delayed A/B handoff did not reject A and commit B."))) return;
  std::cerr << "WSJT-X receive handoff stage passed: "
            << diagnostics ().toStdString () << std::endl;
  QTimer::singleShot (0, this, [this] {
    finish (true, tr ("All three receive handoff stages passed."));
  });
}

void ReceiveHandoffTestController::resetScenario (QString const& name)
{
  scenario_ = name;
  scenario_error_.clear ();
  epoch_ = 0;
  obsolete_epoch_ = 0;
  sample_period_ = 0;
  accepted_ = rejected_ = committed_ = rejected_end_ = final_submissions_ = 0;
}

QString ReceiveHandoffTestController::diagnostics () const
{
  return tr ("scenario=%1 capture=%2 epoch=%3 obsolete=%4 committed=%5 "
             "accepted=%6 rejected=%7 rejected-end=%8 final-submissions=%9 generation=%10")
    .arg (scenario_).arg (capture_).arg (epoch_).arg (obsolete_epoch_)
    .arg (committed_).arg (accepted_).arg (rejected_).arg (rejected_end_)
    .arg (final_submissions_).arg (decoder_generation_);
}

bool ReceiveHandoffTestController::require (bool condition, QString const& message)
{
  if (!condition) finish (false, message + " " + diagnostics ());
  return condition;
}

bool ReceiveHandoffTestController::advance (qint64 frames, bool fresh, bool flush)
{
  {
    std::lock_guard<std::mutex> lock {producer_->mutex};
    producer_->finished = false;
    producer_->frames = -1;
  }
  capture_ = frames;
  auto * fixture = fixture_;
  if (!require (QMetaObject::invokeMethod (fixture, [fixture, frames, fresh, flush] {
        fixture->advanceHandoff (frames, fresh, flush);
      }, Qt::QueuedConnection), tr ("Unable to command the audio fixture."))) return false;
  bool completed;
  QString error;
  qint64 acknowledged;
  {
    std::unique_lock<std::mutex> lock {producer_->mutex};
    completed = producer_->changed.wait_for (
      lock, std::chrono::seconds {10},
      [this] { return producer_->finished || !producer_->error.isEmpty (); });
    error = producer_->error;
    acknowledged = producer_->frames;
  }
  return require (completed && error.isEmpty () && acknowledged == frames,
                  tr ("Capture checkpoint failed: acknowledged=%1 error=%2")
                    .arg (acknowledged).arg (error));
}

bool ReceiveHandoffTestController::drain ()
{
  QCoreApplication::sendPostedEvents (window_, QEvent::MetaCall);
  return require (scenario_error_.isEmpty (), scenario_error_);
}

void ReceiveHandoffTestController::observe (quint64 epoch, int start, int end, bool accepted)
{
  if (!scenario_error_.isEmpty ()) return;
  auto reject = [this, epoch, start, end] (QString const& reason) {
    scenario_error_ = tr ("%1 range=[%2,%3) epoch=%4")
      .arg (reason).arg (start).arg (end).arg (epoch);
  };
  if (!accepted)
    {
      ++rejected_;
      if (scenario_ == QStringLiteral ("within-period one-second backlog"))
        reject (tr ("Current-period audio was rejected."));
      if (obsolete_epoch_ && (epoch != obsolete_epoch_ || start != rejected_end_))
        reject (tr ("Rejected A ranges lost their epoch or continuity."));
      rejected_end_ = end;
      return;
    }
  ++accepted_;
  if (!epoch_) epoch_ = epoch;
  if (epoch != epoch_ || start != committed_ || end <= start)
    {
      reject (tr ("Accepted ranges changed epoch, duplicated, or skipped samples."));
      return;
    }
  for (int index = 0; index < end; ++index)
    if (dec_data.d2[index] != FixtureAudioInput::handoffSample (sample_period_, index))
      {
        reject (tr ("Committed sample %1 was %2, expected %3.")
                  .arg (index).arg (dec_data.d2[index])
                  .arg (FixtureAudioInput::handoffSample (sample_period_, index)));
        return;
      }
  committed_ = end;
}

void ReceiveHandoffTestController::finish (bool success, QString const& message)
{
  if (finished_) return;
  finished_ = true;
  succeeded_ = success;
  timeout_.stop ();
  retry_.stop ();
  std::cerr << "WSJT-X receive handoff test " << (success ? "passed: " : "failed: ")
            << message.toStdString () << std::endl;
  window_->close ();
  QCoreApplication::exit (success ? EXIT_SUCCESS : EXIT_FAILURE);
}
