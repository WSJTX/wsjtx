#include "FootswitchInput.hpp"

FootswitchInput::FootswitchInput (QObject * parent)
  : QObject {parent}
  , port_ {new QSerialPort {this}}
  , pin_ {Pin::CTS}
  , invert_ {false}
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

bool FootswitchInput::start (QString const& port_name, Pin pin, bool invert)
{
  stop ();
  port_name_ = port_name;
  pin_ = pin;
  invert_ = invert;
  if (!openPort ())
    {
      Q_EMIT error (tr ("Footswitch: cannot open %1: %2")
                    .arg (port_name, port_->errorString ()));
      retry_timer_.start ();
      return false;
    }
  startPolling ([this] {return readPort ();});
  return true;
}

void FootswitchInput::start (Reader reader, Pin pin, bool invert)
{
  stop ();
  pin_ = pin;
  invert_ = invert;
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
  port_->clearError ();
  port_->setPortName (port_name_);
  if (!port_->open (QIODevice::ReadWrite)) return false;
  port_->setFlowControl (QSerialPort::NoFlowControl);
  port_->setDataTerminalReady (true); // supply for the footswitch contact
  port_->setRequestToSend (false);
  return true;
}

void FootswitchInput::startPolling (Reader reader)
{
  reader_ = std::move (reader);
  candidate_ = false;
  stable_reads_ = 0;
  poll_timer_.start ();
}

void FootswitchInput::releaseIfPressed ()
{
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
      auto const reason = tr ("Footswitch: port error: %1").arg (reading.error);
      poll_timer_.stop ();
      reader_ = nullptr;
      releaseIfPressed ();
      if (!port_name_.isEmpty ())
        {
          port_->close ();      // the device may come back; keep trying
          retry_timer_.start ();
        }
      Q_EMIT error (reason);
      return;
    }

  QSerialPort::PinoutSignal mask {QSerialPort::ClearToSendSignal};
  switch (pin_)
    {
    case Pin::CTS: mask = QSerialPort::ClearToSendSignal; break;
    case Pin::DSR: mask = QSerialPort::DataSetReadySignal; break;
    case Pin::DCD: mask = QSerialPort::DataCarrierDetectSignal; break;
    case Pin::RI:  mask = QSerialPort::RingIndicatorSignal; break;
    }
  bool const level = reading.lines & mask;
  bool const active = invert_ ? !level : level;

  if (active != candidate_)
    {
      candidate_ = active;
      stable_reads_ = 1;
      return;
    }
  if (stable_reads_ < debounce_count) ++stable_reads_;
  if (stable_reads_ >= debounce_count && active != pressed_)
    {
      pressed_ = active;
      if (pressed_) Q_EMIT pressed ();
      else Q_EMIT released ();
    }
}
