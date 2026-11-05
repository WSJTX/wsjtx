// -*- Mode: C++ -*-
#ifndef FOOTSWITCH_INPUT_HPP__
#define FOOTSWITCH_INPUT_HPP__

#include <functional>

#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QTimer>

//
// FootswitchInput - reads a footswitch wired to a serial port status line
//
// The port is opened without CAT traffic. DTR is asserted so that it
// can supply the positive voltage for a simple contact-closure
// footswitch wired from DTR to the chosen input pin (with a pull-down
// of around 10k to ground). The input pin is polled and debounced,
// and pressed() / released() are emitted on each stable change.
//
class FootswitchInput final
  : public QObject
{
  Q_OBJECT

public:
  enum class Pin {CTS, DSR, DCD, RI};

  // One reading of the port's status lines.
  struct Reading
  {
    bool ok {true};
    QSerialPort::PinoutSignals lines {QSerialPort::NoSignal};
    QString error;              // set when !ok
  };
  using Reader = std::function<Reading ()>;

  static int constexpr poll_interval_ms {20};
  static int constexpr debounce_count {3}; // consecutive identical reads

  explicit FootswitchInput (QObject * parent = nullptr);
  ~FootswitchInput ();

  // Open the port and start polling. Returns false and emits error()
  // if the port cannot be opened. Calling again reconfigures.
  bool start (QString const& port_name, Pin pin, bool invert);

  // Start polling from an arbitrary source instead of a serial port.
  // Used by the unit tests to feed simulated line levels.
  void start (Reader reader, Pin pin, bool invert);

  void stop ();

  bool isActive () const {return poll_timer_.isActive ();}
  bool isPressed () const {return pressed_;}

  // Take one reading now; normally called by the poll timer.
  void poll ();

  Q_SIGNAL void pressed ();
  Q_SIGNAL void released ();
  Q_SIGNAL void error (QString const& reason);

private:
  Reading readPort ();

  QSerialPort * port_;
  Reader reader_;
  QTimer poll_timer_;
  Pin pin_;
  bool invert_;
  bool pressed_;
  bool candidate_;
  int stable_reads_;
};

#endif
