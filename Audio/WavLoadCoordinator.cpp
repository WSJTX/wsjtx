#include "WavLoadCoordinator.hpp"

#include <utility>

#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include "DecDataMutex.hpp"

WavLoadCoordinator::WavLoadCoordinator (QObject * parent)
  : QObject {parent}
{
  connect (&watcher_, &QFutureWatcher<Result>::finished,
           this, &WavLoadCoordinator::handleFinished);
}

WavLoadCoordinator::~WavLoadCoordinator ()
{
  waitForFinished ();
  if (loading_)
    {
      set_dec_data_input_blocked (false);
      loading_ = false;
    }
}

bool WavLoadCoordinator::start (Work work)
{
  if (loading_ || !work)
    {
      return false;
    }

  result_.reset ();
  loading_ = true;
  set_dec_data_input_blocked (true);
  QPointer<WavLoadCoordinator> guard {this};
  Q_EMIT loadingChanged (true);
  if (!guard)
    {
      return false;
    }
  watcher_.setFuture (QtConcurrent::run (std::move (work)));
  return true;
}

bool WavLoadCoordinator::isLoading () const
{
  return loading_;
}

WavLoadCoordinator::Result WavLoadCoordinator::result () const
{
  return result_;
}

void WavLoadCoordinator::waitForFinished ()
{
  watcher_.waitForFinished ();
}

void WavLoadCoordinator::handleFinished ()
{
  try
    {
      result_ = watcher_.result ();
    }
  catch (...)
    {
      result_ = std::make_shared<Radio::WavInputResult> ();
    }

  QPointer<WavLoadCoordinator> guard {this};
  Q_EMIT resultReady ();
  if (!guard)
    {
      return;
    }
  set_dec_data_input_blocked (false);
  loading_ = false;
  Q_EMIT loadingChanged (false);
}
