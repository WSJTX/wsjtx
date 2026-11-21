// Tests for HeardByWindow: nothing is asked until it is shown, then the
// reports of the call in use arrive over the live feed and are all shown.
#include <QDateTime>
#include <QSettings>
#include <QTemporaryFile>
#include <QtTest>

#include "widgets/HeardByWindow.hpp"
#include "heard_by_fake_broker.hpp"

namespace
{
  QString band_of (quint64 f)
  {
    return f > 10000000000ULL ? QString {"3cm"} : f > 2300000000ULL && f < 2450000000ULL ? QString {"13cm"}
      : f > 14000000 && f < 14350000 ? QString {"20m"} : QString {};
  }

  HeardByReport report (QString const& rx, QString const& grid, QString const& mode, quint64 f, int snr, qint64 when)
  {
    HeardByReport r;
    r.receiver = rx; r.locator = grid; r.mode = mode; r.frequency = f; r.snr = snr; r.when = when;
    r.id = HeardByReport::id_of (rx, when, f);
    return r;
  }

  QByteArray spot (QString const& rx, QString const& grid, int snr, qint64 when)
  {
    return fake_broker::json ({{"sc", "K1ABC"}, {"rc", rx}, {"rl", grid}, {"md", "FT8"},
                               {"f", 10489541745.0}, {"rp", snr}, {"t", double (when)}});
  }

  // nothing listens here: for the tests that never show the window
  HeardByMqtt::Options nowhere ()
  {
    return fake_broker::fast (1);
  }
}

