#include "FootswitchInput.hpp"

#include <QSerialPort>

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
}

FootswitchInput::~FootswitchInput ()
{
  blockSignals (true);   // receivers may already be partly destroyed
  stop ();
}

bool FootswitchInput::start (QString const& port_name, Pin pin, bool invert)
{
  stop ();
  pin_ = pin;
  invert_ = invert;
  port_->setPortName (port_name);
  if (!port_->open (QIODevice::ReadWrite))
    {
      Q_EMIT error (tr ("Footswitch: cannot open %1: %2")
                    .arg (port_name, port_->errorString ()));
      return false;
    }
  port_->setFlowControl (QSerialPort::NoFlowControl);
  port_->setDataTerminalReady (true); // supply for the footswitch contact
  port_->setRequestToSend (false);
  poll_timer_.start ();
  return true;
}

void FootswitchInput::stop ()
{
  poll_timer_.stop ();
  if (port_->isOpen ())
    {
      port_->setDataTerminalReady (false);
      port_->close ();
    }
  if (pressed_)
    {
      pressed_ = false;
      Q_EMIT released ();   // never leave the caller transmitting
    }
  candidate_ = false;
  stable_reads_ = 0;
}

bool FootswitchInput::isActive () const
{
  return port_->isOpen () && poll_timer_.isActive ();
}

void FootswitchInput::poll ()
{
  auto const signals_now = port_->pinoutSignals ();
  if (QSerialPort::NoError != port_->error ()
      && QSerialPort::TimeoutError != port_->error ())
    {
      auto const reason = tr ("Footswitch: port error: %1").arg (port_->errorString ());
      stop ();              // releases if pressed
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
  bool const level = signals_now & mask;
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
