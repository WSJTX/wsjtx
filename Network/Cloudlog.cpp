#include "Cloudlog.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>

#include <QApplication>
#include <QDate>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QXmlStreamReader>

#include "pimpl_impl.hpp"

#include "moc_Cloudlog.cpp"

#include "revision_utils.hpp"

namespace
{
  enum class EndpointLayout
  {
    DirectApi,
    IndexPhpApi
  };

  enum class ApiVersion
  {
    Legacy,
    V2
  };

  enum class AuthStatus
  {
    Writable,
    ReadOnly,
    Invalid,
    Unavailable,
    Unexpected
  };

  enum class ServerProduct
  {
    Wavelog,
    UnknownOrOther
  };

  struct AuthResult
  {
    AuthResult () = default;
    AuthResult (AuthStatus status, QString const& detail)
      : status {status}
      , detail {detail}
    {
    }

    AuthStatus status {AuthStatus::Unavailable};
    QString detail;
  };

  QString endpointPath (EndpointLayout layout, QString const& apiPath)
  {
    return (EndpointLayout::DirectApi == layout ? QStringLiteral ("/api/") : QStringLiteral ("/index.php/api/")) + apiPath;
  }

  QString encodedKey (QString const& apiKey)
  {
    return QString::fromLatin1 (QUrl::toPercentEncoding (apiKey));
  }

  QUrl endpointUrl (QString const& baseUrl, EndpointLayout layout, QString const& apiPath, QString const& apiKey)
  {
    return QUrl {baseUrl + endpointPath (layout, apiPath) + QLatin1Char ('/') + encodedKey (apiKey)};
  }

  QUrl v2EndpointUrl (QString const& baseUrl, QString const& resource)
  {
    return QUrl {baseUrl + QStringLiteral ("/index.php/api/v2/") + resource};
  }

  bool usesApiV2 (QString const& apiKey)
  {
    return apiKey.startsWith (QStringLiteral ("wl2_"));
  }

  QHash<QString, EndpointLayout>& uploadLayoutCache ()
  {
    static QHash<QString, EndpointLayout> cache;
    return cache;
  }

