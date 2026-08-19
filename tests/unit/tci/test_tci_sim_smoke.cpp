#include "TciSimScript.hpp"
#include "TciSimServer.hpp"

#include <memory>

#include <QElapsedTimer>
#include <QTimer>
#include <QtTest>
#include <QtWebSockets/QWebSocket>

namespace
{
  struct ReconnectState
  {
    bool client_disconnected {false};
    bool server_disconnected {false};
    bool reopened {false};
  };

  void reconnect_once_after_retirement (
    QWebSocket& client, TciSimServer& server)
  {
    auto state = std::make_shared<ReconnectState> ();
    auto reopen = [state, &client, &server]
      {
        if (state->reopened || !state->client_disconnected
            || !state->server_disconnected)
          {
            return;
          }
        state->reopened = true;
        QTimer::singleShot (0, &client,
                            [&client, &server]
                            {
                              client.open (QUrl {server.url ()});
                            });
      };

    QObject::connect (&client, &QWebSocket::disconnected, &client,
                      [state, reopen]
                      {
                        state->client_disconnected = true;
                        reopen ();
                      });
    QObject::connect (&server, &TciSimServer::client_gone, &client,
                      [state, reopen] (int)
                      {
                        state->server_disconnected = true;
                        reopen ();
                      });
  }
}

