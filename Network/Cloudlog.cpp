#include "Cloudlog.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>

#include <QApplication>
#include <QDate>
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

  QHash<QString, EndpointLayout>& uploadLayoutCache ()
  {
    static QHash<QString, EndpointLayout> cache;
    return cache;
  }

  void applyCommonHeaders (QNetworkRequest& request, bool acceptJson)
  {
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

  bool isTransportError (QNetworkReply const& reply)
  {
    return QNetworkReply::NoError != reply.error ()
      && !reply.attribute (QNetworkRequest::HttpStatusCodeAttribute).isValid ();
  }

  int httpStatus (QNetworkReply const& reply)
  {
    return reply.attribute (QNetworkRequest::HttpStatusCodeAttribute).toInt ();
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

    auto const base_url = Cloudlog::normalizeBaseUrl (config_->cloudlog_api_url ());
    auto const data = Cloudlog::qsoUploadPayload (config_->cloudlog_api_key (), config_->cloudlog_api_station_id (), ADIF);
    auto const layout = uploadLayoutCache ().value (base_url, EndpointLayout::IndexPhpApi);
    auto const path = EndpointLayout::DirectApi == layout ? QStringLiteral ("/api/qso") : QStringLiteral ("/index.php/api/qso");
    auto const u = QUrl {base_url + path};

    auto request = jsonPostRequest (u);
    upload_reply_ = network_manager_->post (request, data);
    connect (upload_reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_logqso);
  }

  void checkConnection (Cloudlog::ConnectionCheckRequest const& request)
  {
    abortCheckReply ();
    check_request_ = request;
    check_request_.url = Cloudlog::normalizeBaseUrl (request.url);
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

    startJsonAuth ();
  }

private:
  using Layouts = std::array<EndpointLayout, 2>;

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
        startStationInfo ();
        break;
      case AuthStatus::ReadOnly:
        finish ({Cloudlog::ConnectionCheckStatus::ReadOnlyKey,
                 tr ("API key is valid but read-only."),
                 tr ("Generate a read/write API key before enabling QSO uploads."),
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
    finish ({Cloudlog::ConnectionCheckStatus::NetworkError,
             tr ("Network error while checking Cloudlog or Wavelog."),
             detail,
             check_request_.url});
  }

  void finish (Cloudlog::ConnectionCheckResult const& result)
  {
    check_reply_ = nullptr;
    auto result_with_profiles = result;
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

    auto const result = classifyQsoUploadReply (httpStatus (*reply), reply->error (), reply->errorString (), reply->readAll ());
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

  Cloudlog * self_;
  CloudlogConfiguration const * config_;
  QNetworkAccessManager * network_manager_;
  QPointer<QNetworkReply> upload_reply_;
  QPointer<QNetworkReply> check_reply_;
  Cloudlog::ConnectionCheckRequest check_request_;
  EndpointLayout validated_auth_layout_ {EndpointLayout::IndexPhpApi};
  EndpointLayout validated_dry_run_layout_ {EndpointLayout::IndexPhpApi};
  std::size_t auth_layout_index_ {0};
  std::size_t dry_run_layout_index_ {0};
  QList<Cloudlog::StationProfile> checked_station_profiles_;
};

#include "Cloudlog.moc"

QString Cloudlog::normalizeBaseUrl (QString const& url)
{
  auto result = url.trimmed ();
  while (result.endsWith (QLatin1Char ('/')))
    {
      result.chop (1);
    }
  for (auto const& endpoint : {QStringLiteral ("/index.php/api/qso"), QStringLiteral ("/api/qso")})
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
