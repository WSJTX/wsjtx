#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

#include <memory>
#include <sstream>

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/make_shared.hpp>

#include "Logger.hpp"
#include "Network/MessageClient.hpp"
#include "Network/NetworkMessage.hpp"

namespace
{
  class CapturedLog final
  {
  public:
    CapturedLog ()
      : stream_ {boost::make_shared<std::ostringstream> ()}
      , sink_ {boost::make_shared<Sink> ()}
    {
      sink_->locked_backend ()->add_stream (stream_);
      sink_->set_formatter (boost::log::expressions::stream
                            << boost::log::trivial::severity << ": " << boost::log::expressions::smessage);
      setSeverity (boost::log::trivial::error);
      boost::log::core::get ()->add_sink (sink_);
      boost::log::core::get ()->set_logging_enabled (true);
    }

    ~CapturedLog ()
    {
      boost::log::core::get ()->set_logging_enabled (false);
      boost::log::core::get ()->remove_sink (sink_);
    }

    void setSeverity (boost::log::trivial::severity_level severity)
    {
      sink_->set_filter (boost::log::trivial::severity >= severity);
    }

    QString text () const {return QString::fromStdString (stream_->str ());}

  private:
    using Sink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;
    boost::shared_ptr<std::ostringstream> stream_;
    boost::shared_ptr<Sink> sink_;
  };

