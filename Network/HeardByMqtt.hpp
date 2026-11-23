#ifndef HEARD_BY_MQTT_HPP__
#define HEARD_BY_MQTT_HPP__

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSslCertificate>
#include <QString>

#include "Network/HeardByReport.hpp"

class QTcpSocket;
class QTimer;

//
// HeardByMqtt - PSK Reporter's live spot feed (mqtt.pskreporter.info),
// MQTT 3.1.1, subscribe only, QoS 0, over TLS (port 1884) or plain TCP
// (1883).  Qt Core + Network only (no QtMqtt).  Everything runs on the
// object's thread.
//
// TLS (Options::tls): the server's certificate AND host name are checked
// against the system's trusted authorities.  A failed handshake is an
// ordinary failed connection (status text, retry with backoff) and NEVER
// a fall back to plain MQTT, which would defeat the check.  The spots are
// public data and the only thing sent is the user's own call, but plain
// MQTT lets anyone on the network path feed the window false reports.
//
// Built so that it never sits "connected" while dead:
//   - one connection path and one reconnect path, with exponential
//     backoff (5 s .. 2 min, +-20 %), reset after a connection that
//     lasted; after one that lasted the first retry is spread over
//     5 s .. 1 min, so that a broker restart does not bring every
//     station back at once;
//   - every connection starts with a NEW socket and an EMPTY buffer;
//   - timeouts on connect, CONNACK and SUBACK;
//   - keep-alive: PINGREQ every ping_every_ms, spots or not (the
//     broker drops a client that SENDS nothing for 1.5 x keep-alive);
//     if NOTHING (spot or PINGRESP) arrives for dead_after_ms the
//     connection is dropped and made again;
//   - a malformed packet drops the connection (it is never skipped);
//   - a new client id per connection: WSJT-X-<CALL>-<random>.
//
// The feed (seen 2026-11-18):
//   topic   pskr/filter/v2/{band}/{mode}/{tx}/{rx}/{tx_grid}/{rx_grid}/
//           {tx_dxcc}/{rx_dxcc}  (11 levels; "/" in a callsign is
//           written "." in the topic, the payload keeps the real call)
//   payload JSON: sq f md rp t t_tx sc sl rc rl sa ra b
// One busy band and mode carries ~40 spots/s, so subscribe ONLY to the
// topic of the user's own call (topic_for_sender).
//
// Is the feed delivering at all?  The broker publishes the time of its
// last spot once a second on status_topic.  check_feed() subscribes to
// it on the same connection for a moment: a value that changes means
// spots are flowing.  No second connection, no spots of other stations.
//
class HeardByMqtt final
  : public QObject
{
  Q_OBJECT

public:
  struct Options
  {
    QString host {"mqtt.pskreporter.info"};
    quint16 port {1883};                // with tls: 1883 means "the TLS port" (1884)
    bool tls {false};
    QString tls_name;                   // name the certificate must carry; empty = host
    QList<QSslCertificate> extra_cas;   // trusted besides the system's (tests)
    QString client_prefix {"WSJT-X"};
    int keep_alive_secs {60};           // sent in CONNECT
    int ping_every_ms {30000};
    int dead_after_ms {90000};
    int connect_timeout_ms {10000};
    int handshake_timeout_ms {10000};   // CONNACK, then SUBACK
    int backoff_min_ms {5000};
    int backoff_max_ms {120000};
    int stable_after_ms {60000};        // a subscription this long resets the backoff
    int first_retry_max_ms {60000};     // first retry after such a one: backoff_min .. this
    QString status_topic {"pskr2mqtt/stats/last_spot_published_to_broker"};

    // 1883 is plain MQTT, 1884 the same over TLS
    quint16 port_in_use () const {return tls && 1883 == port ? quint16 {1884} : port;}
  };

  struct Stats
  {
    int connects {0};                   // subscriptions that succeeded
    int failures {0};
    int spots {0};
    int bad_payloads {0};
    qint64 bytes_in {0};
    qint64 last_spot_ms {0};            // epoch ms
    QString last_error;
  };

  explicit HeardByMqtt (QObject * parent = nullptr);
  explicit HeardByMqtt (Options const&, QObject * parent = nullptr);
  ~HeardByMqtt () override;

  void set_options (Options const& o) {options_ = o;}
  Options const& options () const {return options_;}

  // Start (or restart) watching topic; call goes into the client id.
  void start (QString const& topic, QString const& call = {});
  void stop ();
  bool running () const {return state_ != State::Idle;}
  bool subscribed () const {return state_ == State::Subscribed;}
  bool encrypted () const;                      // subscribed over TLS
  QString topic () const {return topic_;}
  QString status () const {return status_;}

  // Watch status_topic for window_ms at most; feed_checked says whether
  // its value changed.  Does nothing unless subscribed.
  void check_feed (int window_ms);
  void cancel_check ();                         // no feed_checked follows
  bool checking () const {return checking_;}
  Stats const& stats () const {return stats_;}

  // A callsign as a topic level: "/" becomes ".".
  static QString call_in_topic (QString const& call);
  static QString topic_for_sender (QString const& call);
  static QString client_id (QString const& prefix, QString const& call);

  // A spot as a report for HeardByModel.
  static HeardByReport report (QJsonObject const& spot);

  // Packet helpers (public for the tests).
  struct Packet
  {
    quint8 type {0};
    quint8 flags {0};
    QByteArray body;
    int used {0};
  };
  enum class Take {NeedMore, Ok, Malformed};
  static Take take_packet (QByteArray const& buffer, Packet& out);
  static QByteArray encode_length (int length);
  static QByteArray connect_packet (QString const& client_id, int keep_alive);
  static QByteArray subscribe_packet (quint16 packet_id, QString const& topic);
  static QByteArray unsubscribe_packet (quint16 packet_id, QString const& topic);

  // The payload's JSON object plus "topic" and "rx" (receive time,
  // epoch ms).  retained = the broker's stored copy at subscribe time.
  Q_SIGNAL void spot (QJsonObject const& spot, bool retained);
  Q_SIGNAL void status_changed (QString const& status);
  Q_SIGNAL void feed_checked (bool delivering);

private:
  enum class State {Idle, Connecting, WaitConnAck, WaitSubAck, Subscribed, Backoff};

  void start_connection ();
  void teardown ();
  void fail (QString const& reason);
  void set_status (QString const&);
  void on_connected ();
  void on_ready_read ();
  void process_packets ();
  bool dispatch (Packet const&);              // false = connection ended
  bool handle_publish (quint8 flags, QByteArray const& body);
  void watchdog ();
  void write (QByteArray const&);

  Options options_;
  State state_ {State::Idle};
  QString topic_;
  QString call_;
  QString status_;
  QString ssl_error_;                         // first certificate problem of this attempt
  Stats stats_;
  QPointer<QTcpSocket> socket_;
  QByteArray buffer_;
  QTimer * phase_timer_;                      // connect / CONNACK / SUBACK
  QTimer * watchdog_timer_;
  QTimer * retry_timer_;
  QTimer * check_timer_;
  QByteArray check_first_;                    // first value seen in this check
  bool checking_ {false};
  QElapsedTimer clock_;
  qint64 last_rx_ {0};
  qint64 last_tx_ {0};
  qint64 subscribed_at_ {0};
  int delay_ms_ {0};
  bool stopping_ {false};
};

#endif
