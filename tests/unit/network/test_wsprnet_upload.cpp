#include <QtTest>

#include <QBuffer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    : public QNetworkAccessManager
  {
  public:
    struct Request
    {
      QUrl url;
      QByteArray body;
    };

    explicit FakeNetworkAccessManager (QObject *parent = nullptr)
      : QNetworkAccessManager {parent}
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
      for (auto *reply : pending)
        {
          reply->finish ();
        }
    }

    void finishNext ()
    {
      QVERIFY (next_reply_to_finish_ < replies_.size ());
      replies_[next_reply_to_finish_++]->finish ();
    }

    QList<Request> requests;
    bool auto_finish;

  protected:
    QNetworkReply *createRequest (Operation op, QNetworkRequest const& request, QIODevice *outgoing_data = nullptr) override
    {
      Q_UNUSED (op);
      Request recorded {request.url (), outgoing_data ? outgoing_data->readAll () : QByteArray {}};
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
    QList<FakeReply *> replies_;
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
    QCOMPARE (manager.requests.size (), 2);   // one leg per destination site
    QVERIFY (manager.requests.front ().body.contains ("function=wspr"));
    QVERIFY (manager.requests.front ().body.contains ("tcall=K1ABC"));
  }

  void fst4wRetrySurvivesLaterDecodeCycles ()
  {
    FakeNetworkAccessManager manager;
    // The first leg sent (K1ABC → primary site) times out once; every other
    // leg succeeds (unspecified responses default to "1 spot(s) added").
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    WSPRNet wspr {&manager, retryPolicy (3), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    postFst4w (wspr);
    wspr.work ();
    QVERIFY (manager.requests.front ().body.contains ("tcall=K1ABC"));

    // A later decode cycle arrives while K1ABC's timed-out leg is still queued
    // for retry; it must survive and eventually be sent.
    postFst4w (wspr, "1235 -11 0.2 1505 ` K2DEF FN31 33");
    postFst4w (wspr);

    QTRY_VERIFY (sawStatus (statuses, "done"));

    int k1abc = 0, k2def = 0;
    for (auto const& request : manager.requests)
      {
        if (request.body.contains ("tcall=K1ABC")) ++k1abc;
        if (request.body.contains ("tcall=K2DEF")) ++k2def;
      }
    QVERIFY (k1abc >= 3);   // both legs plus the retry of the timed-out leg
    QCOMPARE (k2def, 2);    // both legs of the later cycle
  }

  void emptyFst4wFlushUploadsStatusAndCompletes ()
  {
    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 2);   // status is reported to each site
    QVERIFY (manager.requests.front ().body.contains ("function=wsprstat"));
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
    QCOMPARE (manager.requests.size (), 4);   // status (2 legs) + one spot (2 legs)
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
    QCOMPARE (manager.requests.size (), 1);
    QVERIFY (manager.requests.front ().body.contains ("tcall=K1ABC"));

    writeSpotFile (dir, secondSpotFileLine ());
    manager.finishNext ();                 // primary leg completes...
    QCoreApplication::processEvents ();     // ...which schedules the alternate leg
    manager.finishNext ();                 // alternate leg completes
    QCoreApplication::processEvents ();

    QVERIFY (sawStatus (statuses, "done"));
    QVERIFY (QFile::exists (path));

    QFile file {path};
    QVERIFY (file.open (QIODevice::ReadOnly | QIODevice::Text));
    auto const contents = file.readAll ();
    QVERIFY (contents.contains ("K2DEF"));
    QVERIFY (!contents.contains ("K1ABC"));
  }

  void fileBackedPartialFailureRetainsSpotFile ()
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
    QCOMPARE (manager.requests.size (), 4);   // two spots, one leg per site each
    QVERIFY (QFile::exists (path));           // a rejected leg keeps the file
  }

  void fileBackedTransportFailureRetriesThenRetainsSpotFile ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    // Both legs time out on every attempt; each exhausts its own retry budget
    // (max_attempts = 2 → two requests per site).
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});
    WSPRNet wspr {&manager, retryPolicy (2), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 4);   // 2 sites x 2 attempts
    QVERIFY (QFile::exists (path));
  }

  void directUploadStopsAfterConfiguredAttempts ()
  {
    FakeNetworkAccessManager manager;
    // Every attempt to either site times out; each leg stops after its own
    // configured number of attempts (max_attempts = 3 → three per site).
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
    QCOMPARE (manager.requests.size (), 6);   // 2 sites x 3 attempts, then stop
  }

  void fileBackedBadServerResponseRetriesThenRetainsSpotFile ()
  {
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto const path = writeSpotFile (dir);

    FakeNetworkAccessManager manager;
    // Both sites reject on every attempt; each leg exhausts its own retries.
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "rejected"});
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "rejected"});
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "still rejected"});
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "still rejected"});
    WSPRNet wspr {&manager, retryPolicy (2), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    uploadFile (wspr, path);
    wspr.work ();

    QTRY_VERIFY (sawStatus (statuses, "done"));
    QCOMPARE (manager.requests.size (), 4);   // 2 sites x 2 attempts
    QVERIFY (QFile::exists (path));
  }

  // Regression: a spot accepted by one site must never be re-sent to that site
  // just because the other site failed. Each destination retries on its own.
  void alternateSiteFailureDoesNotResendAcceptedPrimary ()
  {
    FakeNetworkAccessManager manager;
    manager.enqueueResponse ({QNetworkReply::NoError, {}, "1 spot(s) added"});  // primary accepts
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});     // alternate, attempt 1
    manager.enqueueResponse ({QNetworkReply::TimeoutError, "timeout", {}});     // alternate, attempt 2
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
    QCOMPARE (primary, 1);                     // accepted once, never re-sent
    QCOMPARE (alternate, 2);                    // only the failing site retries
    QCOMPARE (manager.requests.size (), 3);
  }

  void expiredPendingUploadsAreDroppedAndFinalize ()
  {
    FakeNetworkAccessManager manager;
    WSPRNet wspr {&manager, retryPolicy (2, 1024, 1), false};
    QSignalSpy statuses {&wspr, &WSPRNet::uploadStatus};

    postFst4w (wspr, fst4wDecode ());
    QTest::qWait (5);
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
    QCOMPARE (manager.requests.size (), 2);
    QVERIFY (sawStatusContaining (statuses, "oldest pending"));
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
    QCOMPARE (manager.requests.size (), 1);

    wspr.abortOutstandingRequests ();
    manager.finishAll ();
    QCoreApplication::processEvents ();

    QVERIFY (QFile::exists (path));
    QVERIFY (!sawStatus (statuses, "done"));
  }
};

QTEST_MAIN (TestWSPRNetUpload)

#include "test_wsprnet_upload.moc"