  class DatagramReceiver final
  {
  public:
    DatagramReceiver ()
    {
      QObject::connect (&socket_, &QUdpSocket::readyRead, [&] {drain ();});
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

    QHostAddress senderAddress () const
    {
      return sender_address_;
    }

    quint16 senderPort () const
    {
      return sender_port_;
    }

    void clear ()
    {
      drain ();
      datagrams_.clear ();
    }

  private:
    void drain ()
    {
      while (socket_.hasPendingDatagrams ())
        {
          QByteArray datagram;
          datagram.resize (static_cast<int> (socket_.pendingDatagramSize ()));
          if (0 <= socket_.readDatagram (datagram.data (), datagram.size (),
                                         &sender_address_, &sender_port_))
            {
              datagrams_.append (datagram);
            }
        }
    }

    QUdpSocket socket_;
    QVector<QByteArray> datagrams_;
    QHostAddress sender_address_;
    quint16 sender_port_ {0};
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

  void sendReply (DatagramReceiver& receiver, QTime time, qint32 snr, float delta_time,
                  quint32 delta_frequency, QString const& mode, QString const& message,
                  bool low_confidence = false, quint8 modifiers = 0)
  {
    QByteArray datagram;
    NetworkMessage::Builder out {&datagram, NetworkMessage::Reply, "test-client", 2};
    out << time << snr << delta_time << delta_frequency << mode.toUtf8 () << message.toUtf8 ()
        << low_confidence << modifiers;
    QCOMPARE (receiver.socket ()->writeDatagram (datagram, receiver.senderAddress (),
                                                  receiver.senderPort ()),
              static_cast<qint64> (datagram.size ()));
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
  void initTestCase ()
  {
    Logger::disable ();
  }

  void replyNormalizesDecoderMetadata_data ()
  {
    QTest::addColumn<QString> ("sent");
    QTest::addColumn<QString> ("reply");
    QTest::addColumn<bool> ("sent_low_confidence");
    QTest::addColumn<bool> ("reply_low_confidence");
    QString const text {"CQ JA2XYZ PM95"};
    auto const padded = text.leftJustified (37, ' ');
    auto const annotated = padded + " a1";
    QTest::newRow ("annotation-echo") << annotated << annotated << false << false;
    QTest::newRow ("annotation-stripped") << annotated << padded << false << false;
    QTest::newRow ("padding-trimmed") << annotated << text << false << false;
    QTest::newRow ("padding-trimmed-with-annotation") << annotated << text + " a1" << false << false;
    QTest::newRow ("padding-after-annotation") << annotated << annotated + "  " << false << false;
    QTest::newRow ("annotation-added") << text << annotated << false << false;
    QTest::newRow ("q65-annotation") << padded + " q12" << text << false << false;
    QTest::newRow ("q65-annotation-echo") << padded + " q12" << padded + " q12" << false << false;
    auto const low = QString {"CQ W1AW FN31"}.leftJustified (36, ' ') + "? a1";
    QTest::newRow ("low-confidence-toggled") << low << low << true << false;
    QTest::newRow ("low-confidence-stripped") << low << QString {"CQ W1AW FN31"} << true << false;
    QTest::newRow ("bare-confidence-marker") << low << low.left (37) << true << false;
    QTest::newRow ("low-confidence-added") << annotated << annotated << false << true;
    QTest::newRow ("bare-free-text-question") << QString {"X?"} << QString {"X"} << false << false;
    QTest::newRow ("bare-question-added") << QString {"X"} << QString {"X?"} << false << false;
  }

  void replyNormalizesDecoderMetadata ()
  {
    QFETCH (QString, sent);
    QFETCH (QString, reply);
    QFETCH (bool, sent_low_confidence);
    QFETCH (bool, reply_low_confidence);
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    client->enable (true);
    discardInitialHeartbeat (receiver);
    QSignalSpy replies {client.get (), &MessageClient::reply};
    QTime const time {12, 34, 45};
    client->decode (true, time, -12, -0.0f, 1425, "~", sent, sent_low_confidence, false);
    QTRY_COMPARE (receiver.datagrams ().size (), 1);

    sendReply (receiver, time, -12, 0.0f, 1425, "~", reply, reply_low_confidence, 0x02);
    QTRY_COMPARE (replies.size (), 1);
    QCOMPARE (replies.front ().at (5).toString (), sent);
    QCOMPARE (replies.front ().at (6).toBool (), sent_low_confidence);
    QCOMPARE (replies.front ().at (7).value<quint8> (), quint8 {0x02});
  }

  void replyRequiresMatchingContentAndFields_data ()
  {
    QTest::addColumn<QString> ("field");
    for (auto const field : {"message", "time", "snr", "dt", "df", "mode", "internal-padding",
                             "uppercase-annotation", "annotation-followed-by-content", "embedded-question"})
      QTest::newRow (field) << QString {field};
  }

  void replyRequiresMatchingContentAndFields ()
  {
    QFETCH (QString, field);
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    client->enable (true);
    discardInitialHeartbeat (receiver);
    QSignalSpy replies {client.get (), &MessageClient::reply};
    QTime const time {12, 34, 45};
    QString const text {"CQ JA2XYZ PM95"};
    QString const sent = text.leftJustified (37, ' ') + " a1";
    client->decode (true, time, -12, 0.2f, 1425, "~", sent, false, false);
    QTRY_COMPARE (receiver.datagrams ().size (), 1);
    QString reply = text;
    if (field == "message") reply = "CQ JA2XYZ PM96";
    if (field == "internal-padding") reply = "CQ  JA2XYZ PM95";
    if (field == "uppercase-annotation") reply += " A1";
    if (field == "annotation-followed-by-content") reply += " a1 EXTRA";
    if (field == "embedded-question") reply = "CQ JA2XYZ? PM95";
    sendReply (receiver, field == "time" ? time.addSecs (15) : time,
               field == "snr" ? -11 : -12, field == "dt" ? 0.3f : 0.2f,
               field == "df" ? 1426 : 1425, field == "mode" ? "+" : "~", reply);
    sendReply (receiver, time, -12, 0.2f, 1425, "~", sent);
    QTRY_COMPARE (replies.size (), 1);
    QCOMPARE (replies.front ().at (5).toString (), sent);
  }

  void refusedReplyLogsOnlyAtDebug ()
  {
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    client->enable (true);
    discardInitialHeartbeat (receiver);
    QSignalSpy replies {client.get (), &MessageClient::reply};
    QTime const time {12, 34, 45};
    client->decode (true, time, -12, 0.2f, 1425, "~", "CQ JA2XYZ PM95", false, false);
    QTRY_COMPARE (receiver.datagrams ().size (), 1);
    CapturedLog log;
    for (int attempt = 1; attempt <= 2; ++attempt)
      {
        sendReply (receiver, time, -13, 0.2f, 1425, "~", "CQ W1AW FN31", true, 0x02);
        sendReply (receiver, time, -12, 0.2f, 1425, "~", "CQ JA2XYZ PM95");
        QTRY_COMPARE (replies.size (), attempt);
        if (attempt == 1)
          {
            QVERIFY (log.text ().isEmpty ());
            log.setSeverity (boost::log::trivial::debug);
          }
      }
    QCOMPARE (log.text ().count ("UDP Reply refused:"), 1);
    QVERIFY (log.text ().contains ("debug: UDP Reply refused: decode not found;"));
    for (auto const field : {"time: 12:34:45.000", "snr: -13", "dt: 0.2", "df: 1425", "mode: ~",
                             "message: \"CQ W1AW FN31\"", "low confidence: true", "modifiers: 2"})
      QVERIFY2 (log.text ().contains (field), qPrintable (log.text ()));
  }

  void replyHistoryHonorsInboundClear_data ()
  {
    QTest::addColumn<int> ("window");
    QTest::newRow ("band-activity") << 0;
    QTest::newRow ("rx-frequency") << 1;
    QTest::newRow ("both") << 2;
  }

  void replyHistoryHonorsInboundClear ()
  {
    QFETCH (int, window);
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    client->enable (true);
    discardInitialHeartbeat (receiver);
    QSignalSpy replies {client.get (), &MessageClient::reply};
    QSignalSpy clears {client.get (), &MessageClient::clear_decodes};
    QTime const time {12, 34, 45};
    QString const text = QString {"CQ JA2XYZ PM95"}.leftJustified (37, ' ') + " a1";
    client->decode (true, time, -12, 0.2f, 1425, "~", text, false, false);
    QTRY_COMPARE (receiver.datagrams ().size (), 1);
    QByteArray datagram;
    NetworkMessage::Builder out {&datagram, NetworkMessage::Clear, "test-client", 2};
    out << quint8 (window);
    QCOMPARE (receiver.socket ()->writeDatagram (datagram, receiver.senderAddress (), receiver.senderPort ()),
              qint64 (datagram.size ()));
    QTRY_COMPARE (clears.size (), 1);
    sendReply (receiver, time, -12, 0.2f, 1425, "~", "CQ JA2XYZ PM95");
    client->decode (true, time, -12, 0.2f, 1500, "~", text, false, false);
    sendReply (receiver, time, -12, 0.2f, 1500, "~", text);
    QTRY_COMPARE (replies.size (), window == 1 ? 2 : 1);
    QCOMPARE (replies.back ().at (3).toUInt (), quint32 {1500});
  }

  void replayRecordsDecodeForReply ()
  {
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    client->enable (true);
    discardInitialHeartbeat (receiver);
    QSignalSpy replies {client.get (), &MessageClient::reply};
    QTime const time {12, 34, 45};
    QString const text = QString {"CQ JA2XYZ PM95"}.leftJustified (37, ' ') + " a1";
    QVERIFY (client->begin_replay ());
    client->decode (false, time, -12, 0.2f, 1425, "~", text, false, false);
    client->end_replay ();
    QTRY_COMPARE (receiver.datagrams ().size (), 1);
    QVERIFY (!decodeIsNew (receiver.datagrams ().front ()));
    sendReply (receiver, time, -12, 0.2f, 1425, "~", "CQ JA2XYZ PM95");
    QTRY_COMPARE (replies.size (), 1);
  }

  void replyHistoryEvictsOldestDecode ()
  {
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    client->enable (true);
    discardInitialHeartbeat (receiver);
    QSignalSpy replies {client.get (), &MessageClient::reply};
    QTime const time {12, 34, 45};
    QString const text = QString {"CQ JA2XYZ PM95"}.leftJustified (37, ' ') + " a1";
    for (quint32 df = 1000; df < 6000; ++df)
      {
        client->decode (true, time, -12, 0.2f, df, "~", text, false, false);
        if (df % 100 == 0) QCoreApplication::processEvents ();
      }
    sendReply (receiver, time, -12, 0.2f, 1000, "~", "CQ JA2XYZ PM95");
    QTRY_COMPARE (replies.size (), 1);
    client->decode (true, time, -12, 0.2f, 6000, "~", text, false, false);
    sendReply (receiver, time, -12, 0.2f, 1000, "~", "CQ JA2XYZ PM95");
    sendReply (receiver, time, -12, 0.2f, 1001, "~", "CQ JA2XYZ PM95");
    sendReply (receiver, time, -12, 0.2f, 6000, "~", "CQ JA2XYZ PM95");
    QTRY_COMPARE (replies.size (), 3);
    QCOMPARE (replies.at (1).at (3).toUInt (), quint32 {1001});
    QCOMPARE (replies.at (2).at (3).toUInt (), quint32 {6000});
  }

  void replyMatchesSentDecodeIndependentlyOfDisplay ()
  {
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    client->enable (true);
    discardInitialHeartbeat (receiver);

    QSignalSpy replies {client.get (), &MessageClient::reply};
    QTime const time {12, 34, 45};
    QString const message {"K1ABC N0CALL -10"};
    client->decode (true, time, -12, 0.2f, 1425, "~", message, false, false);
    QTRY_COMPARE (receiver.datagrams ().size (), 1);

    sendReply (receiver, time, -12, 0.2f, 1425, "~", message);

    QTRY_COMPARE (replies.size (), 1);
    QCOMPARE (replies.front ().at (5).toString (), message);
  }

  void replyRequiresUnclearedExactDecode ()
  {
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    client->enable (true);
    discardInitialHeartbeat (receiver);

    QSignalSpy replies {client.get (), &MessageClient::reply};
    QTime const time {12, 34, 45};
    QString const message {"CQ K1ABC FN42"};
    client->decode (true, time, -12, 0.2f, 1425, "~", message, false, false);
    QTRY_COMPARE (receiver.datagrams ().size (), 1);

    sendReply (receiver, time, -12, 0.2f, 1425, "~", "CQ K1ABC FN43");
    QTest::qWait (20);
    QCOMPARE (replies.size (), 0);

    sendReply (receiver, time, -12, 0.2f, 1425, "~", message);
    QTRY_COMPARE (replies.size (), 1);

    client->decodes_cleared ();
    sendReply (receiver, time, -12, 0.2f, 1425, "~", message);
    QTest::qWait (20);
    QCOMPARE (replies.size (), 1);
  }

  void replayIsDeferredPacedAndOrdered ()
  {
    DatagramReceiver receiver;
    QVERIFY (receiver.bind ());
    auto client = makeClient (receiver.port ());
    discardInitialHeartbeat (receiver);
    QElapsedTimer replay_clock;
    QVector<int> replay_batch_sizes;
    QVector<qint64> replay_batch_times;
    QObject::connect (client.get (), &MessageClient::replay_batch_processed,
                      [&] (int message_count) {
                        replay_batch_sizes.append (message_count);
                        replay_batch_times.append (replay_clock.elapsed ());
                      });

    QVERIFY (client->begin_replay ());
    for (int sequence = 0; sequence != 24; ++sequence)
      {
        sendDecode (*client, false, sequence);
      }
    client->WSPR_decode (false, QTime {12, 1}, -20, 0.2f,
                         static_cast<MessageClient::Frequency> (14095600), 0,
                         "K1ABC", "FN42", 37, false);
    sendStatus (*client, "replay-status");
    replay_clock.start ();
    client->end_replay ();

    sendDecode (*client, true, 24);
    client->decodes_cleared ();

    QVERIFY (!receiver.hasPendingDatagrams ());
    QCOMPARE (replay_batch_sizes.size (), 0);
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

    int replayed_messages {0};
    for (auto const batch_size : replay_batch_sizes)
      {
        QVERIFY (batch_size > 0);
        QVERIFY (batch_size <= 10);
        replayed_messages += batch_size;
      }
    QCOMPARE (replayed_messages, 28);
    QVERIFY (replay_batch_sizes.size () >= 3);
    QVERIFY (replay_batch_times.front () >= 8);
    QVERIFY (replay_batch_times.back () - replay_batch_times.front () >= 15);
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
    QVERIFY (original_receiver.datagrams ().size () > 0);

    // Cancel as soon as the first paced batch is observed. Packets already
    // written to the OS before cancel can still arrive after the first
    // readyRead, so settle the original socket before taking a baseline.
    client->set_server_port (new_receiver.port ());
    QTest::qWait (50);
    QCoreApplication::processEvents ();
    auto const delivered_after_cancel = original_receiver.datagrams ().size ();
    QVERIFY (delivered_after_cancel > 0);
    QVERIFY (delivered_after_cancel <= 10);

    sendDecode (*client, true, 25);

    QTRY_COMPARE (new_receiver.datagrams ().size (), 2);
    QTest::qWait (50);
    QCOMPARE (original_receiver.datagrams ().size (), delivered_after_cancel);
    QCOMPARE (messageType (new_receiver.datagrams ().front ()), NetworkMessage::Heartbeat);
    QCOMPARE (messageType (new_receiver.datagrams ().back ()), NetworkMessage::Decode);
    QVERIFY (decodeIsNew (new_receiver.datagrams ().back ()));
  }
};

QTEST_GUILESS_MAIN (TestMessageClientReplay)

#include "test_message_client_replay.moc"