class TestTciSim final
  : public QObject
{
  Q_OBJECT

private slots:
  void recordsOrderedFullDuplexTranscript ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());

    QWebSocket client;
    client.open (QUrl {server.url ()});
    QVERIFY (server.wait_for ([&client, &server]
                             {
                               return server.client_connected ()
                                 && client.state () == QAbstractSocket::ConnectedState;
                             }, 1000));

    client.sendTextMessage (QStringLiteral (
      "vfo:0,0,14074000;vfo:0,1,14075000;split_enable:0,true;"));
    QVERIFY (server.wait_for ([&server]
                             {
                               return server.count (
                                 TciSimServer::Direction::ClientToServer) == 3;
                             }, 1000));
    server.send_text (QStringLiteral ("vfo:0,0,14074000;trx:0,false"));

    auto const& frames = server.frames ();
    QCOMPARE (frames.size (), 2);
    QCOMPARE (frames.at (0).raw, QStringLiteral (
      "vfo:0,0,14074000;vfo:0,1,14075000;split_enable:0,true;"));
    QCOMPARE (frames.at (1).raw,
              QStringLiteral ("vfo:0,0,14074000;trx:0,false;"));
    QCOMPARE (frames.at (0).ordinal, quint64 {0});
    QCOMPARE (frames.at (1).ordinal, quint64 {1});

    auto const& transcript = server.transcript ();
    QCOMPARE (transcript.size (), 5);
    QCOMPARE (transcript.at (0).direction,
              TciSimServer::Direction::ClientToServer);
    QCOMPARE (transcript.at (0).connection_generation, 1);
    QCOMPARE (transcript.at (0).ordinal, quint64 {0});
    QCOMPARE (transcript.at (0).raw, QStringLiteral ("vfo:0,0,14074000"));
    QCOMPARE (transcript.at (0).verb, QStringLiteral ("vfo"));
    QCOMPARE (transcript.at (0).args,
              QStringList ({"0", "0", "14074000"}));
    QCOMPARE (transcript.at (1).verb, QStringLiteral ("vfo"));
    QCOMPARE (transcript.at (2).verb, QStringLiteral ("split_enable"));
    QCOMPARE (transcript.at (3).direction,
              TciSimServer::Direction::ServerToClient);
    QCOMPARE (transcript.at (4).verb, QStringLiteral ("trx"));
    QVERIFY (server.occurs_before (TciSimServer::Direction::ClientToServer,
                                   QStringLiteral ("vfo"),
                                   TciSimServer::Direction::ClientToServer,
                                   QStringLiteral ("split_enable")));
  }

  void cursorConsumesRepeatedCommands ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());

    QWebSocket client;
    client.open (QUrl {server.url ()});
    QVERIFY (server.wait_for ([&client, &server]
                             {
                               return server.client_connected ()
                                 && client.state () == QAbstractSocket::ConnectedState;
                             }, 1000));
    client.sendTextMessage (QStringLiteral (
      "vfo:0,0,7074000;vfo:0,0,14074000;vfo:0,0,21074000;"));
    QVERIFY (server.wait_for ([&server]
                             {
                               return server.count (
                                 TciSimServer::Direction::ClientToServer,
                                 QStringLiteral ("vfo")) == 3;
                             }, 1000));

    auto cursor = server.cursor ();
    TciSimServer::Message message;
    QVERIFY (server.consume (&cursor, TciSimServer::Direction::ClientToServer,
                             QStringLiteral ("vfo"), &message));
    QCOMPARE (message.args.at (2), QStringLiteral ("7074000"));
    QVERIFY (server.consume (&cursor, TciSimServer::Direction::ClientToServer,
                             QStringLiteral ("vfo"), &message));
    QCOMPARE (message.args.at (2), QStringLiteral ("14074000"));
    QVERIFY (server.consume (&cursor, TciSimServer::Direction::ClientToServer,
                             QStringLiteral ("vfo"), &message,
                             [] (QStringList const& args)
                             {
                               return args.value (2) == QStringLiteral ("21074000");
                             }));
    QCOMPARE (message.ordinal, quint64 {2});
    QVERIFY (!server.consume (&cursor, TciSimServer::Direction::ClientToServer,
                              QStringLiteral ("vfo")));
    QCOMPARE (server.count (TciSimServer::Direction::ClientToServer,
                            QStringLiteral ("vfo")), 3);
    QCOMPARE (server.index_of (TciSimServer::Direction::ClientToServer,
                               QStringLiteral ("vfo"), 2), 2);
  }

  void scriptGreetsDelaysAndWithholds ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());

    QWebSocket client;
    QStringList received;
    bool command_sent {false};
    QElapsedTimer delayed_reply;
    connect (&client, &QWebSocket::textMessageReceived, this,
             [&client, &received, &command_sent,
              &delayed_reply] (QString const& text)
             {
               received.append (text);
               if (text != QStringLiteral ("ready;") || command_sent) return;
               command_sent = true;
               delayed_reply.start ();
               client.sendTextMessage (QStringLiteral ("vfo:0,0,14074000;"));
             });

    TciSimScript script {&server};
    script.greet (TciSimGreeting::Thetis)
          .withhold (QStringLiteral ("vfo"),
                     [] (QStringList const& args)
                     {
                       return args.value (2) == QStringLiteral ("14074000");
                     })
          .delay (30)
          .push (QStringLiteral ("vfo:0,0,14074000"));
    client.open (QUrl {server.url ()});
    QVERIFY2 (script.run (2000), qPrintable (script.last_error ()));
    QVERIFY (received.contains (QStringLiteral ("ready;")));
    QVERIFY (command_sent);
    QVERIFY (delayed_reply.elapsed () >= 20);
    QCOMPARE (server.count (TciSimServer::Direction::ClientToServer,
                            QStringLiteral ("vfo")), 1);
    QCOMPARE (server.count (TciSimServer::Direction::ServerToClient,
                            QStringLiteral ("vfo")), 3);
  }

  void scriptClosesAndAcceptsReconnect ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());

    QWebSocket client;
    reconnect_once_after_retirement (client, server);

    TciSimScript script {&server};
    script.greet (TciSimGreeting::Minimal)
          .close_normally ()
          .reconnect ()
          .greet (TciSimGreeting::Minimal);
    client.open (QUrl {server.url ()});
    QVERIFY2 (script.run (3000), qPrintable (script.last_error ()));
    QCOMPARE (server.connection_count (), 2);
    QCOMPARE (server.count (TciSimServer::Direction::ServerToClient,
                            QStringLiteral ("ready")), 2);
    auto const ready = server.messages (TciSimServer::Direction::ServerToClient,
                                        QStringLiteral ("ready"));
    QCOMPARE (ready.at (0).connection_generation, 1);
    QCOMPARE (ready.at (1).connection_generation, 2);
  }

  void reconnectExpectationsIgnoreOldConnectionCommands ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());

    QWebSocket client;
    reconnect_once_after_retirement (client, server);
    int client_connection_count {0};
    connect (&client, &QWebSocket::connected, this,
             [&client, &client_connection_count]
             {
               ++client_connection_count;
               if (client_connection_count == 2)
                 {
                   QTimer::singleShot (30, &client,
                                       [&client]
                                       {
                                         client.sendTextMessage (
                                           QStringLiteral ("future:2;"));
                   });
                 }
             });

    TciSimScript script {&server};
    script.expect (QStringLiteral ("trigger"))
          .close_normally ()
          .reconnect ()
          .expect (QStringLiteral ("future"),
                   [] (QStringList const& args) {return args == QStringList {"2"};});
    client.open (QUrl {server.url ()});
    QVERIFY (server.wait_for ([&client, &server]
                             {
                               return server.client_connected ()
                                 && client.state () == QAbstractSocket::ConnectedState;
                             }, 1000));
    QTimer::singleShot (0, &client,
                        [&client]
                        {
                          client.sendTextMessage (
                            QStringLiteral ("trigger;future:1;"));
                        });
    QVERIFY2 (script.run (3000), qPrintable (script.last_error ()));
    auto const future = server.messages (TciSimServer::Direction::ClientToServer,
                                         QStringLiteral ("future"));
    QCOMPARE (future.size (), 2);
    QCOMPARE (future.at (0).connection_generation, 1);
    QCOMPARE (future.at (1).connection_generation, 2);
  }

  void delayedPushDoesNotCrossConnectionGeneration ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());

    QWebSocket client;
    reconnect_once_after_retirement (client, server);
    client.open (QUrl {server.url ()});
    QVERIFY (server.wait_for ([&client, &server]
                             {
                               return server.client_connected ()
                                 && client.state () == QAbstractSocket::ConnectedState;
                             }, 1000));

    TciSimScript script {&server};
    script.push (QStringLiteral ("old-generation"), 50);
    QTimer::singleShot (10, &server, [&server] {server.close_client ();});
    QVERIFY2 (script.run (1000), qPrintable (script.last_error ()));
    QVERIFY (server.wait_for ([&server] {return server.connection_count () == 2;}, 1000));
    QCOMPARE (server.count (TciSimServer::Direction::ServerToClient,
                            QStringLiteral ("old-generation")), 0);
  }

  void replyUsesTheConsumedExpectationGeneration ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());

    QWebSocket client;
    QStringList received;
    connect (&client, &QWebSocket::textMessageReceived, this,
             [&client, &received] (QString const& text)
             {
               received.append (text);
               if (text == QStringLiteral ("ready;"))
                 {
                   client.sendTextMessage (QStringLiteral ("request:1;"));
                 }
             });

    TciSimScript script {&server};
    script.greet (TciSimGreeting::Minimal)
          .expect (QStringLiteral ("request"))
          .reply (QStringLiteral ("ack:1"));
    client.open (QUrl {server.url ()});

    QVERIFY2 (script.run (2000), qPrintable (script.last_error ()));
    QVERIFY (server.wait_for ([&received]
                             {
                               return received.contains (QStringLiteral ("ack:1;"));
                             }, 1000));
    auto const replies = server.messages (
      TciSimServer::Direction::ServerToClient, QStringLiteral ("ack"));
    QCOMPARE (replies.size (), 1);
    QCOMPARE (replies.first ().connection_generation, 1);
  }

  void replyWithoutAnExpectationFails ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());

    QWebSocket client;
    TciSimScript script {&server};
    script.greet (TciSimGreeting::Minimal)
          .reply (QStringLiteral ("ack:1"));
    client.open (QUrl {server.url ()});

    QVERIFY (!script.run (1000));
    QVERIFY (script.last_error ().contains (
      QStringLiteral ("reply requires an expectation")));
    QCOMPARE (server.count (TciSimServer::Direction::ServerToClient,
                            QStringLiteral ("ack")), 0);
  }

  void closeWithoutAClientFails ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());

    TciSimScript script {&server};
    script.close_normally ();

    QVERIFY (!script.run (100));
    QVERIFY (script.last_error ().contains (
      QStringLiteral ("cannot close without a connected client")));
    QCOMPARE (server.connection_count (), 0);
  }

  void failureIncludesTranscript ()
  {
    TciSimServer server;
    QVERIFY (server.listen ());
    QWebSocket client;
    client.open (QUrl {server.url ()});
    QVERIFY (server.wait_for ([&server] { return server.client_connected (); }, 1000));
    client.sendTextMessage (QStringLiteral ("vfo:0,0,7074000;"));

    TciSimScript script {&server};
    script.expect (QStringLiteral ("trx"), {}, 20);
    QVERIFY (!script.run (500));
    QCOMPARE (script.failed_step (), 1);
    QVERIFY (script.last_error ().contains (QStringLiteral ("expecting trx")));
    QVERIFY (script.last_error ().contains (QStringLiteral ("vfo:0,0,7074000")));
  }

  void waitForShortCircuits ()
  {
    TciSimServer server;
    QElapsedTimer elapsed;
    elapsed.start ();
    QVERIFY (server.wait_for ([] { return true; }, 1000));
    QVERIFY (elapsed.elapsed () < 100);
  }
};

QTEST_MAIN (TestTciSim)

#include "test_tci_sim_smoke.moc"
