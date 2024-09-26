#include "FoxVerifier.hpp"
#include "Logger.hpp"

FoxVerifier::FoxVerifier(QString user_agent, QNetworkAccessManager *manager,QString base_url, QString callsign, QDateTime timestamp, QString code, unsigned int hz=750) : QObject(nullptr)
{
  manager_ = manager;
  finished_ = false;
  errored_ = false;
  callsign_ = callsign;
  code_ = code;
  ts_ = timestamp;
  hz_ = hz;

  // make sure we URLencode the callsign, for things like E51D/MM
  QString encodedCall = QString::fromUtf8(QUrl::toPercentEncoding(callsign));
  QString url = QString("%1/check/").arg(base_url) + encodedCall + QString("/%1/%2.text").arg(timestamp.toString(Qt::ISODate)).arg(code);
  LOG_INFO(QString("FoxVerifier: url %1").arg(url).toStdString());
  q_url_ = QUrl(url);
  if (manager_ == nullptr) {
    LOG_INFO("FoxVerifier: manager is null, creating new one");
    manager_ = new QNetworkAccessManager(this);
    manager_->deleteLater();
  }
  if (q_url_.isValid()) {
    request_ = QNetworkRequest(q_url_);
    request_.setRawHeader( "User-Agent" , user_agent.toUtf8());
    request_.setRawHeader( "Accept" , "*/*" );
    request_.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request_.setTransferTimeout(FOXVERIFIER_DEFAULT_TIMEOUT_MSEC);
#endif

    reply_ =  manager_->get(request_);
    connect(reply_, &QNetworkReply::finished, this, &FoxVerifier::httpFinished);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(reply_, &QNetworkReply::errorOccurred, this, &FoxVerifier::errorOccurred);
#endif
    connect(reply_, &QNetworkReply::redirected, this, &FoxVerifier::httpRedirected);
    connect(reply_, &QNetworkReply::encrypted, this, &FoxVerifier::httpEncrypted);
#if QT_CONFIG(ssl)
    connect(reply_, &QNetworkReply::sslErrors, this, &FoxVerifier::sslErrors);
#else
    LOG_INFO("FoxVerifier: ssl not supported");
#endif

  } else {
    LOG_INFO(QString("FoxVerifier: url invalid ! %1").arg(url).toStdString());
  }
}

FoxVerifier::~FoxVerifier() {
}

bool FoxVerifier::finished() {
  return finished_;
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
void FoxVerifier::errorOccurred(QNetworkReply::NetworkError code)
{
  int status =  reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QString reason = reply_->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
  errored_ = true;
  error_reason_ = reply_->errorString();
  if (reply_->error() != QNetworkReply::NoError) {

    LOG_INFO(QString("FoxVerifier: errorOccurred status %1 error [%2][%3] isFinished %4 isrunning %5 code %6").arg(status).arg(
            reason).arg(error_reason_).arg(reply_->isFinished()).arg(reply_->isRunning()).arg(code).toStdString());
    return;
  }
  // TODO emit
}
#endif

void FoxVerifier::httpFinished()
{
  int status =  reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QString reason = reply_->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
  if (reply_->error() != QNetworkReply::NoError) {
    LOG_INFO(QString("FoxVerifier: httpFinished error:[%1 - %2] msg:[%3]").arg(status).arg(reason).arg(reply_->errorString()).toStdString());
    reply_->abort();
    emit verifyError(status, ts_, callsign_, code_, hz_, reply_->errorString());
  }
  return_value = reply_->read(1024); // limit amount we get
  LOG_INFO(QString("FoxVerifier: httpFinished status:[%1 - %2] body:[%3] ").arg(status).arg(reason).arg(return_value).toStdString());
  finished_ = true;
  reply_->deleteLater();
  if (status >= 200 && status <= 299) {
    emit verifyComplete(status, ts_, callsign_, code_, hz_, return_value);
  }
}

void FoxVerifier::sslErrors(const QList<QSslError> &)
{
  LOG_INFO(QString("FoxVerifier: sslErrors").toStdString());
  reply_->ignoreSslErrors();
}

void FoxVerifier::httpRedirected(const QUrl &url) {
  LOG_INFO(QString("FoxVerifier: redirected to %1").arg(url.toString()).toStdString());
}

void FoxVerifier::httpEncrypted() {
  LOG_INFO("FoxVerifier: httpEncrypted");
}

QString FoxVerifier::formatDecodeMessage(QDateTime ts, QString callsign, unsigned int hz_, QString const& verify_message) {
  //"172100 -00  0.0  750 ~  K8R VERIFIED"
  QTime rx_time = ts.time();
  QString hz=QString("%1").arg(hz_, 4, 10 ); // insert Hz
  if (verify_message.endsWith(" VERIFIED")) {
    return QString("%1   0  0.0 %2 ~  %3 verified").arg(rx_time.toString("hhmmss")).arg(hz).arg(callsign);
  } else
    if (verify_message.endsWith(" INVALID"))
    {
      return QString("%1   0  0.0 %2 ~  %3 invalid").arg(rx_time.toString("hhmmss")).arg(hz).arg(callsign);
    }
    else
      return QString{};
}

QString FoxVerifier::default_url() {
  return QString(FOXVERIFIER_DEFAULT_BASE_URL);
}
