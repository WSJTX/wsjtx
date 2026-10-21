#include <QtTest>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include "Network/MessageClient.hpp"
#include "Network/NetworkMessage.hpp"
#include "logbook/AdifQso.hpp"

class TestMessageClientAdif final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void sends_jtty_with_current_header ()
  {
    QUdpSocket receiver;
    QVERIFY (receiver.bind (QHostAddress {QHostAddress::LocalHost}, 0));
    MessageClient client {"test-client", "version", "revision", "127.0.0.1",
                          receiver.localPort (), {}, 1};
    QTRY_VERIFY (receiver.hasPendingDatagrams ());
    while (receiver.hasPendingDatagrams ()) receiver.receiveDatagram ();

    auto const time = QDateTime {QDate {2026, 10, 15}, QTime {12, 0}, Qt::UTC};
    auto const record = AdifQso::to_adif ("K1ABC", "FN42", "JTTY", "-10", "-12", time, time,
                                        "20m", {}, {}, "14.080000", "W1AW", "FN31", {}, {},
                                        {}, {}, {}, {}, {}, {}, AdifQso::Contest::None);
    client.logged_ADIF (record);
    QTRY_VERIFY (receiver.hasPendingDatagrams ());
    NetworkMessage::Reader reader {receiver.receiveDatagram ().data ()};
    QCOMPARE (reader.type (), NetworkMessage::LoggedADIF);
    QCOMPARE (reader.id (), QString {"test-client"});
    QByteArray adif;
    reader >> adif;
    QCOMPARE (reader.status (), QDataStream::Ok);
    QCOMPARE (adif, QByteArray {"\n<adif_ver:5>3.1.8\n<programid:6>WSJT-X\n<EOH>\n"}
              + record + " <EOR>");
    QVERIFY (adif.contains ("<mode:4>MFSK <submode:4>JTTY"));
  }
};

QTEST_GUILESS_MAIN (TestMessageClientAdif)
#include "test_message_client_adif.moc"
