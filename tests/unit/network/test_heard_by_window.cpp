// Tests for HeardByWindow: nothing is asked until it is shown, then the
// reports of the call in use appear, filtered to the mode and band in use.
#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QtTest>

#include "widgets/HeardByWindow.hpp"
#include "heard_by_fake_broker.hpp"

namespace
{
  class HttpStub final : public QObject
  {
  public:
    explicit HttpStub (QByteArray const& body) : body_ {body}
    {
      server_.listen (QHostAddress::LocalHost, 0);
      connect (&server_, &QTcpServer::newConnection, this, [this] {
          while (auto * s = server_.nextPendingConnection ())
            connect (s, &QTcpSocket::readyRead, this, [this, s] {
                buffer_[s] += s->readAll ();
                if (!buffer_[s].contains ("\r\n\r\n")) return;
                ++requests;
                last_request = QString::fromLatin1 (buffer_[s].left (buffer_[s].indexOf ("\r\n")));
                buffer_.remove (s);
                s->write ("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: "
                          + QByteArray::number (body_.size ()) + "\r\nConnection: close\r\n\r\n" + body_);
                s->disconnectFromHost ();
              });
        });
    }
    QString url () const {return QString {"http://127.0.0.1:%1/query"}.arg (server_.serverPort ());}
    int requests {0};
    QString last_request;
  private:
    QTcpServer server_;
    QByteArray body_;
    QHash<QTcpSocket *, QByteArray> buffer_;
  };

  QByteArray body (qint64 now)
  {
    auto one = [] (char const * rx, char const * grid, quint64 f, qint64 t, char const * mode, int snr) {
      return QByteArray {"<receptionReport receiverCallsign=\""} + rx + "\" receiverLocator=\"" + grid
        + "\" senderCallsign=\"K1ABC\" frequency=\"" + QByteArray::number (f) + "\" flowStartSeconds=\""
        + QByteArray::number (t) + "\" mode=\"" + mode + "\" sNR=\"" + QByteArray::number (snr) + "\"/>\n";
    };
    return "<?xml version=\"1.0\"?><receptionReports>\n"
      + one ("DK3RW", "JO40PS", 10489541745ULL, now - 120, "FT8", 4)
      + one ("F6BLP", "IN94KP", 10489541745ULL, now - 60, "FT8", 11)
      + "</receptionReports>";
  }
}

