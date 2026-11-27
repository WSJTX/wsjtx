// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QUdpSocket>

#include "Network/MessageClient.hpp"
#include "Network/NetworkMessage.hpp"
#include "UDPExamples/MessageServer.hpp"

class TestMessageJttyText final
  : public QObject
{
  Q_OBJECT

private slots:
  void roundTrip_data ()
  {
    QTest::addColumn<quint32> ("schema");
    QTest::addColumn<bool> ("complete");
    QTest::addColumn<bool> ("off_air");
    for (quint32 schema : {2u, 3u})
      for (bool complete : {false, true})
        for (bool off_air : {false, true}) {
          auto const name = QString {"schema-%1-complete-%2-off-air-%3"}.arg (schema).arg (complete).arg (off_air);
          QTest::newRow (qPrintable (name)) << schema << complete << off_air;
        }
  }

  void roundTrip ()
  {
    QFETCH (quint32, schema);
    QFETCH (bool, complete);
    QFETCH (bool, off_air);
    QString const id {"WSJT-X"};
    qint64 constexpr message_id {(qint64 {1} << 40) + 19};
    QDateTime const start {QDate {2026, 11, 14}, QTime {12, 34, 56, 789}, Qt::UTC};
    quint32 constexpr frequency {1501};
    qint32 constexpr snr {-12};
    QString const text = QString::fromUtf8 ("CQ K1ABC caf\xc3\xa9");
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::JTTYText, id, schema};
    out << message_id << start << frequency << snr << text.toUtf8 () << complete << off_air;
    QCOMPARE (out.status (), QDataStream::Ok);
    NetworkMessage::Reader in {message};
    QCOMPARE (int (in.type ()), 19);
    QCOMPARE (in.schema (), schema);
    QCOMPARE (in.id (), id);
    qint64 read_id {};
    QDateTime read_start;
    quint32 read_frequency {};
    qint32 read_snr {};
    QByteArray read_text;
    bool read_complete {}, read_off_air {};
    in >> read_id >> read_start >> read_frequency >> read_snr >> read_text >> read_complete >> read_off_air;
    QCOMPARE (in.status (), QDataStream::Ok);
    QVERIFY (in.atEnd ());
    QCOMPARE (read_id, message_id);
    QCOMPARE (read_start, start);
    QCOMPARE (read_start.timeSpec (), Qt::UTC);
    QCOMPARE (read_frequency, frequency);
    QCOMPARE (read_snr, snr);
    QCOMPARE (QString::fromUtf8 (read_text), text);
    QCOMPARE (read_complete, complete);
    QCOMPARE (read_off_air, off_air);

    qRegisterMetaType<MessageServer::ClientKey> ("ClientKey");
    QHostAddress const loopback {QHostAddress::LocalHost};
    QUdpSocket sender;
    QVERIFY (sender.bind (loopback, 0));
    auto const port = sender.localPort ();
    sender.close ();
    QObject owner;
    auto *server = new MessageServer {&owner};
    QSignalSpy texts {server, &MessageServer::jtty_text};
    QSignalSpy decodes {server, &MessageServer::decode};
    server->start (port);
    // A new type requires all its fields, but permits future trailing fields.
    auto const truncated = message.left (message.size () - 1);
    auto const extended = message + QByteArray {"future fields"};
    QCOMPARE (sender.writeDatagram (truncated, loopback, port), qint64 (truncated.size ()));
    QCOMPARE (sender.writeDatagram (extended, loopback, port), qint64 (extended.size ()));
    QTRY_COMPARE (texts.count (), 1);
    auto const args = texts.front ();
    // The server may see an IPv4 sender as its IPv4-mapped IPv6 address.
    auto const client_key = args[0].value<MessageServer::ClientKey> ();
    QVERIFY (client_key.first.isEqual (loopback, QHostAddress::ConvertV4MappedToIPv4));
    QCOMPARE (client_key.second, id);
    QCOMPARE (args[1].toLongLong (), message_id);
    QCOMPARE (args[2].toDateTime (), start);
    QCOMPARE (args[3].toUInt (), frequency);
    QCOMPARE (args[4].toInt (), snr);
    QCOMPARE (args[5].toString (), text);
    QCOMPARE (args[6].toBool (), complete);
    QCOMPARE (args[7].toBool (), off_air);
    QVERIFY (decodes.isEmpty ());
  }

  void clientSendsGrowingAndCompleteText ()
  {
    QUdpSocket receiver;
    QVERIFY (receiver.bind (QHostAddress {QHostAddress::LocalHost}, 0));
    MessageClient client {"test-client", "version", "revision", "127.0.0.1",
                          receiver.localPort (), {}, 1};
    QTRY_VERIFY (receiver.hasPendingDatagrams ());
    while (receiver.hasPendingDatagrams ()) receiver.receiveDatagram ();
    qint64 constexpr message_id {qint64 {1} << 40};
    QDateTime const start {QDate {2026, 11, 14}, QTime {12, 34, 56, 789}, Qt::UTC};
    for (bool complete : {false, true}) {
      client.jtty_text (message_id, start.toOffsetFromUtc (3600), 1501, -12, "CQ K1ABC FN42", complete, true);
      QTRY_VERIFY (receiver.hasPendingDatagrams ());
      NetworkMessage::Reader in {receiver.receiveDatagram ().data ()};
      QCOMPARE (in.type (), NetworkMessage::JTTYText);
      QCOMPARE (in.id (), QString {"test-client"});
      qint64 read_id {};
      QDateTime read_start;
      quint32 frequency {};
      qint32 snr {};
      QByteArray text;
      bool read_complete {}, off_air {};
      in >> read_id >> read_start >> frequency >> snr >> text >> read_complete >> off_air;
      QCOMPARE (in.status (), QDataStream::Ok);
      QVERIFY (in.atEnd ());
      QCOMPARE (read_id, message_id);
      QCOMPARE (read_start, start);
      QCOMPARE (read_start.timeSpec (), Qt::UTC);
      QCOMPARE (frequency, quint32 {1501});
      QCOMPARE (snr, qint32 {-12});
      QCOMPARE (text, QByteArray {"CQ K1ABC FN42"});
      QCOMPARE (read_complete, complete);
      QVERIFY (off_air);
    }
  }
};

QTEST_GUILESS_MAIN (TestMessageJttyText)
#include "test_message_jtty_text.moc"
