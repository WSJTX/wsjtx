// SPDX-License-Identifier: GPL-3.0-or-later

#include "agent.hpp"

#include <QDateTime>
#include <QFile>
#include <QHostInfo>
#include <QSerialPortInfo>

#include <algorithm>

namespace {

QByteArray encode_hold (QString const& station, QString const& band,
                        qint64 seq, int ttl_ms)
{
  QByteArray body;
  body += "{\"tx_inhibit\":1,\"ttl_ms\":";
  body += QByteArray::number (ttl_ms);
  body += ",\"station\":\"";
  body += station.toUtf8 ();
  body += "\",\"band\":\"";
  body += band.toUtf8 ();
  body += "\",\"seq\":";
  body += QByteArray::number (seq);
  body += '}';
  return body;
}

bool looks_like_keyline (QSerialPortInfo const& info)
{
  QString blob = (info.manufacturer () + QLatin1Char (' ')
                  + info.description () + QLatin1Char (' ')
                  + info.serialNumber ()).toUpper ();
  return blob.contains (QLatin1String ("WA1HCO"))
    || blob.contains (QLatin1String ("KEYLINE"));
}

bool looks_like_builtin_com (QSerialPortInfo const& info)
{
  QString n = info.portName ();
#if defined (Q_OS_WIN)
  return n.compare (QLatin1String ("COM1"), Qt::CaseInsensitive) == 0;
#else
  return n.startsWith (QLatin1String ("ttyS"));
#endif
}

} // namespace

InhibitAgent::InhibitAgent (Config const& cfg, QObject * parent)
  : QObject (parent)
  , cfg_ (cfg)
{
  poll_.setInterval (5);
  keepalive_.setInterval (std::max (50, cfg_.keepalive_ms));
  connect (&poll_, &QTimer::timeout, this, &InhibitAgent::on_poll);
  connect (&keepalive_, &QTimer::timeout, this, &InhibitAgent::on_keepalive);
}

QString InhibitAgent::state_name (AgentState s)
{
  switch (s)
    {
    case AgentState::Open: return QStringLiteral ("OPEN");
    case AgentState::Inhibiting: return QStringLiteral ("INHIBITING");
    case AgentState::Hang: return QStringLiteral ("HANG");
    case AgentState::SenseFault: return QStringLiteral ("SENSE FAULT");
    }
  return QStringLiteral ("UNKNOWN");
}

QString InhibitAgent::dest_text () const
{
  return cfg_.dest_host + QLatin1Char (':')
    + QString::number (cfg_.dest_port);
}

bool InhibitAgent::parse_dest_addr (QString const& addr, QString * host,
                                    quint16 * port, QString * err)
{
  int const c = addr.lastIndexOf (QLatin1Char (':'));
  if (c <= 0 || c == addr.size () - 1)
    {
      if (err)
        {
          *err = QStringLiteral ("addr must be host:port (got %1)").arg (addr);
        }
      return false;
    }
  bool ok = false;
  uint p = addr.mid (c + 1).toUInt (&ok);
  if (!ok || p < 1 || p > 65535)
    {
      if (err)
        {
          *err = QStringLiteral ("addr port must be 1..65535");
        }
      return false;
    }
  if (host)
    {
      *host = addr.left (c);
    }
  if (port)
    {
      *port = static_cast<quint16> (p);
    }
  return true;
}

QString InhibitAgent::auto_detect_serial_port (QString * note)
{
  QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts ();
  QStringList keyline;
  QStringList usb;
  for (QSerialPortInfo const& p : ports)
    {
      if (looks_like_keyline (p))
        {
          keyline << p.portName ();
        }
      else if (!looks_like_builtin_com (p))
        {
          usb << p.portName ();
        }
    }
  if (keyline.size () == 1)
    {
      if (note)
        {
          *note = QStringLiteral ("auto-selected Keyline %1").arg (keyline[0]);
        }
      return keyline[0];
    }
  if (keyline.size () > 1)
    {
      if (note)
        {
          *note = QStringLiteral (
              "multiple Keyline ports (%1); pass --port")
              .arg (keyline.join (QLatin1String (", ")));
        }
      return {};
    }
  if (usb.size () == 1)
    {
      if (note)
        {
          *note = QStringLiteral ("auto-selected serial %1").arg (usb[0]);
        }
      return usb[0];
    }
  if (note)
    {
      if (ports.isEmpty ())
        {
          *note = QStringLiteral ("no serial ports found");
        }
      else
        {
          *note = QStringLiteral (
              "could not auto-pick a KEY serial port (%1); pass --port")
              .arg (list_serial_ports ().simplified ());
        }
    }
  return {};
}

