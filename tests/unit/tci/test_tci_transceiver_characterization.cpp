#include <functional>
#include <memory>

#include <boost/log/keywords/channel.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QTimer>
#include <QVector>

#include "commons.h"
#include "TciSimScript.hpp"
#include "TciSimServer.hpp"
#include "Transceiver/TCITransceiver.hpp"
#include "widgets/itoneAndicw.h"

namespace
{
  dec_data_t test_dec_data {};

  using Direction = TciSimServer::Direction;
  using Message = TciSimServer::Message;
  using State = Transceiver::TransceiverState;
  using Frequency = Transceiver::Frequency;
  using CommandHandler = std::function<bool (QString const&, QStringList const&, int)>;

  Frequency constexpr initial_frequency {14074000};

  using RigFactory = std::function<std::unique_ptr<Transceiver> (
    Transceiver::logger_type *, QString const&)>;

  RigFactory legacy_rig_factory ()
  {
    return [] (Transceiver::logger_type * logger, QString const& address)
      {
        return std::unique_ptr<Transceiver> {
          new TCITransceiver {logger, {}, QStringLiteral ("0"), address,
                              true, 0}};
      };
  }

  int message_index (
    TciSimServer const& server, int start, Direction direction,
    QString const& verb,
    std::function<bool (Message const&)> const& predicate = {})
  {
    auto const& transcript = server.transcript ();
    for (int i = qMax (0, start); i < transcript.size (); ++i)
      {
        auto const& message = transcript.at (i);
        if (message.direction == direction && message.verb == verb
            && (!predicate || predicate (message)))
          {
            return i;
          }
      }
    return -1;
  }

  bool has_args (Message const& message, QStringList const& args)
  {
    return message.args == args;
  }

  class RigHarness
  {
  public:
    using StatePredicate = std::function<bool (State const&)>;

    explicit RigHarness (RigFactory factory = legacy_rig_factory ())
      : logger_ {boost::log::keywords::channel = "TCI_CHARACTERIZATION"}
      , factory_ {std::move (factory)}
    {
      QObject::connect (&server_, &TciSimServer::client_arrived, &server_,
                        [this] (int)
                        {
                          server_.send_text (startup_greeting ());
                        });
      QObject::connect (&server_, &TciSimServer::text_received, &server_,
                        [this] (QString const& text, int generation)
                        {
                          handle_client_text (text, generation);
                        });
    }

    ~RigHarness ()
    {
      tearing_down_ = true;
      command_handler_ = {};
      destroy_rig ();
      server_.close ();
    }

    bool listen ()
    {
      if (!server_.listen ()) return false;
      create_rig ();
      return true;
    }

    bool start (unsigned sequence_number = 1)
    {
      auto const update_cursor = observations_.size ();
      auto const previous_generation = server_.connection_generation ();
      auto * rig = rig_.get ();
      QTimer::singleShot (0, rig,
                          [rig, sequence_number]
                          {
                            rig->start (sequence_number);
                          });
      auto const completed = server_.wait_for (
        [this, update_cursor, previous_generation, sequence_number]
        {
          return server_.client_connected ()
            && server_.connection_generation () > previous_generation
            && observed_state_since (
              update_cursor, sequence_number,
              [] (State const& state) {return state.online ();});
        }, 7000);
      return completed && server_.client_connected ()
        && server_.connection_generation () > previous_generation
        && observed_state_since (
          update_cursor, sequence_number,
          [] (State const& state) {return state.online ();});
    }

    bool restart (unsigned sequence_number)
    {
      return start (sequence_number);
    }

    bool stop ()
    {
      auto const completed = std::make_shared<bool> (false);
      auto * rig = rig_.get ();
      QTimer::singleShot (0, rig,
                          [rig, completed]
                          {
                            rig->stop ();
                            *completed = true;
                          });
      return server_.wait_for (
        [this, completed]
        {
          return *completed && server_.client_disconnected ();
        }, 7000);
    }

    int set (State state, unsigned sequence_number)
    {
      auto const update_cursor = observations_.size ();
      auto * rig = rig_.get ();
      QTimer::singleShot (0, rig,
                          [rig, state, sequence_number]
                          {
                            rig->set (state, sequence_number);
                          });
      return update_cursor;
    }

    void set_now (State const& state, unsigned sequence_number)
    {
      rig_->set (state, sequence_number);
    }

