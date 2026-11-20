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
// can supply the positive voltage for a normally open footswitch wired
// from DTR to the chosen input pin (with a pull-down of around 10k to
// ground). An active line means pressed, so an unplugged cable, broken
// wire or unpowered adapter all read as released.
//
// The input pin is polled and debounced, and pressed() / released() are
// emitted on each stable change. After every start or reconnection a
// press is accepted only once the switch has first been seen released,
// so a stuck or held switch never keys the transmitter by itself.
//
// If the port cannot be opened, or fails while in use (for example a
// USB adapter losing power), error() is emitted, any press is released,
// and the port is retried every few seconds until it can be opened
// again, when restored() is emitted.
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

  // Asked just before every open of the serial port, including each
  // reconnection retry. Returns why the port must not be opened now
  // (e.g. it is also the CAT or PTT port), or an empty string if it may.
  using PortGuard = std::function<QString (QString const& port_name)>;

  static int constexpr poll_interval_ms {20};
  static int constexpr debounce_count {3}; // consecutive identical reads
  static int constexpr retry_interval_ms {2000};

  explicit FootswitchInput (QObject * parent = nullptr);
  ~FootswitchInput ();

  // Open the port and start polling. Returns false and emits error()
  // if the port cannot be opened; it is then retried in the background.
  // Calling again reconfigures.
  bool start (QString const& port_name, Pin pin);

  // Start polling from an arbitrary source instead of a serial port.
  // Used by the unit tests to feed simulated line levels.
  void start (Reader reader, Pin pin);

  void stop ();

  void setPortGuard (PortGuard guard) {port_guard_ = std::move (guard);}

  bool isActive () const {return poll_timer_.isActive ();}
  bool isRetrying () const {return retry_timer_.isActive ();}
  bool isArmed () const {return armed_;}
  bool isPressed () const {return pressed_;}

  // Take one reading now; normally called by the poll timer.
  void poll ();

  Q_SIGNAL void pressed ();
  Q_SIGNAL void released ();
  Q_SIGNAL void error (QString const& reason);
  Q_SIGNAL void restored (QString const& port_name);
  // The switch was already closed when polling started; it is ignored
  // until it has been released.
  Q_SIGNAL void waitingForRelease ();
  // Emitted when a switch that was held at start has been released and
  // presses are now accepted.
  Q_SIGNAL void readyAfterRelease ();

private:
  Reading readPort ();
  bool openPort ();
  void startPolling (Reader reader);
  void releaseIfPressed ();
  void retryPort ();

  QSerialPort * port_;
  Reader reader_;
  QTimer poll_timer_;
  QTimer retry_timer_;
  QString port_name_;          // empty unless using a serial port
  PortGuard port_guard_;
  QString open_error_;         // why the last openPort () failed
  bool guard_refused_ {false}; // ... and it was the port guard
  Pin pin_;
  bool armed_;                 // a stable release has been seen
  bool reported_waiting_;
  bool read_ok_since_open_;
  bool pressed_;
  bool candidate_;
  int stable_reads_;
};

#endif
