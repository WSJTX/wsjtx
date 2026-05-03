#include "Cloudlog.hpp"

#include <future>
#include <chrono>

#include <QHash>
#include <QString>
#include <QDate>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDebug>
#include <QMessageBox>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>

#include "pimpl_impl.hpp"

#include "moc_Cloudlog.cpp"

#include "Configuration.hpp"

namespace
{
  // Dictionary mapping call sign to date of last upload to LotW
  using dictionary = QHash<QString, QDate>;
}

class Cloudlog::impl final
  : public QObject
{
  Q_OBJECT

public:
  impl (Cloudlog * self, Configuration const * config)
    : self_ {self}
    , config_ {config}
    , network_manager_ {new QNetworkAccessManager(this)}
  {
  }

  void logQso (QByteArray ADIF)
  {
    QByteArray data;

    QString str = QString("") +
      "{" +
        "\"key\":\""+ config_->cloudlog_api_key() +"\"," +
        "\"station_profile_id\":\""+ QVariant(config_->cloudlog_api_station_id()).toString() +"\"," +
        "\"type\":\"adif\"," +
        "\"string\":\"" + ADIF + "<eor>" + "\"" +
      "}";
    data = str.toUtf8();

    QUrl u = QUrl(config_->cloudlog_api_url()+"/index.php/api/qso");

    QNetworkRequest request(u);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));
    reply_ = network_manager_->post(request, data);
    connect (reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_logqso);

  }

  void testApi (QString const& url, QString const& apiKey)
  {
    QString apiUrl = url.trimmed();
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    if (QNetworkAccessManager::Accessible != network_manager_->networkAccessible ())
      {
        // try and recover network access for QNAM
        network_manager_->setNetworkAccessible (QNetworkAccessManager::Accessible);
      }
#endif

    // Remove trailing slash if given
    if (apiUrl.endsWith('/')) {
      apiUrl.chop(1);
    }
    // Remove full path to API if given
    if (apiUrl.endsWith("/index.php/api/qso")) {
      apiUrl.chop(QStringLiteral("/index.php/api/qso").size());
    }

    QNetworkRequest request {apiUrl+"/index.php/api/auth/"+apiKey};
    request.setRawHeader ("User-Agent", "WSJT-X Cloudlog API");
    request.setOriginatingObject (this);
    reply_ = network_manager_->get (request);
    connect (reply_.data (), &QNetworkReply::finished, this, &Cloudlog::impl::reply_apitest);
  }

  void reply_apitest()
  {
    QString result;
    if (reply_ && reply_->isFinished ())
      {
        result = reply_->readAll();
        if (result.contains("<status>Valid</status>"))
          {
            if (result.contains("<rights>rw</rights>"))
              {
                // fprintf(stderr, "API key OK\n");
                Q_EMIT self_->apikey_ok ();
              } else {
                // fprintf(stderr, "API key not writable!\n");
		Q_EMIT self_->apikey_ro ();
              }
          } else {
            // fprintf(stderr, "API key invalid!\n");
	    Q_EMIT self_->apikey_invalid ();
          }
      }
  }

  void reply_logqso()
  {
    QString result;
    if (reply_ && reply_->isFinished ())
      {
        result = reply_->readAll();
        QJsonDocument data = QJsonDocument::fromJson(result.toUtf8());
        QJsonObject obj = data.object();
        if (obj["status"] == "failed") {
          QMessageBox msgBox;
          msgBox.setIcon(QMessageBox::Warning);
	  msgBox.setWindowTitle("Cloudlog Error!");
          msgBox.setText("QSO could not be sent to Cloudlog!\nPlease check your log.");
          msgBox.setDetailedText("Reason: "+obj["reason"].toString());
          msgBox.exec();
        }
      }
  }

  void abort ()
  {
    if (reply_ && reply_->isRunning ())
      {
        reply_->abort ();
      }
  }

  Cloudlog * self_;
  Configuration const * config_;
  QNetworkAccessManager * network_manager_;
  QPointer<QNetworkReply> reply_;
};

#include "Cloudlog.moc"

Cloudlog::Cloudlog (Configuration const * config,
                   QObject * parent)
  : QObject {parent}
  , m_ {this, config}
{
}

Cloudlog::~Cloudlog ()
{
}

void Cloudlog::testApi (QString const& url, QString const& apiKey)
{
  m_->testApi (url, apiKey);
}

void Cloudlog::logQso (QByteArray ADIF)
{
  m_->logQso(ADIF);
}
