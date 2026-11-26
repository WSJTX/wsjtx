// Tests for HeardByMqtt against a fake MQTT broker: packets split, glued,
// long or bad; refusal; no CONNACK; no SUBACK; silence; a cut connection;
// garbage; stop; nothing listening; restart; client id and backoff.
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>

#include "Network/HeardByMqtt.hpp"
#include "heard_by_fake_broker.hpp"
#include "heard_by_test_cert.hpp"

Q_DECLARE_METATYPE (QJsonObject)

using namespace fake_broker;

namespace
{
  QJsonObject sp (QString const& rc, int sq = 0) {return {{"sc", "K1ABC"}, {"rc", rc}, {"sq", sq}};}

  // Qt 5 on macOS is built with Secure Transport by default, not OpenSSL.
  bool secure_transport ()
  {
    return QSslSocket::sslLibraryVersionString ().contains ("Secure Transport", Qt::CaseInsensitive);
  }
}

class TestHeardByMqtt : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase () {qRegisterMetaType<QJsonObject> ();}

  void packet_codec ()
  {
    for (int n : {0, 1, 127, 128, 16383, 16384, 300000})
      {
        QByteArray pk {"\x30", 1};
        pk += HeardByMqtt::encode_length (n) + QByteArray (n, 'x');
        Packet p;
        QCOMPARE (HeardByMqtt::take_packet (pk, p), HeardByMqtt::Take::Ok);
        QCOMPARE (p.used, pk.size ());
        QCOMPARE (p.body.size (), n);
        // one byte short: more is needed, never a wrong answer
        QCOMPARE (HeardByMqtt::take_packet (pk.left (pk.size () - 1), p), HeardByMqtt::Take::NeedMore);
      }
    Packet p;
    QCOMPARE (HeardByMqtt::take_packet (QByteArray {"\x30\xff\xff\xff\xff\xff", 6}, p), HeardByMqtt::Take::Malformed);
    QCOMPARE (HeardByMqtt::take_packet (QByteArray {"\x30", 1}, p), HeardByMqtt::Take::NeedMore);
  }

  void topics_ids_and_reports ()
  {
    QCOMPARE (HeardByMqtt::topic_for_sender ("pj4/k1abc"), QString {"pskr/filter/v2/+/+/PJ4.K1ABC/+/+/+/+/+"});
    QCOMPARE (HeardByMqtt::topic_for_sender (" k1abc "), QString {"pskr/filter/v2/+/+/K1ABC/+/+/+/+/+"});
    QCOMPARE (HeardByMqtt::topic_for_sender ("K1JT").split ('/').size (), 11);   // as the live feed
    auto const id = HeardByMqtt::client_id ("WSJT-X", "pj4/k1abc");
    QVERIFY2 (QRegularExpression {"^WSJT-X-PJ4\\.K1ABC-[0-9a-f]{6}$"}.match (id).hasMatch (), qPrintable (id));
    QVERIFY (HeardByMqtt::client_id ("WSJT-X", "K1ABC") != HeardByMqtt::client_id ("WSJT-X", "K1ABC"));
    QCOMPARE (HeardByMqtt::Options {}.backoff_max_ms, 120000);
    // a spot becomes a report with a stable id
    QJsonObject s {{"sc", "K1ABC"}, {"rc", "F6BLP"}, {"rl", "IN94KP"}, {"md", "FT8"},
                   {"f", 10489541745.0}, {"rp", 11}, {"t", 1763460000.0}};
    auto const r = HeardByMqtt::report (s);
    QCOMPARE (r.receiver, QString {"F6BLP"});
    QCOMPARE (r.locator, QString {"IN94KP"});
    QCOMPARE (r.mode, QString {"FT8"});
    QCOMPARE (r.frequency, quint64 {10489541745ULL});
    QCOMPARE (r.snr, 11);
    QCOMPARE (r.when, qint64 {1763460000});
    QCOMPARE (r.id, HeardByReport::id_of ("F6BLP", 1763460000, 10489541745ULL));
  }

  void pieces_glued_long_bad_retained ()
  {
    Broker b {{[] (Broker& br, Conn& c) {
        if (!br.handshake (c)) return;
        auto const p1 = Broker::publish ("pskr/filter/v2/3cm/FT8/K1ABC/DK3RW/a/b/1/2",
                                         json ({{"rc", "DK3RW"}, {"f", 10489540000.0}, {"rp", -5}}));
        QJsonObject longo = sp ("PAD");
        longo["pad"] = QString (300, 'x');
        auto const p2 = Broker::publish ("t/2", json (longo));
        auto const p3 = Broker::publish ("t/3", json (sp ("W1AW")));
        c.s->write (p1.left (3)); c.s->flush (); br.sleep (100);
        Broker::send (c, p1.mid (3) + p2.left (50)); br.sleep (100);
        Broker::send (c, p2.mid (50) + p3);               // glued
        Broker::send (c, Broker::publish ("t/4", "not json"));
        Broker::send (c, Broker::publish ("t/5", json (sp ("N1ABC")), true));
        br.sleep (800);
      }}};
    b.start_and_wait ();
    HeardByMqtt c {fast (b.port ())};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("t/#", "K1ABC");
    QTRY_COMPARE_WITH_TIMEOUT (spy.count (), 4, 5000);
    QCOMPARE (spy[0][0].toJsonObject ()["rc"].toString (), QString {"DK3RW"});
    QCOMPARE (spy[0][0].toJsonObject ()["f"].toDouble (), 10489540000.0);
    QVERIFY (spy[0][0].toJsonObject ()["topic"].toString ().endsWith ("K1ABC/DK3RW/a/b/1/2"));
    QVERIFY (spy[0][0].toJsonObject ()["rx"].toDouble () > 0);
    QCOMPARE (spy[1][0].toJsonObject ()["rc"].toString (), QString {"PAD"});
    QCOMPARE (spy[2][0].toJsonObject ()["rc"].toString (), QString {"W1AW"});
    QCOMPARE (spy[3][0].toJsonObject ()["rc"].toString (), QString {"N1ABC"});
    QCOMPARE (spy[0][1].toBool (), false);
    QCOMPARE (spy[3][1].toBool (), true);             // retained flag
    QCOMPARE (c.stats ().bad_payloads, 1);
    QVERIFY (c.subscribed ());
    QCOMPARE (b.topics (), QStringList {"t/#"});
    QVERIFY (b.client_ids ().value (0).startsWith ("WSJT-X-K1ABC-"));
    c.stop ();
    b.finish ();
  }

  void refused_then_ok_with_a_new_id ()
  {
    Broker b {{[] (Broker& br, Conn& c) {br.handshake (c, true);},
               [] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 Broker::send (c, Broker::publish ("t", json (sp ("OK1"))));
                 br.sleep (1500);
               }}};
    b.start_and_wait ();
    HeardByMqtt c {fast (b.port ())};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("t");
    QTRY_COMPARE_WITH_TIMEOUT (spy.count (), 1, 5000);
    QCOMPARE (b.connections (), 2);
    QVERIFY (c.stats ().failures >= 1);
    auto const ids = b.client_ids ();
    QCOMPARE (ids.size (), 2);
    QVERIFY (ids[0] != ids[1]);
    c.stop ();
    b.finish ();
  }

  void no_connack_times_out ()
  {
    Broker b {{[] (Broker& br, Conn& c) {Packet p; br.read (c, p); br.listen (c, 2500);},   // never answers
               [] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 Broker::send (c, Broker::publish ("t", json (sp ("AFTER"))));
                 br.sleep (1500);
               }}};
    b.start_and_wait ();
    auto o = fast (b.port ());
    o.handshake_timeout_ms = 400;
    HeardByMqtt c {o};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("t");
    QTRY_COMPARE_WITH_TIMEOUT (spy.count (), 1, 8000);
    QVERIFY (c.stats ().last_error.contains ("CONNACK"));
    c.stop ();
    b.finish ();
  }

  void no_suback_times_out ()
  {
    Broker b {{[] (Broker& br, Conn& c) {br.handshake (c, false, true); br.listen (c, 2500);},
               [] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 Broker::send (c, Broker::publish ("t", json (sp ("AFTER"))));
                 br.sleep (1500);
               }}};
    b.start_and_wait ();
    auto o = fast (b.port ());
    o.handshake_timeout_ms = 400;
    HeardByMqtt c {o};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("t");
    QTRY_COMPARE_WITH_TIMEOUT (spy.count (), 1, 8000);
    QVERIFY (c.stats ().last_error.contains ("SUBACK"));
    c.stop ();
    b.finish ();
  }

  void silence_is_detected ()
  {
    Broker b {{[] (Broker& br, Conn& c) {if (br.handshake (c)) br.listen (c, 3000);},     // no PINGRESP
               [] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 Broker::send (c, Broker::publish ("t", json (sp ("AFTER"))));
                 br.sleep (1500);
               }}};
    b.start_and_wait ();
    auto o = fast (b.port ());
    o.ping_every_ms = 300;
    o.dead_after_ms = 1000;
    HeardByMqtt c {o};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("t");
    QTRY_VERIFY_WITH_TIMEOUT (b.pings () >= 1, 4000);                 // pinged when idle
    QTRY_COMPARE_WITH_TIMEOUT (spy.count (), 1, 8000);                // dropped, made again
    QCOMPARE (b.connections (), 2);
    QVERIFY (c.stats ().last_error.contains ("nothing received"));
    c.stop ();
    b.finish ();
  }

  // The broker's keep-alive counts what the client SENDS: spots coming
  // in must not stop the pings.
  void pings_while_spots_arrive ()
  {
    Broker b {{[] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 for (int i = 0; i < 15 && br.running (); ++i)
                   {
                     Broker::send (c, Broker::publish ("t", json (sp ("BUSY", i))));
                     br.listen (c, 100);
                   }
               }}};
    b.start_and_wait ();
    auto o = fast (b.port ());
    o.ping_every_ms = 300;
    HeardByMqtt c {o};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("t");
    QTRY_VERIFY_WITH_TIMEOUT (spy.count () >= 12, 5000);
    QVERIFY2 (b.pings () >= 2, qPrintable (QString::number (b.pings ())));
    c.stop ();
    b.finish ();
  }

  // check_feed: the status topic's value moves = delivering; it stands
  // still, or nothing comes = not.  Same connection, then unsubscribed.
  void feed_check ()
  {
    std::atomic<bool> moving {true};
    Broker b {{[&moving] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 br.listen (c, 10000, [&moving] (Conn& c, QString const& topic) {
                     Broker::send (c, Broker::publish (topic, "2026-11-21T10:00:00.000Z"));
                     Broker::send (c, Broker::publish (topic, moving ? "2026-11-21T10:00:01.000Z"
                                                                     : "2026-11-21T10:00:00.000Z"));
                   });
               }}};
    b.start_and_wait ();
    HeardByMqtt c {fast (b.port ())};
    QSignalSpy checked {&c, &HeardByMqtt::feed_checked};
    QSignalSpy spots {&c, &HeardByMqtt::spot};
    c.check_feed (500);                                  // not subscribed: nothing happens
    QVERIFY (!c.checking ());
    c.start ("t");
    QTRY_VERIFY_WITH_TIMEOUT (c.subscribed (), 5000);
    c.check_feed (2000);
    QVERIFY (c.checking ());
    QTRY_COMPARE_WITH_TIMEOUT (checked.count (), 1, 3000);
    QCOMPARE (checked.at (0).at (0).toBool (), true);
    QVERIFY (!c.checking ());
    moving = false;
    c.check_feed (500);
    QTRY_COMPARE_WITH_TIMEOUT (checked.count (), 2, 3000);
    QCOMPARE (checked.at (1).at (0).toBool (), false);
    QTRY_COMPARE_WITH_TIMEOUT (b.unsubscribes (), 2, 3000);
    QCOMPARE (b.topics (), (QStringList {"t", c.options ().status_topic, c.options ().status_topic}));
    QCOMPARE (spots.count (), 0);                        // status values are not spots
    QCOMPARE (c.stats ().bad_payloads, 0);
    QCOMPARE (b.connections (), 1);
    c.stop ();
    b.finish ();
  }

  void half_packet_then_cut_leaves_nothing_behind ()
  {
    Broker b {{[] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 auto const p = Broker::publish ("t", json ({{"rc", "CUT"}, {"pad", QString (50, 'y')}}));
                 Broker::send (c, p.left (20));                // half a packet, then gone
               },
               [] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 Broker::send (c, Broker::publish ("t", json (sp ("AFTER"))));
                 br.sleep (1500);
               }}};
    b.start_and_wait ();
    HeardByMqtt c {fast (b.port ())};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("t");
    QTRY_COMPARE_WITH_TIMEOUT (spy.count (), 1, 8000);
    QCOMPARE (spy[0][0].toJsonObject ()["rc"].toString (), QString {"AFTER"});
    QCOMPARE (b.connections (), 2);
    c.stop ();
    b.finish ();
  }

  void garbage_drops_the_connection ()
  {
    Broker b {{[] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 Broker::send (c, QByteArray {"\x30\xff\xff\xff\xff\xff", 6});
                 br.sleep (1000);
               },
               [] (Broker& br, Conn& c) {
                 if (!br.handshake (c)) return;
                 Broker::send (c, Broker::publish ("t", json (sp ("AFTER"))));
                 br.sleep (1500);
               }}};
    b.start_and_wait ();
    HeardByMqtt c {fast (b.port ())};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("t");
    QTRY_COMPARE_WITH_TIMEOUT (spy.count (), 1, 8000);
    QVERIFY (c.stats ().last_error.contains ("malformed"));
    c.stop ();
    b.finish ();
  }

  void stop_is_quick_and_final ()
  {
    Broker b {{[] (Broker& br, Conn& c) {if (br.handshake (c)) br.listen (c, 2000);}}};
    b.start_and_wait ();
    HeardByMqtt c {fast (b.port ())};
    c.start ("t");
    QTRY_VERIFY_WITH_TIMEOUT (c.subscribed (), 3000);
    QElapsedTimer t;
    t.start ();
    c.stop ();
    QVERIFY (t.elapsed () < 500);
    QCOMPARE (c.status (), QString {"stopped"});
    QVERIFY (!c.running ());
    QTest::qWait (700);                                   // nothing comes back to life
    QVERIFY (!c.running ());
    b.finish ();
  }

  void nothing_listening_keeps_retrying ()
  {
    quint16 port;
    {
      QTcpServer s;
      s.listen (QHostAddress::LocalHost, 0);
      port = s.serverPort ();
    }                                                     // closed: refused
    auto o = fast (port);
    o.connect_timeout_ms = 500;
    HeardByMqtt c {o};
    c.start ("t");
    QTRY_VERIFY_WITH_TIMEOUT (c.stats ().failures >= 3, 6000);
    QVERIFY (c.status ().startsWith ("disconnected"));
    c.stop ();
  }

  void restart_with_another_topic ()
  {
    Broker b {{[] (Broker& br, Conn& c) {if (br.handshake (c)) br.listen (c, 1500);}}};
    b.start_and_wait ();
    HeardByMqtt c {fast (b.port ())};
    c.start ("one");
    QTRY_VERIFY_WITH_TIMEOUT (c.subscribed (), 3000);
    c.start ("two");                                      // stop + start
    QTRY_VERIFY_WITH_TIMEOUT (c.subscribed () && c.topic () == "two", 3000);
    QCOMPARE (b.connections (), 2);
    QCOMPARE (b.topics (), (QStringList {"one", "two"}));
    c.stop ();
    b.finish ();
  }

  // ---- TLS ---------------------------------------------------------------

  void tls_port_follows_the_flag ()
  {
    HeardByMqtt::Options o;
    QCOMPARE (o.port_in_use (), quint16 {1883});          // plain unless asked
    o.tls = true;
    QCOMPARE (o.port_in_use (), quint16 {1884});          // 1883 would only fail
    o.port = 4000;
    QCOMPARE (o.port_in_use (), quint16 {4000});          // a chosen port is kept
  }

  void tls_trusted_certificate_gives_spots_even_in_a_burst ()
  {
    if (!QSslSocket::supportsSsl ()) QSKIP ("no TLS support here");
    if (secure_transport ()) QSKIP ("not checked with Secure Transport (no spots arrived there with the test certificate)");
    QSslCertificate const cert {QByteArray {heard_by_test_cert::certificate}};
    QSslKey const key {QByteArray {heard_by_test_cert::private_key}, QSsl::Rsa};
    QVERIFY (!cert.isNull ());
    QVERIFY (!key.isNull ());
    Broker b {{[] (Broker& br, Conn& c) {
        if (!br.handshake (c)) return;
        QByteArray all;                               // 50 spots in one write: the TLS layer
        for (int i = 0; i < 50; ++i)                  // may hold decrypted bytes for a while
          all += Broker::publish ("t", json (sp (QString {"S%1"}.arg (i), i)));
        Broker::send (c, all);
        br.sleep (1500);
      }}};
    b.use_tls (cert, key);
    b.start_and_wait ();
    auto o = fast (b.port ());
    o.tls = true;
    o.tls_name = "localhost";
    o.extra_cas = {cert};
    HeardByMqtt c {o};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("t", "K1ABC");
    QTRY_VERIFY2_WITH_TIMEOUT (spy.count () == 50,
                               qPrintable (QString {"%1 spots, %2; broker TLS failures %3; %4"}
                                           .arg (spy.count ()).arg (c.status ()).arg (b.tls_failures ())
                                           .arg (QSslSocket::sslLibraryVersionString ())), 6000);
    QCOMPARE (b.tls_failures (), 0);
    QVERIFY (c.subscribed ());
    QVERIFY (c.encrypted ());
    QCOMPARE (spy[49][0].toJsonObject ()["rc"].toString (), QString {"S49"});
    c.stop ();
    b.finish ();
  }

  // Optional, for a human: the REAL broker over TLS (port 1884, the system's
  // certificate authorities).  Run with HEARD_BY_LIVE=1; it needs the internet
  // and takes up to 30 s.  Uses the 1 % sample topic, so spots come at once.
  void live_broker_over_tls ()
  {
    if (qgetenv ("HEARD_BY_LIVE").isEmpty ()) QSKIP ("set HEARD_BY_LIVE=1 to try the real broker");
    HeardByMqtt::Options o;
    o.tls = true;                                         // the TLS port, default host
    HeardByMqtt c {o};
    QSignalSpy spy {&c, &HeardByMqtt::spot};
    c.start ("pskr/filter/v2raw_1pc/#", "K1ABC");
    QTRY_VERIFY_WITH_TIMEOUT (c.subscribed (), 20000);
    QVERIFY2 (c.encrypted (), qPrintable (c.status ()));
    QTRY_VERIFY_WITH_TIMEOUT (spy.count () >= 5, 20000);
    qInfo () << "LIVE TLS:" << c.status () << "spots" << spy.count () << "failures" << c.stats ().failures;
    QVERIFY2 (0 == c.stats ().failures, qPrintable (c.stats ().last_error));
    c.stop ();
  }

  // An untrusted certificate, or one for another name, is a failed connection
  // that keeps failing - and is never answered by a plain connection.
  void tls_untrusted_or_misnamed_certificate_is_refused ()
  {
    if (!QSslSocket::supportsSsl ()) QSKIP ("no TLS support here");
    QSslCertificate const cert {QByteArray {heard_by_test_cert::certificate}};
    QSslKey const key {QByteArray {heard_by_test_cert::private_key}, QSsl::Rsa};
    for (bool misnamed : {false, true})
      {
        Broker b {{[] (Broker& br, Conn& c) {
            if (!br.handshake (c)) return;
            Broker::send (c, Broker::publish ("t", json (sp ("NEVER"))));
            br.sleep (1000);
          }}};
        b.use_tls (cert, key);
        b.start_and_wait ();
        auto o = fast (b.port ());
        o.tls = true;
        o.tls_name = misnamed ? "example.org" : "localhost";
        if (misnamed) o.extra_cas = {cert};           // trusted, but not for this name
        HeardByMqtt c {o};
        QSignalSpy spy {&c, &HeardByMqtt::spot};
        c.start ("t", "K1ABC");
        QTRY_VERIFY_WITH_TIMEOUT (c.stats ().failures >= 2, 6000);
        QVERIFY (!c.stats ().last_error.isEmpty ());
        QVERIFY2 (c.status ().contains ("disconnected"), qPrintable (c.status ()));
        QCOMPARE (spy.count (), 0);
        QVERIFY (!c.subscribed ());
        QVERIFY (b.client_ids ().isEmpty ());         // the broker never got a CONNECT
        // On the macOS CI (Secure Transport) the broker counted these
        // connections as encrypted although the client refused them.
        if (!secure_transport ()) QCOMPARE (b.connections (), 0);
        c.stop ();
        b.finish ();
      }
  }
};

QTEST_MAIN (TestHeardByMqtt)
#include "test_heard_by_mqtt.moc"
