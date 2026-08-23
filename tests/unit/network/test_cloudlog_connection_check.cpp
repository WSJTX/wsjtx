#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

#include "Network/Cloudlog.hpp"

namespace test_cloudlog_configuration_stub
{
  void reset ();
  void setCloudlogValues (QString const& url, QString const& apiKey, qint32 stationId);
  Configuration const * configuration ();
}

namespace
{
  struct FakeResponse
  {
    FakeResponse () = default;
    FakeResponse (int status, QByteArray const& body, QNetworkReply::NetworkError error = QNetworkReply::NoError,
                  QString const& errorString = {}, bool autoFinish = true)
      : status {status}
      , body {body}
      , error {error}
      , errorString {errorString}
      , autoFinish {autoFinish}
    {
    }

    int status {200};
    QByteArray body;
    QNetworkReply::NetworkError error {QNetworkReply::NoError};
    QString errorString;
    bool autoFinish {true};
  };

  struct CapturedRequest
  {
    QString method;
    QUrl url;
    QByteArray payload;
    QByteArray contentType;
    QByteArray accept;
  };

  class FakeReply final
    : public QNetworkReply
  {
    Q_OBJECT

  public:
    FakeReply (QNetworkRequest const& request, FakeResponse const& response, QObject * parent)
      : QNetworkReply {parent}
      , data_ {response.body}
    {
      setRequest (request);
      setUrl (request.url ());
      setOpenMode (QIODevice::ReadOnly);
      if (response.status)
        {
          setAttribute (QNetworkRequest::HttpStatusCodeAttribute, response.status);
        }
      if (QNetworkReply::NoError != response.error)
        {
          setError (response.error, response.errorString);
        }

      if (response.autoFinish)
        {
          QTimer::singleShot (0, this, [this] {
            finishNow ();
          });
        }
    }

    void finishNow ()
    {
      if (finished_)
        {
          return;
        }
      finished_ = true;
      setFinished (true);
      Q_EMIT readyRead ();
      Q_EMIT finished ();
    }

    void abort () override
    {
      setError (QNetworkReply::OperationCanceledError, QStringLiteral ("operation canceled"));
    }

    qint64 bytesAvailable () const override
    {
      return data_.size () - offset_ + QNetworkReply::bytesAvailable ();
    }

  protected:
    qint64 readData (char * buffer, qint64 maxSize) override
    {
      auto const remaining = data_.size () - offset_;
      if (remaining <= 0)
        {
          return -1;
        }
      auto const size = qMin (maxSize, static_cast<qint64> (remaining));
      memcpy (buffer, data_.constData () + offset_, static_cast<size_t> (size));
      offset_ += size;
      return size;
    }

  private:
    QByteArray data_;
    qint64 offset_ {0};
    bool finished_ {false};
  };

  class FakeNetworkAccessManager final
    : public QNetworkAccessManager
  {
    Q_OBJECT

  public:
    void queueResponse (FakeResponse const& response)
    {
      responses_.append (response);
    }

    void queueJson (int status, QByteArray const& body, QNetworkReply::NetworkError error = QNetworkReply::NoError,
                    QString const& errorString = {})
    {
      queueResponse ({status, body, error, errorString});
    }

    void queueWritableJsonAuth ()
    {
      queueJson (200, QByteArray {R"({"status":"valid","rights":"rw"})"});
    }

    void queueStationInfo (QByteArray const& body)
    {
      queueJson (200, body);
    }

    void queueManifest (QByteArray const& body, int status = 200)
    {
      queueJson (status, body, status >= 400 ? QNetworkReply::ContentNotFoundError : QNetworkReply::NoError,
                 status >= 400 ? QStringLiteral ("not found") : QString {});
    }

    QList<CapturedRequest> requests () const
    {
      return requests_;
    }

    QList<QPointer<FakeReply>> replies () const
    {
      return replies_;
    }

  protected:
    QNetworkReply * createRequest (Operation operation, QNetworkRequest const& request, QIODevice * outgoingData) override
    {
      CapturedRequest captured;
      captured.method = operation == GetOperation ? QStringLiteral ("GET") : QStringLiteral ("POST");
      captured.url = request.url ();
      captured.contentType = request.header (QNetworkRequest::ContentTypeHeader).toByteArray ();
      captured.accept = request.rawHeader ("Accept");
      if (outgoingData)
        {
          captured.payload = outgoingData->readAll ();
        }
      requests_.append (captured);

      auto response = responses_.isEmpty () ? FakeResponse {404, QByteArray {"not found"}, QNetworkReply::ContentNotFoundError, QStringLiteral ("not found")} : responses_.takeFirst ();
      auto * reply = new FakeReply {request, response, this};
      replies_.append (reply);
      return reply;
    }

  private:
    QList<FakeResponse> responses_;
    QList<CapturedRequest> requests_;
    QList<QPointer<FakeReply>> replies_;
  };

