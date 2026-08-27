#include "TciSimScript.hpp"

#include <utility>

#include <QEventLoop>

#include "moc_TciSimScript.cpp"

TciSimScript::TciSimScript (TciSimServer * server, QObject * parent)
  : QObject {parent}
  , server_ {server}
{
  poll_timer_.setInterval (5);
  delay_timer_.setSingleShot (true);
  overall_timer_.setSingleShot (true);
  connect (&poll_timer_, &QTimer::timeout,
           this, &TciSimScript::poll_step);
  connect (&delay_timer_, &QTimer::timeout,
           this, &TciSimScript::delayed_step_ready);
  connect (&overall_timer_, &QTimer::timeout,
           this, &TciSimScript::overall_timeout);
  connect (server_, &TciSimServer::client_gone, this,
           [this] (int generation)
           {
             if (generation != close_generation_) return;
             close_observed_ = true;
             cursor_ = server_->cursor (server_->transcript ().size ());
           });
}

TciSimScript& TciSimScript::greet (
  TciSimGreeting greeting_value, int timeout_ms)
{
  steps_.push_back ({StepKind::Greet, {}, {}, qMax (0, timeout_ms),
                     greeting_value});
  return *this;
}

TciSimScript& TciSimScript::expect (
  QString verb, ArgsPredicate predicate, int timeout_ms)
{
  append_expectation (StepKind::Expect, std::move (verb),
                      std::move (predicate), timeout_ms);
  return *this;
}

TciSimScript& TciSimScript::reply (QString text, int delay_ms)
{
  steps_.push_back ({StepKind::Reply, std::move (text), {},
                     qMax (0, delay_ms), TciSimGreeting::Minimal});
  return *this;
}

TciSimScript& TciSimScript::push (QString text, int delay_ms)
{
  steps_.push_back ({StepKind::Push, std::move (text), {},
                     qMax (0, delay_ms), TciSimGreeting::Minimal});
  return *this;
}

TciSimScript& TciSimScript::withhold (
  QString verb, ArgsPredicate predicate, int timeout_ms)
{
  append_expectation (StepKind::Withhold, std::move (verb),
                      std::move (predicate), timeout_ms);
  return *this;
}

TciSimScript& TciSimScript::delay (int delay_ms)
{
  steps_.push_back ({StepKind::Delay, {}, {}, qMax (0, delay_ms),
                     TciSimGreeting::Minimal});
  return *this;
}

TciSimScript& TciSimScript::close_normally (QString reason)
{
  steps_.push_back ({StepKind::CloseNormally, std::move (reason), {}, 2000,
                     TciSimGreeting::Minimal});
  return *this;
}

TciSimScript& TciSimScript::reconnect (int timeout_ms)
{
  steps_.push_back ({StepKind::Reconnect, {}, {}, qMax (0, timeout_ms),
                     TciSimGreeting::Minimal});
  return *this;
}

void TciSimScript::start (int overall_timeout_ms)
{
  if (running_) return;

  running_ = true;
  succeeded_ = false;
  current_step_ = 0;
  reconnect_after_generation_ = server_->connection_generation ();
  active_connection_generation_ = server_->client_connected ()
    ? server_->connection_generation () : 0;
  last_consumed_generation_ = 0;
  delayed_step_generation_ = 0;
  close_generation_ = 0;
  close_observed_ = false;
  has_consumed_expectation_ = false;
  close_requested_ = false;
  cursor_ = server_->cursor ();
  last_error_.clear ();
  overall_timer_.start (qMax (1, overall_timeout_ms));
  QTimer::singleShot (0, this, &TciSimScript::execute_step);
}

bool TciSimScript::run (int overall_timeout_ms)
{
  QEventLoop loop;
  connect (this, &TciSimScript::finished, &loop, &QEventLoop::quit);
  start (overall_timeout_ms);
  if (running_) loop.exec ();
  return succeeded_;
}

void TciSimScript::stop ()
{
  if (running_) fail (QStringLiteral ("script stopped"));
}

