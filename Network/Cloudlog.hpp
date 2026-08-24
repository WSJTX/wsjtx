#ifndef CLOUDLOG_HPP_
#define CLOUDLOG_HPP_

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include "CloudlogConfiguration.hpp"
#include "pimpl_h.hpp"

class QNetworkAccessManager;

//
// Cloudlog
//
class Cloudlog final
  : public QObject
{
  Q_OBJECT

public:
  enum class ConnectionCheckStatus
  {
    Success,
    ReadOnlyKey,
    InvalidKey,
    StationProfileUnavailable,
    EndpointUnavailable,
    UploadShapeRejected,
    NetworkError,
    UnexpectedResponse
  };
  Q_ENUM (ConnectionCheckStatus)

  enum class UploadStatus
  {
    Success,
    Rejected,
    NetworkError,
    HttpError,
    UnexpectedResponse
  };
  Q_ENUM (UploadStatus)

  struct ConnectionCheckRequest
  {
    ConnectionCheckRequest () = default;
    ConnectionCheckRequest (QString const& url, QString const& apiKey, qint32 stationId)
      : url {url}
      , apiKey {apiKey}
      , stationId {stationId}
    {
    }

    QString url;
    QString apiKey;
    qint32 stationId {0};
  };

  struct StationProfile
  {
    StationProfile () = default;
    StationProfile (int id, QString const& name, QString const& callsign)
      : id {id}
      , name {name}
      , callsign {callsign}
    {
    }

    int id {0};
    QString name;
    QString callsign;
  };

  struct ConnectionCheckResult
  {
    ConnectionCheckResult () = default;
    ConnectionCheckResult (ConnectionCheckStatus status, QString const& message, QString const& detail,
                           QString const& normalizedBaseUrl, QList<StationProfile> const& stationProfiles = {})
      : status {status}
      , message {message}
      , detail {detail}
      , normalizedBaseUrl {normalizedBaseUrl}
      , stationProfiles {stationProfiles}
    {
    }

    ConnectionCheckStatus status {ConnectionCheckStatus::UnexpectedResponse};
    QString message;
    QString detail;
    QString normalizedBaseUrl;
    QList<StationProfile> stationProfiles;
  };

  struct UploadResult
  {
    UploadResult () = default;
    UploadResult (UploadStatus status, QString const& message, QString const& detail, int httpStatusCode)
      : status {status}
      , message {message}
      , detail {detail}
      , httpStatusCode {httpStatusCode}
    {
    }

    UploadStatus status {UploadStatus::UnexpectedResponse};
    QString message;
    QString detail;
    int httpStatusCode {0};
  };

  explicit Cloudlog (CloudlogConfiguration const * config, QNetworkAccessManager * network_manager,
                     QObject * parent = nullptr);
  ~Cloudlog ();

  static QString normalizeBaseUrl (QString const& url);
  static QString stationProfileDisplayText (StationProfile const& profile);
  static QByteArray qsoUploadPayload (QString const& apiKey, qint32 stationId, QByteArray const& ADIF);

  void logQso (QByteArray ADIF);
  Q_SLOT void checkConnection (ConnectionCheckRequest const& request);

  Q_SIGNAL void connection_check_finished (ConnectionCheckResult const& result) const;
  Q_SIGNAL void qso_upload_finished (UploadResult const& result) const;

private:
  class impl;
  pimpl<impl> m_;
};

Q_DECLARE_METATYPE (Cloudlog::ConnectionCheckResult)
Q_DECLARE_METATYPE (Cloudlog::StationProfile)
Q_DECLARE_METATYPE (Cloudlog::UploadStatus)
Q_DECLARE_METATYPE (Cloudlog::UploadResult)

#endif
