#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "Network/Cloudlog.hpp"

class TestCloudlogConfiguration final
  : public CloudlogConfiguration
{
public:
  TestCloudlogConfiguration (QString const& url, QString const& apiKey, qint32 stationId)
    : url {url}
    , apiKey {apiKey}
    , stationId {stationId}
  {
  }

  QString url;
  QString apiKey;
  qint32 stationId {0};
  bool radioEnabled {false};
  QString radioName {QStringLiteral ("WSJT-X")};

  bool cloudlog_radio_enabled () const override {return radioEnabled;}
  QString cloudlog_radio_name () const override {return radioName;}

  QString cloudlog_api_url () const override
  {
    return url;
  }

  QString cloudlog_api_key () const override
  {
    return apiKey;
  }

  qint32 cloudlog_api_station_id () const override
  {
    return stationId;
  }
};

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
    QByteArray authorization;
    int redirectPolicy {0};
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
    QNetworkReply::NetworkError httpsError {QNetworkReply::NoError};

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
      captured.authorization = request.rawHeader ("Authorization");
      captured.redirectPolicy = request.attribute (QNetworkRequest::RedirectPolicyAttribute).toInt ();
      if (outgoingData)
        {
          captured.payload = outgoingData->readAll ();
        }
      requests_.append (captured);

      auto response = request.url ().scheme () == QStringLiteral ("https") && httpsError != QNetworkReply::NoError
        ? FakeResponse {0, {}, httpsError, QStringLiteral ("HTTPS unavailable")}
        : responses_.isEmpty () ? FakeResponse {404, QByteArray {"not found"}, QNetworkReply::ContentNotFoundError, QStringLiteral ("not found")} : responses_.takeFirst ();
      auto * reply = new FakeReply {request, response, this};
      replies_.append (reply);
      return reply;
    }

  private:
    QList<FakeResponse> responses_;
    QList<CapturedRequest> requests_;
    QList<QPointer<FakeReply>> replies_;
  };

  Cloudlog::ConnectionCheckResult runCheck (FakeNetworkAccessManager& network, int stationId = 7,
                                             QString const& url = QStringLiteral (" https://example.test/api/qso/ "),
                                             QString const& apiKey = QStringLiteral ("secret/key"),
                                             bool radioEnabled = false, bool qsoEnabled = true)
  {
    Cloudlog cloudlog {nullptr, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::connection_check_finished};
    cloudlog.checkConnection ({url, apiKey, stationId, radioEnabled, qsoEnabled});
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

  Cloudlog::UploadResult runLogQso (
    FakeNetworkAccessManager& network, FakeResponse const& response,
    QString const& url = QStringLiteral (" https://example.test/index.php/api/qso/ "),
    QString const& apiKey = QStringLiteral ("secret/key"))
  {
    TestCloudlogConfiguration configuration {
      url,
      apiKey,
      7};
    network.queueResponse (response);

    Cloudlog cloudlog {&configuration, &network};
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

  void requiresRadioScopeOnlyWhenLiveStatusIsEnabled ()
  {
    FakeNetworkAccessManager network;
    network.queueJson (200, json (R"({"data":{"name":"Wavelog API","status":"ok"}})"));
    network.queueJson (200, json (R"({"data":{"scopes":["station:read","qso:write"]}})"));
    auto const result = runCheck (network, 7, QStringLiteral ("https://wavelog.example"),
                                  QStringLiteral ("wl2_secret"), true);
    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::InsufficientScope);
    QCOMPARE (result.detail, QStringLiteral ("Required scopes: radio:write"));
    QCOMPARE (network.requests ().size (), 2);
  }

  void doesNotPublishRadioUnlessEnabledAndOnline ()
  {
    FakeNetworkAccessManager network;
    TestCloudlogConfiguration config {QStringLiteral ("https://wavelog.example"), QStringLiteral ("wl2_secret"), 7};
    Cloudlog cloudlog {&config, &network};
    cloudlog.updateRadio ({14074000, 14074000, QStringLiteral ("FT8")});
    QVERIFY (network.requests ().isEmpty ());
    config.radioEnabled = true;
    cloudlog.updateRadio ({});
    QVERIFY (network.requests ().isEmpty ());
  }

  void acceptsRadioOnlyTokenWithoutQsoOrStationScopes ()
  {
    FakeNetworkAccessManager network;
    network.queueJson (200, json (R"({"data":{"name":"Wavelog API","status":"ok"}})"));
    network.queueJson (200, json (R"({"data":{"scopes":["radio:write"]}})"));
    auto const result = runCheck (network, 0, QStringLiteral ("https://wavelog.example"),
                                  QStringLiteral ("wl2_secret"), true, false);
    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QCOMPARE (network.requests ().size (), 2);
  }

  void acceptsLegacyRadioOnlyCheckWithoutStationOrQsoUpload ()
  {
    FakeNetworkAccessManager network;
    network.queueWritableJsonAuth ();
    auto const result = runCheck (network, 0, QStringLiteral ("https://wavelog.example"),
                                  QStringLiteral ("legacy-secret"), true, false);
    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QCOMPARE (network.requests ().size (), 1);
  }

  void rejectsHtmlInsteadOfRadioApiSuccess ()
  {
    FakeNetworkAccessManager network;
    TestCloudlogConfiguration config {QStringLiteral ("https://wavelog.example"), QStringLiteral ("legacy-secret"), 7};
    config.radioEnabled = true;
    network.queueJson (200, QByteArray {"<html>Login required</html>"});
    Cloudlog cloudlog {&config, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::radio_status_finished};
    cloudlog.updateRadio ({14074000, 14074000, QStringLiteral ("FT8")});
    QVERIFY (spy.wait (1000));
    QCOMPARE (qvariant_cast<Cloudlog::UploadResult> (spy.at (0).at (0)).status,
              Cloudlog::UploadStatus::UnexpectedResponse);
  }

  void publishesRadioWithV2BearerToken ()
  {
    FakeNetworkAccessManager network;
    TestCloudlogConfiguration config {QStringLiteral ("https://wavelog.example"), QStringLiteral ("wl2_secret"), 7};
    config.radioEnabled = true;
    config.radioName = QStringLiteral ("Digital station");
    network.queueJson (201, json (R"({"data":{"radio":"Digital station","frequency":14075000}})"));
    Cloudlog cloudlog {&config, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::radio_status_finished};
    cloudlog.updateRadio ({14075000, 14074000, QStringLiteral ("FT8")});
    QVERIFY (spy.wait (1000));
    QCOMPARE (qvariant_cast<Cloudlog::UploadResult> (spy.at (0).at (0)).status, Cloudlog::UploadStatus::Success);
    auto const request = network.requests ().at (0);
    QCOMPARE (request.url.path (), QStringLiteral ("/index.php/api/v2/radio"));
    QCOMPARE (request.authorization, QByteArray {"Bearer wl2_secret"});
    QCOMPARE (request.redirectPolicy, int (QNetworkRequest::NoLessSafeRedirectPolicy));
    auto const payload = QJsonDocument::fromJson (request.payload).object ();
    QCOMPARE (payload.value (QStringLiteral ("radio")).toString (), config.radioName);
    QCOMPARE (payload.value (QStringLiteral ("frequency")).toInt (), 14075000);
    QCOMPARE (payload.value (QStringLiteral ("frequency_rx")).toInt (), 14074000);
    QCOMPARE (payload.value (QStringLiteral ("mode")).toString (), QStringLiteral ("FT8"));
    QCOMPARE (payload.value (QStringLiteral ("mode_rx")).toString (), QStringLiteral ("FT8"));
    QVERIFY (!request.payload.contains ("wl2_secret"));
    QVERIFY (!request.url.toString ().contains (QStringLiteral ("wl2_secret")));
    cloudlog.updateRadio ({14075000, 14074000, QStringLiteral ("FT8")});
    QCOMPARE (network.requests ().size (), 1);
  }

  void publishesRadioWithLegacyKeyInBody ()
  {
    FakeNetworkAccessManager network;
    TestCloudlogConfiguration config {QStringLiteral ("https://wavelog.example"), QStringLiteral ("legacy-secret"), 7};
    config.radioEnabled = true;
    network.queueJson (200, json (R"({"status":"success"})"));
    Cloudlog cloudlog {&config, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::radio_status_finished};
    cloudlog.updateRadio ({7074000, 7074000, QStringLiteral ("FT8")});
    QVERIFY (spy.wait (1000));
    QCOMPARE (qvariant_cast<Cloudlog::UploadResult> (spy.at (0).at (0)).status, Cloudlog::UploadStatus::Success);
    auto const request = network.requests ().at (0);
    QCOMPARE (request.url.path (), QStringLiteral ("/index.php/api/radio"));
    QVERIFY (request.authorization.isEmpty ());
    QCOMPARE (QJsonDocument::fromJson (request.payload).object ().value (QStringLiteral ("key")).toString (), config.apiKey);
  }

  void publishesLatestRadioStateWithoutOverlappingRequests ()
  {
    FakeNetworkAccessManager network;
    TestCloudlogConfiguration config {QStringLiteral ("https://wavelog.example"), QStringLiteral ("wl2_secret"), 7};
    config.radioEnabled = true;
    network.queueResponse ({200, json (R"({"data":{"radio":"WSJT-X"}})"), QNetworkReply::NoError, {}, false});
    Cloudlog cloudlog {&config, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::radio_status_finished};
    cloudlog.updateRadio ({14075000, 14074000, QStringLiteral ("FT8")});
    cloudlog.updateRadio ({7074000, 7074000, QStringLiteral ("FT4")});
    QCOMPARE (network.requests ().size (), 1);
    network.replies ().at (0)->finishNow ();
    QTest::qWait (1100);
    cloudlog.updateRadio ({14075000, 14074000, QStringLiteral ("FT8")});
    QCOMPARE (network.requests ().size (), 1);
    network.queueJson (200, json (R"({"data":{"radio":"WSJT-X"}})"));
    cloudlog.updateRadio ({7074000, 7074000, QStringLiteral ("FT4")});
    QVERIFY (spy.wait (1000));
    QCOMPARE (network.requests ().size (), 2);
    auto const payload = QJsonDocument::fromJson (network.requests ().at (1).payload).object ();
    QCOMPARE (payload.value (QStringLiteral ("frequency")).toInt (), 7074000);
    QCOMPARE (payload.value (QStringLiteral ("frequency_rx")).toInt (), 7074000);
    QCOMPARE (payload.value (QStringLiteral ("mode")).toString (), QStringLiteral ("FT4"));
  }

  void preservesRadioBearerTokenAcrossHttpFallback ()
  {
    FakeNetworkAccessManager network;
    TestCloudlogConfiguration config {QStringLiteral ("http://wavelog.example"), QStringLiteral ("wl2_secret"), 7};
    config.radioEnabled = true;
    network.queueResponse ({0, {}, QNetworkReply::SslHandshakeFailedError, QStringLiteral ("TLS unavailable")});
    network.queueJson (200, json (R"({"data":{"radio":"WSJT-X"}})"));
    Cloudlog cloudlog {&config, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::radio_status_finished};
    cloudlog.updateRadio ({14074000, 14074000, QStringLiteral ("FT8")});
    QVERIFY (spy.wait (1000));
    QCOMPARE (qvariant_cast<Cloudlog::UploadResult> (spy.at (0).at (0)).status, Cloudlog::UploadStatus::Success);
    QCOMPARE (network.requests ().size (), 2);
    QCOMPARE (network.requests ().at (0).url.scheme (), QStringLiteral ("https"));
    QCOMPARE (network.requests ().at (1).url.scheme (), QStringLiteral ("http"));
    QCOMPARE (network.requests ().at (1).authorization, QByteArray {"Bearer wl2_secret"});
    QCOMPARE (network.requests ().at (1).payload, network.requests ().at (0).payload);
  }

  void backsOffRadioErrorsWithoutProtocolOrHttpFallback ()
  {
    FakeNetworkAccessManager network;
    TestCloudlogConfiguration config {QStringLiteral ("http://wavelog.example"), QStringLiteral ("wl2_secret"), 7};
    config.radioEnabled = true;
    network.queueJson (403, json (R"({"error":{"message":"Missing radio:write"}})"), QNetworkReply::ContentAccessDenied);
    Cloudlog cloudlog {&config, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::radio_status_finished};
    cloudlog.updateRadio ({14074000, 14074000, QStringLiteral ("FT8")});
    QVERIFY (spy.wait (1000));
    auto const result = qvariant_cast<Cloudlog::UploadResult> (spy.at (0).at (0));
    QCOMPARE (result.status, Cloudlog::UploadStatus::HttpError);
    QCOMPARE (result.detail, QStringLiteral ("Missing radio:write"));
    cloudlog.updateRadio ({7074000, 7074000, QStringLiteral ("FT8")});
    QCOMPARE (network.requests ().size (), 1);
  }

  void cancelsRadioReportingWhenDisabled ()
  {
    FakeNetworkAccessManager network;
    TestCloudlogConfiguration config {QStringLiteral ("https://wavelog.example"), QStringLiteral ("wl2_secret"), 7};
    config.radioEnabled = true;
    network.queueResponse ({200, {}, QNetworkReply::NoError, {}, false});
    Cloudlog cloudlog {&config, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::radio_status_finished};
    cloudlog.updateRadio ({14074000, 14074000, QStringLiteral ("FT8")});
    auto reply = network.replies ().at (0);
    config.radioEnabled = false;
    cloudlog.updateRadio ({14074000, 14074000, QStringLiteral ("FT8")});
    QCOMPARE (reply->error (), QNetworkReply::OperationCanceledError);
    reply->finishNow ();
    QVERIFY (spy.isEmpty ());
    QCOMPARE (network.requests ().size (), 1);
  }

  void normalizesBaseUrl_data ()
  {
    QTest::addColumn<QString> ("input");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("base") << "https://log.example" << "https://log.example";
    QTest::newRow ("trailing slash") << "https://log.example/" << "https://log.example";
    QTest::newRow ("api qso") << " https://log.example/api/qso/ " << "https://log.example";
    QTest::newRow ("index qso") << "https://log.example/index.php/api/qso/" << "https://log.example";
    QTest::newRow ("v2 api qso") << "https://log.example/api/v2/qso/" << "https://log.example";
    QTest::newRow ("v2 index qso") << "https://log.example/index.php/api/v2/qso/" << "https://log.example";
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

  void usesWavelogV2ForScopedToken ()
  {
    FakeNetworkAccessManager network;
    network.queueJson (200, json (R"({"data":{"name":"Wavelog API","status":"ok"}})"));
    network.queueJson (200, json (R"({"data":{"scopes":["station:read","qso:write"]}})"));
    network.queueJson (200, json (R"({"data":[{"id":7,"name":"Home","callsign":"K1ABC"}]})"));
    network.queueJson (200, json (R"({"data":{"dryrun":true,"parsed":1}})"));

    auto const result = runCheck (
      network, 7, QStringLiteral ("https://wavelog.example"), QStringLiteral ("wl2_secret"));

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QVERIFY (result.detail.contains (QStringLiteral ("API v2")));
    QCOMPARE (result.stationProfiles.size (), 1);
    QCOMPARE (result.stationProfiles.at (0).id, 7);
    auto const requests = network.requests ();
    QCOMPARE (requests.size (), 4);
    QCOMPARE (requests.at (0).url.path (), QStringLiteral ("/index.php/api/v2/status"));
    QVERIFY (requests.at (0).authorization.isEmpty ());
    QCOMPARE (requests.at (1).url.path (), QStringLiteral ("/index.php/api/v2/token"));
    QCOMPARE (requests.at (2).url.path (), QStringLiteral ("/index.php/api/v2/station"));
    QCOMPARE (requests.at (3).url.path (), QStringLiteral ("/index.php/api/v2/qso"));
    for (int i = 1; i < requests.size (); ++i)
      {
        QCOMPARE (requests.at (i).authorization, QByteArray {"Bearer wl2_secret"});
        QVERIFY (!requests.at (i).url.toString ().contains (QStringLiteral ("wl2_secret")));
      }
    QCOMPARE (requests.at (3).method, QStringLiteral ("POST"));
    auto const payload = QJsonDocument::fromJson (requests.at (3).payload).object ();
    QCOMPARE (payload.value (QStringLiteral ("import_type")).toString (), QStringLiteral ("adif"));
    QCOMPARE (payload.value (QStringLiteral ("station_profile_id")).toInt (), 7);
    QCOMPARE (payload.value (QStringLiteral ("dryrun")).toBool (), true);
    QVERIFY (payload.value (QStringLiteral ("adif")).toString ().endsWith (QStringLiteral ("<eor>")));
  }

  void rejectsWavelogV2TokenWithoutRequiredScopes ()
  {
    FakeNetworkAccessManager network;
    network.queueJson (200, json (R"({"data":{"name":"Wavelog API","status":"ok"}})"));
    network.queueJson (200, json (R"({"data":{"scopes":["qso:read"]}})"));

    auto const result = runCheck (
      network, 7, QStringLiteral ("https://wavelog.example"), QStringLiteral ("wl2_secret"));

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::InsufficientScope);
    QVERIFY (result.detail.contains (QStringLiteral ("qso:write")));
    QVERIFY (result.detail.contains (QStringLiteral ("station:read")));
    QCOMPARE (network.requests ().size (), 2);
  }

  void doesNotSendWavelogV2TokenToLegacyApi ()
  {
    FakeNetworkAccessManager network;
    network.queueJson (200, json (R"({"data":{"name":"Wavelog API","status":"ok"}})"));
    network.queueResponse ({401,
                            json (R"({"error":{"code":"invalid_token","message":"Token revoked"}})"),
                            QNetworkReply::AuthenticationRequiredError,
                            QStringLiteral ("unauthorized")});

    auto const result = runCheck (
      network, 7, QStringLiteral ("https://wavelog.example"), QStringLiteral ("wl2_revoked"));

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::InvalidKey);
    QCOMPARE (result.detail, QStringLiteral ("Token revoked"));
    QCOMPARE (network.requests ().size (), 2);
    for (auto const& request : network.requests ())
      {
        QVERIFY (request.url.path ().contains (QStringLiteral ("/api/v2/")));
        QVERIFY (!request.url.toString ().contains (QStringLiteral ("wl2_revoked")));
      }
  }

  void reportsUnavailableWavelogV2WithoutLegacyFallback ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({404, QByteArray {"not found"}, QNetworkReply::ContentNotFoundError,
                            QStringLiteral ("not found")});

    auto const result = runCheck (
      network, 7, QStringLiteral ("https://old-wavelog.example"), QStringLiteral ("wl2_secret"));

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::EndpointUnavailable);
    QVERIFY (result.detail.contains (QStringLiteral ("3.1.0")));
    QCOMPARE (network.requests ().size (), 1);
    QCOMPARE (network.requests ().at (0).url.path (), QStringLiteral ("/index.php/api/v2/status"));
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
    QCOMPARE (network.requests ().at (0).redirectPolicy,
              static_cast<int> (QNetworkRequest::NoLessSafeRedirectPolicy));
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

  void uploadsQsoWithWavelogV2BearerToken ()
  {
    FakeNetworkAccessManager network;
    auto const result = runLogQso (
      network, {201, json (R"({"data":{"parsed":1,"imported":1,"skipped":0,"messages":[]}})")},
      QStringLiteral ("https://wavelog.example"), QStringLiteral ("wl2_secret"));

    QCOMPARE (result.status, Cloudlog::UploadStatus::Success);
    QCOMPARE (network.requests ().size (), 1);
    auto const request = network.requests ().at (0);
    QCOMPARE (request.method, QStringLiteral ("POST"));
    QCOMPARE (request.url.path (), QStringLiteral ("/index.php/api/v2/qso"));
    QVERIFY (!request.url.toString ().contains (QStringLiteral ("wl2_secret")));
    QCOMPARE (request.authorization, QByteArray {"Bearer wl2_secret"});
    auto const payload = QJsonDocument::fromJson (request.payload).object ();
    QCOMPARE (payload.value (QStringLiteral ("import_type")).toString (), QStringLiteral ("adif"));
    QCOMPARE (payload.value (QStringLiteral ("station_profile_id")).toInt (), 7);
    QVERIFY (!payload.contains (QStringLiteral ("dryrun")));
    QVERIFY (!payload.contains (QStringLiteral ("key")));
    QCOMPARE (payload.value (QStringLiteral ("adif")).toString (), QStringLiteral ("<call:5>K1ABC<eor>"));
  }

  void reportsWavelogV2UploadErrorWithoutLegacyFallback ()
  {
    FakeNetworkAccessManager network;
    auto const result = runLogQso (
      network,
      {403,
       json (R"({"error":{"code":"insufficient_scope","message":"Missing qso:write"}})"),
       QNetworkReply::ContentAccessDenied,
       QStringLiteral ("forbidden")},
      QStringLiteral ("https://wavelog.example"), QStringLiteral ("wl2_read_only"));

    QCOMPARE (result.status, Cloudlog::UploadStatus::HttpError);
    QCOMPARE (result.detail, QStringLiteral ("Missing qso:write"));
    QCOMPARE (network.requests ().size (), 1);
    QCOMPARE (network.requests ().at (0).url.path (), QStringLiteral ("/index.php/api/v2/qso"));
  }

  void preservesWavelogV2AcrossHttpTransportFallback ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({0, {}, QNetworkReply::SslHandshakeFailedError, QStringLiteral ("TLS unavailable")});
    auto const result = runLogQso (
      network, {201, json (R"({"data":{"parsed":1,"imported":1,"skipped":0}})")},
      QStringLiteral ("http://legacy-wavelog.example"), QStringLiteral ("wl2_secret"));

    QCOMPARE (result.status, Cloudlog::UploadStatus::Success);
    auto const requests = network.requests ();
    QCOMPARE (requests.size (), 2);
    QCOMPARE (requests.at (0).url.scheme (), QStringLiteral ("https"));
    QCOMPARE (requests.at (1).url.scheme (), QStringLiteral ("http"));
    for (auto const& request : requests)
      {
        QCOMPARE (request.url.path (), QStringLiteral ("/index.php/api/v2/qso"));
        QCOMPARE (request.authorization, QByteArray {"Bearer wl2_secret"});
        QVERIFY (!request.url.toString ().contains (QStringLiteral ("wl2_secret")));
      }
  }

  void prefersHttpsAndFallsBackForQsoUpload ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({0, {}, QNetworkReply::SslHandshakeFailedError, QStringLiteral ("TLS unavailable")});
    auto const result = runLogQso (
      network, {201, json (R"({"status":"created"})")},
      QStringLiteral (" http://legacy-upload.example/index.php/api/qso/ "));

    QCOMPARE (result.status, Cloudlog::UploadStatus::Success);
    auto const requests = network.requests ();
    QCOMPARE (requests.size (), 2);
    QCOMPARE (requests.at (0).url.scheme (), QStringLiteral ("https"));
    QCOMPARE (requests.at (1).url.scheme (), QStringLiteral ("http"));
    QCOMPARE (requests.at (0).url.host (), requests.at (1).url.host ());
    QCOMPARE (requests.at (0).payload, requests.at (1).payload);
    QCOMPARE (requests.at (0).redirectPolicy,
              static_cast<int> (QNetworkRequest::NoLessSafeRedirectPolicy));
    QCOMPARE (requests.at (1).redirectPolicy,
              static_cast<int> (QNetworkRequest::NoLessSafeRedirectPolicy));
  }

  void doesNotDowngradeAfterHttpsHttpResponse ()
  {
    FakeNetworkAccessManager network;
    auto const result = runLogQso (
      network,
      {404, json (R"({"message":"not found"})"), QNetworkReply::ContentNotFoundError, QStringLiteral ("not found")},
      QStringLiteral ("http://legacy-upload.example"));

    QCOMPARE (result.status, Cloudlog::UploadStatus::HttpError);
    QCOMPARE (network.requests ().size (), 1);
    QCOMPARE (network.requests ().at (0).url.scheme (), QStringLiteral ("https"));
  }

  void prefersHttpsAndFallsBackForConnectionCheck ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({0, {}, QNetworkReply::SslHandshakeFailedError, QStringLiteral ("TLS unavailable")});
    network.queueWritableJsonAuth ();
    network.queueStationInfo (json (R"([{"station_id":7,"station_profile_name":"Home"}])"));
    network.queueManifest (json (R"({"name":"Cloudlog"})"));

    auto const result = runCheck (network, 7, QStringLiteral (" http://legacy-check.example/api/qso/ "));

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QCOMPARE (result.normalizedBaseUrl, QStringLiteral ("http://legacy-check.example"));
    QVERIFY (result.detail.contains (QStringLiteral ("connected over HTTP")));
    auto const requests = network.requests ();
    QCOMPARE (requests.size (), 4);
    QCOMPARE (requests.at (0).url.scheme (), QStringLiteral ("https"));
    for (int i = 1; i < requests.size (); ++i)
      {
        QCOMPARE (requests.at (i).url.scheme (), QStringLiteral ("http"));
        QCOMPARE (requests.at (i).redirectPolicy,
                  static_cast<int> (QNetworkRequest::NoLessSafeRedirectPolicy));
      }
  }

  void preservesWavelogV2ConnectionCheckAcrossHttpFallback ()
  {
    FakeNetworkAccessManager network;
    network.queueResponse ({0, {}, QNetworkReply::SslHandshakeFailedError, QStringLiteral ("TLS unavailable")});
    network.queueJson (200, json (R"({"data":{"name":"Wavelog API","status":"ok"}})"));
    network.queueJson (200, json (R"({"data":{"scopes":["station:read","qso:write"]}})"));
    network.queueJson (200, json (R"({"data":[{"id":7,"name":"Home"}]})"));
    network.queueJson (200, json (R"({"data":{"dryrun":true,"parsed":1}})"));

    auto const result = runCheck (
      network, 7, QStringLiteral ("http://legacy-wavelog.example"), QStringLiteral ("wl2_secret"));

    QCOMPARE (result.status, Cloudlog::ConnectionCheckStatus::Success);
    QVERIFY (result.detail.contains (QStringLiteral ("connected over HTTP")));
    auto const requests = network.requests ();
    QCOMPARE (requests.size (), 5);
    QCOMPARE (requests.at (0).url.scheme (), QStringLiteral ("https"));
    for (int i = 1; i < requests.size (); ++i)
      {
        QCOMPARE (requests.at (i).url.scheme (), QStringLiteral ("http"));
        QVERIFY (requests.at (i).url.path ().contains (QStringLiteral ("/api/v2/")));
      }
  }

  void reusesCheckedHttpTransportAcrossInstances_data ()
  {
    QTest::addColumn<bool> ("v2");
    QTest::newRow ("legacy") << false;
    QTest::newRow ("v2") << true;
  }

  void reusesCheckedHttpTransportAcrossInstances ()
  {
    QFETCH (bool, v2);
    auto const url = QStringLiteral ("http://checked-timeout-%1.example/log").arg (v2);
    auto const key = v2 ? QStringLiteral ("wl2_test") : QStringLiteral ("test-key");
    FakeNetworkAccessManager network;
    network.httpsError = QNetworkReply::TimeoutError;
    TestCloudlogConfiguration config {url + QStringLiteral ("/index.php/api/qso/"), key, 7};
    config.radioEnabled = true;
    Cloudlog uploader {&config, &network};
    QSignalSpy qsoSpy {&uploader, &Cloudlog::qso_upload_finished};
    QSignalSpy radioSpy {&uploader, &Cloudlog::radio_status_finished};
    if (v2)
      {
        network.queueJson (200, json (R"({"data":{"name":"Wavelog API","status":"ok"}})"));
        network.queueJson (200, json (R"({"data":{"scopes":["station:read","qso:write","radio:write"]}})"));
        network.queueJson (200, json (R"({"data":[{"id":7,"name":"Home"}]})"));
        network.queueJson (200, json (R"({"data":{"dryrun":true,"parsed":1}})"));
      }
    else
      {
        network.queueWritableJsonAuth ();
        network.queueStationInfo (json (R"([{"station_id":7,"station_profile_name":"Home"}])"));
        network.queueManifest (json (R"({"name":"Wavelog"})"));
        network.queueJson (201, json (R"({"status":"created"})"));
      }
    auto const check = runCheck (network, 7, url + QStringLiteral ("/api/qso/"), key, true);
    QCOMPARE (check.status, Cloudlog::ConnectionCheckStatus::Success);
    QCOMPARE (check.normalizedBaseUrl, url);
    QVERIFY (check.detail.contains (QStringLiteral ("connected over HTTP")));
    auto const checkRequests = network.requests ().size ();
    for (int i = 0; i < 2; ++i)
      {
        network.queueJson (201, v2 ? json (R"({"data":{"parsed":1,"imported":1,"skipped":0}})")
                                  : json (R"({"status":"created"})"));
        uploader.logQso (QByteArray {"<call:5>K1ABC"});
        QVERIFY (qsoSpy.wait (1000));
        QCOMPARE (qvariant_cast<Cloudlog::UploadResult> (qsoSpy.takeFirst ().at (0)).status,
                  Cloudlog::UploadStatus::Success);
      }
    network.queueJson (200, v2 ? json (R"({"data":{"radio":"WSJT-X"}})")
                              : json (R"({"status":"success"})"));
    uploader.updateRadio ({14074000, 14074000, QStringLiteral ("FT8")});
    QVERIFY (radioSpy.wait (1000));
    QCOMPARE (qvariant_cast<Cloudlog::UploadResult> (radioSpy.takeFirst ().at (0)).status,
              Cloudlog::UploadStatus::Success);
    QCOMPARE (network.requests ().size (), checkRequests + 3);
    auto const path = v2 ? QStringLiteral ("/log/index.php/api/v2/") : QStringLiteral ("/log/api/");
    for (int i = checkRequests; i < network.requests ().size (); ++i)
      {
        auto const request = network.requests ().at (i);
        QCOMPARE (request.url.scheme (), QStringLiteral ("http"));
        QCOMPARE (request.url.path (), path + (i == checkRequests + 2 ? QStringLiteral ("radio") : QStringLiteral ("qso")));
        QCOMPARE (request.authorization, v2 ? QByteArray {"Bearer wl2_test"} : QByteArray {});
      }

    // An explicitly configured HTTPS URL must not inherit the HTTP selection.
    config.url = QString {url}.replace (QStringLiteral ("http:"), QStringLiteral ("https:"));
    uploader.logQso (QByteArray {"<call:5>K1ABC"});
    QVERIFY (qsoSpy.wait (1000));
    QCOMPARE (qvariant_cast<Cloudlog::UploadResult> (qsoSpy.takeFirst ().at (0)).status,
              Cloudlog::UploadStatus::NetworkError);
    QCOMPARE (network.requests ().size (), checkRequests + 4);
    QCOMPARE (network.requests ().last ().url.scheme (), QStringLiteral ("https"));

    // A successful recheck probes HTTPS again and restores its preference.
    network.httpsError = QNetworkReply::NoError;
    network.queueWritableJsonAuth ();
    network.queueStationInfo (json (R"([{"station_id":7}])"));
    network.queueManifest (json (R"({"name":"Cloudlog"})"));
    auto const recheck = runCheck (network, 7, url);
    QCOMPARE (recheck.status, Cloudlog::ConnectionCheckStatus::Success);
    QVERIFY (recheck.normalizedBaseUrl.startsWith (QStringLiteral ("https:")));
    auto const restored = runLogQso (network, {201, json (R"({"status":"created"})")}, url);
    QCOMPARE (restored.status, Cloudlog::UploadStatus::Success);
    QCOMPARE (network.requests ().last ().url.scheme (), QStringLiteral ("https"));
  }

  void doesNotRetryUncertainPost_data ()
  {
    QTest::addColumn<int> ("error");
    QTest::addColumn<bool> ("radio");
    for (auto error : {QNetworkReply::TimeoutError, QNetworkReply::OperationCanceledError,
                       QNetworkReply::RemoteHostClosedError})
      {
        for (bool radio : {false, true})
          {
            auto const name = QByteArray::number (error) + (radio ? "-radio" : "-qso");
            QTest::newRow (name.constData ()) << static_cast<int> (error) << radio;
          }
      }
  }

  void doesNotSelectHttpAfterFailedCheck ()
  {
    FakeNetworkAccessManager network;
    network.httpsError = QNetworkReply::TimeoutError;
    network.queueJson (200, json (R"({"status":"valid","rights":"r"})"));
    auto const url = QStringLiteral ("http://failed-check.example");
    QCOMPARE (runCheck (network, 7, url).status, Cloudlog::ConnectionCheckStatus::ReadOnlyKey);
    QCOMPARE (network.requests ().size (), 2);
    auto const result = runLogQso (network, {201, json (R"({"status":"created"})")}, url);
    QCOMPARE (result.status, Cloudlog::UploadStatus::NetworkError);
    QCOMPARE (network.requests ().size (), 3);
    QCOMPARE (network.requests ().last ().url.scheme (), QStringLiteral ("https"));
  }

  void doesNotRetryUncertainPost ()
  {
    QFETCH (int, error);
    QFETCH (bool, radio);
    FakeNetworkAccessManager network;
    TestCloudlogConfiguration config {QStringLiteral ("http://uncertain-upload.example"), QStringLiteral ("test-key"), 7};
    config.radioEnabled = true;
    network.queueResponse ({0, {}, static_cast<QNetworkReply::NetworkError> (error), QStringLiteral ("delivery uncertain")});
    Cloudlog cloudlog {&config, &network};
    QSignalSpy spy {&cloudlog, radio ? &Cloudlog::radio_status_finished : &Cloudlog::qso_upload_finished};
    if (radio)
      {
        cloudlog.updateRadio ({14074000, 14074000, QStringLiteral ("FT8")});
      }
    else
      {
        cloudlog.logQso (QByteArray {"<call:5>K1ABC"});
      }
    QVERIFY (spy.wait (1000));
    QCOMPARE (qvariant_cast<Cloudlog::UploadResult> (spy.takeFirst ().at (0)).status,
              Cloudlog::UploadStatus::NetworkError);
    QCOMPARE (network.requests ().size (), 1);
    QCOMPARE (network.requests ().first ().url.scheme (), QStringLiteral ("https"));
  }

  void doesNotRetryDeliveredPostAfterAbort ()
  {
    QTcpServer server;
    QVERIFY (server.listen (QHostAddress::LocalHost));
    QNetworkAccessManager network;
    network.setProxy (QNetworkProxy::NoProxy);
    TestCloudlogConfiguration config {
      QStringLiteral ("http://127.0.0.1:%1").arg (server.serverPort ()), QStringLiteral ("local-test-key"), 7};
    // Select HTTP without exposing a real POST to an HTTPS timeout.
    FakeNetworkAccessManager checker;
    checker.httpsError = QNetworkReply::TimeoutError;
    checker.queueWritableJsonAuth ();
    checker.queueStationInfo (json (R"([{"station_id":7}])"));
    checker.queueManifest (json (R"({"name":"Cloudlog"})"));
    QCOMPARE (runCheck (checker, 7, config.url).status, Cloudlog::ConnectionCheckStatus::Success);
    Cloudlog cloudlog {&config, &network};
    QSignalSpy spy {&cloudlog, &Cloudlog::qso_upload_finished};
    QPointer<QNetworkReply> reply;
    connect (&network, &QNetworkAccessManager::finished, this, [&reply] (QNetworkReply * finished) { reply = finished; });
    cloudlog.logQso (QByteArray {"<call:5>K1ABC"});
    QTRY_VERIFY (server.hasPendingConnections ());
    auto * socket = server.nextPendingConnection ();
    QByteArray received;
    auto const payload = Cloudlog::qsoUploadPayload (config.apiKey, 7, QByteArray {"<call:5>K1ABC"});
    QTRY_VERIFY ((received += socket->readAll ()).endsWith (payload));
    QVERIFY (received.startsWith ("POST /api/qso HTTP/1.1\r\n"));
    // The server has the complete QSO, but sends no response.
    auto const replies = network.findChildren<QNetworkReply *> ();
    QCOMPARE (replies.size (), 1);
    replies.first ()->abort ();
    QCOMPARE (spy.size (), 1);
    QVERIFY (reply);
    QCOMPARE (reply->error (), QNetworkReply::OperationCanceledError);
    QVERIFY (!reply->attribute (QNetworkRequest::HttpStatusCodeAttribute).isValid ());
    QCOMPARE (qvariant_cast<Cloudlog::UploadResult> (spy.first ().at (0)).status,
              Cloudlog::UploadStatus::NetworkError);
    QCoreApplication::processEvents ();
    QVERIFY (!server.hasPendingConnections ());
    QVERIFY (socket->readAll ().isEmpty ());
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
