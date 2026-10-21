#include <QtTest>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTcpServer>
#include <QUrlQuery>

#include "Network/eqsl.h"

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

    QTcpServer proxy;
    QVERIFY (proxy.listen (QHostAddress::LocalHost));
    EQSL uploader;
    auto manager = uploader.findChild<QNetworkAccessManager *> ();
    QVERIFY (manager);
    manager->setProxy (QNetworkProxy {QNetworkProxy::HttpProxy, "127.0.0.1", proxy.serverPort ()});

    auto const time = QDateTime {QDate {2026, 10, 15}, QTime {12, 34}, Qt::UTC};
    uploader.upload ("user", "password", "station", "K1ABC", mode, time, "-10", "20m", "TNX");
    uploader.work ();
    auto const replies = manager->findChildren<QNetworkReply *> ();
    QCOMPARE (replies.size (), 1);
    auto const adif = QUrlQuery {replies.front ()->url ()}.queryItemValue ("ADIFdata");
    replies.front ()->abort ();

    QVERIFY (adif.startsWith ("<ADIF_VER:5>3.1.8"));
    QVERIFY2 (adif.contains (fields + "<QSO_DATE:"), qPrintable (adif));
    QCOMPARE (adif.contains ("<SUBMODE:"), mode != "FT8" && mode != "RTTY");
    QVERIFY (adif.contains ("<EQSL_USER:4>user<EQSL_PSWD:8>password"));
    QVERIFY (adif.contains ("<QSO_DATE:8>20261015<TIME_ON:4>1234"));
    QVERIFY (adif.endsWith ("<QSLMSG:3>TNX<EOR>"));
  }
};

QTEST_GUILESS_MAIN (TestEqslUpload)
#include "test_eqsl_upload.moc"