  void applyCommonHeaders (QNetworkRequest& request, bool acceptJson)
  {
    request.setAttribute (QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader ("User-Agent", http_user_agent ().toUtf8 ());
    if (acceptJson)
      {
        request.setRawHeader ("Accept", "application/json");
      }
  }

  QNetworkRequest jsonPostRequest (QUrl const& url)
  {
    QNetworkRequest request {url};
    request.setHeader (QNetworkRequest::ContentTypeHeader, QVariant {QStringLiteral ("application/json")});
    applyCommonHeaders (request, true);
    return request;
  }

  QNetworkRequest connectionCheckGetRequest (QUrl const& url, bool acceptJson)
  {
    QNetworkRequest request {url};
    applyCommonHeaders (request, acceptJson);
    return request;
  }

  void applyBearerToken (QNetworkRequest& request, QString const& token)
  {
    request.setRawHeader ("Authorization", QByteArray {"Bearer "} + token.toUtf8 ());
  }

  QNetworkRequest v2GetRequest (QUrl const& url, QString const& token)
  {
    auto request = connectionCheckGetRequest (url, true);
    applyBearerToken (request, token);
    return request;
  }

  QNetworkRequest v2PostRequest (QUrl const& url, QString const& token)
  {
    auto request = jsonPostRequest (url);
    applyBearerToken (request, token);
    return request;
  }

  bool isTransportError (QNetworkReply const& reply)
  {
    return QNetworkReply::NoError != reply.error ()
      && !reply.attribute (QNetworkRequest::HttpStatusCodeAttribute).isValid ();
  }

  int httpStatus (QNetworkReply const& reply)
  {
    return reply.attribute (QNetworkRequest::HttpStatusCodeAttribute).toInt ();
  }

  QString httpsFirstBaseUrl (QString const& baseUrl, QString& httpFallback)
  {
    QUrl url {baseUrl};
    if (url.scheme ().compare (QStringLiteral ("http"), Qt::CaseInsensitive) != 0)
      {
        httpFallback.clear ();
        return baseUrl;
      }

    httpFallback = baseUrl;
    url.setScheme (QStringLiteral ("https"));
    return url.toString ();
  }

  bool canRetryUploadOverHttp (QNetworkReply const& reply)
  {
    if (!isTransportError (reply))
      {
        return false;
      }

    return QNetworkReply::ConnectionRefusedError == reply.error ()
      || QNetworkReply::SslHandshakeFailedError == reply.error ();
  }

  QString lowerString (QJsonObject const& object, char const * key)
  {
    return object.value (QLatin1String {key}).toString ().trimmed ().toLower ();
  }

  AuthResult parseJsonAuth (QByteArray const& body, int statusCode)
  {
    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (body, &error);
    if (QJsonParseError::NoError != error.error || !document.isObject ())
      {
        return {statusCode == 401 || statusCode == 403 ? AuthStatus::Invalid : AuthStatus::Unavailable,
                QStringLiteral ("Authentication endpoint did not return JSON.")};
      }

    auto const object = document.object ();
    auto const status = lowerString (object, "status");
    auto const rights = lowerString (object, "rights");

    if (status == QLatin1String ("invalid") || status == QLatin1String ("failed")
        || status == QLatin1String ("failure") || statusCode == 401 || statusCode == 403)
      {
        return {AuthStatus::Invalid, object.value (QStringLiteral ("message")).toString ()};
      }

    if (status != QLatin1String ("valid") && status != QLatin1String ("ok"))
      {
        return {AuthStatus::Unavailable, QStringLiteral ("Authentication endpoint did not report a valid status.")};
      }

    if (rights.isEmpty ())
      {
        return {AuthStatus::Unexpected, QStringLiteral ("Authentication succeeded but did not include API key rights.")};
      }

    if (rights.startsWith (QLatin1String ("rw")))
      {
        return {AuthStatus::Writable, QStringLiteral ("API key is writable.")};
      }

    if (rights.startsWith (QLatin1Char ('r')))
      {
        return {AuthStatus::ReadOnly, QStringLiteral ("API key is read-only.")};
      }

    return {AuthStatus::Unexpected, QStringLiteral ("Authentication returned unrecognized API key rights: %1").arg (rights)};
  }

  AuthResult parseXmlAuth (QByteArray const& body)
  {
    QString status;
    QString rights;
    QXmlStreamReader xml;
    xml.addData (QString::fromUtf8 (body));
    while (!xml.atEnd ())
      {
        xml.readNext ();
        if (xml.isStartElement ())
          {
            if (xml.name () == QLatin1String ("status"))
              {
                status = xml.readElementText ().trimmed ().toLower ();
              }
            else if (xml.name () == QLatin1String ("rights"))
              {
                rights = xml.readElementText ().trimmed ().toLower ();
              }
          }
      }

    if (xml.hasError ())
      {
        return {AuthStatus::Unavailable, QStringLiteral ("Authentication endpoint did not return XML.")};
      }

    if (status != QLatin1String ("valid"))
      {
        return {AuthStatus::Invalid, QStringLiteral ("API key is invalid.")};
      }

    if (rights.isEmpty ())
      {
        return {AuthStatus::Unexpected, QStringLiteral ("Authentication succeeded but did not include API key rights.")};
      }

    if (rights.startsWith (QLatin1String ("rw")))
      {
        return {AuthStatus::Writable, QStringLiteral ("API key is writable.")};
      }

    if (rights.startsWith (QLatin1Char ('r')))
      {
        return {AuthStatus::ReadOnly, QStringLiteral ("API key is read-only.")};
      }

    return {AuthStatus::Unexpected, QStringLiteral ("Authentication returned unrecognized API key rights: %1").arg (rights)};
  }

  int intValue (QJsonValue const& value)
  {
    if (value.isDouble ())
      {
        return value.toInt ();
      }
    bool ok {false};
    auto const result = value.toString ().toInt (&ok);
    return ok ? result : 0;
  }

  QString stringValue (QJsonObject const& object, std::initializer_list<char const *> keys)
  {
    for (auto const key : keys)
      {
        auto const value = object.value (QLatin1String {key});
        if (!value.isUndefined () && !value.isNull ())
          {
            return value.toVariant ().toString ().trimmed ();
          }
      }
    return {};
  }

  bool appendStationProfile (QJsonObject const& object, QList<Cloudlog::StationProfile>& profiles)
  {
    auto const id = intValue (object.value (QStringLiteral ("station_id")));
    auto const alternate_id = id ? id : intValue (object.value (QStringLiteral ("station_profile_id")));
    auto const profile_id = alternate_id ? alternate_id : intValue (object.value (QStringLiteral ("id")));
    if (!profile_id)
      {
        return false;
      }

    profiles.append ({profile_id,
                      stringValue (object, {"station_profile_name", "profile_name", "name"}),
                      stringValue (object, {"station_callsign", "callsign", "call"})});
    return true;
  }

  void collectStationProfiles (QJsonValue const& value, QList<Cloudlog::StationProfile>& profiles)
  {
    if (value.isArray ())
      {
        auto const array = value.toArray ();
        for (auto const& entry : array)
          {
            collectStationProfiles (entry, profiles);
          }
        return;
      }

    if (!value.isObject ())
      {
        return;
      }

    auto const object = value.toObject ();
    if (appendStationProfile (object, profiles))
      {
        return;
      }

    for (auto const& key : {QStringLiteral ("stations"), QStringLiteral ("station_profiles"),
                            QStringLiteral ("stationProfiles"), QStringLiteral ("profiles")})
      {
        auto const child = object.value (key);
        if (!child.isUndefined ())
          {
            collectStationProfiles (child, profiles);
          }
      }
  }

  QString stationProfileList (QList<Cloudlog::StationProfile> const& profiles)
  {
    QStringList entries;
    for (auto const& profile : profiles)
      {
        entries.append (Cloudlog::stationProfileDisplayText (profile));
      }
    return entries.join (QStringLiteral (", "));
  }

  QString jsonDiagnostic (QJsonValue const& value)
  {
    if (value.isString ())
      {
        return value.toString ();
      }
    if (value.isArray ())
      {
        QStringList values;
        for (auto const& entry : value.toArray ())
          {
            auto const text = jsonDiagnostic (entry);
            if (!text.isEmpty ())
              {
                values.append (text);
              }
          }
        return values.join (QStringLiteral ("; "));
      }
    if (value.isObject ())
      {
        QStringList values;
        auto const object = value.toObject ();
        for (auto it = object.constBegin (); it != object.constEnd (); ++it)
          {
            auto const text = jsonDiagnostic (it.value ());
            if (!text.isEmpty ())
              {
                values.append (it.key () + QStringLiteral (": ") + text);
              }
          }
        return values.join (QStringLiteral ("; "));
      }
    return value.toVariant ().toString ();
  }

  QString responseFailureDetail (QJsonObject const& object, QString const& fallback)
  {
    for (auto const& key : {QStringLiteral ("reason"), QStringLiteral ("message"), QStringLiteral ("messages")})
      {
        auto const detail = jsonDiagnostic (object.value (key));
        if (!detail.isEmpty ())
          {
            return detail;
          }
      }
    return fallback;
  }

  QString v2FailureDetail (QJsonObject const& object, QString const& fallback)
  {
    auto const error = object.value (QStringLiteral ("error")).toObject ();
    auto const message = error.value (QStringLiteral ("message")).toString ().trimmed ();
    auto const details = jsonDiagnostic (error.value (QStringLiteral ("details")));
    if (!message.isEmpty () && !details.isEmpty ())
      {
        return message + QStringLiteral (" ") + details;
      }
    if (!message.isEmpty ())
      {
        return message;
      }
    if (!details.isEmpty ())
      {
        return details;
      }
    return fallback;
  }

  QByteArray v2QsoPayload (qint32 stationId, QByteArray const& ADIF, bool dryRun = false)
  {
    QJsonObject object;
    object.insert (QStringLiteral ("import_type"), QStringLiteral ("adif"));
    object.insert (QStringLiteral ("station_profile_id"), stationId);
    object.insert (QStringLiteral ("adif"), QString::fromUtf8 (ADIF + QByteArray {"<eor>"}));
    if (dryRun)
      {
        object.insert (QStringLiteral ("dryrun"), true);
      }
    return QJsonDocument {object}.toJson (QJsonDocument::Compact);
  }

  ServerProduct parseProductManifest (QByteArray const& body)
  {
    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (body, &error);
    if (QJsonParseError::NoError != error.error || !document.isObject ())
      {
        return ServerProduct::UnknownOrOther;
      }

    auto const object = document.object ();
    for (auto const& key : {QStringLiteral ("name"), QStringLiteral ("short_name")})
      {
        if (object.value (key).toString ().trimmed ().compare (QStringLiteral ("Wavelog"), Qt::CaseInsensitive) == 0)
          {
            return ServerProduct::Wavelog;
          }
      }

    return ServerProduct::UnknownOrOther;
  }

  Cloudlog::UploadResult classifyQsoUploadReply (int httpStatusCode, QNetworkReply::NetworkError networkError,
                                                  QString const& errorString, QByteArray const& body)
  {
    if (QNetworkReply::NoError != networkError && httpStatusCode == 0)
      {
        return {Cloudlog::UploadStatus::NetworkError,
                Cloudlog::tr ("QSO could not be sent to Cloudlog."),
                errorString,
                httpStatusCode};
      }

    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (body, &error);
    if (QJsonParseError::NoError != error.error || !document.isObject ())
      {
        if (httpStatusCode >= 400)
          {
            return {Cloudlog::UploadStatus::HttpError,
                    Cloudlog::tr ("QSO upload was rejected by Cloudlog."),
                    Cloudlog::tr ("HTTP status %1. Response was not JSON.").arg (httpStatusCode),
                    httpStatusCode};
          }
        return {Cloudlog::UploadStatus::UnexpectedResponse,
                Cloudlog::tr ("Cloudlog returned an unexpected upload response."),
                error.errorString (),
                httpStatusCode};
      }

    auto const object = document.object ();
    auto const status = lowerString (object, "status");
    if (status == QLatin1String ("failed") || status == QLatin1String ("failure") || status == QLatin1String ("invalid"))
      {
        auto detail = responseFailureDetail (object, {});
        if (detail.isEmpty ())
          {
            detail = Cloudlog::tr ("Cloudlog reported upload status: %1").arg (status);
          }
        return {Cloudlog::UploadStatus::Rejected,
                Cloudlog::tr ("QSO could not be sent to Cloudlog."),
                detail,
                httpStatusCode};
      }

    if (httpStatusCode >= 400)
      {
        auto const detail = responseFailureDetail (object, Cloudlog::tr ("HTTP status %1.").arg (httpStatusCode));
        return {Cloudlog::UploadStatus::HttpError,
                Cloudlog::tr ("QSO upload was rejected by Cloudlog."),
                detail,
                httpStatusCode};
      }

    return {Cloudlog::UploadStatus::Success, {}, {}, httpStatusCode};
  }

  Cloudlog::UploadResult classifyV2QsoUploadReply (int httpStatusCode, QNetworkReply::NetworkError networkError,
                                                    QString const& errorString, QByteArray const& body)
  {
    if (QNetworkReply::NoError != networkError && httpStatusCode == 0)
      {
        return {Cloudlog::UploadStatus::NetworkError,
                Cloudlog::tr ("QSO could not be sent to Wavelog."),
                errorString,
                httpStatusCode};
      }

    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (body, &error);
    if (QJsonParseError::NoError != error.error || !document.isObject ())
      {
        return {httpStatusCode >= 400 ? Cloudlog::UploadStatus::HttpError : Cloudlog::UploadStatus::UnexpectedResponse,
                Cloudlog::tr ("Wavelog returned an unexpected API v2 upload response."),
                httpStatusCode >= 400
                  ? Cloudlog::tr ("HTTP status %1. Response was not JSON.").arg (httpStatusCode)
                  : error.errorString (),
                httpStatusCode};
      }

    auto const object = document.object ();
    if (httpStatusCode >= 400)
      {
        return {Cloudlog::UploadStatus::HttpError,
                Cloudlog::tr ("QSO upload was rejected by Wavelog."),
                v2FailureDetail (object, Cloudlog::tr ("HTTP status %1.").arg (httpStatusCode)),
                httpStatusCode};
      }

    auto const data = object.value (QStringLiteral ("data")).toObject ();
    if (httpStatusCode == 201 && intValue (data.value (QStringLiteral ("parsed"))) > 0
        && intValue (data.value (QStringLiteral ("imported")))
             + intValue (data.value (QStringLiteral ("skipped"))) > 0)
      {
        return {Cloudlog::UploadStatus::Success, {}, {}, httpStatusCode};
      }

    return {Cloudlog::UploadStatus::UnexpectedResponse,
            Cloudlog::tr ("Wavelog returned an unexpected API v2 upload response."),
            Cloudlog::tr ("HTTP status %1 did not include a valid ADIF import summary.").arg (httpStatusCode),
            httpStatusCode};
  }
}

class Cloudlog::impl final
  : public QObject
{
  Q_OBJECT

public:
  impl (Cloudlog * self, CloudlogConfiguration const * config, QNetworkAccessManager * network_manager)
    : self_ {self}
    , config_ {config}
    , network_manager_ {network_manager}
  {
  }

  void logQso (QByteArray const& ADIF)
  {
    if (!config_)
      {
        return;
      }

    auto const configured_base_url = Cloudlog::normalizeBaseUrl (config_->cloudlog_api_url ());
    QString http_fallback;
    auto const base_url = httpsFirstBaseUrl (configured_base_url, http_fallback);
    auto const api_key = config_->cloudlog_api_key ();
    auto const api_version = usesApiV2 (api_key) ? ApiVersion::V2 : ApiVersion::Legacy;
    auto const data = ApiVersion::V2 == api_version
      ? v2QsoPayload (config_->cloudlog_api_station_id (), ADIF)
      : Cloudlog::qsoUploadPayload (api_key, config_->cloudlog_api_station_id (), ADIF);
    startQsoUpload (base_url, data, http_fallback, api_version, api_key);
  }

  void startQsoUpload (QString const& baseUrl, QByteArray const& data, QString const& httpFallback,
                       ApiVersion apiVersion, QString const& apiKey)
  {
    QUrl url;
    if (ApiVersion::V2 == apiVersion)
      {
        url = v2EndpointUrl (baseUrl, QStringLiteral ("qso"));
      }
    else
      {
        auto const layout = uploadLayoutCache ().value (baseUrl, EndpointLayout::IndexPhpApi);
        auto const path = EndpointLayout::DirectApi == layout
          ? QStringLiteral ("/api/qso") : QStringLiteral ("/index.php/api/qso");
        url = QUrl {baseUrl + path};
      }

    auto request = ApiVersion::V2 == apiVersion ? v2PostRequest (url, apiKey) : jsonPostRequest (url);
    upload_reply_ = network_manager_->post (request, data);
    upload_reply_->setProperty ("cloudlogHttpFallback", httpFallback);
    upload_reply_->setProperty ("cloudlogUploadPayload", data);
    upload_reply_->setProperty ("cloudlogApiVersion", static_cast<int> (apiVersion));
    upload_reply_->setProperty ("cloudlogApiKey", apiKey);
    connect (upload_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_logqso);
  }

  void checkConnection (Cloudlog::ConnectionCheckRequest const& request)
  {
    abortCheckReply ();
    check_request_ = request;
    auto const configured_base_url = Cloudlog::normalizeBaseUrl (request.url);
    check_request_.url = httpsFirstBaseUrl (configured_base_url, check_http_fallback_url_);
    check_api_version_ = usesApiV2 (request.apiKey) ? ApiVersion::V2 : ApiVersion::Legacy;
    check_using_http_fallback_ = false;
    auth_layout_index_ = 0;
    dry_run_layout_index_ = 0;
    checked_station_profiles_.clear ();

#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    if (QNetworkAccessManager::Accessible != network_manager_->networkAccessible ())
      {
        network_manager_->setNetworkAccessible (QNetworkAccessManager::Accessible);
      }
#endif

    if (check_request_.url.isEmpty ())
      {
        finish ({Cloudlog::ConnectionCheckStatus::EndpointUnavailable,
                 tr ("Enter a Cloudlog or Wavelog URL."),
                 tr ("The connection check needs a base URL before it can contact the API."),
                 check_request_.url});
        return;
      }

    startConnectionCheck ();
  }

  void updateRadio (Cloudlog::RadioState const& state)
  {
    if (!config_ || !config_->cloudlog_radio_enabled () || !state.txFrequency || !state.rxFrequency)
      {
        if (radio_reply_)
          {
            disconnect (radio_reply_, nullptr, this, nullptr);
            radio_reply_->abort ();
            radio_reply_->deleteLater ();
            radio_reply_ = nullptr;
          }
        radio_attempt_.invalidate ();
        return;
      }
    if (radio_reply_)
      {
        return;
      }

    auto const base_url = Cloudlog::normalizeBaseUrl (config_->cloudlog_api_url ());
    auto const api_key = config_->cloudlog_api_key ();
    if (base_url.isEmpty () || api_key.isEmpty ())
      {
        return;
      }
    auto const v2 = usesApiV2 (api_key);
    QJsonObject object;
    object.insert (QStringLiteral ("radio"), config_->cloudlog_radio_name ());
    object.insert (QStringLiteral ("frequency"), static_cast<qint64> (state.txFrequency));
    object.insert (QStringLiteral ("frequency_rx"), static_cast<qint64> (state.rxFrequency));
    object.insert (QStringLiteral ("mode"), state.mode);
    object.insert (QStringLiteral ("mode_rx"), state.mode);
    if (!v2)
      {
        object.insert (QStringLiteral ("key"), api_key);
      }
    auto const payload = QJsonDocument {object}.toJson (QJsonDocument::Compact);
    if (radio_attempt_.isValid () && base_url == radio_base_url_ && api_key == radio_api_key_)
      {
        auto const elapsed = radio_attempt_.elapsed ();
        if (elapsed < (radio_succeeded_ ? 1000 : 30000)
            || (radio_succeeded_ && payload == radio_payload_ && elapsed < 60000))
          {
            return;
          }
      }

    radio_base_url_ = base_url;
    radio_api_key_ = api_key;
    radio_payload_ = payload;
    radio_attempt_.start ();
    radio_succeeded_ = false;
    QString http_fallback;
    auto const preferred_url = httpsFirstBaseUrl (base_url, http_fallback);
    startRadioUpload (preferred_url, payload, http_fallback, v2, api_key);
  }

private:
  using Layouts = std::array<EndpointLayout, 2>;

  void startRadioUpload (QString const& baseUrl, QByteArray const& payload, QString const& httpFallback,
                         bool v2, QString const& apiKey)
  {
    auto const url = v2 ? v2EndpointUrl (baseUrl, QStringLiteral ("radio"))
      : QUrl {baseUrl + endpointPath (uploadLayoutCache ().value (baseUrl, EndpointLayout::IndexPhpApi),
                                    QStringLiteral ("radio"))};
    auto const request = v2 ? v2PostRequest (url, apiKey) : jsonPostRequest (url);
    radio_reply_ = network_manager_->post (request, payload);
    auto * reply = radio_reply_.data ();
    QTimer::singleShot (10000, reply, [reply] { reply->abort (); });
    connect (reply, &QNetworkReply::finished, this, [this, reply, payload, httpFallback, v2, apiKey] {
      radio_reply_ = nullptr;
      if (!httpFallback.isEmpty () && canRetryUploadOverHttp (*reply))
        {
          reply->deleteLater ();
          startRadioUpload (httpFallback, payload, {}, v2, apiKey);
          return;
        }

      auto const status = httpStatus (*reply);
      Cloudlog::UploadResult result;
      result.httpStatusCode = status;
      auto const object = QJsonDocument::fromJson (reply->readAll ()).object ();
      auto const valid_response = v2
        ? !object.value (QStringLiteral ("data")).toObject ().value (QStringLiteral ("radio")).toString ().isEmpty ()
        : lowerString (object, "status") == QLatin1String ("success");
      if (reply->error () == QNetworkReply::NoError && (status == 200 || status == 201)
          && valid_response)
        {
          result.status = Cloudlog::UploadStatus::Success;
          radio_succeeded_ = true;
        }
      else
        {
          result.status = !status ? Cloudlog::UploadStatus::NetworkError
            : status >= 400 ? Cloudlog::UploadStatus::HttpError : Cloudlog::UploadStatus::UnexpectedResponse;
          result.message = tr ("Live radio status could not be sent to Wavelog.");
          auto const fallback = reply->error () != QNetworkReply::NoError ? reply->errorString ()
            : tr ("HTTP status %1 did not include a valid radio status response.").arg (status);
          result.detail = v2 ? v2FailureDetail (object, fallback) : responseFailureDetail (object, fallback);
        }
      Q_EMIT self_->radio_status_finished (result);
      reply->deleteLater ();
    });
  }

  void startConnectionCheck ()
  {
    if (ApiVersion::V2 == check_api_version_)
      {
        startV2Status ();
      }
    else
      {
        startJsonAuth ();
      }
  }

  void startV2Status ()
  {
    auto request = connectionCheckGetRequest (
      v2EndpointUrl (check_request_.url, QStringLiteral ("status")), true);
    check_reply_ = network_manager_->get (request);
    connect (check_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_v2_status);
  }

  void reply_v2_status ()
  {
    auto * reply = takeCheckReply ();
    if (!reply)
      {
        return;
      }

    if (isTransportError (*reply))
      {
        auto const detail = reply->errorString ();
        reply->deleteLater ();
        finishNetworkError (detail);
        return;
      }

    auto const status_code = httpStatus (*reply);
    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (reply->readAll (), &error);
    reply->deleteLater ();
    auto const data = document.isObject ()
      ? document.object ().value (QStringLiteral ("data")).toObject () : QJsonObject {};
    if (status_code != 200 || lowerString (data, "name") != QLatin1String ("wavelog api")
        || lowerString (data, "status") != QLatin1String ("ok"))
      {
        finish ({Cloudlog::ConnectionCheckStatus::EndpointUnavailable,
                 tr ("Wavelog API v2 was not found."),
                 tr ("A wl2_ token requires Wavelog 3.1.0 or later."),
                 check_request_.url});
        return;
      }

    startV2Token ();
  }

  void startV2Token ()
  {
    auto request = v2GetRequest (
      v2EndpointUrl (check_request_.url, QStringLiteral ("token")), check_request_.apiKey);
    check_reply_ = network_manager_->get (request);
    connect (check_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_v2_token);
  }

  void reply_v2_token ()
  {
    auto * reply = takeCheckReply ();
    if (!reply)
      {
        return;
      }

    if (isTransportError (*reply))
      {
        auto const detail = reply->errorString ();
        reply->deleteLater ();
        finishNetworkError (detail);
        return;
      }

    auto const status_code = httpStatus (*reply);
    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (reply->readAll (), &error);
    reply->deleteLater ();
    auto const object = document.isObject () ? document.object () : QJsonObject {};
    if (status_code == 401)
      {
        finish ({Cloudlog::ConnectionCheckStatus::InvalidKey,
                 tr ("Wavelog API v2 token is invalid."),
                 v2FailureDetail (object, tr ("The token was rejected.")),
                 check_request_.url});
        return;
      }
    if (status_code != 200 || !document.isObject ())
      {
        finish ({Cloudlog::ConnectionCheckStatus::UnexpectedResponse,
                 tr ("Wavelog API v2 token response was not usable."),
                 v2FailureDetail (object, error.errorString ()),
                 check_request_.url});
        return;
      }

    QStringList scopes;
    for (auto const& value : object.value (QStringLiteral ("data")).toObject ().value (QStringLiteral ("scopes")).toArray ())
      {
        scopes.append (value.toString ());
      }
    QStringList missing_scopes;
    for (auto const& required : {QStringLiteral ("qso:write"), QStringLiteral ("station:read")})
      {
        if (check_request_.qsoEnabled && !scopes.contains (required))
          {
            missing_scopes.append (required);
          }
      }
    if (check_request_.radioEnabled && !scopes.contains (QStringLiteral ("radio:write")))
      {
        missing_scopes.append (QStringLiteral ("radio:write"));
      }
    if (!missing_scopes.isEmpty ())
      {
        finish ({Cloudlog::ConnectionCheckStatus::InsufficientScope,
                 tr ("Wavelog API v2 token is missing required scopes."),
                 tr ("Required scopes: %1").arg (missing_scopes.join (QStringLiteral (", "))),
                 check_request_.url});
        return;
      }

    if (check_request_.qsoEnabled)
      {
        startV2Station ();
      }
    else
      {
        finish ({Cloudlog::ConnectionCheckStatus::Success,
                 tr ("Connection check passed."),
                 tr ("Wavelog API v2 token has the required live radio reporting scope."),
                 check_request_.url});
      }
  }

  void startV2Station ()
  {
    auto request = v2GetRequest (
      v2EndpointUrl (check_request_.url, QStringLiteral ("station")), check_request_.apiKey);
    check_reply_ = network_manager_->get (request);
    connect (check_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_v2_station);
  }

  void reply_v2_station ()
  {
    auto * reply = takeCheckReply ();
    if (!reply)
      {
        return;
      }

    if (isTransportError (*reply))
      {
        auto const detail = reply->errorString ();
        reply->deleteLater ();
        finishNetworkError (detail);
        return;
      }

    auto const status_code = httpStatus (*reply);
    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (reply->readAll (), &error);
    reply->deleteLater ();
    auto const object = document.isObject () ? document.object () : QJsonObject {};
    if (status_code == 401)
      {
        finish ({Cloudlog::ConnectionCheckStatus::InvalidKey,
                 tr ("Wavelog API v2 token is invalid."),
                 v2FailureDetail (object, tr ("The token was rejected.")),
                 check_request_.url});
        return;
      }
    if (status_code != 200 || !document.isObject ())
      {
        finish ({status_code == 403 ? Cloudlog::ConnectionCheckStatus::InsufficientScope
                                    : Cloudlog::ConnectionCheckStatus::UnexpectedResponse,
                 tr ("Wavelog API v2 station response was not usable."),
                 v2FailureDetail (object, error.errorString ()),
                 check_request_.url});
        return;
      }

    QList<Cloudlog::StationProfile> profiles;
    collectStationProfiles (object.value (QStringLiteral ("data")), profiles);
    checked_station_profiles_ = profiles;
    startV2DryRun ();
  }

  void startV2DryRun ()
  {
    auto const data = v2QsoPayload (check_request_.stationId, dryRunAdif (), true);
    auto request = v2PostRequest (
      v2EndpointUrl (check_request_.url, QStringLiteral ("qso")), check_request_.apiKey);
    check_reply_ = network_manager_->post (request, data);
    connect (check_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_v2_dry_run);
  }

  void reply_v2_dry_run ()
  {
    auto * reply = takeCheckReply ();
    if (!reply)
      {
        return;
      }

    if (isTransportError (*reply))
      {
        auto const detail = reply->errorString ();
        reply->deleteLater ();
        finishNetworkError (detail);
        return;
      }

    auto const status_code = httpStatus (*reply);
    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (reply->readAll (), &error);
    reply->deleteLater ();
    auto const object = document.isObject () ? document.object () : QJsonObject {};
    auto const data = object.value (QStringLiteral ("data")).toObject ();
    if (status_code == 200 && data.value (QStringLiteral ("dryrun")).toBool ()
        && intValue (data.value (QStringLiteral ("parsed"))) > 0)
      {
        finish ({Cloudlog::ConnectionCheckStatus::Success,
                 tr ("Connection check passed."),
                 tr ("Wavelog API v2 token, station profile, and dry-run QSO upload were validated."),
                 check_request_.url});
        return;
      }

    auto const error_code = lowerString (object.value (QStringLiteral ("error")).toObject (), "code");
    auto const detail = v2FailureDetail (object, tr ("The API v2 QSO dry run was rejected."));
    if (status_code == 401)
      {
        finish ({Cloudlog::ConnectionCheckStatus::InvalidKey,
                 tr ("Wavelog API v2 token is invalid."), detail, check_request_.url});
      }
    else if (status_code == 403 && error_code == QLatin1String ("insufficient_scope"))
      {
        finish ({Cloudlog::ConnectionCheckStatus::InsufficientScope,
                 tr ("Wavelog API v2 token is missing required scopes."), detail, check_request_.url});
      }
    else if (status_code == 403 || status_code == 404)
      {
        finish ({Cloudlog::ConnectionCheckStatus::StationProfileUnavailable,
                 tr ("Configured station profile was not available to the API token."),
                 detail, check_request_.url});
      }
    else if (status_code == 400)
      {
        finish ({Cloudlog::ConnectionCheckStatus::UploadShapeRejected,
                 tr ("Wavelog API v2 dry-run upload was rejected."), detail, check_request_.url});
      }
    else
      {
        finish ({Cloudlog::ConnectionCheckStatus::UnexpectedResponse,
                 tr ("Wavelog API v2 dry-run response was not usable."),
                 detail, check_request_.url});
      }
  }

  void startJsonAuth ()
  {
    static Layouts const layouts {EndpointLayout::DirectApi, EndpointLayout::IndexPhpApi};
    if (auth_layout_index_ >= layouts.size ())
      {
        startXmlAuth ();
        return;
      }

    validated_auth_layout_ = layouts[auth_layout_index_++];
    auto request = connectionCheckGetRequest (
      endpointUrl (check_request_.url, validated_auth_layout_, QStringLiteral ("check_auth"), check_request_.apiKey), true);
    check_reply_ = network_manager_->get (request);
    connect (check_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_json_auth);
  }

  void reply_json_auth ()
  {
    auto * reply = takeCheckReply ();
    if (!reply)
      {
        return;
      }

    if (isTransportError (*reply))
      {
        auto const detail = reply->errorString ();
        reply->deleteLater ();
        finishNetworkError (detail);
        return;
      }

    auto const result = parseJsonAuth (reply->readAll (), httpStatus (*reply));
    reply->deleteLater ();
    handleAuthResult (result, [this] { startJsonAuth (); });
  }

  void startXmlAuth ()
  {
    validated_auth_layout_ = EndpointLayout::IndexPhpApi;
    auto request = connectionCheckGetRequest (
      endpointUrl (check_request_.url, validated_auth_layout_, QStringLiteral ("auth"), check_request_.apiKey), false);
    check_reply_ = network_manager_->get (request);
    connect (check_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_xml_auth);
  }

  void reply_xml_auth ()
  {
    auto * reply = takeCheckReply ();
    if (!reply)
      {
        return;
      }

    if (isTransportError (*reply))
      {
        auto const detail = reply->errorString ();
        reply->deleteLater ();
        finishNetworkError (detail);
        return;
      }

    auto const result = parseXmlAuth (reply->readAll ());
    reply->deleteLater ();
    handleAuthResult (result, [this] {
      finish ({Cloudlog::ConnectionCheckStatus::EndpointUnavailable,
               tr ("Cloudlog or Wavelog authentication endpoint was not found."),
               tr ("Tried /api/check_auth, /index.php/api/check_auth, and /index.php/api/auth."),
               check_request_.url});
    });
  }

  template<typename Continue>
  void handleAuthResult (AuthResult const& result, Continue continueProbe)
  {
    switch (result.status)
      {
      case AuthStatus::Writable:
        if (check_request_.qsoEnabled)
          {
            startStationInfo ();
          }
        else
          {
            finish ({Cloudlog::ConnectionCheckStatus::Success,
                     tr ("Connection check passed."),
                     tr ("API key is writable for live radio reporting."),
                     check_request_.url});
          }
        break;
      case AuthStatus::ReadOnly:
        finish ({Cloudlog::ConnectionCheckStatus::ReadOnlyKey,
                 tr ("API key is valid but read-only."),
                 tr ("Generate a read/write API key before enabling uploads."),
                 check_request_.url});
        break;
      case AuthStatus::Invalid:
        finish ({Cloudlog::ConnectionCheckStatus::InvalidKey,
                 tr ("API key is invalid."),
                 result.detail,
                 check_request_.url});
        break;
      case AuthStatus::Unexpected:
        finish ({Cloudlog::ConnectionCheckStatus::UnexpectedResponse,
                 tr ("Authentication response was not usable."),
                 result.detail,
                 check_request_.url});
        break;
      case AuthStatus::Unavailable:
        continueProbe ();
        break;
      }
  }

  void startStationInfo ()
  {
    auto request = connectionCheckGetRequest (
      endpointUrl (check_request_.url, validated_auth_layout_, QStringLiteral ("station_info"), check_request_.apiKey), true);
    check_reply_ = network_manager_->get (request);
    connect (check_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_station_info);
  }

  void reply_station_info ()
  {
    auto * reply = takeCheckReply ();
    if (!reply)
      {
        return;
      }

    if (isTransportError (*reply))
      {
        auto const detail = reply->errorString ();
        reply->deleteLater ();
        finishNetworkError (detail);
        return;
      }

    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (reply->readAll (), &error);
    reply->deleteLater ();
    if (QJsonParseError::NoError != error.error)
      {
        finish ({Cloudlog::ConnectionCheckStatus::UnexpectedResponse,
                 tr ("Station profile response was not JSON."),
                 error.errorString (),
                 check_request_.url});
        return;
      }

    QList<Cloudlog::StationProfile> profiles;
    collectStationProfiles (document.isArray () ? QJsonValue {document.array ()} : QJsonValue {document.object ()}, profiles);
    checked_station_profiles_ = profiles;
    auto const match = std::find_if (profiles.cbegin (), profiles.cend (), [this] (Cloudlog::StationProfile const& profile) {
      return profile.id == check_request_.stationId;
    });

    if (match == profiles.cend ())
      {
        auto const available = stationProfileList (profiles);
        finish ({Cloudlog::ConnectionCheckStatus::StationProfileUnavailable,
                 tr ("Configured station profile was not found."),
                 available.isEmpty ()
                   ? tr ("No station profiles were returned by the server.")
                   : tr ("Available station profiles: %1").arg (available),
                 check_request_.url});
        return;
      }

    startProductProbe ();
  }

  void startProductProbe ()
  {
    auto request = connectionCheckGetRequest (QUrl {check_request_.url + QStringLiteral ("/manifest.json")}, true);
    check_reply_ = network_manager_->get (request);
    connect (check_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_product_probe);
  }

  void reply_product_probe ()
  {
    auto * reply = takeCheckReply ();
    if (!reply)
      {
        return;
      }

    auto const status_code = httpStatus (*reply);
    auto const body = reply->readAll ();
    auto const has_transport_error = isTransportError (*reply);
    reply->deleteLater ();

    if (!has_transport_error && status_code == 200 && ServerProduct::Wavelog == parseProductManifest (body))
      {
        startDryRun ();
        return;
      }

    finishWithoutDryRun ();
  }

  void startDryRun ()
  {
    static Layouts const layouts {EndpointLayout::DirectApi, EndpointLayout::IndexPhpApi};
    if (dry_run_layout_index_ >= layouts.size ())
      {
        finish ({Cloudlog::ConnectionCheckStatus::EndpointUnavailable,
                 tr ("Wavelog dry-run QSO endpoint was not found."),
                 tr ("Tried /api/qso/true and /index.php/api/qso/true after Wavelog was identified."),
                 check_request_.url});
        return;
      }

    validated_dry_run_layout_ = layouts[dry_run_layout_index_++];
    auto const data = Cloudlog::qsoUploadPayload (check_request_.apiKey, check_request_.stationId, dryRunAdif ());
    auto request = jsonPostRequest (QUrl {check_request_.url + endpointPath (validated_dry_run_layout_, QStringLiteral ("qso/true"))});
    check_reply_ = network_manager_->post (request, data);
    connect (check_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_dry_run);
  }

  void reply_dry_run ()
  {
    auto * reply = takeCheckReply ();
    if (!reply)
      {
        return;
      }

    if (isTransportError (*reply))
      {
        auto const detail = reply->errorString ();
        reply->deleteLater ();
        finishNetworkError (detail);
        return;
      }

    auto const status_code = httpStatus (*reply);
    auto const body = reply->readAll ();
    reply->deleteLater ();
    if (status_code == 404 || status_code == 405)
      {
        startDryRun ();
        return;
      }

    QJsonParseError error;
    auto const document = QJsonDocument::fromJson (body, &error);
    if (QJsonParseError::NoError != error.error || !document.isObject ())
      {
        finish ({Cloudlog::ConnectionCheckStatus::UnexpectedResponse,
                 tr ("Dry-run upload response was not usable."),
                 error.errorString (),
                 check_request_.url});
        return;
      }

    auto const object = document.object ();
    auto const status = lowerString (object, "status");

    if (status_code == 201 && status == QLatin1String ("created"))
      {
        uploadLayoutCache ().insert (check_request_.url, validated_dry_run_layout_);
        finish ({Cloudlog::ConnectionCheckStatus::Success,
                 tr ("Connection check passed."),
                 tr ("Writable API key, station profile, and dry-run QSO upload were validated."),
                 check_request_.url});
        return;
      }

    if (status_code == 400 || status_code == 401 || status_code == 403)
      {
        finish ({Cloudlog::ConnectionCheckStatus::UploadShapeRejected,
                 tr ("Dry-run QSO upload was rejected."),
                 responseFailureDetail (object, tr ("The QSO dry-run upload was rejected.")),
                 check_request_.url});
        return;
      }

    finish ({Cloudlog::ConnectionCheckStatus::UnexpectedResponse,
             tr ("Dry-run QSO upload returned an unexpected response."),
             responseFailureDetail (object, tr ("The QSO dry-run upload was rejected.")),
             check_request_.url});
  }

  void finishWithoutDryRun ()
  {
    uploadLayoutCache ().insert (check_request_.url, validated_auth_layout_);
    finish ({Cloudlog::ConnectionCheckStatus::Success,
             tr ("Connection check passed."),
             tr ("Writable API key and station profile were validated. No safe dry-run QSO endpoint was identified, so no test QSO was uploaded."),
             check_request_.url});
  }

  QByteArray dryRunAdif () const
  {
    auto const today = QDate::currentDate ().toString (QStringLiteral ("yyyyMMdd")).toLatin1 ();
    return QByteArray {"<call:5>WSJTX<qso_date:8>"} + today
      + QByteArray {"<time_on:6>000000<band:3>20m<mode:3>FT8"};
  }

  void finishNetworkError (QString const& detail)
  {
    if (retryCheckOverHttp ())
      {
        return;
      }

    finish ({Cloudlog::ConnectionCheckStatus::NetworkError,
             tr ("Network error while checking Cloudlog or Wavelog."),
             detail,
             check_request_.url});
  }

  void finish (Cloudlog::ConnectionCheckResult const& result)
  {
    check_reply_ = nullptr;
    auto result_with_profiles = result;
    if (check_using_http_fallback_ && Cloudlog::ConnectionCheckStatus::Success == result_with_profiles.status)
      {
        result_with_profiles.detail += tr (" HTTPS was unavailable; connected over HTTP.");
      }
    if (result_with_profiles.stationProfiles.isEmpty ())
      {
        result_with_profiles.stationProfiles = checked_station_profiles_;
      }
    Q_EMIT self_->connection_check_finished (result_with_profiles);
  }

  void showUploadWarning (Cloudlog::UploadResult const& result)
  {
    if (Cloudlog::UploadStatus::Success == result.status || !qobject_cast<QApplication *> (QCoreApplication::instance ()))
      {
        return;
      }

    QMessageBox msgBox;
    msgBox.setIcon (QMessageBox::Warning);
    msgBox.setWindowTitle (tr ("Cloudlog Error!"));
    msgBox.setText (result.message);
    if (!result.detail.isEmpty ())
      {
        msgBox.setDetailedText (tr ("Reason: %1").arg (result.detail));
      }
    msgBox.exec ();
  }

  void reply_logqso ()
  {
    auto * reply = qobject_cast<QNetworkReply *> (sender ());
    if (!reply)
      {
        return;
      }

    if (reply == upload_reply_)
      {
        upload_reply_ = nullptr;
      }

    auto const http_fallback = reply->property ("cloudlogHttpFallback").toString ();
    auto const api_version = static_cast<ApiVersion> (reply->property ("cloudlogApiVersion").toInt ());
    if (!http_fallback.isEmpty () && canRetryUploadOverHttp (*reply))
      {
        auto const data = reply->property ("cloudlogUploadPayload").toByteArray ();
        auto const api_key = reply->property ("cloudlogApiKey").toString ();
        reply->deleteLater ();
        startQsoUpload (http_fallback, data, {}, api_version, api_key);
        return;
      }

    auto const result = ApiVersion::V2 == api_version
      ? classifyV2QsoUploadReply (httpStatus (*reply), reply->error (), reply->errorString (), reply->readAll ())
      : classifyQsoUploadReply (httpStatus (*reply), reply->error (), reply->errorString (), reply->readAll ());
    Q_EMIT self_->qso_upload_finished (result);
    showUploadWarning (result);
    reply->deleteLater ();
  }

  QNetworkReply * takeCheckReply ()
  {
    auto * reply = qobject_cast<QNetworkReply *> (sender ());
    if (!reply || reply != check_reply_)
      {
        return nullptr;
      }
    check_reply_ = nullptr;
    return reply;
  }

  void abortCheckReply ()
  {
    if (check_reply_)
      {
        auto * reply = check_reply_.data ();
        check_reply_ = nullptr;
        if (reply->isRunning ())
          {
            reply->abort ();
          }
        reply->deleteLater ();
      }
  }

  bool retryCheckOverHttp ()
  {
    if (check_http_fallback_url_.isEmpty () || check_using_http_fallback_)
      {
        return false;
      }

    check_request_.url = check_http_fallback_url_;
    check_http_fallback_url_.clear ();
    check_using_http_fallback_ = true;
    auth_layout_index_ = 0;
    dry_run_layout_index_ = 0;
    checked_station_profiles_.clear ();
    startConnectionCheck ();
    return true;
  }

  Cloudlog * self_;
  CloudlogConfiguration const * config_;
  QNetworkAccessManager * network_manager_;
  QPointer<QNetworkReply> upload_reply_;
  QPointer<QNetworkReply> check_reply_;
  QPointer<QNetworkReply> radio_reply_;
  QElapsedTimer radio_attempt_;
  QString radio_base_url_;
  QString radio_api_key_;
  QByteArray radio_payload_;
  bool radio_succeeded_ {false};
  Cloudlog::ConnectionCheckRequest check_request_;
  ApiVersion check_api_version_ {ApiVersion::Legacy};
  EndpointLayout validated_auth_layout_ {EndpointLayout::IndexPhpApi};
  EndpointLayout validated_dry_run_layout_ {EndpointLayout::IndexPhpApi};
  std::size_t auth_layout_index_ {0};
  std::size_t dry_run_layout_index_ {0};
  QList<Cloudlog::StationProfile> checked_station_profiles_;
  QString check_http_fallback_url_;
  bool check_using_http_fallback_ {false};
};

#include "Cloudlog.moc"

QString Cloudlog::normalizeBaseUrl (QString const& url)
{
  auto result = url.trimmed ();
  while (result.endsWith (QLatin1Char ('/')))
    {
      result.chop (1);
    }
  for (auto const& endpoint : {QStringLiteral ("/index.php/api/v2/qso"), QStringLiteral ("/api/v2/qso"),
                               QStringLiteral ("/index.php/api/qso"), QStringLiteral ("/api/qso")})
    {
      if (result.endsWith (endpoint))
        {
          result.chop (endpoint.size ());
          break;
        }
    }
  while (result.endsWith (QLatin1Char ('/')))
    {
      result.chop (1);
    }
  return result;
}

QByteArray Cloudlog::qsoUploadPayload (QString const& apiKey, qint32 stationId, QByteArray const& ADIF)
{
  QJsonObject object;
  object.insert (QStringLiteral ("key"), apiKey);
  object.insert (QStringLiteral ("station_profile_id"), QString::number (stationId));
  object.insert (QStringLiteral ("type"), QStringLiteral ("adif"));
  object.insert (QStringLiteral ("string"), QString::fromUtf8 (ADIF + QByteArray {"<eor>"}));
  return QJsonDocument {object}.toJson (QJsonDocument::Compact);
}

QString Cloudlog::stationProfileDisplayText (StationProfile const& profile)
{
  QStringList fields {QString::number (profile.id)};
  if (!profile.name.isEmpty ())
    {
      fields.append (profile.name);
    }
  if (!profile.callsign.isEmpty ())
    {
      fields.append (profile.callsign);
    }
  return fields.join (QStringLiteral (" - "));
}

Cloudlog::Cloudlog (CloudlogConfiguration const * config, QNetworkAccessManager * network_manager, QObject * parent)
  : QObject {parent}
  , m_ {this, config, network_manager}
{
  qRegisterMetaType<Cloudlog::ConnectionCheckResult> ("Cloudlog::ConnectionCheckResult");
  qRegisterMetaType<Cloudlog::ConnectionCheckResult> ("ConnectionCheckResult");
  qRegisterMetaType<Cloudlog::StationProfile> ("Cloudlog::StationProfile");
  qRegisterMetaType<Cloudlog::UploadResult> ("Cloudlog::UploadResult");
  qRegisterMetaType<Cloudlog::UploadResult> ("UploadResult");
}

Cloudlog::~Cloudlog ()
{
}

void Cloudlog::checkConnection (ConnectionCheckRequest const& request)
{
  m_->checkConnection (request);
}

void Cloudlog::logQso (QByteArray ADIF)
{
  m_->logQso (ADIF);
}

void Cloudlog::updateRadio (RadioState const& state)
{
  m_->updateRadio (state);
}
