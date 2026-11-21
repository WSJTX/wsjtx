// Tests for HeardByRecords (PSK Reporter retrieve reply, rate limit) and
// HeardByModel (rows, filters, expiry).
#include <QHash>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include "Network/HeardByRecords.hpp"
#include "models/HeardByModel.hpp"

namespace
{
  QByteArray const sample =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<receptionReports>\n"
    "<activeReceiver callsign=\"X1XXX\" locator=\"AA00\" frequency=\"1\"/>\n"
    "<receptionReport receiverCallsign=\"DK3RW\" receiverLocator=\"JO40PS\" senderCallsign=\"K1ABC\" "
    "senderLocator=\"FN42\" frequency=\"2400041745\" flowStartSeconds=\"1795010000\" mode=\"FT8\" sNR=\"4\"/>\n"
    "<receptionReport receiverCallsign=\"F6BLP\" receiverLocator=\"IN94KP\" senderCallsign=\"K1ABC\" "
    "frequency=\"10489541745\" flowStartSeconds=\"1795010715\" mode=\"FT8\" sNR=\"11\"/>\n"
    "<receptionReport receiverCallsign=\"F6BLP\" receiverLocator=\"IN94KP\" senderCallsign=\"K1ABC\" "
    "frequency=\"10489541745\" flowStartSeconds=\"1795010400\" mode=\"FT8\" sNR=\"9\"/>\n"
    "<lastSequenceNumber value=\"1\"/>\n</receptionReports>\n";

  // answers every request on a local port with body
  class HttpStub final : public QObject
  {
  public:
    explicit HttpStub (QByteArray const& body, int status = 200)
      : body_ {body}, status_ {status}
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
                s->write ("HTTP/1.1 " + QByteArray::number (status_) + " X\r\nContent-Type: text/xml\r\n"
                          "Content-Length: " + QByteArray::number (body_.size ())
                          + "\r\nConnection: close\r\n\r\n" + body_);
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
    int status_;
    QHash<QTcpSocket *, QByteArray> buffer_;
  };

  HeardByRecords::Report report (QString const& rx, qint64 when, quint64 f, int snr,
                                 QString const& mode = "FT8", QString const& grid = "IN94")
  {
    HeardByRecords::Report r;
    r.receiver = rx; r.locator = grid; r.mode = mode; r.frequency = f; r.snr = snr; r.when = when;
    r.id = HeardByRecords::report_id (rx, when, f);
    return r;
  }

  QString band_of (quint64 f)
  {
    return f > 10000000000ULL ? "3cm" : f > 2000000000ULL ? "13cm" : f > 14000000 && f < 14350000 ? "20m" : QString {};
  }
}