bool TciSimScript::running () const
{
  return running_;
}

bool TciSimScript::succeeded () const
{
  return succeeded_;
}

int TciSimScript::failed_step () const
{
  return succeeded_ ? -1 : current_step_ + 1;
}

QString TciSimScript::last_error () const
{
  return last_error_;
}

QStringList TciSimScript::greeting_messages (TciSimGreeting greeting)
{
  switch (greeting)
    {
    case TciSimGreeting::Thetis:
      return {
        QStringLiteral ("protocol:Thetis,1.6"),
        QStringLiteral ("device:HermesLite"),
        QStringLiteral ("trx_count:1"),
        QStringLiteral ("channels_count:2"),
        QStringLiteral ("vfo:0,0,14074000"),
        QStringLiteral ("vfo:0,1,14075000"),
        QStringLiteral ("modulation:0,digu"),
        QStringLiteral ("split_enable:0,false"),
        QStringLiteral ("trx:0,false"),
        QStringLiteral ("start"),
        QStringLiteral ("ready"),
      };
    case TciSimGreeting::Minimal:
      return {
        QStringLiteral ("protocol:TCI,1.6"),
        QStringLiteral ("start"),
        QStringLiteral ("ready"),
      };
    }
  return {};
}

void TciSimScript::execute_step ()
{
  if (!running_) return;
  if (current_step_ >= static_cast<int> (steps_.size ()))
    {
      finish (true);
      return;
    }

  auto const& step = steps_.at (current_step_);
  switch (step.kind)
    {
    case StepKind::Greet:
      if (!server_->client_connected ())
        {
          start_polling (step.timeout_ms);
          return;
        }
      active_connection_generation_ = server_->connection_generation ();
      last_consumed_generation_ = 0;
      has_consumed_expectation_ = false;
      for (auto const& message : greeting_messages (step.greeting))
        {
          server_->send_text (message);
        }
      complete_step ();
      return;
    case StepKind::Expect:
    case StepKind::Withhold:
      if (current_expectation_satisfied ())
        {
          complete_step ();
          return;
        }
      start_polling (step.timeout_ms);
      return;
    case StepKind::Reply:
      if (!has_consumed_expectation_
          || last_consumed_generation_ != active_connection_generation_)
        {
          fail (QStringLiteral (
            "reply requires an expectation consumed on the active connection"));
          return;
        }
      delayed_step_generation_ = last_consumed_generation_;
      if (step.timeout_ms > 0)
        {
          delay_timer_.start (step.timeout_ms);
        }
      else
        {
          delayed_step_ready ();
        }
      return;
    case StepKind::Push:
    case StepKind::Delay:
      delayed_step_generation_ = active_connection_generation_;
      if (step.timeout_ms > 0)
        {
          delay_timer_.start (step.timeout_ms);
        }
      else
        {
          delayed_step_ready ();
        }
      return;
    case StepKind::CloseNormally:
      if (!close_requested_)
        {
          if (!server_->client_connected ())
            {
              fail (QStringLiteral ("cannot close without a connected client"));
              return;
            }
          close_generation_ = server_->connection_generation ();
          reconnect_after_generation_ = close_generation_;
          close_observed_ = false;
          close_requested_ = true;
          server_->close_client (step.text);
          start_polling (step.timeout_ms);
        }
      return;
    case StepKind::Reconnect:
      if (server_->client_connected ()
          && server_->connection_generation () > reconnect_after_generation_)
        {
          active_connection_generation_ = server_->connection_generation ();
          last_consumed_generation_ = 0;
          has_consumed_expectation_ = false;
          complete_step ();
          return;
        }
      start_polling (step.timeout_ms);
      return;
    }
}

