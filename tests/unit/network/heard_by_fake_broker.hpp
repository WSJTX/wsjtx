// A fake MQTT broker for the Heard by tests.  It runs in its own thread
// with blocking sockets, so each test is a short script: one script per
// incoming connection (the last one is used again for further ones).
#ifndef HEARD_BY_FAKE_BROKER_HPP__
#define HEARD_BY_FAKE_BROKER_HPP__

#include <atomic>
#include <functional>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSemaphore>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "Network/HeardByMqtt.hpp"

namespace fake_broker
{
  using Packet = HeardByMqtt::Packet;

  // Qt 5 has no QSslServer: each accepted socket becomes a server-side TLS one.
  class SslServer final : public QTcpServer
  {
  public:
    QSslCertificate certificate;
    QSslKey key;

  protected:
    void incomingConnection (qintptr descriptor) override
    {
      auto * s = new QSslSocket {this};
      s->setSocketDescriptor (descriptor);
      s->setLocalCertificate (certificate);
      s->setPrivateKey (key);
      s->startServerEncryption ();
      addPendingConnection (s);
    }
  };

  struct Conn
  {
    QTcpSocket * s;
    QByteArray buf;
    int n;                                  // connection number, from 1
  };

  class Broker final : public QThread
  {
  public:
    using Script = std::function<void (Broker&, Conn&)>;

    explicit Broker (QList<Script> scripts) : scripts_ {scripts} {}
    ~Broker () override {finish ();}
    quint16 port () const {return port_;}
    int connections () const {return connections_;}         // (with TLS: those that got encrypted)
    // Serve TLS with this certificate (call before start_and_wait).
    void use_tls (QSslCertificate const& c, QSslKey const& k) {cert_ = c; key_ = k; tls_ = true;}
    int tls_failures () const {return tls_failures_;}      // handshakes that did not finish
    QStringList client_ids () const {QMutexLocker l {&m_}; return ids_;}
    QStringList topics () const {QMutexLocker l {&m_}; return topics_;}
    int pings () const {return pings_;}
    int unsubscribes () const {return unsubscribes_;}
    bool running () const {return running_;}

    void start_and_wait ()
    {
      start ();
      ready_.acquire ();
    }
    void finish ()
    {
      running_ = false;
      wait ();
    }

