#include "HeardByMqtt.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>
#include <QtGlobal>

namespace
{
  quint8 constexpr CONNECT = 1, CONNACK = 2, PUBLISH = 3, SUBACK = 9;
  int constexpr max_packet = 1000000;           // anything larger is garbage

  QByteArray utf8_string (QString const& s)
  {
    auto const b = s.toUtf8 ();
    QByteArray out;
    out.append (char (b.size () >> 8));
    out.append (char (b.size () & 0xff));
    return out + b;
  }
}

HeardByMqtt::HeardByMqtt (QObject * parent)
  : HeardByMqtt {Options {}, parent}
{
}

HeardByMqtt::HeardByMqtt (Options const& options, QObject * parent)
  : QObject {parent}
  , options_ {options}
  , status_ {tr ("stopped")}
  , phase_timer_ {new QTimer {this}}
  , watchdog_timer_ {new QTimer {this}}
  , retry_timer_ {new QTimer {this}}
  , check_timer_ {new QTimer {this}}
{
  clock_.start ();
  phase_timer_->setSingleShot (true);
  retry_timer_->setSingleShot (true);
  check_timer_->setSingleShot (true);
  connect (check_timer_, &QTimer::timeout, this, [this] {
      cancel_check ();
      Q_EMIT feed_checked (false);
    });
  connect (phase_timer_, &QTimer::timeout, this, [this] {
      switch (state_)
        {
        case State::Connecting: fail (tr ("no connection within %1 s").arg (options_.connect_timeout_ms / 1000.0)); break;
        case State::WaitConnAck: fail (tr ("no CONNACK within %1 s").arg (options_.handshake_timeout_ms / 1000.0)); break;
        case State::WaitSubAck: fail (tr ("no SUBACK within %1 s").arg (options_.handshake_timeout_ms / 1000.0)); break;
        default: break;
        }
    });
  connect (watchdog_timer_, &QTimer::timeout, this, &HeardByMqtt::watchdog);
  connect (retry_timer_, &QTimer::timeout, this, [this] {
      if (!stopping_ && State::Backoff == state_) start_connection ();
    });
}

HeardByMqtt::~HeardByMqtt ()
{
  stop ();
}

// ---- topics, ids, reports and packets ------------------------------------

QString HeardByMqtt::call_in_topic (QString const& call)
{
  return call.trimmed ().toUpper ().replace ('/', '.');
}

QString HeardByMqtt::topic_for_sender (QString const& call)
{
  return "pskr/filter/v2/+/+/" + call_in_topic (call) + "/+/+/+/+/+";
}

QString HeardByMqtt::client_id (QString const& prefix, QString const& call)
{
  auto const c = call_in_topic (call);
  return prefix + (c.isEmpty () ? QString {} : "-" + c)
    + QString {"-%1"}.arg (QRandomGenerator::global ()->generate () & 0xffffff, 6, 16, QChar {'0'});
}

HeardByRecords::Report HeardByMqtt::report (QJsonObject const& spot)
{
  HeardByRecords::Report r;
  r.receiver = spot["rc"].toString ();
  r.locator = spot["rl"].toString ();
  r.mode = spot["md"].toString ();
  r.frequency = static_cast<quint64> (qMax (0.0, spot["f"].toDouble ()));
  r.snr = spot["rp"].toInt ();
  r.when = static_cast<qint64> (spot["t"].toDouble ());
  if (!r.when) r.when = static_cast<qint64> (spot["rx"].toDouble () / 1000.0);
  if (!r.receiver.isEmpty () && r.when) r.id = HeardByRecords::report_id (r.receiver, r.when, r.frequency);
  return r;
}

QByteArray HeardByMqtt::encode_length (int n)
{
  QByteArray out;
  do
    {
      quint8 b = n % 128;
      n /= 128;
      if (n) b |= 0x80;
      out.append (char (b));
    }
  while (n);
  return out;
}

HeardByMqtt::Take HeardByMqtt::take_packet (QByteArray const& buf, Packet& out)
{
  if (buf.size () < 2) return Take::NeedMore;
  int mult = 1, value = 0, i = 1;
  for (;;)
    {
      if (i >= buf.size ()) return Take::NeedMore;
      auto const b = static_cast<quint8> (buf[i++]);
      value += (b & 0x7f) * mult;
      if (!(b & 0x80)) break;
      mult *= 128;
      if (mult > 128 * 128 * 128) return Take::Malformed;
    }
  if (value > max_packet) return Take::Malformed;
  if (buf.size () < i + value) return Take::NeedMore;
  out.type = static_cast<quint8> (buf[0]) >> 4;
  out.flags = static_cast<quint8> (buf[0]) & 0x0f;
  out.body = buf.mid (i, value);
  out.used = i + value;
  return Take::Ok;
}

