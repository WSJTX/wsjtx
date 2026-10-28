#include <QtTest>

#include <memory>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>

#include "Network/LiveCQUpload.hpp"

namespace
{
  class FakeReply final
    : public QNetworkReply
  {
  public:
    FakeReply (QNetworkRequest const& request, QNetworkReply::NetworkError error,
               QString const& error_string, QObject *parent = nullptr)
      : QNetworkReply {parent}
      , error_ {error}
      , error_string_ {error_string}
    {
      setRequest (request);
      setUrl (request.url ());
      setOperation (QNetworkAccessManager::PostOperation);
      open (QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void abort () override
    {
      error_ = QNetworkReply::OperationCanceledError;
      error_string_ = "aborted";
      finish ();
    }

    void finish ()
    {
      if (isFinished ())
        {
          return;
        }
      setFinished (true);
      if (QNetworkReply::NoError != error_)
        {
          setError (error_, error_string_);
        }
      Q_EMIT finished ();
    }

    void setHttpStatus (int status)
    {
      setAttribute (QNetworkRequest::HttpStatusCodeAttribute, status);
    }

  protected:
    qint64 readData (char *, qint64) override
    {
      return -1;
    }

  private:
    QNetworkReply::NetworkError error_;
    QString error_string_;
  };

  class FakeTransport final
    : public QObject
    , public LiveCQUpload::Transport
  {
  public:
    struct Request
    {
      QUrl url;
      QByteArray body;
      QString content_type;
      QByteArray user_agent;
    };

    explicit FakeTransport (QObject *parent = nullptr)
      : QObject {parent}
    {
    }

    QNetworkReply *post (QNetworkRequest const& request, QByteArray const& body) override
    {
      Request recorded;
      recorded.url = request.url ();
      recorded.body = body;
      recorded.content_type
        = request.header (QNetworkRequest::ContentTypeHeader).toString ();
      recorded.user_agent = request.rawHeader ("User-Agent");
      requests.append (recorded);

      auto *reply = new FakeReply {request, error_, error_string_, this};
      replies.append (reply);
      if (auto_finish)
        {
          QTimer::singleShot (0, reply, [reply] () { reply->finish (); });
        }
      return reply;
    }

    QList<Request> requests;
    QList<FakeReply *> replies;
    bool auto_finish {true};
    QNetworkReply::NetworkError error_ {QNetworkReply::NoError};
    QString error_string_;
  };

  QUrlQuery spotQuery ()
  {
    QUrlQuery query;
    query.addQueryItem ("skedfreq", "50313.0");
    query.addQueryItem ("callsign", "K1ABC");
    query.addQueryItem ("apptype", "MAP65");
    return query;
  }
}

class TestLiveCQUpload final
  : public QObject
{
  Q_OBJECT

private slots:
  void postsSpotToEndpoint ()
  {
    FakeTransport transport;
    LiveCQUpload upload {&transport, false};
    QVERIFY (upload.setEndpoint (QUrl {"https://w3sz.com/livecq_update.php"}));
    upload.setUserAgent ("WSJT-X/test");

    QSignalSpy posted {&upload, &LiveCQUpload::spotPosted};
    upload.postSpot (spotQuery ());

    QCOMPARE (transport.requests.size (), 1);
    auto const& request = transport.requests.first ();
    QCOMPARE (request.url.host (), QString {"w3sz.com"});
    QCOMPARE (request.url.path (), QString {"/livecq_update.php"});
    QVERIFY (request.body.contains ("skedfreq=50313.0"));
    QVERIFY (request.body.contains ("apptype=MAP65"));
    QCOMPARE (request.content_type, QString {"application/x-www-form-urlencoded"});
    QCOMPARE (request.user_agent, QByteArray {"WSJT-X/test"});

    QTRY_COMPARE (posted.count (), 1);
  }

  void rejectsPlaintextEndpoint ()
  {
    FakeTransport transport;
    LiveCQUpload upload {&transport, false};
    QSignalSpy errors {&upload, &LiveCQUpload::errorOccurred};

    QVERIFY (!upload.setEndpoint (QUrl {"http://w3sz.com/livecq_update.php"}));
    QCOMPARE (errors.count (), 1);
    QVERIFY (!upload.endpoint ().isValid ());

    upload.postSpot (spotQuery ());
    QCOMPARE (transport.requests.size (), 0);
  }

  void rejectsInvalidEndpoint_data ()
  {
    QTest::addColumn<QUrl> ("endpoint");
    QTest::newRow ("empty") << QUrl {};
    QTest::newRow ("http-without-host") << QUrl {"http:///livecq"};
    QTest::newRow ("https-without-host") << QUrl {"https:///livecq"};
    QTest::newRow ("unsupported-scheme") << QUrl {"ftp://example.com/livecq"};
  }

  void rejectsInvalidEndpoint ()
  {
    QFETCH (QUrl, endpoint);
    FakeTransport transport;
    LiveCQUpload upload {&transport, false};
    QSignalSpy errors {&upload, &LiveCQUpload::errorOccurred};
    QVERIFY (!upload.setEndpoint (endpoint));
    QCOMPARE (errors.count (), 1);
    QVERIFY (!upload.endpoint ().isValid ());
    upload.postSpot (spotQuery ());
    QVERIFY (transport.requests.isEmpty ());
  }

  void rejectsPostBeforeEndpointSet ()
  {
    FakeTransport transport;
    LiveCQUpload upload {&transport, false};
    QSignalSpy errors {&upload, &LiveCQUpload::errorOccurred};

    upload.postSpot (spotQuery ());
    QCOMPARE (errors.count (), 1);
    QCOMPARE (transport.requests.size (), 0);
  }

  void postIsAsynchronous ()
  {
    FakeTransport transport;
    transport.auto_finish = false;
    LiveCQUpload upload {&transport, false};
    QVERIFY (upload.setEndpoint (QUrl {"https://w3sz.com/livecq_update.php"}));

    QSignalSpy posted {&upload, &LiveCQUpload::spotPosted};
    upload.postSpot (spotQuery ());

    QCOMPARE (transport.requests.size (), 1);
    QCOMPARE (posted.count (), 0);
    QCOMPARE (transport.replies.size (), 1);
    transport.replies.first ()->finish ();
    QCOMPARE (posted.count (), 1);
  }

  void reportsReplyError ()
  {
    FakeTransport transport;
    transport.error_ = QNetworkReply::HostNotFoundError;
    transport.error_string_ = "host not found";
    LiveCQUpload upload {&transport, false};
    QVERIFY (upload.setEndpoint (QUrl {"https://w3sz.com/livecq_update.php"}));

    QSignalSpy errors {&upload, &LiveCQUpload::errorOccurred};
    upload.postSpot (spotQuery ());
    QTRY_COMPARE (errors.count (), 1);
  }

  void reportsHttpErrorStatus ()
  {
    FakeTransport transport;
    transport.auto_finish = false;
    LiveCQUpload upload {&transport, false};
    QVERIFY (upload.setEndpoint (QUrl {"https://w3sz.com/livecq_update.php"}));

    QSignalSpy posted {&upload, &LiveCQUpload::spotPosted};
    QSignalSpy errors {&upload, &LiveCQUpload::errorOccurred};
    upload.postSpot (spotQuery ());
    QCOMPARE (transport.replies.size (), 1);
    transport.replies.first ()->setHttpStatus (500);
    transport.replies.first ()->finish ();
    QCOMPARE (posted.count (), 0);
    QCOMPARE (errors.count (), 1);
  }

  void abortsOutstandingRequestsOnDestruction ()
  {
    FakeTransport transport;
    transport.auto_finish = false;
    auto upload = std::make_unique<LiveCQUpload> (&transport, false);
    QVERIFY (upload->setEndpoint (QUrl {"https://w3sz.com/livecq_update.php"}));
    upload->postSpot (spotQuery ());
    QCOMPARE (transport.replies.size (), 1);
    auto *reply = transport.replies.first ();

    upload.reset ();
    QCOMPARE (reply->error (), QNetworkReply::OperationCanceledError);
  }

  void runsOnCallingThread ()
  {
    FakeTransport transport;
    LiveCQUpload upload {&transport, false};
    QCOMPARE (upload.thread (), QThread::currentThread ());
    QVERIFY (upload.findChildren<QThread *> ().isEmpty ());
  }
};

QTEST_MAIN (TestLiveCQUpload)

#include "test_livecq_upload.moc"
