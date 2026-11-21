#include "HeardByRecords.hpp"

#include <algorithm>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

HeardByRecords::HeardByRecords (QNetworkAccessManager * network, QObject * parent,
                                QString const& base_url, int min_interval_ms)
  : QObject {parent}
  , network_ {network}
  , base_url_ {base_url}
  , min_interval_ms_ {min_interval_ms}
{
  qRegisterMetaType<Result> ();
}

int HeardByRecords::ms_until_next () const
{
  if (!last_ms_) return 0;
  auto const left = min_interval_ms_ - (QDateTime::currentMSecsSinceEpoch () - last_ms_);
  return left > 0 ? static_cast<int> (left) : 0;
}

int HeardByRecords::poll_delay_ms (int min_interval_ms, int jitter_ms)
{
  return min_interval_ms + (jitter_ms > 0 ? static_cast<int> (QRandomGenerator::global ()->bounded (jitter_ms + 1)) : 0);
}

bool HeardByRecords::fetch (QString const& call, int lookback_secs)
{
  if (busy_ || ms_until_next () > 0 || call.trimmed ().isEmpty ()) return false;
  last_ms_ = QDateTime::currentMSecsSinceEpoch ();
  busy_ = true;
  QUrl url {base_url_};
  QUrlQuery query;
  query.addQueryItem ("senderCallsign", call.trimmed ());
  query.addQueryItem ("flowStartSeconds", QString::number (-qAbs (lookback_secs)));
  query.addQueryItem ("rronly", "1");
  url.setQuery (query);
  QNetworkRequest request {url};
  request.setHeader (QNetworkRequest::UserAgentHeader, "WSJT-X");
  request.setTransferTimeout (30000);
  auto * reply = network_->get (request);
  connect (reply, &QNetworkReply::finished, this, [this, reply] {
      busy_ = false;
      Result result;
      if (reply->error () != QNetworkReply::NoError) result.error = reply->errorString ();
      else result = parse (reply->readAll ());
      reply->deleteLater ();
      Q_EMIT finished (result);
    });
  return true;
}

qint64 HeardByRecords::report_id (QString const& receiver, qint64 when, quint64 frequency)
{
  // FNV-1a over the three values; the API has no sequence number.
  quint64 h = 1469598103934665603ULL;
  auto mix = [&h] (QByteArray const& bytes) {
    for (char c : bytes)
      {
        h ^= static_cast<quint8> (c);
        h *= 1099511628211ULL;
      }
  };
  mix (receiver.toUtf8 ());
  mix ("|" + QByteArray::number (when) + "|" + QByteArray::number (frequency));
  h &= (1ULL << 52) - 1;
  return h ? static_cast<qint64> (h) : 1;
}

HeardByRecords::Result HeardByRecords::parse (QByteArray const& xml)
{
  Result result;
  QXmlStreamReader in {xml};
  QList<Report> reports;
  while (!in.atEnd ())
    {
      in.readNext ();
      if (in.isStartElement () && in.name () == QLatin1String {"receptionReport"})
        {
          auto const a = in.attributes ();
          Report r;
          r.receiver = a.value ("receiverCallsign").toString ();
          r.locator = a.value ("receiverLocator").toString ();
          r.mode = a.value ("mode").toString ();
          r.frequency = a.value ("frequency").toULongLong ();
          r.snr = a.value ("sNR").toInt ();
          r.when = a.value ("flowStartSeconds").toLongLong ();
          if (r.receiver.isEmpty () || !r.when) continue;
          r.id = report_id (r.receiver, r.when, r.frequency);
          reports.append (r);
        }
    }
  if (in.hasError ())
    {
      result.error = tr ("Unreadable reply: %1").arg (in.errorString ());
      return result;
    }
  std::stable_sort (reports.begin (), reports.end (), [] (Report const& x, Report const& y) {
      return x.when > y.when;
    });
  result.ok = true;
  result.reports = reports;
  return result;
}