QByteArray HeardByMqtt::connect_packet (QString const& client_id, int keep_alive)
{
  QByteArray body = utf8_string ("MQTT");
  body.append (char (4));                       // protocol level 3.1.1
  body.append (char (2));                       // clean session
  body.append (char (keep_alive >> 8));
  body.append (char (keep_alive & 0xff));
  body += utf8_string (client_id);
  QByteArray p;
  p.append (char (CONNECT << 4));
  return p + encode_length (body.size ()) + body;
}

QByteArray HeardByMqtt::subscribe_packet (quint16 id, QString const& topic)
{
  QByteArray body;
  body.append (char (id >> 8));
  body.append (char (id & 0xff));
  body += utf8_string (topic);
  body.append (char (0));                       // QoS 0
  QByteArray p;
  p.append (char (0x82));                       // SUBSCRIBE, flags 0010
  return p + encode_length (body.size ()) + body;
}

QByteArray HeardByMqtt::unsubscribe_packet (quint16 id, QString const& topic)
{
  QByteArray body;
  body.append (char (id >> 8));
  body.append (char (id & 0xff));
  body += utf8_string (topic);
  QByteArray p;
  p.append (char (0xa2));                       // UNSUBSCRIBE, flags 0010
  return p + encode_length (body.size ()) + body;
}

// ---- control ---------------------------------------------------------------

void HeardByMqtt::check_feed (int window_ms)
{
  if (State::Subscribed != state_ || checking_) return;
  checking_ = true;
  check_first_.clear ();
  check_timer_->start (window_ms);
  write (subscribe_packet (2, options_.status_topic));
}

void HeardByMqtt::cancel_check ()
{
  if (!checking_) return;
  checking_ = false;
  check_timer_->stop ();
  if (State::Subscribed == state_) write (unsubscribe_packet (3, options_.status_topic));
}

void HeardByMqtt::start (QString const& topic, QString const& call)
{
  stop ();
  topic_ = topic;
  call_ = call;
  stopping_ = false;
  delay_ms_ = options_.backoff_min_ms;
  start_connection ();
}

void HeardByMqtt::stop ()
{
  stopping_ = true;
  if (State::Subscribed == state_ && socket_)
    {
      socket_->write (QByteArray {"\xe0\x00", 2});   // DISCONNECT
      socket_->flush ();
    }
  teardown ();
  retry_timer_->stop ();
  state_ = State::Idle;
  set_status (tr ("stopped"));
}

void HeardByMqtt::set_status (QString const& s)
{
  if (s != status_)
    {
      status_ = s;
      Q_EMIT status_changed (s);
    }
}

void HeardByMqtt::teardown ()
{
  phase_timer_->stop ();
  watchdog_timer_->stop ();
  check_timer_->stop ();
  checking_ = false;
  buffer_.clear ();                             // never carried to the next connection
  if (socket_)
    {
      socket_->disconnect (this);               // no more signals from it
      socket_->abort ();
      socket_->deleteLater ();
    }
  socket_.clear ();
}

void HeardByMqtt::start_connection ()
{
  teardown ();
  set_status (tr ("connecting to %1").arg (options_.host));
  state_ = State::Connecting;
  socket_ = new QTcpSocket {this};
  connect (socket_.data (), &QTcpSocket::connected, this, &HeardByMqtt::on_connected);
  connect (socket_.data (), &QTcpSocket::readyRead, this, &HeardByMqtt::on_ready_read);
  connect (socket_.data (), &QTcpSocket::disconnected, this, [this] {
      fail (tr ("closed by the broker"));
    });
  connect (socket_.data (), &QAbstractSocket::errorOccurred, this, [this] {
      fail (socket_ ? socket_->errorString () : tr ("socket error"));
    });
  phase_timer_->start (options_.connect_timeout_ms);
  socket_->connectToHost (options_.host, options_.port);
}

void HeardByMqtt::fail (QString const& reason)
{
  if (stopping_ || State::Idle == state_ || State::Backoff == state_) return;
  bool const was_subscribed = State::Subscribed == state_;
  ++stats_.failures;
  stats_.last_error = reason;
  teardown ();
  auto const rnd = QRandomGenerator::global ()->generateDouble ();
  auto wait = int (delay_ms_ * (0.8 + 0.4 * rnd));
  if (was_subscribed && clock_.elapsed () - subscribed_at_ >= options_.stable_after_ms)
    {
      // It was a good connection.  Everyone else's dropped with it, so
      // do not all come back in the same second.
      delay_ms_ = options_.backoff_min_ms;
      wait = delay_ms_ + int (rnd * qMax (0, options_.first_retry_max_ms - delay_ms_));
    }
  delay_ms_ = qMin (delay_ms_ * 2, options_.backoff_max_ms);
  state_ = State::Backoff;
  set_status (tr ("disconnected (%1) - retry in %2 s").arg (reason).arg (wait / 1000.0, 0, 'f', 1));
  retry_timer_->start (wait);
}

