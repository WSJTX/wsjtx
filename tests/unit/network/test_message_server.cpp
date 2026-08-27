#include <QtTest>

#include <QSignalSpy>
#include <QUdpSocket>

#include "Network/NetworkMessage.hpp"
#include "UDPExamples/MessageServer.hpp"

namespace
{
  QByteArray heartbeat (QString const& id)
  {
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::Heartbeat, id, 2};
    out << quint32 {2} << QByteArray {"test"} << QByteArray {};
    return message;
  }

  QByteArray decode (QString const& id, QTime time, qint32 snr, float delta_time
                     , quint32 delta_frequency, QByteArray const& mode
                     , QByteArray const& text)
  {
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::Decode, id, 2};
    out << true << time << snr << delta_time << delta_frequency << mode << text
        << false << false;
    return message;
  }

  QByteArray receiveDatagram (QUdpSocket& socket)
  {
    QByteArray message;
    message.resize (socket.pendingDatagramSize ());
    socket.readDatagram (message.data (), message.size ());
    return message;
  }
}

class TestMessageServer final
  : public QObject
{
  Q_OBJECT

private slots:
  void refreshesClientEndpointWhenSourcePortChanges ()
  {
    qRegisterMetaType<MessageServer::ClientKey> ("ClientKey");

    QHostAddress const loopback {QHostAddress::LocalHost};
    QUdpSocket port_reservation;
    QVERIFY (port_reservation.bind (loopback, 0));
    auto const server_port = port_reservation.localPort ();
    port_reservation.close ();

    QObject owner;
    auto *server = new MessageServer {&owner};
    QSignalSpy clients {server, &MessageServer::client_opened};
    QSignalSpy decodes {server, &MessageServer::decode};
    server->start (server_port);

    QString const id {"WSJT-X"};
    QUdpSocket original_client;
    QVERIFY (original_client.bind (loopback, 0));
    auto const heartbeat_message = heartbeat (id);
    QCOMPARE (original_client.writeDatagram (heartbeat_message, loopback, server_port)
              , qint64 (heartbeat_message.size ()));
    QTRY_COMPARE (clients.count (), 1);
    auto const client_key = clients.at (0).at (0).value<MessageServer::ClientKey> ();

    QTime const time {18, 6};
    qint32 constexpr snr {-5};
    float constexpr delta_time {-0.5f};
    quint32 constexpr delta_frequency {1269};
    QByteArray const mode {"~"};
    QByteArray const text {"CQ NP4TX FK68"};

    QUdpSocket restarted_client;
    QVERIFY (restarted_client.bind (loopback, 0));
    QVERIFY (restarted_client.localPort () != original_client.localPort ());
    auto const decode_message = decode (id, time, snr, delta_time, delta_frequency, mode, text);
    QCOMPARE (restarted_client.writeDatagram (decode_message, loopback, server_port)
              , qint64 (decode_message.size ()));
    QTRY_COMPARE (decodes.count (), 1);

    server->reply (client_key, time, snr, delta_time, delta_frequency
                   , QString::fromUtf8 (mode), QString::fromUtf8 (text), false, 0);

    QTRY_VERIFY_WITH_TIMEOUT (restarted_client.hasPendingDatagrams (), 1000);
    NetworkMessage::Reader reply {receiveDatagram (restarted_client)};
    QCOMPARE (reply.type (), NetworkMessage::Reply);
  }
};

QTEST_GUILESS_MAIN (TestMessageServer)

#include "test_message_server.moc"