QString InhibitAgent::list_serial_ports ()
{
  QStringList lines;
  int skipped_builtin = 0;
  for (QSerialPortInfo const& p : QSerialPortInfo::availablePorts ())
    {
      if (looks_like_builtin_com (p)
          && p.manufacturer ().isEmpty () && p.description ().isEmpty ())
        {
          ++skipped_builtin;
          continue;
        }
      QString tag;
      if (looks_like_keyline (p))
        {
          tag = QStringLiteral (" [Keyline]");
        }
      lines << QStringLiteral ("%1  %2 %3%4")
                   .arg (p.portName (), -12)
                   .arg (p.manufacturer ())
                   .arg (p.description ())
                   .arg (tag);
    }
  if (lines.isEmpty ())
    {
      return skipped_builtin
        ? QStringLiteral ("(no USB-serial ports; %1 builtin COM/ttyS skipped)")
              .arg (skipped_builtin)
        : QStringLiteral ("(no serial ports)");
    }
  if (skipped_builtin)
    {
      lines << QStringLiteral ("(%1 builtin COM/ttyS omitted)").arg (skipped_builtin);
    }
  return lines.join (QLatin1Char ('\n'));
}

bool InhibitAgent::start ()
{
  fault_.clear ();
  dest_addr_ = QHostAddress (cfg_.dest_host);
  if (dest_addr_.isNull ())
    {
      QHostInfo info = QHostInfo::fromName (cfg_.dest_host);
      if (info.addresses ().isEmpty ())
        {
          enter_fault (QStringLiteral ("cannot resolve %1").arg (cfg_.dest_host));
          return false;
        }
      dest_addr_ = info.addresses ().first ();
    }

  if (cfg_.serial_port.isEmpty ())
    {
      enter_fault (QStringLiteral ("no serial port"));
      return false;
    }

  serial_.setPortName (cfg_.serial_port);
  // Open so modem-status (CTS) is readable. Do not assert RTS/DTR —
  // Keyline J3 is an inhibit/PTT output driven by RTS.
  if (!serial_.open (QIODevice::ReadWrite))
    {
      enter_fault (QStringLiteral ("open %1 failed: %2")
                   .arg (cfg_.serial_port, serial_.errorString ()));
      return false;
    }
  serial_.setDataTerminalReady (false);
  serial_.setRequestToSend (false);
  try_set_usb_latency ();

  keying_.reset_transmission ();
  key_down_ = false;
  hold_active_ = false;
  hang_until_ms_ = -1;
  keepalive_.stop ();
  poll_.start ();
  set_state (AgentState::Open, QStringLiteral ("CTS on %1 -> %2")
             .arg (cfg_.serial_port, dest_text ()));
  return true;
}

void InhibitAgent::stop ()
{
  poll_.stop ();
  if (hold_active_)
    {
      end_hold ("stop");
    }
  if (serial_.isOpen ())
    {
      serial_.close ();
    }
}

void InhibitAgent::try_set_usb_latency ()
{
#if defined (Q_OS_LINUX)
  QString name = cfg_.serial_port;
  if (name.startsWith (QLatin1String ("/dev/")))
    {
      name = name.section (QLatin1Char ('/'), -1);
    }
  QStringList paths {
    QStringLiteral ("/sys/class/tty/%1/device/latency_timer").arg (name),
    QStringLiteral ("/sys/bus/usb-serial/devices/%1/latency_timer").arg (name),
  };
  for (QString const& path : paths)
    {
      QFile f (path);
      if (f.open (QIODevice::WriteOnly | QIODevice::Truncate))
        {
          f.write ("1");
          return;
        }
    }
#else
  (void) 0;
#endif
}

void InhibitAgent::set_state (AgentState s, QString const& detail)
{
  bool changed = (s != state_);
  state_ = s;
  if (s != AgentState::SenseFault)
    {
      fault_.clear ();
    }
  if (changed)
    {
      emit stateChanged (s, detail);
    }
}

void InhibitAgent::enter_fault (QString const& reason)
{
  fault_ = reason;
  poll_.stop ();
  if (hold_active_)
    {
      end_hold ("sense fault");
    }
  if (serial_.isOpen ())
    {
      serial_.close ();
    }
  set_state (AgentState::SenseFault, reason);
  emit logLine (QStringLiteral ("SENSE FAULT  %1").arg (reason));
}

