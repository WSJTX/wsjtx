#include "ReceiveHandoffTestController.hpp"

#include "Audio/FixtureAudioInput.hpp"
#include "widgets/mainwindow.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

#include <QAbstractButton>
#include <QAction>
#include <QCoreApplication>
#include <QMetaObject>

#include "moc_ReceiveHandoffTestController.cpp"

namespace
{
constexpr qint16 periodBPattern = 5678;
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
    finish (false, tr ("Timed out waiting for the full receive handoff."));
  });

  // Direct delivery is intentional: prepare() blocks the GUI thread so real
  // framesWritten callbacks remain queued while the audio thread produces B.
  connect (fixture_, &FixtureAudioInput::emissionFinished, this,
           [this] (qint64) {
             std::lock_guard<std::mutex> lock {producer_mutex_};
             producer_finished_ = true;
             producer_changed_.notify_all ();
           }, Qt::DirectConnection);
  connect (fixture_, &AudioInputSource::error, this,
           [this] (QString const& error) {
             std::lock_guard<std::mutex> lock {producer_mutex_};
             producer_error_ = error;
             producer_changed_.notify_all ();
           }, Qt::DirectConnection);
  connect (fixture_, &AudioInputSource::status, this,
           [] (QString const& status) {
             std::cerr << "WSJT-X receive handoff fixture: "
                       << status.toStdString () << std::endl;
           }, Qt::DirectConnection);
  connect (fixture_, &FixtureAudioInput::emissionStarted, this,
           [] (qint64 timestamp) {
             std::cerr << "WSJT-X receive handoff fixture started at "
                       << timestamp << std::endl;
           }, Qt::DirectConnection);

  connect (window_, &MainWindow::liveAudioTestReceiveRejected, this,
           [this] (qint64) { ++rejected_; });
  connect (window_, &MainWindow::liveAudioTestReceiveCallback, this,
           [this] (qint64 notifiedFrames, qint64 currentFrames,
                   qint16 observedLastSample) {
             if (callback_observed_ || notifiedFrames <= 0) return;
             callback_observed_ = true;
             // Evaluate after MainWindow's real dataSink invocation returns.
             QTimer::singleShot (0, this,
               [this, notifiedFrames, currentFrames, observedLastSample] {
                 if (currentFrames != periodFrames || notifiedFrames >= currentFrames)
                   {
                     finish (false, tr ("The delayed-delivery fixture did not establish "
                                        "the expected A-behind-B backlog: notified=%1 current=%2.")
                                      .arg (notifiedFrames).arg (currentFrames));
                   }
                 else if (!rejected_ || observedLastSample != periodBPattern)
                   {
                     finish (false, tr ("Stale A was not rejected or accepted B at %1 "
                                        "observed sample %2 instead of B sample %3.")
                                      .arg (notifiedFrames).arg (observedLastSample)
                                      .arg (periodBPattern));
                   }
                 else
                   {
                     finish (true, tr ("Delayed A was rejected; accepted B retained B data."));
                   }
               });
           });
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
  if (!monitor->isEnabled ())
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
  if (!QMetaObject::invokeMethod (fixture_, "arm", Qt::QueuedConnection))
    {
      finish (false, tr ("Unable to arm the receive handoff fixture."));
      return;
    }

  bool completed;
  QString error;
  {
    std::unique_lock<std::mutex> lock {producer_mutex_};
    completed = producer_changed_.wait_for (
      lock, std::chrono::seconds {10},
      [this] { return producer_finished_ || !producer_error_.isEmpty (); });
    error = producer_error_;
  }
  if (!completed)
    {
      finish (false, tr ("Audio thread did not produce both receive periods."));
    }
  else if (!error.isEmpty ())
    {
      finish (false, tr ("Receive fixture failed: %1").arg (error));
    }
  // Returning releases the queued MainWindow framesWritten callbacks.
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