class TestHeardByWindow : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void nothing_is_asked_until_shown ()
  {
    HttpStub http {body (QDateTime::currentSecsSinceEpoch ())};
    QTemporaryFile ini;
    QVERIFY (ini.open ());
    QSettings settings {ini.fileName (), QSettings::IniFormat};
    QNetworkAccessManager nam;
    HeardByWindow::Context ctx {"K1ABC", "FN42", "FT8", "3cm"};
    auto band_of = [] (quint64 f) {return f > 10000000000ULL ? QString {"3cm"} : f > 14000000 && f < 14350000 ? QString {"20m"} : QString {};};
    auto distance_to = [] (QString const& g) {return g.startsWith ("IN") ? 1000.0 : -1.0;};
    HeardByWindow w {&settings, QFont {}, &nam, [&ctx] {return ctx;}, band_of, distance_to, nullptr, http.url ()};
    w.set_source (HeardByWindow::Source::Records);
    w.tick ();                                           // not shown: nothing is asked
    QTest::qWait (200);
    QCOMPARE (http.requests, 0);
    w.show ();
    QTRY_COMPARE_WITH_TIMEOUT (w.model ()->rowCount (), 2, 5000);   // DK3RW, F6BLP
    QCOMPARE (http.requests, 1);
    QVERIFY (http.last_request.contains ("senderCallsign=K1ABC"));
    QVERIFY (http.last_request.contains ("flowStartSeconds=-21600"));   // first look: 6 hours
    QCOMPARE (w.model ()->visible (0).receiver, QString {"F6BLP"});
    QCOMPARE (w.model ()->index (0, HeardByModel::Distance).data ().toInt (), 1000);
    QVERIFY (w.status_text ().contains ("2 stations heard K1ABC"));
    w.tick ();
    QTest::qWait (200);
    QCOMPARE (http.requests, 1);                         // not again within 5 minutes
    w.close ();
    settings.sync ();
    QVERIFY (settings.value ("HeardBy/LastQueryMs").toLongLong () > 0);
  }

  void shows_every_report_in_this_order_and_the_label_follows_the_main_window ()
  {
    QTemporaryFile ini;
    QVERIFY (ini.open ());
    QSettings settings {ini.fileName (), QSettings::IniFormat};
    QNetworkAccessManager nam;
    HeardByWindow::Context ctx {"K1ABC", "FN42", "FT8", "3cm"};
    auto band_of = [] (quint64 f) {
      return f > 10000000000ULL ? QString {"3cm"} : f > 2300000000ULL && f < 2450000000ULL ? QString {"13cm"}
        : f > 14000000 && f < 14350000 ? QString {"20m"} : QString {};
    };
    HeardByWindow w {&settings, QFont {}, &nam, [&ctx] {return ctx;}, band_of, [] (QString const&) {return -1.0;}};
    w.set_source (HeardByWindow::Source::Records);
    auto const now = QDateTime::currentSecsSinceEpoch ();
    w.tick ();                                           // first sight of the call (hidden: asks nothing)
    for (auto const& r : HeardByRecords::parse (body (now)).reports) w.model ()->add_report (r);
    w.model ()->add_report ({"W1AW", "FN31", "FT8", 14074500, -5, now - 30, 1001});
    w.model ()->add_report ({"K2ZZ", "FN20", "Q65", 10489541745ULL, 2, now - 20, 1002});
    w.model ()->add_report ({"UPLINKER", "JO40", "FT8", 2400041745ULL, 3, now - 10, 1003});
    w.tick ();
    QCOMPARE (w.model ()->rowCount (), 5);               // nothing is hidden, whatever the main window uses
    QCOMPARE (w.in_use_text (), QString {"WSJT-X: FT8 on 3cm"});
    ctx.band = "20m";
    ctx.mode = "Q65";
    w.tick ();
    QCOMPARE (w.in_use_text (), QString {"WSJT-X: Q65 on 20m"});
    QCOMPARE (w.model ()->rowCount (), 5);
    // columns: the ones an operator wants first, band, MHz and mode last, no "Best"
    QStringList heads;
    for (int c = 0; c < w.model ()->columnCount (); ++c)
      heads << w.model ()->headerData (c, Qt::Horizontal, Qt::DisplayRole).toString ();
    QCOMPARE (heads, (QStringList {"UTC", "Ago", "Heard by", "Grid", "dB", "km", "Spots", "Band", "MHz", "Mode"}));
    QCOMPARE (w.model ()->index (0, HeardByModel::Ago).data ().toString (), QString {"0m"});   // "12m", no space
    ctx.call = "";
    w.tick ();
    QCOMPARE (w.model ()->all_rows (), 0);               // another call: list cleared
    QVERIFY (w.status_text ().contains ("Settings"));
  }

  void can_be_narrowed_to_the_first_columns_and_is_not_wider_than_the_columns ()
  {
    QTemporaryFile ini;
    QVERIFY (ini.open ());
    QSettings settings {ini.fileName (), QSettings::IniFormat};
    QNetworkAccessManager nam;
    HeardByWindow w {&settings, QFont {}, &nam, [] {return HeardByWindow::Context {"K1ABC", "FN42", "FT8", "3cm"};},
                     [] (quint64) {return QString {"3cm"};}, [] (QString const&) {return 100.0;}};
    w.set_source (HeardByWindow::Source::Records);
    w.tick ();
    for (auto const& r : HeardByRecords::parse (body (QDateTime::currentSecsSinceEpoch ())).reports)
      w.model ()->add_report (r);
    w.show ();
    w.tick ();
    QTest::qWait (100);
    w.tick ();
    // no label or box keeps the window wide: UTC, Ago, Heard by and Grid fit
    QVERIFY2 (w.minimumSizeHint ().width () < 330, qPrintable (QString::number (w.minimumSizeHint ().width ())));
    // and it cannot be stretched into empty space beyond the last column
    QVERIFY (w.maximumWidth () < QWIDGETSIZE_MAX);
    QVERIFY (w.maximumWidth () > w.minimumSizeHint ().width ());
    w.resize (3000, 400);
    QVERIFY (w.width () <= w.maximumWidth ());
  }
  void mqtt_source_history_once_then_live_spots ()
  {
    using namespace fake_broker;
    auto const now = QDateTime::currentSecsSinceEpoch ();
    HttpStub http {body (now)};
    // the live feed: a new report, and one the records already gave (counted once)
    auto const live = json ({{"sc", "K1ABC"}, {"rc", "G4XYZ"}, {"rl", "IO91"}, {"md", "FT8"},
                             {"f", 10489541745.0}, {"rp", 7}, {"t", double (now - 5)}});
    auto const again = json ({{"sc", "K1ABC"}, {"rc", "DK3RW"}, {"rl", "JO40PS"}, {"md", "FT8"},
                              {"f", 10489541745.0}, {"rp", 4}, {"t", double (now - 120)}});
    Broker feed {{[live, again] (Broker& br, Conn& c) {
        if (!br.handshake (c)) return;
        br.sleep (1500);                                 // after the history
        Broker::send (c, Broker::publish ("pskr/filter/v2/3cm/FT8/K1ABC/G4XYZ/FN42/IO91/291/223", live));
        Broker::send (c, Broker::publish ("pskr/filter/v2/3cm/FT8/K1ABC/DK3RW/FN42/JO40/291/230", again));
        br.listen (c, 30000);
      }}};
    feed.start_and_wait ();
    QTemporaryFile ini;
    QVERIFY (ini.open ());
    QSettings settings {ini.fileName (), QSettings::IniFormat};
    QNetworkAccessManager nam;
    HeardByWindow::Context ctx {"K1ABC", "FN42", "FT8", "3cm"};
    auto band_of = [] (quint64 f) {return f > 10000000000ULL ? QString {"3cm"} : QString {};};
    HeardByWindow w {&settings, QFont {}, &nam, [&ctx] {return ctx;}, band_of,
                     [] (QString const&) {return -1.0;}, nullptr, http.url ()};
    QCOMPARE (w.source (), HeardByWindow::Source::Mqtt);   // the default
    HeardByWindow::Timing timing;
    timing.silent_after_ms = 60000;                      // no feed check in this test
    w.set_mqtt (fast (feed.port ()), timing);
    w.set_source (HeardByWindow::Source::Mqtt);
    w.tick ();                                           // hidden: nothing is asked
    QTest::qWait (200);
    QCOMPARE (feed.connections (), 0);
    QCOMPARE (http.requests, 0);
    w.show ();
    QTRY_COMPARE_WITH_TIMEOUT (w.model ()->rowCount (), 2, 5000);   // history: DK3RW, F6BLP
    QTRY_COMPARE_WITH_TIMEOUT (w.model ()->rowCount (), 3, 8000);   // + G4XYZ live
    QCOMPARE (w.model ()->visible (0).receiver, QString {"G4XYZ"});
    for (int i = 0; i < w.model ()->rowCount (); ++i) QCOMPARE (w.model ()->visible (i).count, 1);
    QCOMPARE (feed.topics (), QStringList {HeardByMqtt::topic_for_sender ("K1ABC")});
    QVERIFY (feed.client_ids ().value (0).startsWith ("WSJT-X-K1ABC-"));
    QVERIFY (w.status_text ().contains ("Live feed: subscribed"));
    for (int i = 0; i < 3; ++i) w.tick ();
    QCOMPARE (http.requests, 1);                         // the records only once
    QVERIFY (w.banner_text ().isEmpty ());
    w.close ();                                          // closed: the feed is let go
    QVERIFY (!w.mqtt ()->running ());
    settings.sync ();
    QCOMPARE (settings.value ("HeardBy/Source").toString (), QString {"mqtt"});
    feed.finish ();
  }

  void switching_source_clears_the_list ()
  {
    using namespace fake_broker;
    auto const now = QDateTime::currentSecsSinceEpoch ();
    HttpStub http {body (now)};
    // over the feed: one the records already gave (counted again after the switch)
    auto const again = json ({{"sc", "K1ABC"}, {"rc", "DK3RW"}, {"rl", "JO40PS"}, {"md", "FT8"},
                              {"f", 10489541745.0}, {"rp", 4}, {"t", double (now - 120)}});
    Broker feed {{[again] (Broker& br, Conn& c) {
        if (!br.handshake (c)) return;
        br.sleep (500);
        Broker::send (c, Broker::publish ("pskr/filter/v2/3cm/FT8/K1ABC/DK3RW/FN42/JO40/291/230", again));
        br.listen (c, 30000);
      }}};
    feed.start_and_wait ();
    QTemporaryFile ini;
    QVERIFY (ini.open ());
    QSettings settings {ini.fileName (), QSettings::IniFormat};
    QNetworkAccessManager nam;
    HeardByWindow::Context ctx {"K1ABC", "FN42", "FT8", "3cm"};
    auto band_of = [] (quint64 f) {return f > 10000000000ULL ? QString {"3cm"} : QString {};};
    HeardByWindow w {&settings, QFont {}, &nam, [&ctx] {return ctx;}, band_of,
                     [] (QString const&) {return -1.0;}, nullptr, http.url ()};
    w.set_source (HeardByWindow::Source::Records);
    HeardByWindow::Timing timing;
    timing.silent_after_ms = 60000;
    w.set_mqtt (fast (feed.port ()), timing);
    w.show ();
    QTRY_COMPARE_WITH_TIMEOUT (w.model ()->rowCount (), 2, 5000);   // records: DK3RW, F6BLP
    w.set_source (HeardByWindow::Source::Mqtt);          // in view: live rows only
    QCOMPARE (w.model ()->all_rows (), 0);
    QTRY_COMPARE_WITH_TIMEOUT (w.model ()->rowCount (), 1, 5000);   // DK3RW, over the feed
    QCOMPARE (w.model ()->visible (0).receiver, QString {"DK3RW"});
    for (int i = 0; i < 3; ++i) w.tick ();
    QCOMPARE (http.requests, 1);                         // no records query after the switch
    QCOMPARE (w.model ()->rowCount (), 1);
    w.set_source (HeardByWindow::Source::Records);       // back: cleared, the feed let go
    QCOMPARE (w.model ()->all_rows (), 0);
    QVERIFY (!w.mqtt ()->running ());
    QVERIFY (w.status_text ().contains ("Next check in"));   // PSK Reporter's 5 minutes
    w.close ();
    feed.finish ();
  }

  void mqtt_silent_feed_banner_clears_when_spots_return ()
  {
    using namespace fake_broker;
    auto const now = QDateTime::currentSecsSinceEpoch ();
    HttpStub http {"<?xml version=\"1.0\"?><receptionReports></receptionReports>"};
    auto const spot = json ({{"sc", "K1ABC"}, {"rc", "OH2LIY"}, {"rl", "KP20"}, {"md", "FT8"},
                             {"f", 10489541745.0}, {"rp", -3}, {"t", double (now)}});
    std::atomic<bool> release {false};
    // quiet until released; the broker's "last spot" time does not move
    Broker feed {{[spot, &release] (Broker& br, Conn& c) {
        if (!br.handshake (c)) return;
        while (!release && br.running ())
          br.listen (c, 50, [] (Conn& c, QString const& topic) {
              for (int i = 0; i < 2; ++i) Broker::send (c, Broker::publish (topic, "2026-11-21T10:00:00.000Z"));
            });
        Broker::send (c, Broker::publish ("pskr/filter/v2/3cm/FT8/K1ABC/OH2LIY/FN42/KP20/291/224", spot));
        br.listen (c, 30000);
      }}};
    feed.start_and_wait ();
    QTemporaryFile ini;
    QVERIFY (ini.open ());
    QSettings settings {ini.fileName (), QSettings::IniFormat};
    QNetworkAccessManager nam;
    HeardByWindow::Context ctx {"K1ABC", "FN42", "FT8", "3cm"};
    auto band_of = [] (quint64 f) {return f > 10000000000ULL ? QString {"3cm"} : QString {};};
    HeardByWindow w {&settings, QFont {}, &nam, [&ctx] {return ctx;}, band_of,
                     [] (QString const&) {return -1.0;}, nullptr, http.url ()};
    w.set_source (HeardByWindow::Source::Records);
    HeardByWindow::Timing timing;
    timing.silent_after_ms = 500;
    timing.check_window_ms = 800;
    timing.check_every_ms = 600000;                      // one check only
    w.set_mqtt (fast (feed.port ()), timing);
    w.set_source (HeardByWindow::Source::Mqtt);
    w.show ();
    QTRY_VERIFY_WITH_TIMEOUT (w.mqtt ()->subscribed (), 5000);
    QTRY_VERIFY_WITH_TIMEOUT (w.banner_text ().contains ("checking"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT (w.banner_text ().contains ("silent"), 5000);
    // one connection; our own call and the broker's status topic, nothing else
    QCOMPARE (feed.topics (), (QStringList {HeardByMqtt::topic_for_sender ("K1ABC"),
                                            HeardByMqtt::Options {}.status_topic}));
    QVERIFY (!w.mqtt ()->checking ());
    release = true;                                      // the feed recovers
    QTRY_COMPARE_WITH_TIMEOUT (w.model ()->rowCount (), 1, 5000);
    QVERIFY (!w.banner_text ().contains ("silent"));
    // quiet again: the feed is known to deliver, the check is not repeated
    QTRY_VERIFY_WITH_TIMEOUT (w.banner_text ().contains ("delivering"), 5000);
    QCOMPARE (feed.connections (), 1);
    QCOMPARE (feed.topics ().size (), 2);                // no second check
    w.close ();
    feed.finish ();
  }
};

QTEST_MAIN (TestHeardByWindow)
#include "test_heard_by_window.moc"
