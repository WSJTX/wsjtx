#include <QtTest>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "Network/eqsl.h"

namespace
{
  class FakeReply final : public QNetworkReply
  {
  public:
    FakeReply (QNetworkRequest const& request, QObject * parent)
      : QNetworkReply {parent}
    {
      setRequest (request);
      setUrl (request.url ());
      setOpenMode (QIODevice::ReadOnly);
    }

    void abort () override
    {
      setError (QNetworkReply::OperationCanceledError, QStringLiteral ("operation canceled"));
    }

  protected:
    qint64 readData (char *, qint64) override
    {
      return -1;
    }
  };

  class FakeNetworkAccessManager final : public QNetworkAccessManager
  {
  public:
    Operation operation {UnknownOperation};
    QNetworkRequest request;
    QByteArray body;

  protected:
    QNetworkReply * createRequest (Operation requestedOperation, QNetworkRequest const& requestedRequest,
                                   QIODevice * outgoingData) override
    {
      operation = requestedOperation;
      request = requestedRequest;
      if (outgoingData)
        {
          body = outgoingData->readAll ();
        }
      return new FakeReply {requestedRequest, this};
    }
  };
}

class TestEqslUpload final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void mode_fields_data ()
  {
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<QString> ("fields");
    QTest::newRow ("jtty") << "JTTY" << "<MODE:4>MFSK<SUBMODE:4>JTTY";
    QTest::newRow ("ft4") << "FT4" << "<MODE:4>MFSK<SUBMODE:3>FT4";
    QTest::newRow ("fst4") << "FST4" << "<MODE:4>MFSK<SUBMODE:4>FST4";
    QTest::newRow ("q65") << "Q65" << "<MODE:4>MFSK<SUBMODE:3>Q65";
    QTest::newRow ("ft8") << "FT8" << "<MODE:3>FT8";
    QTest::newRow ("rtty") << "RTTY" << "<MODE:4>RTTY";
  }

  void mode_fields ()
  {
    QFETCH (QString, mode);
    QFETCH (QString, fields);

    FakeNetworkAccessManager manager;
    EQSL uploader {&manager, nullptr};

    auto const time = QDateTime {QDate {2026, 10, 15}, QTime {12, 34}, Qt::UTC};
    uploader.upload ("user", "password", "station", "K1ABC", mode, time, "-10", "20m", "TNX");
    uploader.work ();
    QCOMPARE (manager.operation, QNetworkAccessManager::PostOperation);
    QCOMPARE (manager.request.url (), QUrl {QStringLiteral ("https://www.eqsl.cc/qslcard/importADIF.cfm")});
    QVERIFY (manager.request.url ().query ().isEmpty ());
    QCOMPARE (manager.request.attribute (QNetworkRequest::RedirectPolicyAttribute).toInt (),
              static_cast<int> (QNetworkRequest::NoLessSafeRedirectPolicy));
    QVERIFY (manager.request.header (QNetworkRequest::ContentTypeHeader).toByteArray ().startsWith (
      "multipart/form-data; boundary="));
    QVERIFY (manager.body.contains ("name=\"EQSL_USER\""));
    QVERIFY (manager.body.contains ("\r\n\r\nuser\r\n"));
    QVERIFY (manager.body.contains ("name=\"EQSL_PSWD\""));
    QVERIFY (manager.body.contains ("\r\n\r\npassword\r\n"));
    QVERIFY (manager.body.contains ("name=\"Filename\"; filename=\"wsjtx.adi\""));

    auto const adif = QString::fromUtf8 (manager.body);

    QVERIFY (adif.contains ("<ADIF_VER:5>3.1.8"));
    QVERIFY2 (adif.contains (fields + "<QSO_DATE:"), qPrintable (adif));
    QCOMPARE (adif.contains ("<SUBMODE:"), mode != "FT8" && mode != "RTTY");
    QVERIFY (!adif.contains ("<EQSL_USER:"));
    QVERIFY (!adif.contains ("<EQSL_PSWD:"));
    QVERIFY (adif.contains ("<QSO_DATE:8>20261015<TIME_ON:4>1234"));
    QVERIFY (adif.contains ("<QSLMSG:3>TNX<EOR>"));
    QVERIFY (!manager.request.url ().toString ().contains (QStringLiteral ("user")));
    QVERIFY (!manager.request.url ().toString ().contains (QStringLiteral ("password")));
  }
};

QTEST_GUILESS_MAIN (TestEqslUpload)
#include "test_eqsl_upload.moc"