    void schedule_set (State state, unsigned sequence_number)
    {
      QTimer::singleShot (0, &server_,
                          [this, state, sequence_number]
                          {
                            if (!tearing_down_ && rig_)
                              {
                                rig_->set (state, sequence_number);
                              }
                          });
    }

    void set_command_handler (CommandHandler handler)
    {
      command_handler_ = std::move (handler);
    }

    TciSimServer& server () {return server_;}
    Transceiver& rig () {return *rig_;}
    State const& state () const {return state_;}
    int observation_count () const {return observations_.size ();}
    int failure_count () const {return failures_.size ();}

    bool observed_state_since (
      int cursor, StatePredicate const& predicate) const
    {
      for (int i = qMax (0, cursor); i < observations_.size (); ++i)
        {
          if (predicate (observations_.at (i).state)) return true;
        }
      return false;
    }

    bool observed_state_since (
      int cursor, unsigned sequence_number,
      StatePredicate const& predicate) const
    {
      for (int i = qMax (0, cursor); i < observations_.size (); ++i)
        {
          auto const& observation = observations_.at (i);
          if (observation.sequence_number == sequence_number
              && predicate (observation.state))
            {
              return true;
            }
        }
      return false;
    }

    bool wait_for_state (
      int cursor, unsigned sequence_number,
      StatePredicate predicate, int timeout_ms = 6000)
    {
      auto const failure_cursor = failures_.size ();
      auto const completed = server_.wait_for (
        [this, cursor, sequence_number, &predicate, failure_cursor]
        {
          return failures_.size () > failure_cursor
            || observed_state_since (cursor, sequence_number, predicate);
        }, timeout_ms);
      return completed && failures_.size () == failure_cursor
        && observed_state_since (cursor, sequence_number, predicate);
    }

    bool wait_for_current_state (StatePredicate predicate, int timeout_ms = 1000)
    {
      return server_.wait_for ([this, &predicate] {return predicate (state_);},
                               timeout_ms);
    }

    bool wait_for_failure (int cursor, int timeout_ms = 6000)
    {
      return server_.wait_for ([this, cursor] {return failures_.size () > cursor;},
                               timeout_ms);
    }

    QString diagnostics () const
    {
      auto const sequence = observations_.isEmpty ()
        ? 0u : observations_.last ().sequence_number;
      auto const failure = failures_.isEmpty ()
        ? QStringLiteral ("none") : failures_.last ();
      return QStringLiteral (
        "connected=%1 generation=%2 updates=%3 sequence=%4 online=%5 "
        "rx=%6 tx=%7 split=%8 ptt=%9 failure=%10\n%11")
        .arg (server_.client_connected ())
        .arg (server_.connection_generation ())
        .arg (observations_.size ())
        .arg (sequence)
        .arg (state_.online ())
        .arg (state_.frequency ())
        .arg (state_.tx_frequency ())
        .arg (state_.split ())
        .arg (state_.ptt ())
        .arg (failure, server_.transcript_text ());
    }

  private:
    struct StateObservation
    {
      State state;
      unsigned sequence_number;
    };

    static QString startup_greeting ()
    {
      return TciSimScript::greeting_messages (TciSimGreeting::Thetis).join (';')
        + ';';
    }

    void create_rig ()
    {
      auto const address = QStringLiteral ("127.0.0.1:%1").arg (server_.port ());
      rig_ = factory_ (&logger_, address);
      QObject::connect (
        rig_.get (), &Transceiver::update, &server_,
        [this] (State const& state, unsigned sequence_number)
        {
          state_ = state;
          observations_.append ({state, sequence_number});
        });
      QObject::connect (
        rig_.get (), &Transceiver::failure, &server_,
        [this] (QString const& reason) {failures_.append (reason);});
    }

    void destroy_rig ()
    {
      if (!rig_) return;

      bool client_gone = !server_.client_connected ();
      auto const connection = QObject::connect (
        &server_, &TciSimServer::client_gone, &server_,
        [&client_gone] (int) {client_gone = true;});
      rig_->stop ();
      if (!client_gone)
        {
          server_.wait_for ([&client_gone] {return client_gone;}, 1000);
        }
      QObject::disconnect (connection);
      rig_.reset ();
      QCoreApplication::processEvents (QEventLoop::AllEvents, 20);
    }

