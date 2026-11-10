#include "eqsl.h"
#include "logbook/AdifQso.hpp"

#include <QDateTime>

#include <cmath>

#include <QTimer>
#include <QFile>
#include <QHttpMultiPart>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>

#include "moc_eqsl.cpp"

namespace
{
  char const * const EqslUrl = "https://www.eqsl.cc/qslcard/importADIF.cfm";
  // char const * const wsprNetUrl = "http://127.0.0.1/post?";

  QHttpPart formField (char const * name, QString const& value)
  {
    QHttpPart part;
    part.setHeader (QNetworkRequest::ContentDispositionHeader,
                    QVariant {QStringLiteral ("form-data; name=\"%1\"").arg (QLatin1String {name})});
    part.setBody (value.toUtf8 ());
    return part;
  }
};

EQSL::EQSL(QObject *parent)
  : EQSL {new QNetworkAccessManager, parent}
{
  networkManager->setParent (this);
}

EQSL::EQSL(QNetworkAccessManager * networkManager, QObject * parent)
  : QObject{parent}
  , networkManager {networkManager}
  , uploadTimer {new QTimer {this}}
  , m_in_progress {false}
{
  uploadTimer->setSingleShot(true);
  connect(networkManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(networkReply(QNetworkReply*)));
  connect( uploadTimer, SIGNAL(timeout()), this, SLOT(work()));
}

void EQSL::upload(QString const& eqsl_username, QString const& eqsl_passwd, QString const& eqsl_nickname
                  , QString const& call, QString const& mode
                  , QDateTime const& QSO_date_on
                  , QString const& rpt_sent, QString const& band
                  , QString const& eqslcomments)
{

    if (m_in_progress) {
        reply->abort();
        m_in_progress = false;
    }
    username = eqsl_username;
    password = eqsl_passwd;
    myadif="<ADIF_VER:5>3.1.8";
    myadif+="<PROGRAMID:4>JTDX<EOH><APP_EQSL_QTH_NICKNAME:" + QString::number(eqsl_nickname.length()) + ">" + eqsl_nickname;
    myadif+="<CALL:" + QString::number(call.length()) + ">" + call;
    auto const adif_mode = AdifQso::base_mode (mode);
    myadif+="<MODE:"  + QString::number(adif_mode.length()) + ">" + adif_mode;
    if (adif_mode != mode) {
      myadif+="<SUBMODE:" + QString::number(mode.length()) + ">" + mode;
    }
    myadif+="<QSO_DATE:8>" + QSO_date_on.date().toString("yyyyMMdd");
    myadif+="<TIME_ON:4>" + QSO_date_on.time().toString("hhmm");
    myadif+="<RST_SENT:" + QString::number(rpt_sent.length()) + ">" + rpt_sent;
    myadif+="<BAND:" + QString::number(band.length()) + ">" + band;
    if(eqslcomments!="") myadif+="<QSLMSG:" + QString::number(eqslcomments.length()) + ">" + eqslcomments;
//    myadif+="<QSLMSG:19>TNX For QSO TU 73!.";
    myadif+="<EOR>";

    uploadTimer->start(1);
}

void EQSL::networkReply(QNetworkReply *reply)
{
    if (QNetworkReply::NoError != reply->error ()) {
      
      printf ("eqsl upload error:%d\n",reply->error ());
    }
    else {
      QString serverResponse = reply->readAll();
      
//      printf("eqsl upload result:%s\n",serverResponse.toStdString().c_str());
    }


    // delete request object instance on return to the event loop otherwise it is leaked
    reply->deleteLater ();
    m_in_progress = false;
}

void EQSL::work()
{
#if QT_VERSION < QT_VERSION_CHECK (5, 15, 0)
      if (QNetworkAccessManager::Accessible != networkManager->networkAccessible ()) {
        // try and recover network access for QNAM
        networkManager->setNetworkAccessible (QNetworkAccessManager::Accessible);
      }
#endif
    m_in_progress = true;
    QUrl url(EqslUrl);
//    printf ("eqsl upload request:%s\n",myadif.toStdString().c_str());
    QNetworkRequest request(url);
    request.setAttribute (QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto * form = new QHttpMultiPart {QHttpMultiPart::FormDataType};
    form->append (formField ("EQSL_USER", username));
    form->append (formField ("EQSL_PSWD", password));
    QHttpPart adif;
    adif.setHeader (QNetworkRequest::ContentDispositionHeader,
                    QVariant {QStringLiteral ("form-data; name=\"Filename\"; filename=\"wsjtx.adi\"")});
    adif.setHeader (QNetworkRequest::ContentTypeHeader, QVariant {QStringLiteral ("application/octet-stream")});
    adif.setBody (myadif.toUtf8 ());
    form->append (adif);
    reply = networkManager->post(request, form);
    form->setParent (reply);
//    printf ("sent\n");
}

