#include <QtTest>

#include <QBuffer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>

#include "Network/wsprnet.h"

namespace
{
  struct NetworkResponse
  {
    NetworkResponse () = default;

    NetworkResponse (QNetworkReply::NetworkError error, QString error_string, QByteArray body)
      : error {error}
      , error_string {error_string}
      , body {body}
    {
    }

    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QString error_string;
    QByteArray body = "1 spot(s) added";
  };

  class FakeReply final
    : public QNetworkReply
  {
  public:
    FakeReply (QNetworkRequest const& request, NetworkResponse response, QObject *parent = nullptr)
      : QNetworkReply {parent}
      , response_ {response}
      , body_ {response.body}
      , offset_ {0}
      , finished_ {false}
    {
      setRequest (request);
      setUrl (request.url ());
      setOperation (QNetworkAccessManager::PostOperation);
      open (QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void abort () override
    {
      response_.error = QNetworkReply::OperationCanceledError;
      response_.error_string = "aborted";
      finish ();
    }

    qint64 bytesAvailable () const override
    {
      return body_.size () - offset_ + QNetworkReply::bytesAvailable ();
    }

    bool isSequential () const override
    {
      return true;
    }

    bool isFinished () const
    {
      return finished_;
    }

    void finish ()
    {
      if (finished_)
        {
          return;
        }
      finished_ = true;
      if (QNetworkReply::NoError != response_.error)
        {
          setError (response_.error, response_.error_string);
        }
      if (!body_.isEmpty ())
        {
          Q_EMIT readyRead ();
        }
      Q_EMIT finished ();
    }

  protected:
    qint64 readData (char *data, qint64 max_size) override
    {
      auto const bytes = qMin<qint64> (max_size, body_.size () - offset_);
      if (bytes <= 0)
        {
          return -1;
        }
      memcpy (data, body_.constData () + offset_, static_cast<size_t> (bytes));
      offset_ += bytes;
      return bytes;
    }

  private:
    NetworkResponse response_;
    QByteArray body_;
    qint64 offset_;
    bool finished_;
  };

  class FakeNetworkAccessManager final
    : public QObject
    , public WSPRNet::Transport
  {
  public:
    struct Request
    {
      QUrl url;
      QByteArray body;
    };

    explicit FakeNetworkAccessManager (QObject *parent = nullptr)
      : QObject {parent}
      , auto_finish {true}
      , next_reply_to_finish_ {0}
    {
    }

    void enqueueResponse (NetworkResponse response)
    {
      responses_.append (response);
    }

    void finishAll ()
    {
      auto const pending = replies_;
      for (auto const& reply : pending)
        {
          if (reply)
            {
              reply->finish ();
            }
        }
    }

    void finishNext ()
    {
      while (next_reply_to_finish_ < replies_.size ()
             && (!replies_[next_reply_to_finish_] || replies_[next_reply_to_finish_]->isFinished ()))
        {
          ++next_reply_to_finish_;
        }
      QVERIFY (next_reply_to_finish_ < replies_.size ());
      if (next_reply_to_finish_ < replies_.size ())
        {
          replies_[next_reply_to_finish_++]->finish ();
        }
    }

    void finishHost (QString const& host)
    {
      for (auto const& reply : replies_)
        {
          if (reply && !reply->isFinished () && reply->url ().host () == host)
            {
              reply->finish ();
              return;
            }
        }
      QFAIL (qPrintable (QString {"no unfinished reply for %1"}.arg (host)));
    }

    QList<Request> requests;
    bool auto_finish;

    QNetworkReply *post (QNetworkRequest const& request, QByteArray const& body) override
    {
      Request recorded {request.url (), body};
      requests.append (recorded);

      NetworkResponse response;
      if (!responses_.isEmpty ())
        {
          response = responses_.takeFirst ();
        }
      auto *reply = new FakeReply {request, response, this};
      replies_.append (reply);
      if (auto_finish)
        {
          QTimer::singleShot (0, reply, [reply] () { reply->finish (); });
        }
      return reply;
    }

  private:
    QList<NetworkResponse> responses_;
    QList<QPointer<FakeReply>> replies_;
    int next_reply_to_finish_;
  };

  WSPRNet::RetryPolicy retryPolicy (int max_attempts = 2, int max_pending = 1024, int ttl_ms = 60000)
  {
    WSPRNet::RetryPolicy policy;
    policy.max_attempts = max_attempts;
    policy.max_pending = max_pending;
    policy.ttl_ms = ttl_ms;
    policy.jitter_fraction = 0.;
    policy.retry_delays_ms = {0};
    return policy;
  }

  QString fst4wDecode ()
  {
    return "1234 -10 0.1 1500 ` K1ABC FN20 37";
  }

  QString spotFileLine ()
  {
    return "130223 2256 7    -21 -0.3  14.097090  K1ABC FN20 37          0    40    0\n";
  }

  QString secondSpotFileLine ()
  {
    return "130223 2258 7    -19 -0.2  14.097120  K2DEF FN31 33          0    35    0\n";
  }

  QString type2SpotFileLine ()
  {
    return "130223 2258 7    -19 -0.2  14.097120  K1ABC/P 37          0    35    0\n";
  }

  QString unresolvedHashSpotFileLine ()
  {
    return "130223 2258 7    -19 -0.2  14.097120  <...> IO81UT 37          0    35    0\n";
  }

  QString twoSpotFileLines ()
  {
    return spotFileLine () + secondSpotFileLine ();
  }

  void postFst4w (WSPRNet& wspr, QString const& decode_text = QString {})
  {
    wspr.post ("N0CALL", "FN20", "14.097000", "14.097100", "FST4W", 120.F,
               "10", "37", "test-version", decode_text);
  }

  void uploadFile (WSPRNet& wspr, QString const& file_name)
  {
    wspr.upload ("N0CALL", "FN20", "14.097000", "14.097100", "WSPR", 120.F,
                 "10", "37", "test-version", file_name);
  }

  QString writeSpotFile (QTemporaryDir& dir, QString const& contents = spotFileLine ())
  {
    auto const path = dir.path () + "/wspr_spots.txt";
    QFile file {path};
    if (!file.open (QIODevice::WriteOnly | QIODevice::Text))
      {
        qFatal ("could not create temporary spot file");
      }
    file.write (contents.toUtf8 ());
    file.close ();
    return path;
  }

  bool sawStatus (QSignalSpy const& spy, QString const& status)
  {
    for (auto const& arguments : spy)
      {
        if (!arguments.isEmpty () && arguments.front ().toString () == status)
          {
            return true;
          }
      }
    return false;
  }

  bool sawStatusContaining (QSignalSpy const& spy, QString const& text)
  {
    for (auto const& arguments : spy)
      {
        if (!arguments.isEmpty () && arguments.front ().toString ().contains (text))
          {
            return true;
          }
      }
    return false;
  }

  int requestCountForHost (QList<FakeNetworkAccessManager::Request> const& requests,
                           QString const& host)
  {
    auto count = 0;
    for (auto const& request : requests)
      {
        if (request.url.host () == host)
          {
            ++count;
          }
      }
    return count;
  }
}

class TestWSPRNetUpload final
  : public QObject
{
  Q_OBJECT

private slots:
  void fst4wDecodeQueuesSpotButDoesNotSendUntilFlush ()
  {
    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    QCoreApplication::processEvents ();

    QCOMPARE (manager.requests.size (), 0);

    postFst4w (wspr);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 2);
    QVERIFY (manager.requests.front ().body.contains ("function=wspr"));
    QVERIFY (manager.requests.front ().body.contains ("tcall=K1ABC"));
  }

  void primaryUploadsContinueWhileAlternateUploadIsOutstanding ()
  {
    FakeNetworkAccessManager manager;
    manager.auto_finish = false;
    WSPRNet wspr {&manager, retryPolicy (1), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    postFst4w (wspr, "1235 -11 0.2 1505 ` K2DEF FN31 33");
    postFst4w (wspr);
    wspr.work ();

    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.org"), 1);
    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.eu"), 1);

    manager.finishHost ("wsprnet.org");
    QTRY_COMPARE (requestCountForHost (manager.requests, "wsprnet.org"), 2);
    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.eu"), 1);

    manager.finishHost ("wsprnet.eu");
    QTRY_COMPARE (requestCountForHost (manager.requests, "wsprnet.eu"), 2);
    manager.finishHost ("wsprnet.org");
    manager.finishHost ("wsprnet.eu");
    QTRY_VERIFY (sawStatus (statuses, "done"));
  }

  void alternateRetryDoesNotSuppressLaterPrimaryStatus ()
  {
    FakeNetworkAccessManager manager;
    manager.auto_finish = false;
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "1 spot(s) added"});
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    WSPRNet wspr {&manager, retryPolicy (2), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    postFst4w (wspr);
    wspr.work ();
    manager.finishHost ("wsprnet.org");
    manager.finishHost ("wsprnet.eu");

    postFst4w (wspr);
    wspr.work ();

    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.org"), 2);
    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.eu"), 2);
    QVERIFY (manager.requests.at (2).body.contains ("function=wsprstat"));

    manager.finishHost ("wsprnet.org");
    manager.finishHost ("wsprnet.eu");
    QTRY_VERIFY (sawStatus (statuses, "done"));
  }

  void fst4wRetrySurvivesLaterDecodeCycles ()
  {
    FakeNetworkAccessManager manager;
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    WSPRNet wspr {&manager, retryPolicy (3), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    postFst4w (wspr);
    wspr.work ();
    QVERIFY (manager.requests.front ().body.contains ("tcall=K1ABC"));

    postFst4w (wspr, "1235 -11 0.2 1505 ` K2DEF FN31 33");
    postFst4w (wspr);

    QTRY_VERIFY (sawStatus (statuses, "done"));

    int k1abc = 0, k2def = 0;
    for (auto const& request : manager.requests)
      {
        if (request.body.contains ("tcall=K1ABC")) ++k1abc;
        if (request.body.contains ("tcall=K2DEF")) ++k2def;
      }
    QVERIFY (k1abc >= 3);
    QCOMPARE (k2def, 2);
  }

  void emptyFst4wFlushUploadsStatusAndCompletes ()
  {
    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 1);
    QVERIFY (manager.requests.front ().body.contains ("function=wsprstat"));
  }

  void spotWithoutTargetGridUploadsOnlyToPrimary ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir, type2SpotFileLine ());

    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 1);
    QCOMPARE (manager.requests.front ().url.host (), QString {"wsprnet.org"});
    QUrlQuery query {QString::fromUtf8 (manager.requests.front ().body)};
    QCOMPARE (query.queryItemValue ("tcall", QUrl::FullyDecoded), QString {"K1ABC/P"});
    QCOMPARE (query.queryItemValue ("tgrid", QUrl::FullyDecoded), QString {});
    QVERIFY (!QFile::exists (path));
  }

  void unresolvedHashCallsignIsNotUploadedAsSpot ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir, unresolvedHashSpotFileLine ());

    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 1);
    QUrlQuery query {QString::fromUtf8 (manager.requests.front ().body)};
    QCOMPARE (query.queryItemValue ("function", QUrl::FullyDecoded), QString {"wsprstat"});
    QCOMPARE (query.queryItemValue ("tcall", QUrl::FullyDecoded), QString {});
  }

  void directUploadSuccessDoesNotDeleteStaleFilePath ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    postFst4w (wspr);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QVERIFY (QFile::exists (path));
  }

  void directStatusCompletionDoesNotPoisonNextFileUpload ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "status accepted"});
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "1 spot(s) added"});
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr);
    wspr.work ();
    QTRY_VERIFY (sawStatus (statuses, "done"));

    statuses.clear ();
    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 3);
    QVERIFY (!QFile::exists (path));
  }

  void fileBackedAllSuccessDeletesSpotFile ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.org"), 1);
    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.eu"), 1);
    QVERIFY (!QFile::exists (path));
  }

  void fileBackedSuccessDoesNotDeleteRewrittenSpotFile ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    manager.auto_finish = false;
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();
    QCOMPARE (manager.requests.size (), 2);
    QVERIFY (manager.requests.front ().body.contains ("tcall=K1ABC"));

    writeSpotFile (dir, secondSpotFileLine ());
    manager.finishNext ();
    QCoreApplication::processEvents ();
    manager.finishNext ();
    QCoreApplication::processEvents ();

    QVERIFY (sawStatus (statuses, "done"));
    QVERIFY (QFile::exists (path));

    QFile file {path};
    QVERIFY (file.open (QIODevice::ReadOnly | QIODevice::Text));
    auto const contents = file.readAll ();
    QVERIFY (contents.contains ("K2DEF"));
    QVERIFY (!contents.contains ("K1ABC"));
  }

  void fileBackedAlternateFailureDoesNotRetainAcceptedSpotFile ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir, twoSpotFileLines ());

    FakeNetworkAccessManager manager;
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "1 spot(s) added"});
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "rejected"});
    WSPRNet wspr {&manager, retryPolicy (1), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 4);
    QVERIFY (!QFile::exists (path));
  }

  void fileBackedPrimaryRejectionCanBeDeliveredByAlternate ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "rejected"});
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "1 spot(s) added"});
    WSPRNet wspr {&manager, retryPolicy (1), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 2);
    QVERIFY (!QFile::exists (path));
  }

  void fileBackedTransportFailureRetriesThenRetainsSpotFile ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    WSPRNet wspr {&manager, retryPolicy (2), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 4);
    QVERIFY (QFile::exists (path));
  }

  void directUploadStopsAfterConfiguredAttempts ()
  {
    FakeNetworkAccessManager manager;
    for (int i = 0; i != 6; ++i)
      {
        manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
      }
    WSPRNet wspr {&manager, retryPolicy (3), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    postFst4w (wspr);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 6);
  }

  void fileBackedBadServerResponseRetriesThenRetainsSpotFile ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "rejected"});
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "rejected"});
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "still rejected"});
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "still rejected"});
    WSPRNet wspr {&manager, retryPolicy (2), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 4);
    QVERIFY (QFile::exists (path));
  }

  void alternateSiteFailureDoesNotResendAcceptedPrimary ()
  {
    FakeNetworkAccessManager manager;
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "1 spot(s) added"});
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    WSPRNet wspr {&manager, retryPolicy (2), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    postFst4w (wspr);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));

    int primary = 0, alternate = 0;
    for (auto const& request : manager.requests)
      {
        if ("wsprnet.org" == request.url.host ()) ++primary;
        else if ("wsprnet.eu" == request.url.host ()) ++alternate;
      }
    QCOMPARE (primary, 1);
    QCOMPARE (alternate, 2);
    QCOMPARE (manager.requests.size (), 3);
  }

  void expiredPendingUploadsAreDroppedAndFinalize ()
  {
    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (2, 1024, 50), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    QTest::qWait (100);
    postFst4w (wspr);

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 0);
    QVERIFY (sawStatusContaining (statuses, "expired"));
  }

  void queueCapDropsOldestPendingUploads ()
  {
    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (2, 2), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    for (int i = 0; i != 5; ++i)
      {
        postFst4w (wspr, fst4wDecode ());
      }
    postFst4w (wspr);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 4);
    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.org"), 2);
    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.eu"), 2);
    QVERIFY (sawStatusContaining (statuses, "oldest pending"));
  }

  void queueCapDoesNotDropPrimaryForAlternateLegs ()
  {
    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (1, 1), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    postFst4w (wspr, "1235 -11 0.2 1505 ` K2DEF FN31 33");
    postFst4w (wspr);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.org"), 1);
    QCOMPARE (requestCountForHost (manager.requests, "wsprnet.eu"), 1);
  }

  void abortClearsQueuedRetriesAndIgnoresStaleReplies ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    manager.auto_finish = false;
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();
    QCOMPARE (manager.requests.size (), 2);

    wspr.abortOutstandingRequests ();
    manager.finishAll ();
    QCoreApplication::processEvents ();

    QVERIFY (QFile::exists (path));
    QVERIFY (!sawStatus (statuses, "done"));
  }
};

QTEST_MAIN (TestWSPRNetUpload)

#include "test_wsprnet_upload.moc"