    // -- helpers for the scripts
    bool read (Conn& c, Packet& p, int ms = 5000)
    {
      QElapsedTimer t;
      t.start ();
      for (;;)
        {
          if (HeardByMqtt::Take::Ok == HeardByMqtt::take_packet (c.buf, p))
            {
              c.buf.remove (0, p.used);
              return true;
            }
          if (t.elapsed () >= ms || !running_) return false;
          // (bytes already decrypted give no new readyRead: take them first)
          if (c.s->bytesAvailable ())
            {
              c.buf += c.s->readAll ();
              continue;
            }
          if (!c.s->waitForReadyRead (qMin (100, ms)))
            {
              if (c.s->state () != QAbstractSocket::ConnectedState && !c.s->bytesAvailable ()) return false;
              continue;
            }
          c.buf += c.s->readAll ();
        }
    }
    static void send (Conn& c, QByteArray const& data)
    {
      c.s->write (data);
      c.s->waitForBytesWritten (2000);
    }
    bool handshake (Conn& c, bool refuse = false, bool skip_suback = false)
    {
      Packet p;
      if (!read (c, p) || p.type != 1) return false;
      // CONNECT body: "MQTT" (2+4), level, flags, keep-alive (2), id string
      int const n = (quint8 (p.body[10]) << 8) | quint8 (p.body[11]);
      {
        QMutexLocker l {&m_};
        ids_ << QString::fromUtf8 (p.body.mid (12, n));
      }
      send (c, QByteArray {"\x20\x02\x00", 3} + QByteArray (1, refuse ? '\x05' : '\x00'));
      if (refuse) return false;
      if (!read (c, p) || p.type != 8) return false;
      {
        int const tn = (quint8 (p.body[2]) << 8) | quint8 (p.body[3]);
        QMutexLocker l {&m_};
        topics_ << QString::fromUtf8 (p.body.mid (4, tn));
      }
      if (!skip_suback) send (c, QByteArray {"\x90\x03", 2} + p.body.left (2) + QByteArray (1, '\x00'));
      return true;
    }
    static QByteArray publish (QString const& topic, QByteArray const& payload, bool retain = false)
    {
      auto const t = topic.toUtf8 ();
      QByteArray body;
      body.append (char (t.size () >> 8));
      body.append (char (t.size () & 0xff));
      body += t + payload;
      QByteArray p;
      p.append (char (0x30 | (retain ? 1 : 0)));
      return p + HeardByMqtt::encode_length (body.size ()) + body;
    }
    // Read and count PINGREQs (answering none) until ms have passed.
    // A further SUBSCRIBE is noted in topics() and, if on_subscribe is
    // given, answered by it.
    void listen (Conn& c, int ms, std::function<void (Conn&, QString const&)> on_subscribe = {})
    {
      QElapsedTimer t;
      t.start ();
      Packet p;
      while (running_ && t.elapsed () < ms)
        {
          if (read (c, p, 100))
            {
              if (p.type == 12) ++pings_;
              else if (p.type == 10) ++unsubscribes_;
              else if (p.type == 8)
                {
                  int const tn = (quint8 (p.body[2]) << 8) | quint8 (p.body[3]);
                  auto const topic = QString::fromUtf8 (p.body.mid (4, tn));
                  {
                    QMutexLocker l {&m_};
                    topics_ << topic;
                  }
                  if (on_subscribe) on_subscribe (c, topic);
                }
            }
          else if (c.s->state () != QAbstractSocket::ConnectedState) break;
        }
    }
    void sleep (int ms)
    {
      QElapsedTimer t;
      t.start ();
      while (running_ && t.elapsed () < ms) QThread::msleep (20);
    }

  protected:
    void run () override
    {
      SslServer ssl_server;
      QTcpServer plain_server;
      QTcpServer& server = tls_ ? static_cast<QTcpServer&> (ssl_server) : plain_server;
      ssl_server.certificate = cert_;
      ssl_server.key = key_;
      server.listen (QHostAddress::LocalHost, 0);
      port_ = server.serverPort ();
      ready_.release ();
      while (running_)
        {
          if (!server.waitForNewConnection (100)) continue;
          auto * socket = server.nextPendingConnection ();
          if (tls_)
            {
              // a client that distrusts our certificate ends here
              auto * ssl = qobject_cast<QSslSocket *> (socket);
              if (!ssl->waitForEncrypted (5000))
                {
                  ++tls_failures_;
                  delete socket;
                  continue;
                }
            }
          Conn c {socket, {}, ++connections_};
          auto const& script = scripts_[qMin (c.n, scripts_.size ()) - 1];
          script (*this, c);
          c.s->disconnectFromHost ();
          delete c.s;
        }
    }

  private:
    QList<Script> scripts_;
    QSemaphore ready_;
    mutable QMutex m_;
    QStringList ids_;
    QStringList topics_;
    std::atomic<bool> running_ {true};
    std::atomic<int> connections_ {0};
    std::atomic<int> pings_ {0};
    std::atomic<int> unsubscribes_ {0};
    std::atomic<int> tls_failures_ {0};
    QSslCertificate cert_;
    QSslKey key_;
    bool tls_ {false};
    quint16 port_ {0};
  };

  inline QByteArray json (QJsonObject const& o) {return QJsonDocument {o}.toJson (QJsonDocument::Compact);}

  inline HeardByMqtt::Options fast (quint16 port)
  {
    HeardByMqtt::Options o;
    o.host = "127.0.0.1";
    o.port = port;
    o.backoff_min_ms = 100;
    o.backoff_max_ms = 400;
    o.stable_after_ms = 300;
    o.first_retry_max_ms = 200;
    return o;
  }
}

#endif
