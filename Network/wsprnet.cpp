// Interface to WSPRnet website
//
// by Edson Pereira - PY2SDR

#include "wsprnet.h"

#include <cmath>
#include <limits>

#include <QTimer>
#include <QFile>
#include <QRegExp>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>
#include <QtGlobal>
#include <QCryptographicHash>

#if QT_VERSION >= QT_VERSION_CHECK (5, 10, 0)
#include <QRandomGenerator>
#endif

#include "moc_wsprnet.cpp"

namespace
{
  char const * const wsprNetUrl = "http://wsprnet.org/post/";
  char const * const wsprNetUrl2 = "http://wsprnet.eu:3000/post/";
  //char const * const wsprNetUrl = "http://127.0.0.1:5000/post/";

  //
  // tested with this python REST mock of WSPRNet.org
  //
  /*
# Mock WSPRNet.org RESTful API
from flask import Flask, request, url_for
from flask_restful import Resource, Api

app = Flask(__name__)

@app.route ('/post/', methods=['GET', 'POST'])
def spot ():
    if request.method == 'POST':
        print (request.form)
    return "1 spot(s) added"

with app.test_request_context ():
    print (url_for ('spot'))
  */

  // regexp to parse FST4W decodes
  QRegularExpression fst4_re {R"(
  (?<time>\d{4})
  \s+(?<db>[-+]?\d+)
  \s+(?<dt>[-+]?\d+\.\d+)
  \s+(?<freq>\d+)
  \s+`
  \s+<?(?<call>[A-Z0-9/]+)>?(?:\s(?<grid>[A-R]{2}[0-9]{2}(?:[A-X]{2})?))?(?:\s+(?<dBm>\d+))?
)", QRegularExpression::ExtendedPatternSyntaxOption};

  // regexp to parse wspr_spots.txt from wsprd
  //
  // 130223 2256 7    -21 -0.3  14.097090  DU1MGA PK04 37          0    40    0
  // Date   Time Sync dBm  DT   Freq       Msg
  // 1      2    3     4   5     6         -------7------          8     9    10
  QRegularExpression wspr_re(R"(^(\d+)\s+(\d+)\s+(\d+)\s+([+-]?\d+)\s+([+-]?\d+\.\d+)\s+(\d+\.\d+)\s+([^ ].*[^ ])\s+([+-]?\d+)\s+([+-]?\d+)\s+([+-]?\d+))");
};

WSPRNet::WSPRNet(QObject *parent)
  : QObject {parent}
  , network_manager_ {new QNetworkAccessManager(this)}
  , retry_policy_ {}
  , TR_period_ {0.F}
  , uploads_started_ {0}
  , next_file_batch_id_ {1}
  , upload_session_active_ {false}
{
  upload_timer_.setSingleShot (true);
  connect (&upload_timer_, &QTimer::timeout, this, &WSPRNet::work);
}

WSPRNet::WSPRNet (QNetworkAccessManager *network_manager, RetryPolicy retry_policy,
                  bool take_network_manager_ownership, QObject *parent)
  : QObject {parent}
  , network_manager_ {network_manager ? network_manager : new QNetworkAccessManager {this}}
  , retry_policy_ {retry_policy}
  , TR_period_ {0.F}
  , uploads_started_ {0}
  , next_file_batch_id_ {1}
  , upload_session_active_ {false}
{
  if (take_network_manager_ownership && network_manager)
    {
      network_manager_->setParent (this);
    }
  upload_timer_.setSingleShot (true);
  connect (&upload_timer_, &QTimer::timeout, this, &WSPRNet::work);
}

void WSPRNet::upload (QString const& call, QString const& grid, QString const& rfreq, QString const& tfreq,
                      QString const& mode, float TR_period, QString const& tpct, QString const& dbm,
                      QString const& version, QString const& fileName)
{
  StationContext context {call, grid, rfreq, tfreq, mode, TR_period, tpct, dbm, version};
  queueWsprFile (context, fileName);
  flush (context);
}

void WSPRNet::queueWsprFile (StationContext const& context, QString const& file_name)
{
  applyContext (context);
  auto const snapshot = snapshotFile (file_name);
  auto const file_batch_id = beginFileBatch (snapshot);

  QFile wsprdOutFile (file_name);
  if (!wsprdOutFile.open (QIODevice::ReadOnly | QIODevice::Text) || !wsprdOutFile.size ())
    {
      enqueueUpload (urlEncodeNoSpot (), UploadSource::File, PayloadKind::Status, file_name, file_batch_id);
    }
  else
    {
      while (!wsprdOutFile.atEnd())
        {
          SpotQueue::value_type query;
          if (decodeLine (wsprdOutFile.readLine(), query))
            {
              // Prevent reporting data ouside of the current frequency band
              float f = fabs (m_rfreq.toFloat() - query.queryItemValue ("tqrg", QUrl::FullyDecoded).toFloat());
              if (f < 0.01)     // MHz
                {
                  enqueueUpload (urlEncodeSpot (query), UploadSource::File, PayloadKind::Spot, file_name, file_batch_id);
                }
            }
        }
    }
  if (file_uploads_.value (file_batch_id).total == 0)
    {
      file_uploads_.remove (file_batch_id);
    }
}

void WSPRNet::post (QString const& call, QString const& grid, QString const& rfreq, QString const& tfreq,
                    QString const& mode, float TR_period, QString const& tpct, QString const& dbm,
                    QString const& version, QString const& decode_text)
{
  StationContext context {call, grid, rfreq, tfreq, mode, TR_period, tpct, dbm, version};

  if (!decode_text.size ())
    {
      flush (context);
    }
  else
    {
      queueFst4wDecode (context, decode_text);
    }
}

void WSPRNet::queueFst4wDecode (StationContext const& context, QString const& decode_text)
{
  applyContext (context);
  auto const& match = fst4_re.match (decode_text);
  if (match.hasMatch ())
    {
      SpotQueue::value_type query;
      auto tqrg = match.captured ("freq").toInt ();
      query.addQueryItem ("function", "wspr");
      // FST4W reports are intentionally not constrained to WSPR sub-bands.
      auto const& date = QDateTime::currentDateTimeUtc ().addSecs (-context.TR_period * 3. / 4.).date ();
      query.addQueryItem ("date", date.toString ("yyMMdd"));
      query.addQueryItem ("time", match.captured ("time"));
      query.addQueryItem ("sig", match.captured ("db"));
      query.addQueryItem ("dt", match.captured ("dt"));
      query.addQueryItem ("tqrg", QString::number (context.rfreq.toDouble () + (tqrg - 1500) / 1e6, 'f', 6));
      query.addQueryItem ("tcall", match.captured ("call"));
      query.addQueryItem ("drift", "0");
      query.addQueryItem ("tgrid", match.captured ("grid"));
      query.addQueryItem ("dbm", match.captured ("dBm"));
      enqueueUpload (urlEncodeSpot (query), UploadSource::Direct, PayloadKind::Spot);
    }
}

void WSPRNet::flush (StationContext const& context)
{
  applyContext (context);
  if (pending_uploads_.isEmpty () && outstanding_requests_.isEmpty ())
    {
      enqueueUpload (urlEncodeNoSpot (), UploadSource::Direct, PayloadKind::Status);
    }
  startUploadSession ();
}

void WSPRNet::applyContext (StationContext const& context)
{
  m_call = context.call;
  m_grid = context.grid;
  m_rfreq = context.rfreq;
  m_tfreq = context.tfreq;
  m_mode = context.mode;
  TR_period_ = context.TR_period;
  m_tpct = context.tpct;
  m_dbm = context.dbm;
  m_vers = context.version;
}

auto WSPRNet::snapshotFile (QString const& file_name) const -> FileSnapshot
{
  FileSnapshot snapshot;
  snapshot.path = file_name;

  QFile file {file_name};
  if (!file.open (QIODevice::ReadOnly))
    {
      return snapshot;
    }

  auto const contents = file.readAll ();
  snapshot.size = contents.size ();
  snapshot.hash = QCryptographicHash::hash (contents, QCryptographicHash::Sha256);
  return snapshot;
}

bool WSPRNet::fileMatchesSnapshot (FileSnapshot const& snapshot) const
{
  if (snapshot.path.isEmpty () || snapshot.size < 0)
    {
      return false;
    }

  QFile file {snapshot.path};
  if (!file.open (QIODevice::ReadOnly))
    {
      return false;
    }

  auto const contents = file.readAll ();
  if (contents.size () != snapshot.size)
    {
      return false;
    }

  return QCryptographicHash::hash (contents, QCryptographicHash::Sha256) == snapshot.hash;
}

void WSPRNet::networkReply (QNetworkReply * reply)
{
  if (!outstanding_requests_.contains (reply))
    {
      reply->deleteLater ();
      return;
    }

  auto upload = outstanding_requests_.take (reply);
  QString server_response;
  if (replyAccepted (upload, reply, server_response))
    {
      markAccepted (upload);
    }
  else
    {
      auto const now = QDateTime::currentDateTimeUtc ();
      if (QNetworkReply::NoError != reply->error ())
        {
          Q_EMIT uploadStatus (QString {"Error: %1"}.arg (reply->errorString ()));
        }
      else
        {
          Q_EMIT uploadStatus (QString {"Upload Failed: %1"}.arg (server_response));
        }

      auto const retry_delay_ms = retryDelayMs (upload);
      if (canRetry (upload, now, retry_delay_ms))
        {
          retryUpload (upload, now, retry_delay_ms);
        }
      else
        {
          markFailed (upload);
        }
    }

  qDebug () << QString {"WSPRnet.org %1 outstanding requests"}.arg (outstanding_requests_.size ());

  // delete request object instance on return to the event loop otherwise it is leaked
  reply->deleteLater ();
  scheduleWork ();
  maybeFinalize ();
}

void WSPRNet::enqueueUpload (QUrlQuery const& query, UploadSource source, PayloadKind kind, QString const& source_file, int file_batch_id)
{
  auto const now = QDateTime::currentDateTimeUtc ();
  pending_uploads_.enqueue ({query, source, kind, source_file, file_batch_id, now,
                             now.addMSecs (retry_policy_.ttl_ms), now, 0});
  if (UploadSource::File == source)
    {
      auto& state = file_uploads_[file_batch_id];
      ++state.total;
    }
  pruneExpiredUploads (now);
  enforcePendingLimit ();
}

int WSPRNet::beginFileBatch (FileSnapshot const& snapshot)
{
  auto const file_batch_id = next_file_batch_id_++;
  auto& state = file_uploads_[file_batch_id];
  state.snapshot = snapshot;
  return file_batch_id;
}

void WSPRNet::startUploadSession ()
{
  upload_session_active_ = true;
  uploads_started_ = outstanding_requests_.size ();
  scheduleWork ();
  maybeFinalize ();
}

void WSPRNet::scheduleWork ()
{
  if (!upload_session_active_)
    {
      return;
    }

  auto const now = QDateTime::currentDateTimeUtc ();
  pruneExpiredUploads (now);
  if (pending_uploads_.isEmpty ())
    {
      upload_timer_.stop ();
      return;
    }

  // Send one spot at a time so QNAM reuses a single keep-alive connection
  // instead of opening parallel sockets; the completing reply re-schedules
  // the next send via networkReply().
  if (!outstanding_requests_.isEmpty ())
    {
      upload_timer_.stop ();
      return;
    }

  auto next_attempt_at = pending_uploads_.head ().next_attempt_at;
  for (auto const& upload : pending_uploads_)
    {
      if (upload.next_attempt_at < next_attempt_at)
        {
          next_attempt_at = upload.next_attempt_at;
        }
    }
  auto const delay = qMax<qint64> (0, now.msecsTo (next_attempt_at));
  upload_timer_.start (static_cast<int> (qMin<qint64> (delay, std::numeric_limits<int>::max ())));
}

void WSPRNet::pruneExpiredUploads (QDateTime const& now)
{
  int dropped = 0;
  for (int i = 0; i < pending_uploads_.size ();)
    {
      if (pending_uploads_[i].expires_at <= now)
        {
          markFailed (pending_uploads_.takeAt (i));
          ++dropped;
        }
      else
        {
          ++i;
        }
    }
  if (dropped)
    {
      Q_EMIT uploadStatus (QString {"Dropped %1 expired WSPRNet upload(s)"}.arg (dropped));
    }
}

void WSPRNet::enforcePendingLimit ()
{
  auto const max_pending = qMax (1, retry_policy_.max_pending);
  auto const now = QDateTime::currentDateTimeUtc ();
  pruneExpiredUploads (now);

  int dropped = 0;
  while (pending_uploads_.size () > max_pending)
    {
      markFailed (pending_uploads_.dequeue ());
      ++dropped;
    }
  if (dropped)
    {
      Q_EMIT uploadStatus (QString {"Dropped %1 oldest pending WSPRNet upload(s)"}.arg (dropped));
    }
}

void WSPRNet::sendUpload (PendingUpload upload)
{
#if QT_VERSION < QT_VERSION_CHECK (5, 15, 0)
  if (QNetworkAccessManager::Accessible != network_manager_->networkAccessible ()) {
    // try and recover network access for QNAM
    network_manager_->setNetworkAccessible (QNetworkAccessManager::Accessible);
  }
#endif
  QNetworkRequest request (QUrl {wsprNetUrl});
  request.setHeader (QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  if (!upload.attempts)
    {
      ++uploads_started_;
    }
  ++upload.attempts;
  QNetworkReply *reply = network_manager_->post (request, upload.query.query (QUrl::FullyEncoded).toUtf8 ());
  connect (reply, &QNetworkReply::finished, this, [this, reply]() { networkReply (reply); });
  outstanding_requests_.insert (reply, upload);
  Q_EMIT uploadStatus (QString {"Uploading Spot %1/%2"}.arg (uploads_started_).arg (uploadsToSend ()));
  
  QNetworkRequest request2 (QUrl {wsprNetUrl2});
  request2.setHeader (QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  if (!upload.attempts)
    {
      ++uploads_started_;
    }
  ++upload.attempts;
  QNetworkReply *reply2 = network_manager_->post (request2, upload.query.query (QUrl::FullyEncoded).toUtf8 ());
  connect (reply2, &QNetworkReply::finished, this, [this, reply2]() { networkReply (reply2); });
  outstanding_requests_.insert (reply2, upload);
  Q_EMIT uploadStatus (QString {"Uploading Spot %1/%2"}.arg (uploads_started_).arg (uploadsToSend ()));  
}

bool WSPRNet::replyAccepted (PendingUpload const& upload, QNetworkReply *reply, QString& server_response) const
{
  if (QNetworkReply::NoError != reply->error ())
    {
      return false;
    }
  server_response = reply->readAll ();
  return PayloadKind::Status == upload.kind
    || server_response.contains (QRegExp ("spot\\(s\\) added"));
}

bool WSPRNet::canRetry (PendingUpload const& upload, QDateTime const& now, int retry_delay_ms) const
{
  return upload.attempts < retry_policy_.max_attempts
    && now.addMSecs (retry_delay_ms) < upload.expires_at;
}

int WSPRNet::retryDelayMs (PendingUpload const& upload) const
{
  int delay = 0;
  if (!retry_policy_.retry_delays_ms.isEmpty ())
    {
      auto const index = qMin (upload.attempts - 1, retry_policy_.retry_delays_ms.size () - 1);
      delay = retry_policy_.retry_delays_ms[qMax (0, index)];
    }
  if (delay <= 0 || retry_policy_.jitter_fraction <= 0.)
    {
      return qMax (0, delay);
    }

#if QT_VERSION >= QT_VERSION_CHECK (5, 10, 0)
  auto const random = QRandomGenerator::global ()->generateDouble ();
#else
  auto const random = qrand () / (RAND_MAX + 1.0);
#endif
  auto const factor = 1. - retry_policy_.jitter_fraction
    + random * retry_policy_.jitter_fraction * 2.;
  return qMax (0, static_cast<int> (delay * factor));
}

int WSPRNet::uploadsToSend () const
{
  auto total = uploads_started_;
  for (auto const& upload : pending_uploads_)
    {
      if (!upload.attempts)
        {
          ++total;
        }
    }
  return qMax (uploads_started_, total);
}

void WSPRNet::retryUpload (PendingUpload upload, QDateTime const& now, int retry_delay_ms)
{
  upload.next_attempt_at = now.addMSecs (retry_delay_ms);
  pending_uploads_.enqueue (upload);
  Q_EMIT uploadStatus (QString {"Retrying WSPRNet upload in %1 ms"}.arg (retry_delay_ms));
  enforcePendingLimit ();
}

void WSPRNet::markAccepted (PendingUpload const& upload)
{
  if (UploadSource::File == upload.source)
    {
      auto& state = file_uploads_[upload.file_batch_id];
      ++state.accepted;
      maybeRemoveCompletedFile (upload.file_batch_id);
      maybeRemoveFailedFileBatch (upload.file_batch_id);
    }
}

void WSPRNet::markFailed (PendingUpload const& upload)
{
  if (UploadSource::File == upload.source)
    {
      file_uploads_[upload.file_batch_id].failed = true;
      maybeRemoveFailedFileBatch (upload.file_batch_id);
    }
}

void WSPRNet::maybeRemoveFailedFileBatch (int file_batch_id)
{
  auto const state = file_uploads_.constFind (file_batch_id);
  if (state == file_uploads_.constEnd () || !state->failed)
    {
      return;
    }

  for (auto const& upload : pending_uploads_)
    {
      if (UploadSource::File == upload.source && upload.file_batch_id == file_batch_id)
        {
          return;
        }
    }
  for (auto const& upload : outstanding_requests_)
    {
      if (UploadSource::File == upload.source && upload.file_batch_id == file_batch_id)
        {
          return;
        }
    }
  file_uploads_.remove (file_batch_id);
}

void WSPRNet::maybeRemoveCompletedFile (int file_batch_id)
{
  if (!file_uploads_.contains (file_batch_id))
    {
      return;
    }

  auto const state = file_uploads_.value (file_batch_id);
  if (!state.failed && state.total > 0 && state.accepted == state.total)
    {
      QFile f {state.snapshot.path};
      // wsprd can rewrite this scratch file between the check and remove.
      // The snapshot guard prevents deleting known-new contents.
      if (f.exists () && fileMatchesSnapshot (state.snapshot))
        {
          f.remove ();
        }
      file_uploads_.remove (file_batch_id);
    }
}

void WSPRNet::maybeFinalize ()
{
  if (!upload_session_active_)
    {
      return;
    }
  if (pending_uploads_.isEmpty () && outstanding_requests_.isEmpty ())
    {
      uploads_started_ = 0;
      upload_session_active_ = false;
      upload_timer_.stop ();
      Q_EMIT uploadStatus ("done");
    }
}

bool WSPRNet::decodeLine (QString const& line, SpotQueue::value_type& query) const
{
  auto const& rx_match = wspr_re.match (line);
  if (rx_match.hasMatch ()) {
    int msgType = 0;
    QString msg = rx_match.captured (7);
    QString call, grid, dbm;
    QRegularExpression msgRx;

    // Check for Message Type 1
    msgRx.setPattern(R"(^([A-Z0-9]{3,6})\s+([A-R]{2}\d{2})\s+(\d+))");
    auto match = msgRx.match (msg);
    if (match.hasMatch ()) {
      msgType = 1;
      call = match.captured (1);
      grid = match.captured (2);
      dbm = match.captured (3);
    }

    // Check for Message Type 2
    msgRx.setPattern(R"(^([A-Z0-9/]+)\s+(\d+))");
    match = msgRx.match (msg);
    if (match.hasMatch ()) {
      msgType = 2;
      call = match.captured (1);
      grid = "";
      dbm = match.captured (2);
    }

    // Check for Message Type 3
    msgRx.setPattern(R"(^<([A-Z0-9/]+)>\s+([A-R]{2}\d{2}[A-X]{2})\s+(\d+))");
    match = msgRx.match (msg);
    if (match.hasMatch ()) {
      msgType = 3;
      call = match.captured (1);
      grid = match.captured (2);
      dbm = match.captured (3);
    }

    // Unknown message format
    if (!msgType) {
      return false;
    }

    query.addQueryItem ("function", "wspr");
    query.addQueryItem ("date", rx_match.captured (1));
    query.addQueryItem ("time", rx_match.captured (2));
    query.addQueryItem ("sig", rx_match.captured (4));
    query.addQueryItem ("dt", rx_match.captured(5));
    query.addQueryItem ("drift", rx_match.captured(8));
    query.addQueryItem ("tqrg", rx_match.captured(6));
    query.addQueryItem ("tcall", call);
    query.addQueryItem ("tgrid", grid);
    query.addQueryItem ("dbm", dbm);
  } else {
    return false;
  }
  return true;
}

QString WSPRNet::encode_mode () const
{
  if (m_mode == "WSPR") return "2";
  if (m_mode == "WSPR-15") return "15";
  if (m_mode == "FST4W")
    {
      auto tr = static_cast<int> ((TR_period_ / 60.)+.5);
//      if (2 == tr || 15 == tr)
      if (2 == tr)
        {
          tr += 1;              // distinguish from WSPR-2
        }
      return QString::number (tr);
    }
  return "";
}

auto WSPRNet::urlEncodeNoSpot () const -> SpotQueue::value_type
{
  SpotQueue::value_type query;
  query.addQueryItem ("function", "wsprstat");
  query.addQueryItem ("rcall", m_call);
  query.addQueryItem ("rgrid", m_grid);
  query.addQueryItem ("rqrg", m_rfreq);
  query.addQueryItem ("tpct", m_tpct);
  query.addQueryItem ("tqrg", m_tfreq);
  query.addQueryItem ("dbm", m_dbm);
  query.addQueryItem ("version", m_vers);
  query.addQueryItem ("mode", encode_mode ());
  return query;
}

auto WSPRNet::urlEncodeSpot (SpotQueue::value_type& query) const -> SpotQueue::value_type
{
  query.addQueryItem ("version", m_vers);
  query.addQueryItem ("rcall", m_call);
  query.addQueryItem ("rgrid", m_grid);
  query.addQueryItem ("rqrg", m_rfreq);
  query.addQueryItem ("mode", encode_mode ());
  return query;
}

void WSPRNet::work()
{
  auto const now = QDateTime::currentDateTimeUtc ();
  pruneExpiredUploads (now);

  // Keep at most one request in flight so the connection is reused.
  if (!outstanding_requests_.isEmpty ())
    {
      scheduleWork ();
      maybeFinalize ();
      return;
    }

  for (int i = 0; i < pending_uploads_.size (); ++i)
    {
      if (pending_uploads_[i].next_attempt_at <= now)
        {
          sendUpload (pending_uploads_.takeAt (i));
          scheduleWork ();
          maybeFinalize ();
          return;
        }
    }

  scheduleWork ();
  maybeFinalize ();
}

void WSPRNet::abortOutstandingRequests () {
  upload_timer_.stop ();
  pending_uploads_.clear ();
  file_uploads_.clear ();
  uploads_started_ = 0;
  upload_session_active_ = false;

  auto const requests = outstanding_requests_.keys ();
  outstanding_requests_.clear ();
  for (auto& request : requests) {
    request->abort ();
  }
}
