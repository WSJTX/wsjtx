// Interface to WSPRnet website
//
// by Edson Pereira - PY2SDR

#include "wsprnet.h"

#include <cmath>

#include <QTimer>
#include <QFile>
#include <QRegExp>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>

#include "moc_wsprnet.cpp"

namespace
{
  char const * const wsprNetUrl = "http://wsprnet.org/post/";
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

WSPRNet::WSPRNet (QNetworkAccessManager * manager, QObject *parent)
  : QObject {parent}
  , network_manager_ {manager}
  , spots_to_send_ {0}
{
  connect (network_manager_, &QNetworkAccessManager::finished, this, &WSPRNet::networkReply);
  connect (&upload_timer_, &QTimer::timeout, this, &WSPRNet::work);
}

void WSPRNet::upload (QString const& call, QString const& grid, QString const& rfreq, QString const& tfreq,
                      QString const& mode, float TR_period, QString const& tpct, QString const& dbm,
                      QString const& version, QString const& fileName)
{
  m_call = call;
  m_grid = grid;
  m_rfreq = rfreq;
  m_tfreq = tfreq;
  m_mode = mode;
  TR_period_ = TR_period;
  m_tpct = tpct;
  m_dbm = dbm;
  m_vers = version;
  m_file = fileName;

  // Open the wsprd.out file
  if (m_uploadType != 3)
    {
      QFile wsprdOutFile (fileName);
      if (!wsprdOutFile.open (QIODevice::ReadOnly | QIODevice::Text) || !wsprdOutFile.size ())
        {
          spot_queue_.enqueue (urlEncodeNoSpot ());
          m_uploadType = 1;
        }
      else
        {
          // Read the contents
          while (!wsprdOutFile.atEnd())
            {
              SpotQueue::value_type query;
              if (decodeLine (wsprdOutFile.readLine(), query))
                {
                  // Prevent reporting data ouside of the current frequency band
                  float f = fabs (m_rfreq.toFloat() - query.queryItemValue ("tqrg", QUrl::FullyDecoded).toFloat());
                  if (f < 0.01)     // MHz
                    {
                      spot_queue_.enqueue(urlEncodeSpot (query));
                      m_uploadType = 2;
                    }
                }
            }
        }
    }
  spots_to_send_ = spot_queue_.size ();
  upload_timer_.start (200);
}

void WSPRNet::post (QString const& call, QString const& grid, QString const& rfreq, QString const& tfreq,
                    QString const& mode, float TR_period, QString const& tpct, QString const& dbm,
                    QString const& version, QString const& decode_text)
{
  m_call = call;
  m_grid = grid;
  m_rfreq = rfreq;
  m_tfreq = tfreq;
  m_mode = mode;
  TR_period_ = TR_period;
  m_tpct = tpct;
  m_dbm = dbm;
  m_vers = version;

  if (!decode_text.size ())
    {
      if (!spot_queue_.size ())
        {
          spot_queue_.enqueue (urlEncodeNoSpot ());
          m_uploadType = 3;
        }
    }
  else
    {
      auto const& match = fst4_re.match (decode_text);
      if (match.hasMatch ())
        {
          SpotQueue::value_type query;
          // Prevent reporting data ouside of the current frequency
          // band - removed by G4WJS to accommodate FST4W spots
          // outside of WSPR segments
          auto tqrg = match.captured ("freq").toInt ();
          // if (tqrg >= 1400 && tqrg <= 1600)
            {
              query.addQueryItem ("function", "wspr");
              // use time as at 3/4 of T/R period before current to
              // ensure date is in Rx period
              auto const& date = QDateTime::currentDateTimeUtc ().addSecs (-TR_period * 3. / 4.).date ();
              query.addQueryItem ("date", date.toString ("yyMMdd"));
              query.addQueryItem ("time", match.captured ("time"));
              query.addQueryItem ("sig", match.captured ("db"));
              query.addQueryItem ("dt", match.captured ("dt"));
              query.addQueryItem ("tqrg", QString::number (rfreq.toDouble () + (tqrg - 1500) / 1e6, 'f', 6));
              query.addQueryItem ("tcall", match.captured ("call"));
              query.addQueryItem ("drift", "0");
              query.addQueryItem ("tgrid", match.captured ("grid"));
              query.addQueryItem ("dbm", match.captured ("dBm"));
              spot_queue_.enqueue (urlEncodeSpot (query));
              m_uploadType = 2;
            }
        }
    }
}

void WSPRNet::networkReply (QNetworkReply * reply)
{
  // check if request was ours
  if (m_outstandingRequests.removeOne (reply))
    {
      if (QNetworkReply::NoError != reply->error ())
        {
          Q_EMIT uploadStatus (QString {"Error: %1"}.arg (reply->error ()));
          // not clearing queue or halting queuing as it may be a
          // transient one off request error
        }
      else
        {
          QString serverResponse = reply->readAll ();
          if (m_uploadType == 2)
            {
              if (!serverResponse.contains(QRegExp("spot\\(s\\) added")))
                {
                  Q_EMIT uploadStatus (QString {"Upload Failed: %1"}.arg (serverResponse));
                  spot_queue_.clear ();
                  upload_timer_.stop ();
                }
            }

          if (!spot_queue_.size ())
            {
              Q_EMIT uploadStatus("done");
              QFile f {m_file};
              if (f.exists ()) f.remove ();
              upload_timer_.stop ();
            }
        }

      qDebug () << QString {"WSPRnet.org %1 outstanding requests"}.arg (m_outstandingRequests.size ());

      // delete request object instance on return to the event loop otherwise it is leaked
      reply->deleteLater ();
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
      if (2 == tr || 15 == tr)
        {
          tr += 1;              // distinguish from WSPR-2 and WSPR-15
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
  return query;;
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
  if (spots_to_send_ && spot_queue_.size ())
    {
#if QT_VERSION < QT_VERSION_CHECK (5, 15, 0)
      if (QNetworkAccessManager::Accessible != network_manager_->networkAccessible ()) {
        // try and recover network access for QNAM
        network_manager_->setNetworkAccessible (QNetworkAccessManager::Accessible);
      }
#endif
      QNetworkRequest request (QUrl {wsprNetUrl});
      request.setHeader (QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
      auto const& spot = spot_queue_.dequeue ();
      m_outstandingRequests << network_manager_->post (request, spot.query (QUrl::FullyEncoded).toUtf8 ());
      Q_EMIT uploadStatus(QString {"Uploading Spot %1/%2"}.arg (spots_to_send_ - spot_queue_.size()).arg (spots_to_send_));
    }
  else
    {
      upload_timer_.stop ();
    }
}

void WSPRNet::abortOutstandingRequests () {
  spot_queue_.clear ();
  for (auto& request : m_outstandingRequests) {
    request->abort ();
  }
}
