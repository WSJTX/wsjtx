#include <QtTest>

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

#include <memory>

#include "Network/MessageClient.hpp"
#include "Network/NetworkMessage.hpp"

namespace
{
  class DatagramReceiver final
  {
  public:
    DatagramReceiver ()
    {
      QObject::connect (&socket_, &QUdpSocket::readyRead, [&] {drain ();});
      clock_.start ();
    }

    bool bind ()
    {
      return socket_.bind (QHostAddress {QHostAddress::LocalHost}, 0);
    }

    quint16 port () const
    {
      return socket_.localPort ();
    }

    QUdpSocket * socket ()
    {
      return &socket_;
    }

    bool hasPendingDatagrams () const
    {
      return socket_.hasPendingDatagrams ();
    }

    QVector<QByteArray> const& datagrams () const
    {
      return datagrams_;
    }

    QVector<qint64> const& arrivalTimes () const
    {
      return arrival_times_;
    }

    QVector<int> const& batchSizes () const
    {
      return batch_sizes_;
    }

    void clear ()
    {
      drain ();
      datagrams_.clear ();
      arrival_times_.clear ();
      batch_sizes_.clear ();
      clock_.restart ();
    }

  private:
    void drain ()
    {
      int batch_size {0};
      while (socket_.hasPendingDatagrams ())
        {
          QByteArray datagram;
          datagram.resize (static_cast<int> (socket_.pendingDatagramSize ()));
          if (0 <= socket_.readDatagram (datagram.data (), datagram.size ()))
            {
              datagrams_.append (datagram);
              arrival_times_.append (clock_.elapsed ());
              ++batch_size;
            }
        }
      if (batch_size)
        {
          batch_sizes_.append (batch_size);
        }
    }

    QUdpSocket socket_;
    QElapsedTimer clock_;
    QVector<QByteArray> datagrams_;
    QVector<qint64> arrival_times_;
    QVector<int> batch_sizes_;
  };

  NetworkMessage::Type messageType (QByteArray const& datagram)
  {
    NetworkMessage::Reader reader {datagram};
    return reader.type ();
  }

  bool decodeIsNew (QByteArray const& datagram)
  {
    NetworkMessage::Reader reader {datagram};
    bool is_new {false};
    reader >> is_new;
    return is_new;
  }

  void sendDecode (MessageClient& client, bool is_new, int sequence)
  {
    client.decode (is_new, QTime {12, 0, sequence % 60}, -10, 0.1f,
                   static_cast<quint32> (1000 + sequence), "FT8",
                   QString {"CQ TEST%1 AA00"}.arg (sequence), false, false);
  }

  void sendStatus (MessageClient& client, QString const& tx_message = QString {})
  {
    client.status_update (static_cast<MessageClient::Frequency> (14074000), "FT8", QString {}, QString {},
                          "FT8", false, false, false, 1500, 1500, "N0CALL", "AA00",
                          QString {}, false, QString {}, false, static_cast<quint8> (0),
                          50, 15, "Default", tx_message);
  }

  std::unique_ptr<MessageClient> makeClient (quint16 port)
  {
    return std::unique_ptr<MessageClient> {new MessageClient {
        "test-client", "test-version", "test-revision", "127.0.0.1", port, QStringList {}, 1}};
  }

  void discardInitialHeartbeat (DatagramReceiver& receiver)
  {
    QTRY_VERIFY (!receiver.datagrams ().isEmpty ());
    QCOMPARE (messageType (receiver.datagrams ().front ()), NetworkMessage::Heartbeat);
    receiver.clear ();
  }
}

class TestMessageClientReplay final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void replayIsDeferredPacedAndOrdered ()
  {
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    discardInitialHeartbeat (receiver);

    QVERIFY (client->begin_replay ());
    for (int sequence = 0; sequence != 24; ++sequence)
      {
        sendDecode (*client, false, sequence);
      }
    client->WSPR_decode (false, QTime {12, 1}, -20, 0.2f,
                         static_cast<MessageClient::Frequency> (14095600), 0,
                         "K1ABC", "FN42", 37, false);
    sendStatus (*client, "replay-status");
    client->end_replay ();

    sendDecode (*client, true, 24);
    client->decodes_cleared ();

    QVERIFY (!receiver.hasPendingDatagrams ());
    bool yielded_to_event_loop {false};
    QTimer::singleShot (0, [&] {yielded_to_event_loop = true;});

    QTRY_COMPARE (receiver.datagrams ().size (), 28);
    QVERIFY (yielded_to_event_loop);

    for (int index = 0; index != 24; ++index)
      {
        QCOMPARE (messageType (receiver.datagrams ()[index]), NetworkMessage::Decode);
        QVERIFY (!decodeIsNew (receiver.datagrams ()[index]));
      }
    QCOMPARE (messageType (receiver.datagrams ()[24]), NetworkMessage::WSPRDecode);
    QVERIFY (!decodeIsNew (receiver.datagrams ()[24]));
    QCOMPARE (messageType (receiver.datagrams ()[25]), NetworkMessage::Status);
    QCOMPARE (messageType (receiver.datagrams ()[26]), NetworkMessage::Decode);
    QVERIFY (decodeIsNew (receiver.datagrams ()[26]));
    QCOMPARE (messageType (receiver.datagrams ()[27]), NetworkMessage::Clear);

    QVERIFY (receiver.arrivalTimes ().back () - receiver.arrivalTimes ().front () >= 15);
    for (auto const batch_size : receiver.batchSizes ())
      {
        QVERIFY (batch_size <= 10);
      }
  }

  void repeatedReplayIsCoalesced ()
  {
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    discardInitialHeartbeat (receiver);

    QVERIFY (client->begin_replay ());
    for (int sequence = 0; sequence != 11; ++sequence)
      {
        sendDecode (*client, false, sequence);
      }
    sendStatus (*client);
    client->end_replay ();

    QVERIFY (!client->begin_replay ());
    QTRY_COMPARE (receiver.datagrams ().size (), 12);
    QVERIFY (client->begin_replay ());
    sendStatus (*client);
    client->end_replay ();
    QTRY_COMPARE (receiver.datagrams ().size (), 13);
  }

  void destinationChangeCancelsReplay ()
  {
    DatagramReceiver original_receiver;
    DatagramReceiver new_receiver;
    QVERIFY (original_receiver.bind ());
    QVERIFY (new_receiver.bind ());
    auto client = makeClient (original_receiver.port ());
    discardInitialHeartbeat (original_receiver);

    QSignalSpy first_batch {original_receiver.socket (), &QUdpSocket::readyRead};
    QVERIFY (client->begin_replay ());
    for (int sequence = 0; sequence != 25; ++sequence)
      {
        sendDecode (*client, false, sequence);
      }
    sendStatus (*client);
    client->end_replay ();

    QVERIFY (first_batch.wait (1000));
    auto const delivered_before_change = original_receiver.datagrams ().size ();
    QVERIFY (delivered_before_change > 0);
    QVERIFY (delivered_before_change <= 10);

    client->set_server_port (new_receiver.port ());
    sendDecode (*client, true, 25);

    QTRY_COMPARE (new_receiver.datagrams ().size (), 1);
    QTest::qWait (30);
    QCOMPARE (original_receiver.datagrams ().size (), delivered_before_change);
    QCOMPARE (messageType (new_receiver.datagrams ().front ()), NetworkMessage::Decode);
    QVERIFY (decodeIsNew (new_receiver.datagrams ().front ()));
  }
};

QTEST_GUILESS_MAIN (TestMessageClientReplay)

#include "test_message_client_replay.moc"