bool InhibitAgent::read_key (bool * keyed)
{
  if (!serial_.isOpen ())
    {
      return false;
    }
  QSerialPort::PinoutSignals pins = serial_.pinoutSignals ();
  if (serial_.error () != QSerialPort::NoError
      && serial_.error () != QSerialPort::TimeoutError)
    {
      return false;
    }
  bool cts = (pins & QSerialPort::ClearToSendSignal) != 0;
  *keyed = cfg_.invert ? !cts : cts;
  return true;
}

void InhibitAgent::on_poll ()
{
  qint64 now = QDateTime::currentMSecsSinceEpoch ();
  bool keyed = false;
  if (!read_key (&keyed))
    {
      enter_fault (serial_.isOpen ()
                   ? QStringLiteral ("CTS read failed on %1: %2")
                       .arg (cfg_.serial_port, serial_.errorString ())
                   : QStringLiteral ("serial port closed"));
      return;
    }
  if (keyed != key_down_)
    {
      on_key_edge (keyed, now);
    }
  if (hang_until_ms_ >= 0 && now >= hang_until_ms_)
    {
      hang_until_ms_ = -1;
      end_hold ("hang done");
    }
}

void InhibitAgent::on_key_edge (bool keyed, qint64 now_ms)
{
  key_down_ = keyed;
  if (keyed)
    {
      hang_until_ms_ = -1;
      keying_.on_key_assert (now_ms);
      start_hold ();
      set_state (AgentState::Inhibiting, QStringLiteral ("KEY assert"));
      emit logLine (QStringLiteral ("KEY assert"));
      return;
    }
  int hang_ms = keying_.on_key_open (now_ms);
  emit logLine (QStringLiteral ("KEY open  class=%1  hang_ms=%2")
                .arg (QString::fromLatin1 (keying_.class_name ()))
                .arg (hang_ms));
  if (hang_ms <= 0)
    {
      hang_until_ms_ = -1;
      end_hold ("KEY open");
      return;
    }
  hang_until_ms_ = now_ms + hang_ms;
  set_state (AgentState::Hang,
             QStringLiteral ("hang %1 ms (%2)").arg (hang_ms)
             .arg (QString::fromLatin1 (keying_.class_name ())));
}

void InhibitAgent::start_hold ()
{
  if (hold_active_)
    {
      return;
    }
  hold_active_ = true;
  send_packet (cfg_.hold_timeout_ms, false, nullptr);
  keepalive_.start ();
}

void InhibitAgent::end_hold (char const * reason)
{
  hang_until_ms_ = -1;
  if (!hold_active_)
    {
      if (state_ != AgentState::SenseFault)
        {
          set_state (AgentState::Open, QString::fromLatin1 (reason ? reason : ""));
        }
      return;
    }
  hold_active_ = false;
  keepalive_.stop ();
  send_packet (0, false, reason);
  if (state_ != AgentState::SenseFault)
    {
      set_state (AgentState::Open, QString::fromLatin1 (reason ? reason : "release"));
    }
}

void InhibitAgent::on_keepalive ()
{
  if (hold_active_)
    {
      send_packet (cfg_.hold_timeout_ms, true, nullptr);
    }
}

void InhibitAgent::send_packet (int ttl_ms, bool is_keepalive,
                               char const * release_reason)
{
  if (is_keepalive && !hold_active_)
    {
      return;
    }
  QByteArray payload = encode_hold (cfg_.station, cfg_.band, seq_++, ttl_ms);
  qint64 n = sock_.writeDatagram (payload, dest_addr_, cfg_.dest_port);
  if (ttl_ms == 0)
    {
      ++releases_sent_;
    }
  else if (is_keepalive)
    {
      ++keepalives_sent_;
    }
  else
    {
      ++holds_sent_;
    }
  QString tag = ttl_ms == 0
    ? QStringLiteral ("RELEASE")
    : (is_keepalive ? QStringLiteral ("KEEPALIVE") : QStringLiteral ("HOLD"));
  QString line = QStringLiteral ("%1  ttl_ms=%2  -> %3  (holds=%4 ka=%5 rel=%6)")
                   .arg (tag)
                   .arg (ttl_ms)
                   .arg (dest_text ())
                   .arg (holds_sent_)
                   .arg (keepalives_sent_)
                   .arg (releases_sent_);
  if (ttl_ms == 0 && release_reason && release_reason[0])
    {
      line += QStringLiteral ("  (%1)").arg (QString::fromLatin1 (release_reason));
    }
  if (!is_keepalive)
    {
      emit logLine (line);
    }
  if (n < 0)
    {
      emit logLine (QStringLiteral ("send failed: %1").arg (sock_.errorString ()));
    }
}
