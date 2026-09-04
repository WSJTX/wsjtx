#include <QtTest>

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include <memory>

#include "Network/MessageClient.hpp"
#include "Network/NetworkMessage.hpp"

class TestMessageClientInhibitStatus final
  : public QObject
{
  Q_OBJECT

private:
  static QByteArray command (quint32 ttl = 1000, QByteArray controller = "key-agent",
                             QByteArray station = "W2SZ", QString id = "test-client",
                             quint32 schema = 2)
  {
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::TxInhibit, id, schema};
    out << controller << ttl << station;
    return message;
  }

private Q_SLOTS:
  void acceptUdpRequestsAuthorizesCommands ()
  {
    QUdpSocket receiver;
    QVERIFY (receiver.bind (QHostAddress {QHostAddress::LocalHost}, 0));
    MessageClient client {"test-client", "version", "revision", "127.0.0.1",
                          receiver.localPort (), {}, 1};
    QTRY_VERIFY (receiver.hasPendingDatagrams ());
    auto const endpoint = receiver.receiveDatagram ();
    QSignalSpy commands {&client, &MessageClient::tx_inhibit_command};
    QSignalSpy clears {&client, &MessageClient::clear_decodes};
    QSignalSpy invalid {&client, &MessageClient::tx_inhibit_invalid};
    QByteArray clear;
    NetworkMessage::Builder out {&clear, NetworkMessage::Clear, "test-client", 2};
    out << quint8 {2};
    auto send = [&] (QByteArray const& bytes) {
      QCOMPARE (receiver.writeDatagram (bytes, endpoint.senderAddress (), endpoint.senderPort ()),
                qint64 (bytes.size ()));
    };
    send (command ());
    send (command (99));
    send (clear);
    QTest::qWait (150);
    QCOMPARE (commands.size (), 0);
    QCOMPARE (invalid.size (), 0);
    QCOMPARE (clears.size (), 0);
    client.enable (true);
    send (command (100));
    send (command (30000, "key-agent", "Priority", "test-client", 3));
    send (command (0, "key-agent", {}));
    QTRY_COMPARE (commands.size (), 3);
    QCOMPARE (commands[0][0].toString (), QString {"key-agent"});
    QCOMPARE (commands[0][1].toUInt (), 100u);
    QCOMPARE (commands[1][1].toUInt (), 30000u);
    QCOMPARE (commands[1][2].toString (), QString {"Priority"});
    QCOMPARE (commands[2][1].toUInt (), 0u);
    QVERIFY (commands[2][2].toString ().isEmpty ());
    send (command (1000, "key-agent", {}, "other-instance"));
    send (clear);
    QTRY_COMPARE (clears.size (), 1);
    QCOMPARE (commands.size (), 3);
    client.enable (false);
    send (command ());
    send (command (99));
    send (clear);
    QTest::qWait (150);
    QCOMPARE (commands.size (), 3);
    QCOMPARE (invalid.size (), 0);
    QCOMPARE (clears.size (), 1);
  }

  void rejectsMalformedCommands ()
  {
    QUdpSocket receiver;
    QVERIFY (receiver.bind (QHostAddress {QHostAddress::LocalHost}, 0));
    MessageClient client {"test-client", "version", "revision", "127.0.0.1",
                          receiver.localPort (), {}, 1};
    client.enable (true);
    QTRY_VERIFY (receiver.hasPendingDatagrams ());
    auto const endpoint = receiver.receiveDatagram ();
    QSignalSpy commands {&client, &MessageClient::tx_inhibit_command};
    QSignalSpy invalid {&client, &MessageClient::tx_inhibit_invalid};
    QList<QByteArray> bad {
      command (99), command (30001), command (0xffffffff), command (1000, {}),
      command (1000, "two words"), command (1000, QByteArray (129, 'a')),
      command (1000, QByteArray ("\xc0\xaf", 2)), command (1000, "key", QByteArray (129, 'a')),
      command (1000, "key", QByteArray ("\xff", 1)), command (1000, "key", "line\nbreak"),
      command (1000, "key", {}, "test-client", 1), command () + QByteArray (4096, 'a')
    };
    auto const valid = command ();
    for (int length = 12; length < valid.size (); ++length) bad.append (valid.left (length));
    auto corrupt = valid;
    corrupt[0] = 0;
    bad.append (corrupt);
    corrupt = valid;
    corrupt[7] = 99;
    bad.append (corrupt);
    corrupt = valid;
    for (int index = 12; index < 16; ++index) corrupt[index] = char (0xff);
    bad.append (corrupt);
    for (auto const& bytes : bad)
      QCOMPARE (receiver.writeDatagram (bytes, endpoint.senderAddress (), endpoint.senderPort ()),
                qint64 (bytes.size ()));
    auto count = [&] {
      quint64 result {0};
      for (auto const& event : invalid) result += event[0].toULongLong ();
      return result;
    };
    QTRY_COMPARE (count (), quint64 (bad.size ()));
    QCOMPARE (commands.size (), 0);
    QVERIFY (invalid.size () < bad.size ());
    auto extended = command () + QByteArray {"future fields"};
    receiver.writeDatagram (extended, endpoint.senderAddress (), endpoint.senderPort ());
    QTRY_COMPARE (commands.size (), 1);
  }

  void receivesCommandsWhenOutgoingReportingIsDisabled ()
  {
    QUdpSocket receiver;
    QVERIFY (receiver.bind (QHostAddress {QHostAddress::LocalHost}, 0));
    MessageClient client {"test-client", "version", "revision", "127.0.0.1",
                          receiver.localPort (), {}, 1};
    client.enable (true);
    QTRY_VERIFY (receiver.hasPendingDatagrams ());
    auto const endpoint = receiver.receiveDatagram ();
    QSignalSpy commands {&client, &MessageClient::tx_inhibit_command};
    client.set_server_port (0);
    auto const bytes = command ();
    QCOMPARE (receiver.writeDatagram (bytes, endpoint.senderAddress (), endpoint.senderPort ()),
              qint64 (bytes.size ()));
    QTRY_COMPARE (commands.size (), 1);
  }

  void reportsInvalidServerThroughExistingErrorSignal ()
  {
    MessageClient client {"test-client", "version", "revision", "", 0, {}, 1};
    QSignalSpy errors {&client, &MessageClient::error};
    client.set_server ("255.255.255.255", {});
    QCOMPARE (errors.size (), 1);
    QVERIFY (errors[0][0].toString ().contains ("IPv4 broadcast not supported"));
  }

  void portChangeDuringLookupDoesNotCloseClient ()
  {
    QUdpSocket receiver;
    QVERIFY (receiver.bind (QHostAddress {QHostAddress::Any}, 0));
    MessageClient client {"test-client", "version", "revision", "127.0.0.1", 0, {}, 1};
    QSignalSpy closed {&client, &MessageClient::close};
    client.set_server ("localhost", {});
    client.set_server_port (receiver.localPort ());
    QCOMPARE (closed.size (), 0);
    QTRY_VERIFY (receiver.hasPendingDatagrams ());
    QCOMPARE (closed.size (), 0);
  }

  void preservesType17FieldOrder ()
  {
    QUdpSocket receiver;
    QVERIFY (receiver.bind (QHostAddress {QHostAddress::LocalHost}, 0));

    auto client = std::unique_ptr<MessageClient> {new MessageClient {
      "test-client", "test-version", "test-revision",
      "127.0.0.1", receiver.localPort (), {}, 1}};
    QTRY_VERIFY (receiver.hasPendingDatagrams ());
    while (receiver.hasPendingDatagrams ())
      {
        receiver.receiveDatagram ();
      }

    client->inhibit_status (true, true, QStringLiteral ("W2SZ"), 11, 12, 13, 14);
    QTRY_VERIFY (receiver.hasPendingDatagrams ());
    auto const datagram = receiver.receiveDatagram ().data ();

    NetworkMessage::Reader reader {datagram};
    QCOMPARE (reader.type (), NetworkMessage::InhibitStatus);
    QCOMPARE (reader.id (), QStringLiteral ("test-client"));

    bool supported {false};
    bool inhibited {false};
    QByteArray source;
    quint32 hold_rx {0};
    quint32 release_rx {0};
    quint32 expiries {0};
    quint32 invalid {0};
    reader >> supported >> inhibited >> source >> hold_rx >> release_rx
           >> expiries >> invalid;

    QCOMPARE (reader.status (), QDataStream::Ok);
    QVERIFY (supported);
    QVERIFY (reader.atEnd ());
    QVERIFY (inhibited);
    QCOMPARE (source, QByteArrayLiteral ("W2SZ"));
    QCOMPARE (hold_rx, 11u);
    QCOMPARE (release_rx, 12u);
    QCOMPARE (expiries, 13u);
    QCOMPARE (invalid, 14u);
  }
};

QTEST_GUILESS_MAIN (TestMessageClientInhibitStatus)
#include "test_message_client_inhibit_status.moc"