void HeardByMqtt::write (QByteArray const& data)
{
  if (socket_)
    {
      socket_->write (data);
      last_tx_ = clock_.elapsed ();
    }
}

void HeardByMqtt::on_connected ()
{
  if (!socket_ || State::Connecting != state_) return;
  socket_->setSocketOption (QAbstractSocket::KeepAliveOption, 1);
  state_ = State::WaitConnAck;
  phase_timer_->start (options_.handshake_timeout_ms);
  write (connect_packet (client_id (options_.client_prefix, call_), options_.keep_alive_secs));
}

void HeardByMqtt::on_ready_read ()
{
  if (!socket_) return;
  auto const data = socket_->readAll ();
  stats_.bytes_in += data.size ();
  buffer_ += data;
  last_rx_ = clock_.elapsed ();
  process_packets ();
}

void HeardByMqtt::process_packets ()
{
  for (;;)
    {
      Packet p;
      auto const t = take_packet (buffer_, p);
      if (Take::NeedMore == t) return;
      if (Take::Malformed == t)
        {
          fail (tr ("malformed packet"));
          return;
        }
      buffer_.remove (0, p.used);
      if (!dispatch (p)) return;                // the connection has ended
    }
}

bool HeardByMqtt::dispatch (Packet const& p)
{
  switch (state_)
    {
    case State::WaitConnAck:
      if (p.type != CONNACK)
        {
          fail (tr ("expected CONNACK, got packet type %1").arg (p.type));
          return false;
        }
      if (p.body.size () < 2 || p.body[1] != 0)
        {
          fail (tr ("connection refused (code %1)").arg (p.body.size () >= 2 ? int (quint8 (p.body[1])) : -1));
          return false;
        }
      set_status (tr ("connected, subscribing"));
      state_ = State::WaitSubAck;
      phase_timer_->start (options_.handshake_timeout_ms);
      write (subscribe_packet (1, topic_));
      return true;

    case State::WaitSubAck:
      if (p.type != SUBACK)
        {
          fail (tr ("expected SUBACK, got packet type %1").arg (p.type));
          return false;
        }
      if (p.body.size () < 3 || quint8 (p.body[2]) == 0x80)
        {
          fail (tr ("subscription refused"));
          return false;
        }
      phase_timer_->stop ();
      ++stats_.connects;
      subscribed_at_ = last_rx_ = last_tx_ = clock_.elapsed ();
      state_ = State::Subscribed;
      watchdog_timer_->start (qBound (20, options_.ping_every_ms / 2, 1000));
      set_status (tr ("subscribed"));
      return true;

    case State::Subscribed:
      if (PUBLISH == p.type) return handle_publish (p.flags, p.body);
      return true;                              // PINGRESP and the rest

    default:
      return true;
    }
}

bool HeardByMqtt::handle_publish (quint8 flags, QByteArray const& body)
{
  if (body.size () < 2)
    {
      fail (tr ("short PUBLISH"));
      return false;
    }
  int const n = (quint8 (body[0]) << 8) | quint8 (body[1]);
  if (body.size () < 2 + n)
    {
      fail (tr ("PUBLISH topic longer than the packet"));
      return false;
    }
  auto const topic = QString::fromUtf8 (body.mid (2, n));
  int pos = 2 + n;
  if ((flags >> 1) & 3) pos += 2;               // QoS > 0 has a packet id
  if (topic == options_.status_topic)
    {
      // the time of the broker's last spot: a second, different value
      // means spots are flowing
      auto const value = body.mid (pos);
      if (!checking_) return true;
      if (check_first_.isEmpty ()) check_first_ = value;
      else if (value != check_first_)
        {
          cancel_check ();
          Q_EMIT feed_checked (true);
        }
      return state_ == State::Subscribed;
    }
  QJsonParseError err;
  auto const doc = QJsonDocument::fromJson (body.mid (pos), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject ())
    {
      ++stats_.bad_payloads;
      return true;
    }
  auto s = doc.object ();
  s["topic"] = topic;
  auto const now = QDateTime::currentMSecsSinceEpoch ();
  s["rx"] = double (now);
  ++stats_.spots;
  stats_.last_spot_ms = now;
  Q_EMIT spot (s, bool (flags & 1));
  return state_ == State::Subscribed;           // a slot may have stopped us
}

void HeardByMqtt::watchdog ()
{
  if (State::Subscribed != state_) return;
  auto const now = clock_.elapsed ();
  if (now - last_rx_ > options_.dead_after_ms)
    {
      fail (tr ("nothing received for %1 s").arg (options_.dead_after_ms / 1000.0));
      return;
    }
  // by what we SENT: the broker's keep-alive does not count its own spots
  if (now - last_tx_ >= options_.ping_every_ms)
    {
      write (QByteArray {"\xc0\x00", 2});       // PINGREQ
    }
}