void TciSimScript::poll_step ()
{
  if (!running_) return;
  auto const& step = steps_.at (current_step_);
  bool complete {false};
  if (step.kind == StepKind::Greet)
    {
      complete = server_->client_connected ();
      if (complete)
        {
          poll_timer_.stop ();
          execute_step ();
          return;
        }
    }
  else if (step.kind == StepKind::Reconnect)
    {
      complete = server_->client_connected ()
        && server_->connection_generation () > reconnect_after_generation_;
    }
  else if (step.kind == StepKind::CloseNormally)
    {
      complete = close_observed_;
    }
  else
    {
      complete = current_expectation_satisfied ();
    }

  if (complete)
    {
      poll_timer_.stop ();
      if (step.kind == StepKind::CloseNormally)
        {
          close_requested_ = false;
        }
      else if (step.kind == StepKind::Reconnect)
        {
          active_connection_generation_ = server_->connection_generation ();
          last_consumed_generation_ = 0;
          has_consumed_expectation_ = false;
        }
      complete_step ();
    }
  else if (step_elapsed_.elapsed () >= step.timeout_ms)
    {
      fail (QStringLiteral ("%1 timed out after %2 ms")
            .arg (step_description (step)).arg (step.timeout_ms));
    }
}

void TciSimScript::delayed_step_ready ()
{
  if (!running_) return;
  auto const& step = steps_.at (current_step_);
  if (step.kind == StepKind::Reply || step.kind == StepKind::Push)
    {
      if (server_->client_connected ()
          && server_->connection_generation () == delayed_step_generation_)
        {
          server_->send_text (step.text);
        }
      else if (step.kind == StepKind::Reply)
        {
          fail (QStringLiteral ("reply target connection is no longer active"));
          return;
        }
    }
  complete_step ();
}

void TciSimScript::overall_timeout ()
{
  if (running_) fail (QStringLiteral ("overall script timeout"));
}

void TciSimScript::append_expectation (
  StepKind kind, QString verb, ArgsPredicate predicate, int timeout_ms)
{
  steps_.push_back ({kind, std::move (verb), std::move (predicate),
                     qMax (0, timeout_ms), TciSimGreeting::Minimal});
}

void TciSimScript::start_polling (int)
{
  step_elapsed_.restart ();
  poll_timer_.start ();
}

void TciSimScript::complete_step ()
{
  poll_timer_.stop ();
  delay_timer_.stop ();
  ++current_step_;
  QTimer::singleShot (0, this, &TciSimScript::execute_step);
}

void TciSimScript::fail (QString reason)
{
  auto const step_number = current_step_ + 1;
  last_error_ = QStringLiteral ("step %1: %2\nTranscript:\n%3")
    .arg (step_number).arg (reason, server_->transcript_text ());
  finish (false);
}

void TciSimScript::finish (bool succeeded_value)
{
  poll_timer_.stop ();
  delay_timer_.stop ();
  overall_timer_.stop ();
  running_ = false;
  succeeded_ = succeeded_value;
  emit finished (succeeded_);
}

bool TciSimScript::current_expectation_satisfied ()
{
  auto const& step = steps_.at (current_step_);
  TciSimServer::Message message;
  if (!server_->consume (&cursor_, TciSimServer::Direction::ClientToServer,
                         step.text, &message, step.predicate,
                         active_connection_generation_))
    {
      return false;
    }
  last_consumed_generation_ = message.connection_generation;
  has_consumed_expectation_ = true;
  return true;
}

QString TciSimScript::step_description (Step const& step) const
{
  switch (step.kind)
    {
    case StepKind::Greet: return QStringLiteral ("waiting to greet a client");
    case StepKind::Expect: return QStringLiteral ("expecting %1").arg (step.text);
    case StepKind::Reply: return QStringLiteral ("replying with %1").arg (step.text);
    case StepKind::Push: return QStringLiteral ("pushing %1").arg (step.text);
    case StepKind::Withhold:
      return QStringLiteral ("expecting and withholding %1").arg (step.text);
    case StepKind::Delay: return QStringLiteral ("delaying");
    case StepKind::CloseNormally: return QStringLiteral ("closing the client");
    case StepKind::Reconnect: return QStringLiteral ("waiting for reconnection");
    }
  return QStringLiteral ("running script step");
}
