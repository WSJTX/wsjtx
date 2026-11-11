#include "FootswitchInput.hpp"

FootswitchInput::FootswitchInput (QObject * parent)
  : QObject {parent}
  , port_ {new QSerialPort {this}}
  , pin_ {Pin::CTS}
  , armed_ {false}
  , reported_waiting_ {false}
  , read_ok_since_open_ {false}
  , pressed_ {false}
  , candidate_ {false}
  , stable_reads_ {0}
{
  poll_timer_.setInterval (poll_interval_ms);
  connect (&poll_timer_, &QTimer::timeout, this, &FootswitchInput::poll);
  retry_timer_.setInterval (retry_interval_ms);
  connect (&retry_timer_, &QTimer::timeout, this, &FootswitchInput::retryPort);
}

FootswitchInput::~FootswitchInput ()
{
  blockSignals (true);   // receivers may already be partly destroyed
  stop ();
}

bool FootswitchInput::start (QString const& port_name, Pin pin)
{
  stop ();
  port_name_ = port_name;
  pin_ = pin;
  if (!openPort ())
    {
      Q_EMIT error (tr ("Footswitch: cannot open %1: %2")
                    .arg (port_name, open_error_));
      retry_timer_.start ();
      return false;
    }
  startPolling ([this] {return readPort ();});
  return true;
}

void FootswitchInput::start (Reader reader, Pin pin)
{
  stop ();
  pin_ = pin;
  startPolling (std::move (reader));
}

void FootswitchInput::stop ()
{
  retry_timer_.stop ();
  poll_timer_.stop ();
  reader_ = nullptr;
  port_name_.clear ();
  if (port_->isOpen ())
    {
      port_->setDataTerminalReady (false);
      port_->close ();
    }
  releaseIfPressed ();
}

bool FootswitchInput::openPort ()
{
  if (port_->isOpen ()) port_->close ();
  open_error_.clear ();
  guard_refused_ = false;
  // Check again on every open: a device name can refer to a different
  // adapter after adapters are plugged in or out, and opening asserts DTR.
  if (port_guard_)
    {
      open_error_ = port_guard_ (port_name_);
      guard_refused_ = !open_error_.isEmpty ();
      if (guard_refused_) return false;
    }
  port_->clearError ();
  port_->setPortName (port_name_);
  if (!port_->open (QIODevice::ReadWrite))
    {
      open_error_ = port_->errorString ();
      return false;
    }
  port_->setFlowControl (QSerialPort::NoFlowControl);
  port_->setDataTerminalReady (true); // supply for the footswitch contact
  port_->setRequestToSend (false);
  // QSerialPort errors are sticky; a setter the driver does not support
  // must not make every later reading look like a failure.
  port_->clearError ();
  read_ok_since_open_ = false;
  return true;
}

void FootswitchInput::startPolling (Reader reader)
{
  reader_ = std::move (reader);
  armed_ = false;
  reported_waiting_ = false;
  candidate_ = false;
  stable_reads_ = 0;
  poll_timer_.start ();
}

void FootswitchInput::releaseIfPressed ()
{
  armed_ = false;
  candidate_ = false;
  stable_reads_ = 0;
  if (pressed_)
    {
      pressed_ = false;
      Q_EMIT released ();   // never leave the caller transmitting
    }
}

void FootswitchInput::retryPort ()
{
  if (port_name_.isEmpty ())
    {
      retry_timer_.stop ();
      return;
    }
  if (openPort ())
    {
      retry_timer_.stop ();
      startPolling ([this] {return readPort ();});
      Q_EMIT restored (port_name_);
    }
  else if (guard_refused_)
    {
      // Refused by the guard (not a missing device): say why.
      Q_EMIT error (tr ("Footswitch: cannot open %1: %2").arg (port_name_, open_error_));
    }
}

FootswitchInput::Reading FootswitchInput::readPort ()
{
  Reading reading;
  reading.lines = port_->pinoutSignals ();
  if (QSerialPort::NoError != port_->error ()
      && QSerialPort::TimeoutError != port_->error ())
    {
      reading.ok = false;
      reading.error = port_->errorString ();
    }
  return reading;
}

void FootswitchInput::poll ()
{
  if (!reader_) return;
  auto const reading = reader_ ();
  if (!reading.ok)
    {
      auto reason = tr ("Footswitch: port error: %1").arg (reading.error);
      poll_timer_.stop ();
      reader_ = nullptr;
      releaseIfPressed ();
      if (!port_name_.isEmpty ())
        {
          port_->close ();
          if (read_ok_since_open_)
            {
              retry_timer_.start (); // the device may come back; keep trying
            }
          else
            {
              // Failed straight after opening: the port cannot report its
              // status lines, so retrying would only loop.
              reason = tr ("Footswitch: %1 cannot read its input lines: %2")
                .arg (port_name_, reading.error);
              port_name_.clear ();
            }
        }
      Q_EMIT error (reason);
      return;
    }
  read_ok_since_open_ = true;

  QSerialPort::PinoutSignal mask {QSerialPort::ClearToSendSignal};
  switch (pin_)
    {
    case Pin::CTS: mask = QSerialPort::ClearToSendSignal; break;
    case Pin::DSR: mask = QSerialPort::DataSetReadySignal; break;
    case Pin::DCD: mask = QSerialPort::DataCarrierDetectSignal; break;
    case Pin::RI:  mask = QSerialPort::RingIndicatorSignal; break;
    }
  bool const active = reading.lines & mask;

  if (active != candidate_ || 0 == stable_reads_)
    {
      candidate_ = active;
      stable_reads_ = 1;
    }
  else if (stable_reads_ < debounce_count)
    {
      ++stable_reads_;
    }
  if (stable_reads_ < debounce_count) return;

  if (!armed_)
    {
      if (!active)
        {
          armed_ = true;      // seen released: presses count from now on
          if (reported_waiting_) Q_EMIT readyAfterRelease ();
        }
      else if (!reported_waiting_)
        {
          reported_waiting_ = true;
          Q_EMIT waitingForRelease ();
        }
      return;
    }
  if (active != pressed_)
    {
      pressed_ = active;
      if (pressed_) Q_EMIT pressed ();
      else Q_EMIT released ();
    }
}