class TestHeardByWindow : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void shows_every_report_in_this_order_and_the_label_follows_the_main_window ()
  {
    QTemporaryFile ini;
    QVERIFY (ini.open ());
    QSettings settings {ini.fileName (), QSettings::IniFormat};
    HeardByWindow::Context ctx {"K1ABC", "FN42", "FT8", "3cm"};
    HeardByWindow w {&settings, QFont {}, [&ctx] {return ctx;}, band_of, [] (QString const&) {return -1.0;}};
    w.set_mqtt (nowhere (), {});
    auto const now = QDateTime::currentSecsSinceEpoch ();
    w.tick ();                                           // first sight of the call (hidden: asks nothing)
    QVERIFY (!w.mqtt ()->running ());
    w.model ()->add_report (report ("DK3RW", "JO40PS", "FT8", 10489541745ULL, 4, now - 120));
    w.model ()->add_report (report ("F6BLP", "IN94KP", "FT8", 10489541745ULL, 11, now - 60));
    w.model ()->add_report (report ("W1AW", "FN31", "FT8", 14074500, -5, now - 30));
    w.model ()->add_report (report ("K2ZZ", "FN20", "Q65", 10489541745ULL, 2, now - 20));
    w.model ()->add_report (report ("UPLINKER", "JO40", "FT8", 2400041745ULL, 3, now - 10));
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
    HeardByWindow w {&settings, QFont {}, [] {return HeardByWindow::Context {"K1ABC", "FN42", "FT8", "3cm"};},
                     [] (quint64) {return QString {"3cm"};}, [] (QString const&) {return 100.0;}};
    w.set_mqtt (nowhere (), {});
    w.tick ();
    auto const now = QDateTime::currentSecsSinceEpoch ();
    w.model ()->add_report (report ("DK3RW", "JO40PS", "FT8", 10489541745ULL, 4, now - 120));
    w.model ()->add_report (report ("F6BLP", "IN94KP", "FT8", 10489541745ULL, 11, now - 60));
    w.show ();
    w.tick ();
    QTest::qWait (100);
    w.tick ();
    // no label keeps the window wide: UTC, Ago, Heard by and Grid fit
    QVERIFY2 (w.minimumSizeHint ().width () < 330, qPrintable (QString::number (w.minimumSizeHint ().width ())));
    // and it cannot be stretched into empty space beyond the last column
    QVERIFY (w.maximumWidth () < QWIDGETSIZE_MAX);
    QVERIFY (w.maximumWidth () > w.minimumSizeHint ().width ());
    w.resize (3000, 400);
    QVERIFY (w.width () <= w.maximumWidth ());
  }

  // Nothing is asked until the window is shown; then our own call's
  // topic and nothing else.  Closed, the feed is let go but the list is
  // kept, and a report repeated after opening again is counted once.
  void live_spots_only_while_shown_and_the_list_is_kept ()
  {
    using namespace fake_broker;
    auto const now = QDateTime::currentSecsSinceEpoch ();
    auto const first = spot ("DK3RW", "JO40PS", 4, now - 120);
    auto const second = spot ("G4XYZ", "IO91", 7, now - 5);
    Broker feed {{[first] (Broker& br, Conn& c) {
        if (!br.handshake (c)) return;
        Broker::send (c, Broker::publish ("pskr/filter/v2/3cm/FT8/K1ABC/DK3RW/FN42/JO40/291/230", first));
        br.listen (c, 30000);
      },
      [first, second] (Broker& br, Conn& c) {
        if (!br.handshake (c)) return;
        Broker::send (c, Broker::publish ("pskr/filter/v2/3cm/FT8/K1ABC/DK3RW/FN42/JO40/291/230", first));
        Broker::send (c, Broker::publish ("pskr/filter/v2/3cm/FT8/K1ABC/G4XYZ/FN42/IO91/291/223", second));
        br.listen (c, 30000);
      }}};
    feed.start_and_wait ();
    QTemporaryFile ini;
    QVERIFY (ini.open ());
    QSettings settings {ini.fileName (), QSettings::IniFormat};
    HeardByWindow::Context ctx {"K1ABC", "FN42", "FT8", "3cm"};
    HeardByWindow w {&settings, QFont {}, [&ctx] {return ctx;}, band_of,
                     [] (QString const& g) {return g.startsWith ("IO") ? 1000.0 : -1.0;}};
    HeardByWindow::Timing timing;
    timing.silent_after_ms = 60000;                      // no feed check in this test
    w.set_mqtt (fast (feed.port ()), timing);
    w.tick ();                                           // hidden: nothing is asked
    QTest::qWait (200);
    QCOMPARE (feed.connections (), 0);
    w.show ();
    QTRY_COMPARE_WITH_TIMEOUT (w.model ()->rowCount (), 1, 5000);   // DK3RW
    QCOMPARE (feed.topics (), QStringList {HeardByMqtt::topic_for_sender ("K1ABC")});
    QVERIFY (feed.client_ids ().value (0).startsWith ("WSJT-X-K1ABC-"));
    QVERIFY (w.status_text ().contains ("1 stations heard K1ABC"));
    QVERIFY (w.status_text ().contains ("Live feed: subscribed"));
    QVERIFY (w.banner_text ().isEmpty ());
    w.close ();                                          // closed: the feed is let go, the list is kept
    QVERIFY (!w.mqtt ()->running ());
    QCOMPARE (w.model ()->all_rows (), 1);
    w.show ();
    QTRY_COMPARE_WITH_TIMEOUT (w.model ()->rowCount (), 2, 5000);   // + G4XYZ
    QCOMPARE (feed.connections (), 2);
    QCOMPARE (w.model ()->visible (0).receiver, QString {"G4XYZ"});
    QCOMPARE (w.model ()->index (0, HeardByModel::Distance).data ().toInt (), 1000);
    for (int i = 0; i < w.model ()->rowCount (); ++i) QCOMPARE (w.model ()->visible (i).count, 1);
    w.close ();
    feed.finish ();
  }

  void silent_feed_banner_clears_when_spots_return ()
  {
    using namespace fake_broker;
    auto const now = QDateTime::currentSecsSinceEpoch ();
    auto const own = spot ("OH2LIY", "KP20", -3, now);
    std::atomic<bool> release {false};
    // quiet until released; the broker's "last spot" time does not move
    Broker feed {{[own, &release] (Broker& br, Conn& c) {
        if (!br.handshake (c)) return;
        while (!release && br.running ())
          br.listen (c, 50, [] (Conn& c, QString const& topic) {
              for (int i = 0; i < 2; ++i) Broker::send (c, Broker::publish (topic, "2026-11-21T10:00:00.000Z"));
            });
        Broker::send (c, Broker::publish ("pskr/filter/v2/3cm/FT8/K1ABC/OH2LIY/FN42/KP20/291/224", own));
        br.listen (c, 30000);
      }}};
    feed.start_and_wait ();
    QTemporaryFile ini;
    QVERIFY (ini.open ());
    QSettings settings {ini.fileName (), QSettings::IniFormat};
    HeardByWindow::Context ctx {"K1ABC", "FN42", "FT8", "3cm"};
    HeardByWindow w {&settings, QFont {}, [&ctx] {return ctx;}, band_of, [] (QString const&) {return -1.0;}};
    HeardByWindow::Timing timing;
    timing.silent_after_ms = 500;
    timing.check_window_ms = 800;
    timing.check_every_ms = 600000;                      // one check only
    w.set_mqtt (fast (feed.port ()), timing);
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