  Cloudlog::ConnectionCheckResult runCheck (FakeNetworkAccessManager& network, int stationId = 7)
  {
    Cloudlog cloudlog {nullptr, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::connection_check_finished};
    cloudlog.checkConnection ({QStringLiteral (" https://example.test/api/qso/ "),
                               QStringLiteral ("secret/key"),
                               stationId});
    if (spy.isEmpty ())
      {
        spy.wait (1000);
      }
    Q_ASSERT (!spy.isEmpty ());
    return qvariant_cast<Cloudlog::ConnectionCheckResult> (spy.takeFirst ().at (0));
  }

  QByteArray json (QString const& text)
  {
    return text.toUtf8 ();
  }

  Cloudlog::UploadResult runLogQso (FakeNetworkAccessManager& network, FakeResponse const& response)
  {
    test_cloudlog_configuration_stub::reset ();
    test_cloudlog_configuration_stub::setCloudlogValues (QStringLiteral (" https://example.test/index.php/api/qso/ "),
                                                         QStringLiteral ("secret/key"), 7);
    network.queueResponse (response);

    Cloudlog cloudlog {test_cloudlog_configuration_stub::configuration (), &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::qso_upload_finished};
    cloudlog.logQso (QByteArray {"<call:5>K1ABC"});
    if (spy.isEmpty ())
      {
        spy.wait (1000);
      }
    Q_ASSERT (!spy.isEmpty ());
    return qvariant_cast<Cloudlog::UploadResult> (spy.takeFirst ().at (0));
  }

  int qsoTruePostCount (QList<CapturedRequest> const& requests)
  {
    int count {0};
    for (auto const& request : requests)
      {
        if (request.method == QStringLiteral ("POST") && request.url.path ().endsWith (QStringLiteral ("/qso/true")))
          {
            ++count;
          }
      }
    return count;
  }
}

