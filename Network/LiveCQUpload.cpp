#include "LiveCQUpload.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace
{
  class QNetworkAccessManagerTransport final
    : public QObject
    , public LiveCQUpload::Transport
  {
  public:
    explicit QNetworkAccessManagerTransport (QObject *parent = nullptr)
      : QObject {parent}
    {
    }

    QNetworkReply *post (QNetworkRequest const& request, QByteArray const& body) override
    {
      return manager_.post (request, body);
    }

  private:
    QNetworkAccessManager manager_;
  };
}

LiveCQUpload::LiveCQUpload (QObject *parent)
  : LiveCQUpload {new QNetworkAccessManagerTransport, true, parent}
{
}

LiveCQUpload::LiveCQUpload (Transport *transport, bool take_transport_ownership,
                            QObject *parent)
  : QObject {parent}
  , transport_ {transport}
  , owns_transport_ {take_transport_ownership}
{
}

LiveCQUpload::~LiveCQUpload ()
{
  abortOutstandingRequests ();
  if (owns_transport_)
    {
      delete transport_;
    }
}

bool LiveCQUpload::setEndpoint (QUrl const& endpoint)
{
  if (!endpoint.isValid ()
      || endpoint.host ().isEmpty ()
      || endpoint.scheme ().compare ("https", Qt::CaseInsensitive) != 0)
    {
      Q_EMIT errorOccurred (
        QString {"LiveCQ endpoint must be a valid https URL: %1"}
          .arg (endpoint.toString ()));
      return false;
    }
  endpoint_ = endpoint;
  return true;
}

void LiveCQUpload::postSpot (QUrlQuery const& query)
{
  if (!endpoint_.isValid ())
    {
      Q_EMIT errorOccurred (QStringLiteral ("LiveCQ endpoint is not configured"));
      return;
    }

  QNetworkRequest request {endpoint_};
  request.setAttribute (QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::ManualRedirectPolicy);
  request.setHeader (QNetworkRequest::ContentTypeHeader,
                     "application/x-www-form-urlencoded");
  if (!user_agent_.isEmpty ())
    {
      request.setRawHeader ("User-Agent", user_agent_);
    }

  auto *reply = transport_->post (request, query.query (QUrl::FullyEncoded).toUtf8 ());
  outstanding_.insert (reply);
  connect (reply, &QNetworkReply::finished, this,
           [this, reply] () { handleReply (reply); });
}

void LiveCQUpload::abortOutstandingRequests ()
{
  auto const replies = outstanding_;
  for (auto *reply : replies)
    {
      if (reply)
        {
          disconnect (reply, nullptr, this, nullptr);
          reply->abort ();
        }
    }
  outstanding_.clear ();
}

void LiveCQUpload::handleReply (QNetworkReply *reply)
{
  outstanding_.remove (reply);
  int const http_status
    = reply->attribute (QNetworkRequest::HttpStatusCodeAttribute).toInt ();
  if (reply->error () != QNetworkReply::NoError)
    {
      Q_EMIT errorOccurred (QString {"LiveCQ upload failed: %1"}.arg (reply->errorString ()));
    }
  else if (http_status < 200 || http_status >= 300)
    {
      Q_EMIT errorOccurred (QString {"LiveCQ upload failed: HTTP %1"}.arg (http_status));
    }
  else
    {
      Q_EMIT spotPosted ();
    }
  reply->deleteLater ();
}