class TestHeardBy : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void parses_reports ()
  {
    auto const r = HeardByRecords::parse (sample);
    QVERIFY (r.ok);
    QCOMPARE (r.reports.size (), 3);                  // activeReceiver is not a report
    QCOMPARE (r.reports[0].receiver, QString {"F6BLP"});
    QCOMPARE (r.reports[0].when, qint64 (1795010715)); // newest first
    QCOMPARE (r.reports[0].snr, 11);
    QCOMPARE (r.reports[0].frequency, quint64 (10489541745ULL));
    QCOMPARE (r.reports[2].locator, QString {"JO40PS"});
    QVERIFY (r.reports[0].id != r.reports[1].id);
    QVERIFY (r.reports[0].id > 0);
  }

  void parses_empty_and_refuses_garbage ()
  {
    auto const e = HeardByRecords::parse ("<?xml version=\"1.0\"?><receptionReports></receptionReports>");
    QVERIFY (e.ok);
    QVERIFY (e.reports.isEmpty ());
    QVERIFY (!HeardByRecords::parse ("<receptionReports><receptionReport").ok);
    QVERIFY (!HeardByRecords::parse ("not xml at all").ok);
    QVERIFY (!HeardByRecords::parse ("").ok);
  }

  void poll_delay_has_jitter ()
  {
    for (int i = 0; i < 50; ++i)
      {
        auto const d = HeardByRecords::poll_delay_ms (1000, 200);
        QVERIFY (d >= 1000 && d <= 1200);
      }
    QCOMPARE (HeardByRecords::poll_delay_ms (1000, 0), 1000);
  }

  void fetches_over_http_and_is_rate_limited ()
  {
    HttpStub http {sample};
    QNetworkAccessManager nam;
    HeardByRecords records {&nam, nullptr, http.url (), 5000};
    QSignalSpy done {&records, &HeardByRecords::finished};
    QVERIFY (records.fetch ("K1ABC", 1800));
    QVERIFY (records.busy ());
    QVERIFY (!records.fetch ("K1ABC", 1800));         // one at a time
    QTRY_COMPARE_WITH_TIMEOUT (done.count (), 1, 5000);
    auto const r = done.first ().first ().value<HeardByRecords::Result> ();
    QVERIFY2 (r.ok, qPrintable (r.error));
    QCOMPARE (r.reports.size (), 3);
    QVERIFY (http.last_request.contains ("senderCallsign=K1ABC"));
    QVERIFY (http.last_request.contains ("flowStartSeconds=-1800"));
    QVERIFY (!http.last_request.contains ("appcontact"));
    QVERIFY (!records.busy ());
    QVERIFY (records.ms_until_next () > 0);
    QVERIFY (!records.fetch ("K1ABC", 1800));         // too soon
    QCOMPARE (http.requests, 1);
  }

  void restored_last_query_time_delays_the_first_fetch ()
  {
    HttpStub http {sample};
    QNetworkAccessManager nam;
    HeardByRecords records {&nam, nullptr, http.url (), 60000};
    records.set_last_query_ms (QDateTime::currentMSecsSinceEpoch () - 10000);
    QVERIFY (records.ms_until_next () > 40000);
    QVERIFY (!records.fetch ("K1ABC", 1800));
    QCOMPARE (http.requests, 0);
  }

  void reports_server_error_and_no_server ()
  {
    {
      HttpStub http {"oops", 503};
      QNetworkAccessManager nam;
      HeardByRecords records {&nam, nullptr, http.url (), 0};
      QSignalSpy done {&records, &HeardByRecords::finished};
      QVERIFY (records.fetch ("K1ABC", 600));
      QTRY_COMPARE_WITH_TIMEOUT (done.count (), 1, 5000);
      auto const r = done.first ().first ().value<HeardByRecords::Result> ();
      QVERIFY (!r.ok);
      QVERIFY (!r.error.isEmpty ());
    }
    quint16 port;
    {
      QTcpServer s;
      s.listen (QHostAddress::LocalHost, 0);
      port = s.serverPort ();
    }
    QNetworkAccessManager nam;
    HeardByRecords records {&nam, nullptr, QString {"http://127.0.0.1:%1/query"}.arg (port), 0};
    QSignalSpy done {&records, &HeardByRecords::finished};
    QVERIFY (records.fetch ("K1ABC", 600));
    QTRY_COMPARE_WITH_TIMEOUT (done.count (), 1, 5000);
    QVERIFY (!done.first ().first ().value<HeardByRecords::Result> ().ok);
    QVERIFY (!records.busy ());
    QVERIFY (!records.fetch ("", 600));               // no call, nothing sent
  }

  void model_groups_counts_and_ignores_repeats ()
  {
    HeardByModel m;
    m.set_band_of (band_of);
    m.set_distance_to ([] (QString const& g) {return g == "IN94" ? 1000.0 : -1.0;});
    auto const now = qint64 (1795011000);
    QVERIFY (m.add_report (report ("F6BLP", now - 600, 10489541745ULL, 9)));
    QVERIFY (m.add_report (report ("F6BLP", now - 300, 10489541745ULL, 11)));
    QVERIFY (!m.add_report (report ("F6BLP", now - 300, 10489541745ULL, 11)));   // same report again
    QVERIFY (m.add_report (report ("DK3RW", now - 200, 2400041745ULL, 4, "FT8", "JO40")));
    QVERIFY (m.add_report (report ("F6BLP", now - 900, 10489541745ULL, 15)));   // older: counts only
    m.refresh (now);
    QCOMPARE (m.rowCount (), 2);
    QCOMPARE (m.total (), 4);
    QCOMPARE (m.visible (0).receiver, QString {"DK3RW"});                        // newest first
    auto const f6 = m.visible (1);
    QCOMPARE (f6.count, 3);
    QCOMPARE (f6.snr, 11);                                                       // latest, not the older 15
    QCOMPARE (f6.band, QString {"3cm"});
    QCOMPARE (m.index (1, HeardByModel::Distance).data ().toInt (), 1000);
    QCOMPARE (m.index (0, HeardByModel::Distance).data ().toString (), QString {});  // unknown grid
    QCOMPARE (m.index (1, HeardByModel::Ago).data ().toString (), QString {"5m"});
  }

  void model_filters_by_mode_and_band ()
  {
    HeardByModel m;
    m.set_band_of (band_of);
    auto const now = qint64 (1795011000);
    m.add_report (report ("A1AAA", now - 10, 14074000, 1, "FT8"));
    m.add_report (report ("B1BBB", now - 20, 14076000, 2, "Q65"));
    m.add_report (report ("C1CCC", now - 30, 10489541745ULL, 3, "FT8"));
    m.refresh (now);
    QCOMPARE (m.rowCount (), 3);
    m.set_filter ("FT8", "");
    QCOMPARE (m.rowCount (), 2);
    m.set_filter ("Q65A", "");                       // sub-mode letter follows the mode
    QCOMPARE (m.rowCount (), 1);
    QCOMPARE (m.visible (0).receiver, QString {"B1BBB"});
    m.set_filter ("", "3cm");
    QCOMPARE (m.rowCount (), 1);
    QCOMPARE (m.visible (0).receiver, QString {"C1CCC"});
    m.set_filter ("", "");
    QCOMPARE (m.rowCount (), 3);
    QCOMPARE (m.all_rows (), 3);
  }

  void model_drops_old_rows_and_clears ()
  {
    HeardByModel m;
    m.set_band_of (band_of);
    auto const now = qint64 (1795011000);
    m.add_report (report ("OLD1", now - HeardByModel::keep_seconds - 60, 14074000, 1));
    m.add_report (report ("NEW1", now - 60, 14074000, 1));
    m.refresh (now);
    QCOMPARE (m.rowCount (), 1);
    QCOMPARE (m.visible (0).receiver, QString {"NEW1"});
    m.clear ();
    QCOMPARE (m.rowCount (), 0);
    QCOMPARE (m.total (), 0);
    QVERIFY (m.add_report (report ("NEW1", now - 60, 14074000, 1)));             // seen-set cleared too
  }

  void model_limits_rows_and_ignores_empty_receiver ()
  {
    HeardByModel m;
    QVERIFY (!m.add_report (report ("", 1795010000, 14074000, 1)));
    auto const now = qint64 (1795011000);
    for (int i = 0; i < HeardByModel::max_rows + 20; ++i)
      m.add_report (report (QString {"S%1"}.arg (i), now - 1000 + i % 900, 14074000, 0));
    m.refresh (now);
    QCOMPARE (m.rowCount (), int (HeardByModel::max_rows));
  }
};

QTEST_MAIN (TestHeardBy)
#include "test_heard_by.moc"
