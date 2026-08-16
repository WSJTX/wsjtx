// inhibit-agent core — CTS sense + hold sender + KEYing monitor.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef INHIBIT_AGENT_AGENT_HPP_
#define INHIBIT_AGENT_AGENT_HPP_

#include "keying_monitor.hpp"

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QTimer>
#include <QUdpSocket>

enum class AgentState
{
  Open,
  Inhibiting,
  Hang,
  SenseFault
};

class InhibitAgent : public QObject
{
  Q_OBJECT
public:
  struct Config
  {
    QString serial_port;
    QString dest_host {QStringLiteral ("127.0.0.1")};
    quint16 dest_port {22372};
    bool invert {false};
    int hold_timeout_ms {600};
    int keepalive_ms {200};
    QString station {QStringLiteral ("inhibit-agent")};
    QString band {QStringLiteral ("local")};
  };

  explicit InhibitAgent (Config const& cfg, QObject * parent = nullptr);

  bool start ();
  void stop ();

  AgentState state () const { return state_; }
  QString fault_reason () const { return fault_; }
  QString serial_port () const { return cfg_.serial_port; }
  QString dest_text () const;
  Config const& config () const { return cfg_; }

  static QString state_name (AgentState s);
  static bool parse_dest_addr (QString const& addr, QString * host,
                               quint16 * port, QString * err);
  static QString auto_detect_serial_port (QString * note);
  static QString list_serial_ports ();

signals:
  void stateChanged (AgentState state, QString detail);
  void logLine (QString line);

private slots:
  void on_poll ();
  void on_keepalive ();

private:
  void set_state (AgentState s, QString const& detail);
  void enter_fault (QString const& reason);
  bool read_key (bool * keyed);
  void on_key_edge (bool keyed, qint64 now_ms);
  void start_hold ();
  void end_hold (char const * reason);
  void send_packet (int ttl_ms, bool is_keepalive, char const * release_reason);
  void try_set_usb_latency ();
  // Keyline J3 is RTS. USB-serial open/close defaults assert RTS and
  // key the radio. Force RTS+DTR idle and leave them idle on close.
  void force_outputs_idle ();

  Config cfg_;
  QHostAddress dest_addr_;
  QSerialPort serial_;
  QUdpSocket sock_;
  QTimer poll_;
  QTimer keepalive_;
  KeyingMonitor keying_;
  AgentState state_ {AgentState::SenseFault};
  QString fault_;
  bool key_down_ {false};
  bool hold_active_ {false};
  qint64 hang_until_ms_ {-1};
  qint64 seq_ {1};
  int holds_sent_ {0};
  int keepalives_sent_ {0};
  int releases_sent_ {0};
  bool warned_rts_ {false};
};

#endif