class TestCloudlogConnectionCheck final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase ()
  {
    qRegisterMetaType<Cloudlog::ConnectionCheckResult> ("Cloudlog::ConnectionCheckResult");
    qRegisterMetaType<Cloudlog::ConnectionCheckResult> ("ConnectionCheckResult");
    qRegisterMetaType<Cloudlog::UploadResult> ("Cloudlog::UploadResult");
    qRegisterMetaType<Cloudlog::UploadResult> ("UploadResult");
  }

  void normalizesBaseUrl_data ()
  {
    QTest::addColumn<QString> ("input");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("base") << "https://log.example" << "https://log.example";
    QTest::newRow ("trailing slash") << "https://log.example/" << "https://log.example";
    QTest::newRow ("api qso") << " https://log.example/api/qso/ " << "https://log.example";
    QTest::newRow ("index qso") << "https://log.example/index.php/api/qso/" << "https://log.example";
  }

  void normalizesBaseUrl ()
  {
    QFETCH (QString, input);
    QFETCH (QString, expected);

    QCOMPARE (Cloudlog::normalizeBaseUrl (input), expected);
  }

  void buildsEscapedJsonPayload ()
  {
    auto const payload = Cloudlog::qsoUploadPayload (QStringLiteral ("key\"with\\chars"), 42, QByteArray {"<call:6>K1\"ABC<comment:8>a\\b\nc"});
    auto const document = QJsonDocument::fromJson (payload);
    QVERIFY (document.isObject ());
    auto const object = document.object ();
    QCOMPARE (object.value (QStringLiteral ("key")).toString (), QStringLiteral ("key\"with\\chars"));
    QCOMPARE (object.value (QStringLiteral ("station_profile_id")).toString (), QStringLiteral ("42"));
    QCOMPARE (object.value (QStringLiteral ("type")).toString (), QStringLiteral ("adif"));
    QCOMPARE (object.value (QStringLiteral ("string")).toString (), QStringLiteral ("<call:6>K1\"ABC<comment:8>a\\b\nc<eor>"));
  }

  void classifiesQsoUploadReplies_data ()
  {
    QTest::addColumn<int> ("httpStatus");
    QTest::addColumn<int> ("networkError");
    QTest::addColumn<QString> ("errorString");
    QTest::addColumn<QByteArray> ("body");
    QTest::addColumn<Cloudlog::UploadStatus> ("expectedStatus");
    QTest::addColumn<QString> ("expectedDetail");

    QTest::newRow ("success created")
      << 201
      << static_cast<int> (QNetworkReply::NoError)
      << QString {}
      << json (R"({"status":"created"})")
      << Cloudlog::UploadStatus::Success
      << QString {};

    QTest::newRow ("transport")
      << 0
      << static_cast<int> (QNetworkReply::HostNotFoundError)
      << QStringLiteral ("host not found")
      << QByteArray {}
      << Cloudlog::UploadStatus::NetworkError
      << QStringLiteral ("host not found");

    QTest::newRow ("http non-json")
      << 500
      << static_cast<int> (QNetworkReply::InternalServerError)
      << QStringLiteral ("server error")
      << QByteArray {"internal error"}
      << Cloudlog::UploadStatus::HttpError
      << QStringLiteral ("HTTP status 500");

    QTest::newRow ("http json")
      << 403
      << static_cast<int> (QNetworkReply::ContentAccessDenied)
      << QStringLiteral ("forbidden")
      << json (R"({"message":"missing upload rights"})")
      << Cloudlog::UploadStatus::HttpError
      << QStringLiteral ("missing upload rights");

    QTest::newRow ("server failed")
      << 200
      << static_cast<int> (QNetworkReply::NoError)
      << QString {}
      << json (R"({"status":"failed","reason":"bad ADIF"})")
      << Cloudlog::UploadStatus::Rejected
      << QStringLiteral ("bad ADIF");

    QTest::newRow ("non-json success code")
      << 200
      << static_cast<int> (QNetworkReply::NoError)
      << QString {}
      << QByteArray {"ok"}
      << Cloudlog::UploadStatus::UnexpectedResponse
      << QStringLiteral ("illegal value");
  }

  void classifiesQsoUploadReplies ()
  {
    QFETCH (int, httpStatus);
    QFETCH (int, networkError);
    QFETCH (QString, errorString);
    QFETCH (QByteArray, body);
    QFETCH (Cloudlog::UploadStatus, expectedStatus);
    QFETCH (QString, expectedDetail);

    FakeNetworkAccessManager network;
    auto const result = runLogQso (network, {httpStatus, body, static_cast<QNetworkReply::NetworkError> (networkError), errorString});

    QCOMPARE (result.status, expectedStatus);
    QCOMPARE (result.httpStatusCode, httpStatus);
    QCOMPARE (network.requests ().size (), 1);
    QCOMPARE (network.requests ().at (0).method, QStringLiteral ("POST"));
    QCOMPARE (network.requests ().at (0).url.path (), QStringLiteral ("/index.php/api/qso"));
    QCOMPARE (network.requests ().at (0).contentType, QByteArray {"application/json"});
    QCOMPARE (network.requests ().at (0).accept, QByteArray {"application/json"});
    if (!expectedDetail.isEmpty ())
      {
        QVERIFY2 (result.detail.contains (expectedDetail), qPrintable (result.detail));
      }
  }

  void emitsOnlySuccessForAcceptedQsoUpload ()
  {
    FakeNetworkAccessManager network;
    auto const result = runLogQso (network, {201, json (R"({"status":"created"})")});

    QCOMPARE (result.status, Cloudlog::UploadStatus::Success);
    QVERIFY (result.message.isEmpty ());
    QVERIFY (result.detail.isEmpty ());
    QCOMPARE (result.httpStatusCode, 201);
  }

  void acceptsWritableClassicWithoutDryRun ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({404, QByteArray {"not found"}, QNetworkReply::ContentNotFoundError, QStringLiteral ("not found")});
    network.queueResponse ({404, QByteArray {"not found"}, QNetworkReply::ContentNotFoundError, QStringLiteral ("not found")});
    network.queueResponse ({200, QByteArray {"<auth><status>Valid</status><rights>rw</rights></auth>"}});
    network.queueResponse ({200, json (R"([{"station_id":"7","station_profile_name":"Home","station_callsign":"K1ABC"}])")});
    network.queueResponse ({200, json (R"({"name":"Cloudlog","short_name":"Cloudlog"})")});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QVERIFY (result.detail.contains (QStringLiteral ("No safe dry-run QSO endpoint")));
    auto const requests = network.requests ();
    QCOMPARE (requests.size (), 5);
    QCOMPARE (requests.at (0).url.path (QUrl::FullyEncoded), QStringLiteral ("/api/check_auth/secret%2Fkey"));
    QCOMPARE (requests.at (2).url.path (QUrl::FullyEncoded), QStringLiteral ("/index.php/api/auth/secret%2Fkey"));
    QCOMPARE (requests.at (3).url.path (QUrl::FullyEncoded), QStringLiteral ("/index.php/api/station_info/secret%2Fkey"));
    QCOMPARE (requests.at (4).url.path (), QStringLiteral ("/manifest.json"));
    QCOMPARE (qsoTruePostCount (requests), 0);
  }

  void acceptsJsonCloudlogWithoutDryRun ()
  {
    FakeNetworkAccessManager network;
    network.queueWritableJsonAuth ();
    network.queueStationInfo (json (R"([{"station_id":7,"station_profile_name":"Home"}])"));
    network.queueManifest (json (R"({"name":"Cloudlog"})"));

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QVERIFY (result.detail.contains (QStringLiteral ("No safe dry-run QSO endpoint")));
    auto const requests = network.requests ();
    QCOMPARE (requests.size (), 3);
    QCOMPARE (requests.at (2).url.path (), QStringLiteral ("/manifest.json"));
    QCOMPARE (qsoTruePostCount (requests), 0);
  }

  void reportsReadOnlyKey ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({200, json (R"({"status":"valid","rights":"r"})")});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::ReadOnlyKey);
    QVERIFY (result.detail.contains (QStringLiteral ("read/write API key")));
    QCOMPARE (network.requests ().size (), 1);
  }

  void reportsInvalidKey ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({200, json (R"({"status":"invalid","message":"bad key"})")});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::InvalidKey);
    QCOMPARE (result.detail, QStringLiteral ("bad key"));
  }

  void rejectsAuthWithoutRights ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({200, json (R"({"status":"valid"})")});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::UnexpectedResponse);
    QVERIFY (result.detail.contains (QStringLiteral ("rights")));
  }

  void rejectsXmlAuthWithoutRights ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({404, QByteArray {"not found"}, QNetworkReply::ContentNotFoundError, QStringLiteral ("not found")});
    network.queueResponse ({404, QByteArray {"not found"}, QNetworkReply::ContentNotFoundError, QStringLiteral ("not found")});
    network.queueResponse ({200, QByteArray {"<auth><status>Valid</status></auth>"}});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::UnexpectedResponse);
    QVERIFY (result.detail.contains (QStringLiteral ("rights")));
  }

  void reportsMissingStationProfile ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({200, json (R"({"status":"valid","rights":"rw"})")});
    network.queueResponse ({200, json (R"([{"station_id":"8","station_profile_name":"Portable","station_callsign":"K1ABC/P"}])")});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::StationProfileUnavailable);
    QVERIFY (result.detail.contains (QStringLiteral ("8 - Portable - K1ABC/P")));
    QCOMPARE (result.stationProfiles.size (), 1);
    QCOMPARE (result.stationProfiles.at (0).id, 8);
    QCOMPARE (result.stationProfiles.at (0).name, QStringLiteral ("Portable"));
    QCOMPARE (result.stationProfiles.at (0).callsign, QStringLiteral ("K1ABC/P"));
  }

  void ignoresNestedNonStationObjects ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({200, json (R"({"status":"valid","rights":"rw"})")});
    network.queueResponse ({200, json (R"({"metadata":{"id":7,"name":"Not a station"},"stations":[{"station_id":8,"station_profile_name":"Portable"}]})")});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::StationProfileUnavailable);
    QVERIFY (!result.detail.contains (QStringLiteral ("Not a station")));
    QVERIFY (result.detail.contains (QStringLiteral ("8 - Portable")));
    QCOMPARE (result.stationProfiles.size (), 1);
    QCOMPARE (result.stationProfiles.at (0).id, 8);
  }

  void acceptsCurrentWavelogDryRunResponse ()
  {
    FakeNetworkAccessManager network;
    network.queueWritableJsonAuth ();
    network.queueStationInfo (json (R"({"stations":[{"station_id":7,"station_profile_name":"Home"}]})"));
    network.queueManifest (json (R"({"name":"Wavelog","short_name":"Wavelog"})"));
    network.queueJson (201, json (R"({"status":"created","type":"adif","string":"","adif_count":0,"adif_errors":0,"messages":["Dryrun works"]})"));

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QVERIFY (result.detail.contains (QStringLiteral ("dry-run QSO upload")));
    QCOMPARE (result.stationProfiles.size (), 1);
    QCOMPARE (result.stationProfiles.at (0).id, 7);
    auto const requests = network.requests ();
    QCOMPARE (requests.size (), 4);
    QCOMPARE (requests.at (2).method, QStringLiteral ("GET"));
    QCOMPARE (requests.at (2).url.path (), QStringLiteral ("/manifest.json"));
    QCOMPARE (requests.at (3).method, QStringLiteral ("POST"));
    QCOMPARE (requests.at (3).url.path (), QStringLiteral ("/api/qso/true"));
    QCOMPARE (requests.at (3).contentType, QByteArray {"application/json"});
    QCOMPARE (requests.at (3).accept, QByteArray {"application/json"});
    auto const payload = QJsonDocument::fromJson (requests.at (3).payload).object ();
    QCOMPARE (payload.value (QStringLiteral ("station_profile_id")).toString (), QStringLiteral ("7"));
    QCOMPARE (payload.value (QStringLiteral ("type")).toString (), QStringLiteral ("adif"));
  }

  void acceptsUnknownManifestWithoutDryRun ()
  {
    FakeNetworkAccessManager network;
    network.queueWritableJsonAuth ();
    network.queueStationInfo (json (R"([{"station_id":7}])"));
    network.queueManifest (QByteArray {"not found"}, 404);

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QVERIFY (result.detail.contains (QStringLiteral ("No safe dry-run QSO endpoint")));
    QCOMPARE (qsoTruePostCount (network.requests ()), 0);
  }

  void reportsDryRunForbidden ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({200, json (R"({"status":"valid","rights":"rw"})")});
    network.queueResponse ({200, json (R"([{"station_id":7}])")});
    network.queueResponse ({200, json (R"({"short_name":"Wavelog"})")});
    network.queueResponse ({403, json (R"({"status":"failed","reason":"read-only upload key"})"), QNetworkReply::ContentAccessDenied, QStringLiteral ("forbidden")});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::UploadShapeRejected);
    QCOMPARE (result.detail, QStringLiteral ("read-only upload key"));
  }

  void reportsDryRunAdifMessages ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({200, json (R"({"status":"valid","rights":"rw"})")});
    network.queueResponse ({200, json (R"([{"station_id":7}])")});
    network.queueResponse ({200, json (R"({"name":"Wavelog"})")});
    network.queueResponse ({400, json (R"({"status":"failed","messages":["station_profile_id is required","bad ADIF"]})"), QNetworkReply::ProtocolInvalidOperationError, QStringLiteral ("bad request")});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::UploadShapeRejected);
    QVERIFY (result.detail.contains (QStringLiteral ("station_profile_id is required")));
    QVERIFY (result.detail.contains (QStringLiteral ("bad ADIF")));
  }

  void reportsNetworkFailure ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({0, {}, QNetworkReply::HostNotFoundError, QStringLiteral ("host not found")});

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::NetworkError);
    QCOMPARE (result.detail, QStringLiteral ("host not found"));
  }

  void reportsStationInfoNotJson ()
  {
    FakeNetworkAccessManager network;
    network.queueWritableJsonAuth ();
    network.queueJson (404, QByteArray {"not found"}, QNetworkReply::ContentNotFoundError, QStringLiteral ("not found"));

    auto const result = runCheck (network);

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::UnexpectedResponse);
    QVERIFY (result.message.contains (QStringLiteral ("Station profile response")));
  }

  void ignoresStaleOverlappingConnectionCheckReply ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({200, json (R"({"status":"valid","rights":"rw"})"), QNetworkReply::NoError, {}, false});
    network.queueResponse ({200, json (R"({"status":"valid","rights":"rw"})")});
    network.queueResponse ({200, json (R"([{"station_id":9}])")});
    network.queueResponse ({200, json (R"({"name":"Cloudlog"})")});

    Cloudlog cloudlog {nullptr, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::connection_check_finished};
    cloudlog.checkConnection ({QStringLiteral ("https://first.example"), QStringLiteral ("first"), 7});
    auto replies = network.replies ();
    QCOMPARE (replies.size (), 1);
    QVERIFY (replies.at (0));
    auto * stale = replies.at (0).data ();

    cloudlog.checkConnection ({QStringLiteral ("https://second.example"), QStringLiteral ("second"), 9});
    stale->finishNow ();

    if (spy.isEmpty ())
      {
        spy.wait (1000);
      }
    QCOMPARE (spy.size (), 1);
    auto const result = qvariant_cast<Cloudlog::ConnectionCheckResult> (spy.takeFirst ().at (0));
    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QCOMPARE (result.normalizedBaseUrl, QStringLiteral ("https://second.example"));
    QCOMPARE (qsoTruePostCount (network.requests ()), 0);
  }
};

QTEST_GUILESS_MAIN (TestCloudlogConnectionCheck)

#include "test_cloudlog_connection_check.moc"