    void handle_client_text (QString const& text, int generation)
    {
      auto const commands = text.split (';', Qt::SkipEmptyParts);
      for (auto const& command : commands)
        {
          auto const separator = command.indexOf (':');
          auto const verb = separator < 0
            ? command.trimmed () : command.left (separator).trimmed ();
          auto const args = separator < 0
            ? QStringList {} : command.mid (separator + 1).split (',', Qt::KeepEmptyParts);
          if (command_handler_ && command_handler_ (verb, args, generation)) continue;
          acknowledge (verb, args);
        }
    }

    void acknowledge (QString const& verb, QStringList const& args)
    {
      if (verb == QStringLiteral ("vfo") && args.size () >= 3)
        {
          server_.send_text (QStringLiteral ("vfo:%1,%2,%3")
                             .arg (args.at (0), args.at (1), args.at (2)));
        }
      else if (verb == QStringLiteral ("modulation") && args.size () >= 2)
        {
          server_.send_text (QStringLiteral ("modulation:%1,%2")
                             .arg (args.at (0), args.at (1)));
        }
      else if (verb == QStringLiteral ("split_enable"))
        {
          auto const enabled = args.size () >= 2 ? args.at (1) : args.value (0);
          server_.send_text (QStringLiteral ("split_enable:0,%1").arg (enabled));
        }
      else if (verb == QStringLiteral ("trx") && args.size () >= 2)
        {
          server_.send_text (QStringLiteral ("trx:%1,%2")
                             .arg (args.at (0), args.at (1)));
        }
      else if (verb == QStringLiteral ("rx_enable") && args.size () >= 2)
        {
          server_.send_text (QStringLiteral ("rx_enable:%1,%2")
                             .arg (args.at (0), args.at (1)));
        }
      else if (verb == QStringLiteral ("start") || verb == QStringLiteral ("stop"))
        {
          server_.send_text (verb);
        }
    }

    TciSimServer server_;
    Transceiver::logger_type logger_;
    RigFactory factory_;
    std::unique_ptr<Transceiver> rig_;
    CommandHandler command_handler_;
    QVector<StateObservation> observations_;
    QStringList failures_;
    State state_;
    bool tearing_down_ {false};
  };

  State state_from (RigHarness const& harness)
  {
    auto state = harness.state ();
    state.online (true);
    return state;
  }
}

// The TCI object references application-owned DSP globals through wsjt_qt.
dec_data_t& dec_data = test_dec_data;
int volatile itone[MAX_NUM_SYMBOLS] {};
int volatile icw[NUM_CW_SYMBOLS] {};

float gran ()
{
  return 0.0f;
}

class TestTciTransceiverCharacterization final
  : public QObject
{
  Q_OBJECT

private slots:
  void initTestCase ()
  {
    Logger::disable ();
    qRegisterMetaType<Transceiver::TransceiverState> ();
  }

  void startup_adopts_greeting_and_preserves_framing ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));

    auto const& server = harness.server ();
    QCOMPARE (server.connection_count (), 1);
    QCOMPARE (harness.state ().frequency (), initial_frequency);
    QCOMPARE (harness.state ().tx_frequency (), Frequency {0});
    QCOMPARE (harness.state ().mode (), Transceiver::DIG_U);
    QVERIFY (!harness.state ().split ());

    auto const& frames = server.frames ();
    QVERIFY (!frames.isEmpty ());
    QCOMPARE (frames.first ().raw,
              TciSimScript::greeting_messages (TciSimGreeting::Thetis).join (';')
                + ';');

    auto const device = message_index (server, 0, Direction::ServerToClient,
                                       QStringLiteral ("device"));
    auto const rx_vfo = message_index (server, device + 1, Direction::ServerToClient,
                                       QStringLiteral ("vfo"));
    auto const mode = message_index (server, rx_vfo + 1, Direction::ServerToClient,
                                     QStringLiteral ("modulation"));
    auto const ready = message_index (server, mode + 1, Direction::ServerToClient,
                                      QStringLiteral ("ready"));
    QVERIFY2 (device >= 0 && rx_vfo > device && mode > rx_vfo
              && ready > mode,
              qPrintable (harness.diagnostics ()));
  }

  void rx_vfo_acknowledgement_precedes_dependent_mode ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));

    Frequency constexpr target {14074100};
    auto const operation_start = harness.server ().transcript ().size ();
    harness.set_command_handler (
      [&harness] (QString const& verb, QStringList const& args, int)
      {
        if (verb == QStringLiteral ("vfo") && args == QStringList {"0", "0", "14074100"})
          {
            harness.server ().send_text_later (
              QStringLiteral ("unexpected:payload;vfo:1,0,14074100"), 5);
            harness.server ().send_text_later (QStringLiteral ("vfo:0,0,14074100"), 30);
            return true;
          }
        return false;
      });

    auto requested = state_from (harness);
    requested.frequency (target);
    requested.mode (Transceiver::USB);
    auto const update_cursor = harness.set (requested, 10);
    QVERIFY2 (harness.wait_for_state (
      update_cursor, 10,
      [] (State const& state)
      {
        return state.frequency () == target && state.mode () == Transceiver::USB;
      }), qPrintable (harness.diagnostics ()));

    auto const& server = harness.server ();
    auto const vfo_command = message_index (
      server, operation_start, Direction::ClientToServer, QStringLiteral ("vfo"),
      [] (Message const& message) {return has_args (message, {"0", "0", "14074100"});});
    auto const vfo_ack = message_index (
      server, vfo_command + 1, Direction::ServerToClient, QStringLiteral ("vfo"),
      [] (Message const& message) {return has_args (message, {"0", "0", "14074100"});});
    auto const mode_command = message_index (
      server, operation_start, Direction::ClientToServer, QStringLiteral ("modulation"),
      [] (Message const& message) {return has_args (message, {"0", "usb"});});
    QVERIFY2 (vfo_command >= 0 && vfo_ack > vfo_command && mode_command > vfo_ack,
              qPrintable (harness.diagnostics ()));
    QCOMPARE (harness.state ().frequency (), target);
    QCOMPARE (harness.state ().mode (), Transceiver::USB);

    auto const vfo_count = server.count (Direction::ClientToServer,
                                         QStringLiteral ("vfo"));
    auto const mode_count = server.count (Direction::ClientToServer,
                                          QStringLiteral ("modulation"));
    QTest::qWait (75);
    QCOMPARE (server.count (Direction::ClientToServer, QStringLiteral ("vfo")),
              vfo_count);
    QCOMPARE (server.count (Direction::ClientToServer,
                            QStringLiteral ("modulation")), mode_count);
  }

  void unacknowledged_rx_vfo_fails_boundedly_without_dependent_commands ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));
    auto const failure_cursor = harness.failure_count ();

    Frequency constexpr target {7074125};
    auto const operation_start = harness.server ().transcript ().size ();
    harness.set_command_handler (
      [] (QString const& verb, QStringList const& args, int)
      {
        return verb == QStringLiteral ("vfo")
          && args == QStringList {"0", "0", "7074125"};
      });
    auto requested = state_from (harness);
    requested.frequency (target);
    requested.mode (Transceiver::USB);

    QElapsedTimer deadline;
    deadline.start ();
    harness.set (requested, 20);
    QVERIFY2 (harness.wait_for_failure (failure_cursor),
              qPrintable (harness.diagnostics ()));

    auto const& server = harness.server ();
    auto const attempts = server.messages (Direction::ClientToServer,
                                           QStringLiteral ("vfo"));
    int matching_attempts {0};
    for (auto const& attempt : attempts)
      {
        if (attempt.ordinal < static_cast<quint64> (operation_start)) continue;
        QVERIFY2 (has_args (attempt, {"0", "0", "7074125"}),
                  qPrintable (harness.diagnostics ()));
        ++matching_attempts;
      }
    QVERIFY2 (matching_attempts >= 1, qPrintable (harness.diagnostics ()));
    QCOMPARE (message_index (server, operation_start, Direction::ClientToServer,
                             QStringLiteral ("modulation")), -1);
    QVERIFY (deadline.elapsed () < 6000);
    QVERIFY (harness.failure_count () - failure_cursor <= 1);
    QVERIFY (harness.wait_for_current_state (
      [] (State const& state) {return !state.online ();}));

    auto const attempts_after_failure = matching_attempts;
    QTest::qWait (50);
    int attempts_after_wait {0};
    for (auto const& attempt : server.messages (Direction::ClientToServer,
                                                QStringLiteral ("vfo")))
      {
        if (attempt.ordinal >= static_cast<quint64> (operation_start)
            && has_args (attempt, {"0", "0", "7074125"}))
          {
            ++attempts_after_wait;
          }
      }
    QCOMPARE (attempts_after_wait, attempts_after_failure);
  }

  void split_and_tx_vfo_settle_before_ptt ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));

    Frequency constexpr tx_frequency {14076000};
    auto const operation_start = harness.server ().transcript ().size ();
    struct AcknowledgementState
    {
      bool split_scheduled {false};
      bool split_sent {false};
      bool tx_vfo_scheduled {false};
      bool tx_vfo_sent {false};
      bool ptt_before_prerequisites {false};
    };
    auto const acknowledgements = std::make_shared<AcknowledgementState> ();
    harness.set_command_handler (
      [&harness, acknowledgements] (QString const& verb,
                                   QStringList const& args, int)
      {
        if (verb == QStringLiteral ("split_enable")
            && args == QStringList {"0", "true"})
          {
            if (!acknowledgements->split_scheduled
                && !acknowledgements->split_sent)
              {
                acknowledgements->split_scheduled = true;
                QTimer::singleShot (
                  25, &harness.server (),
                  [&harness, acknowledgements]
                  {
                    acknowledgements->split_sent = true;
                    harness.server ().send_text (
                      QStringLiteral ("split_enable:0,true"));
                  });
              }
            return !acknowledgements->split_sent;
          }
        if (verb == QStringLiteral ("vfo")
            && args == QStringList {"0", "1", "14076000"})
          {
            if (!acknowledgements->tx_vfo_scheduled
                && !acknowledgements->tx_vfo_sent)
              {
                acknowledgements->tx_vfo_scheduled = true;
                QTimer::singleShot (
                  25, &harness.server (),
                  [&harness, acknowledgements]
                  {
                    acknowledgements->tx_vfo_sent = true;
                    harness.server ().send_text (
                      QStringLiteral ("vfo:0,1,14076000"));
                  });
              }
            return !acknowledgements->tx_vfo_sent;
          }
        if (verb == QStringLiteral ("trx")
            && args == QStringList {"0", "true"})
          {
            acknowledgements->ptt_before_prerequisites =
              acknowledgements->ptt_before_prerequisites
              || !acknowledgements->split_sent
              || !acknowledgements->tx_vfo_sent;
          }
        return false;
      });
    auto requested = state_from (harness);
    requested.tx_frequency (tx_frequency);
    requested.split (true);
    requested.ptt (true);
    auto const update_cursor = harness.set (requested, 30);
    QVERIFY2 (harness.wait_for_state (
      update_cursor, 30,
      [] (State const& state)
      {
        return state.split () && state.tx_frequency () == tx_frequency
          && state.ptt ();
      }), qPrintable (harness.diagnostics ()));

    auto const& server = harness.server ();
    auto const split_command = message_index (
      server, operation_start, Direction::ClientToServer,
      QStringLiteral ("split_enable"),
      [] (Message const& message) {return has_args (message, {"0", "true"});});
    auto const split_ack = message_index (
      server, operation_start, Direction::ServerToClient, QStringLiteral ("split_enable"),
      [] (Message const& message) {return has_args (message, {"0", "true"});});
    auto const tx_vfo_command = message_index (
      server, operation_start, Direction::ClientToServer, QStringLiteral ("vfo"),
      [] (Message const& message)
      {
        return has_args (message, {"0", "1", "14076000"});
      });
    auto const tx_vfo_ack = message_index (
      server, operation_start, Direction::ServerToClient, QStringLiteral ("vfo"),
      [] (Message const& message) {return has_args (message, {"0", "1", "14076000"});});
    auto const ptt_command = message_index (
      server, operation_start, Direction::ClientToServer, QStringLiteral ("trx"),
      [] (Message const& message) {return message.args.size () >= 2
                                         && message.args.at (0) == "0"
                                         && message.args.at (1) == "true";});
    QVERIFY2 (!acknowledgements->ptt_before_prerequisites && split_command >= 0
              && split_ack > split_command && tx_vfo_command >= 0
              && tx_vfo_ack > tx_vfo_command && ptt_command > split_ack
              && ptt_command > tx_vfo_ack,
              qPrintable (harness.diagnostics ()));
    QVERIFY (harness.state ().split ());
    QCOMPARE (harness.state ().tx_frequency (), tx_frequency);
    QVERIFY (harness.state ().ptt ());
  }

  void unsolicited_split_divergence_converges_without_ptt ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));

    auto requested = state_from (harness);
    requested.tx_frequency (14076050);
    requested.split (true);
    auto const setup_cursor = harness.set (requested, 35);
    QVERIFY2 (harness.wait_for_state (
      setup_cursor, 35,
      [] (State const& state) {return state.split ();}),
      qPrintable (harness.diagnostics ()));
    QVERIFY (harness.state ().split ());

    auto const operation_start = harness.server ().transcript ().size ();
    auto const observation_cursor = harness.observation_count ();
    harness.server ().send_text (QStringLiteral ("split_enable:0,false"));

    QVERIFY (harness.server ().wait_for (
      [&harness, operation_start, observation_cursor]
      {
        return message_index (
          harness.server (), operation_start, Direction::ClientToServer,
          QStringLiteral ("split_enable"),
          [] (Message const& message) {return has_args (message, {"0", "true"});})
            >= 0
          || harness.observed_state_since (
            observation_cursor,
            [] (State const& state) {return !state.split ();});
      }, 1000));

    auto const correction = message_index (
      harness.server (), operation_start, Direction::ClientToServer,
      QStringLiteral ("split_enable"),
      [] (Message const& message) {return has_args (message, {"0", "true"});});
    if (correction >= 0)
      {
        QVERIFY (harness.server ().wait_for (
          [&harness] {return harness.state ().split ();}, 1000));
      }
    else
      {
        QVERIFY2 (!harness.state ().split (),
                  qPrintable (harness.diagnostics ()));
      }
    QCOMPARE (message_index (harness.server (), operation_start,
                             Direction::ClientToServer,
                             QStringLiteral ("trx")), -1);
  }

  void explicit_stop_can_restart_on_a_new_connection_generation ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));
    auto const first_generation = harness.server ().connection_generation ();

    QVERIFY2 (harness.stop (), qPrintable (harness.diagnostics ()));
    QVERIFY2 (harness.restart (41), qPrintable (harness.diagnostics ()));

    QVERIFY (harness.server ().connection_generation () > first_generation);
    auto const second_generation = harness.server ().connection_generation ();
    QVERIFY (harness.state ().online ());
    QCOMPARE (harness.state ().frequency (), initial_frequency);

    auto requested = state_from (harness);
    requested.frequency (14074125);
    auto const update_cursor = harness.set (requested, 42);
    QVERIFY2 (harness.wait_for_state (
      update_cursor, 42,
      [] (State const& state) {return state.frequency () == 14074125;}),
      qPrintable (harness.diagnostics ()));
    QCOMPARE (harness.state ().frequency (), Frequency {14074125});
    auto const fresh_command = message_index (
      harness.server (), 0, Direction::ClientToServer, QStringLiteral ("vfo"),
      [second_generation] (Message const& message)
      {
        return message.connection_generation == second_generation
          && has_args (message, {"0", "0", "14074125"});
      });
    QVERIFY2 (fresh_command >= 0, qPrintable (harness.diagnostics ()));
  }

  void in_flight_disconnect_is_bounded ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));
    auto const failure_cursor = harness.failure_count ();

    harness.set_command_handler (
      [&harness] (QString const& verb, QStringList const& args, int)
      {
        if (verb != QStringLiteral ("vfo")
            || args != QStringList {"0", "0", "14074150"}) return false;
        QTimer::singleShot (0, &harness.server (),
                            [&harness] {harness.server ().close_client ();});
        return true;
      });
    auto requested = state_from (harness);
    requested.frequency (14074150);

    QElapsedTimer deadline;
    deadline.start ();
    harness.set (requested, 42);
    QVERIFY (harness.server ().wait_for (
      [&harness] {return harness.server ().client_disconnected ();}, 1000));

    harness.set_command_handler ({});
    QVERIFY2 (harness.stop (), qPrintable (harness.diagnostics ()));
    QVERIFY (deadline.elapsed () < 8000);
    QVERIFY (harness.failure_count () - failure_cursor <= 1);
  }

  void unexpected_stale_and_duplicate_messages_leave_coherent_state ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));
    auto const failure_cursor = harness.failure_count ();

    Frequency constexpr target {14074200};
    auto const operation_start = harness.server ().transcript ().size ();
    bool injected {false};
    harness.set_command_handler (
      [&harness, &injected] (QString const& verb, QStringList const& args, int)
      {
        if (injected || verb != QStringLiteral ("vfo")
            || args != QStringList {"0", "0", "14074200"}) return false;
        injected = true;
        harness.server ().send_text (
          QStringLiteral ("unexpected:payload;vfo:1,0,not-a-frequency"));
        harness.server ().send_text_later (QStringLiteral ("vfo:0,0,14074199"), 10);
        harness.server ().send_text_later (
          QStringLiteral ("vfo:0,0,14074200;vfo:0,0,14074200"), 25);
        return true;
      });
    auto requested = state_from (harness);
    requested.frequency (target);
    harness.set (requested, 50);

    QVERIFY (harness.server ().wait_for (
      [&harness]
      {
        int correct_acks {0};
        for (auto const& message : harness.server ().messages (
               Direction::ServerToClient, QStringLiteral ("vfo")))
          {
            if (has_args (message, {"0", "0", "14074200"})) ++correct_acks;
          }
        return correct_acks == 2;
      }, 1000));
    QCOMPARE (harness.failure_count (), failure_cursor);
    QVERIFY (harness.wait_for_current_state (
      [] (State const& state) {return state.frequency () == target;}));
    QCOMPARE (harness.state ().frequency (), target);

    int requested_attempts {0};
    for (auto const& message : harness.server ().messages (
           Direction::ClientToServer, QStringLiteral ("vfo")))
      {
        if (message.ordinal >= static_cast<quint64> (operation_start)
            && has_args (message, {"0", "0", "14074200"}))
          {
            ++requested_attempts;
          }
      }
    QVERIFY (requested_attempts >= 1);
  }

  void reentrant_duplicate_request_is_bounded_and_coherent ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));
    auto const failure_cursor = harness.failure_count ();

    Frequency constexpr target {14074250};
    auto requested = state_from (harness);
    requested.frequency (target);
    bool reentered {false};
    harness.set_command_handler (
      [&harness, &reentered, requested] (QString const& verb,
                                        QStringList const& args, int)
      {
        if (reentered || verb != QStringLiteral ("vfo")
            || args != QStringList {"0", "0", "14074250"}) return false;
        reentered = true;
        harness.schedule_set (requested, 61);
        harness.server ().send_text_later (QStringLiteral ("vfo:0,0,14074250"), 25);
        return true;
      });

    auto const reentrant_cursor = harness.observation_count ();
    harness.set_now (requested, 60);
    QCOMPARE (harness.failure_count (), failure_cursor);
    QCOMPARE (harness.state ().frequency (), target);
    QVERIFY (reentered);
    QVERIFY2 (harness.observed_state_since (
      reentrant_cursor, 61,
      [] (State const& state) {return state.frequency () == target;}),
      qPrintable (harness.diagnostics ()));
  }

  void reentrant_superseding_request_remains_recoverable ()
  {
    RigHarness harness;
    QVERIFY (harness.listen ());
    QVERIFY2 (harness.start (), qPrintable (harness.diagnostics ()));
    auto const failure_cursor = harness.failure_count ();

    Frequency constexpr first_target {14074300};
    Frequency constexpr latest_target {14074350};
    auto first = state_from (harness);
    first.frequency (first_target);
    auto latest = first;
    latest.frequency (latest_target);
    bool reentered {false};
    harness.set_command_handler (
      [&harness, &reentered, latest] (QString const& verb,
                                     QStringList const& args, int)
      {
        if (reentered || verb != QStringLiteral ("vfo")
            || args != QStringList {"0", "0", "14074300"}) return false;
        reentered = true;
        harness.schedule_set (latest, 71);
        harness.server ().send_text_later (QStringLiteral ("vfo:0,0,14074300"), 25);
        return true;
      });

    auto const reentrant_cursor = harness.observation_count ();
    harness.set_now (first, 70);
    auto const settled = harness.state ().frequency ();
    QVERIFY (settled == first_target || settled == latest_target);
    QCOMPARE (harness.failure_count (), failure_cursor);
    QVERIFY2 (harness.observed_state_since (
      reentrant_cursor, 71,
      [] (State const&) {return true;}),
      qPrintable (harness.diagnostics ()));

    harness.set_now (latest, 72);
    QCOMPARE (harness.failure_count (), failure_cursor);
    QCOMPARE (harness.state ().frequency (), latest_target);
    QVERIFY (reentered);
  }
};

QTEST_GUILESS_MAIN (TestTciTransceiverCharacterization)

#include "test_tci_transceiver_characterization.moc"
