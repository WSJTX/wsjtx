#include <limits>

#include <QtTest>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QHostAddress>
#include <QStringList>

#include "Network/NetworkMessage.hpp"
#include "Network/MessageClient.hpp"
#include "Network/StartDxTx.hpp"

namespace
{
  quint32 constexpr quint32_max {std::numeric_limits<quint32>::max ()};

  QStringList const accepted_calls {"snapshot", "cancel", "select", "snapshot", "enable", "selected", "snapshot"};

  // records every call so that tests can verify ordering and that Tx
  // is never enabled on a rejected request
  class MockTarget final
    : public StartDxTx::Target
  {
  public:
    StartDxTx::Snapshot state;
    StartDxTx::Snapshot state_after_select;
    bool change_state_on_select {false};
    StartDxTx::Snapshot state_after_enable;
    bool change_state_on_enable {false};
    int selection_after_enable {0};
    bool select_ok {true};
    QString tx1 {"K1ABC W9XYZ EN37"};
    QString tx2 {"K1ABC W9XYZ -10"};
    bool enable_ok {true};
    mutable QStringList calls;
    bool tx_enabled {false};
    int selected {0};
    StartDxTx::DeferredSelectionGuard guard;

    StartDxTx::Snapshot snapshot () const override
    {
      calls << "snapshot";
      auto s = state;
      s.tx_enabled = s.tx_enabled || tx_enabled;
      return s;
    }

    void cancel_deferred_selections () override
    {
      calls << "cancel";
      guard.invalidate ();
    }

    bool select_initial_message (QString * tx_message) override
    {
      calls << "select";
      if (change_state_on_select) state = state_after_select;
      if (!select_ok) return false;
      selected = 1;
      *tx_message = tx1;
      return true;
    }

    int selected_message (QString * tx_message) const override
    {
      calls << "selected";
      *tx_message = 1 == selected ? tx1 : 2 == selected ? tx2 : QString {};
      return selected;
    }

    bool enable_tx () override
    {
      calls << "enable";
      tx_enabled = enable_ok;
      if (change_state_on_enable) state = state_after_enable;
      if (selection_after_enable) selected = selection_after_enable;
      return enable_ok;
    }

    void disable_tx () override
    {
      calls << "disable";
      tx_enabled = false;
    }

    // models the "Generate Std Msgs" button queuing a Tx2 click
    // because Tx1 was disabled at that time, exactly as MainWindow's
    // defer_tx_selection () does
    void defer_selection (int message)
    {
      auto token = guard.token ();
      QTimer::singleShot (0, &context, [this, message, token] {
          if (guard.is_current (token))
            {
              calls << QString {"deferred %1"}.arg (message);
              selected = message;
            }
        });
    }

    QObject context;
  };

  StartDxTx::Snapshot q65_state ()
  {
    StartDxTx::Snapshot s;
    s.de_call = "W9XYZ";
    s.dial_frequency = 144174000;
    s.dx_call = "K1ABC";
    s.dx_grid = "FN42";
    s.mode = "Q65";
    s.submode = "A";
    s.tr_period = 60;
    return s;
  }

  StartDxTx::Request q65_request ()
  {
    StartDxTx::Request r;
    r.request_id = 42;
    r.de_call = "W9XYZ";
    r.dial_frequency = 144174000;
    r.dx_call = "K1ABC";
    r.dx_grid = "FN42";
    r.mode = "Q65";
    r.submode = "A";
    r.tr_period = 60;
    return r;
  }
}

class TestStartDxTx
  : public QObject
{
  Q_OBJECT

private:
  //
  // protocol
  //
  Q_SLOT void message_type_values ()
  {
    // existing values must never change
    QCOMPARE (static_cast<int> (NetworkMessage::Configure), 15);
    QCOMPARE (static_cast<int> (NetworkMessage::AnnotationInfo), 16);
    QCOMPARE (static_cast<int> (NetworkMessage::EnableTx), 17);
    QCOMPARE (static_cast<int> (NetworkMessage::StartDxTx), 18);
    QCOMPARE (static_cast<int> (NetworkMessage::StartDxTxResult), 19);
    QCOMPARE (static_cast<int> (NetworkMessage::maximum_message_type_), 20);
  }

  Q_SLOT void request_round_trip ()
  {
    QByteArray datagram;
    {
      NetworkMessage::Builder out {&datagram, NetworkMessage::StartDxTx, "WSJT-X", NetworkMessage::Builder::schema_number};
      out << quint32 {7} << QByteArray {"W9XYZ"} << quint64 {144174000} << QByteArray {"K1ABC"}
          << QByteArray {"FN42"} << QByteArray {"Q65"} << QByteArray {"A"} << quint32 {60};
      QCOMPARE (out.status (), QDataStream::Ok);
    }
    NetworkMessage::Reader in {datagram};
    QCOMPARE (in.type (), NetworkMessage::StartDxTx);
    QCOMPARE (in.id (), QString {"WSJT-X"});
    quint32 request_id {0};
    QByteArray de_call, call, grid, mode, submode;
    quint64 frequency {0};
    quint32 tr_period {0};
    in >> request_id >> de_call >> frequency >> call >> grid >> mode >> submode >> tr_period;
    QCOMPARE (in.status (), QDataStream::Ok);
    QVERIFY (in.atEnd ());
    QCOMPARE (request_id, quint32 {7});
    QCOMPARE (de_call, QByteArray {"W9XYZ"});
    QCOMPARE (frequency, quint64 {144174000});
    QCOMPARE (call, QByteArray {"K1ABC"});
    QCOMPARE (grid, QByteArray {"FN42"});
    QCOMPARE (mode, QByteArray {"Q65"});
    QCOMPARE (submode, QByteArray {"A"});
    QCOMPARE (tr_period, quint32 {60});
  }

  //
  // logic with a mock target
  //
  Q_SLOT void accepts_matching_request_atomically ()
  {
    MockTarget target;
    target.state = q65_state ();
    auto result = StartDxTx::execute (q65_request (), target);
    QVERIFY (result.accepted);
    QVERIFY (result.reason.isEmpty ());
    QCOMPARE (result.tx_message, QString {"K1ABC W9XYZ EN37"});
    QCOMPARE (target.calls, accepted_calls);
    QVERIFY (target.tx_enabled);
    QCOMPARE (target.selected, 1);
  }

  Q_SLOT void matching_is_case_insensitive_and_trimmed ()
  {
    MockTarget target;
    target.state = q65_state ();
    auto request = q65_request ();
    request.de_call = "w9xyz ";
    request.dx_call = " k1abc ";
    request.dx_grid = "fn42";
    request.submode = "a";
    QVERIFY (StartDxTx::execute (request, target).accepted);
  }

  Q_SLOT void not_applicable_tr_period_must_match ()
  {
    MockTarget target;
    target.state = q65_state ();
    target.state.mode = "FT8";
    target.state.submode.clear ();
    target.state.tr_period = quint32_max;
    auto request = q65_request ();
    request.mode = "FT8";
    request.submode.clear ();
    request.tr_period = quint32_max;
    QVERIFY (StartDxTx::execute (request, target).accepted);
    request.tr_period = 15;
    target.calls.clear ();
    target.tx_enabled = false;
    auto result = StartDxTx::execute (request, target);
    QCOMPARE (result.reason, QString {StartDxTx::Reason::tr_period_mismatch});
    QVERIFY (!target.tx_enabled);
  }

  Q_SLOT void rejects_without_side_effects_data ()
  {
    QTest::addColumn<int> ("change");
    QTest::addColumn<QString> ("reason");
    QTest::newRow ("empty call") << 0 << StartDxTx::Reason::invalid_request;
    QTest::newRow ("empty mode") << 1 << StartDxTx::Reason::invalid_request;
    QTest::newRow ("empty DE call") << 13 << StartDxTx::Reason::invalid_request;
    QTest::newRow ("zero dial frequency") << 14 << StartDxTx::Reason::invalid_request;
    QTest::newRow ("DE call changed") << 15 << StartDxTx::Reason::de_call_mismatch;
    QTest::newRow ("band changed") << 16 << StartDxTx::Reason::dial_frequency_mismatch;
    QTest::newRow ("dial frequency off by 1 Hz") << 17 << StartDxTx::Reason::dial_frequency_mismatch;
    QTest::newRow ("transmitting") << 2 << StartDxTx::Reason::already_transmitting;
    QTest::newRow ("tx enabled") << 3 << StartDxTx::Reason::tx_already_enabled;
    QTest::newRow ("tuning") << 4 << StartDxTx::Reason::tuning;
    QTest::newRow ("watchdog") << 5 << StartDxTx::Reason::watchdog_timeout;
    QTest::newRow ("fox/hound") << 6 << StartDxTx::Reason::unsupported_special_operation;
    QTest::newRow ("dx call changed") << 7 << StartDxTx::Reason::dx_call_mismatch;
    QTest::newRow ("dx grid changed") << 8 << StartDxTx::Reason::dx_grid_mismatch;
    QTest::newRow ("grid empty vs set") << 9 << StartDxTx::Reason::dx_grid_mismatch;
    QTest::newRow ("mode changed") << 10 << StartDxTx::Reason::mode_mismatch;
    QTest::newRow ("Q65 submode changed") << 11 << StartDxTx::Reason::submode_mismatch;
    QTest::newRow ("Q65 T/R period changed") << 12 << StartDxTx::Reason::tr_period_mismatch;
  }

  Q_SLOT void rejects_without_side_effects ()
  {
    QFETCH (int, change);
    QFETCH (QString, reason);
    MockTarget target;
    target.state = q65_state ();
    auto request = q65_request ();
    switch (change)
      {
      case 0: request.dx_call.clear (); break;
      case 1: request.mode.clear (); break;
      case 2: target.state.transmitting = true; break;
      case 3: target.state.tx_enabled = true; break;
      case 4: target.state.tuning = true; break;
      case 5: target.state.watchdog_timeout = true; break;
      case 6: target.state.special_operation_supported = false; break;
      case 7: target.state.dx_call = "K1ABD"; break;
      case 8: target.state.dx_grid = "FN43"; break;
      case 9: request.dx_grid.clear (); break;
      case 10: target.state.mode = "FT8"; break;
      case 11: target.state.submode = "B"; break;
      case 12: target.state.tr_period = 30; break;
      case 13: request.de_call.clear (); break;
      case 14: request.dial_frequency = 0; break;
      case 15: target.state.de_call = "W9XYZ/P"; break;
      case 16: target.state.dial_frequency = 50313000; break;
      case 17: target.state.dial_frequency = 144174001; break;
      }
    auto result = StartDxTx::execute (request, target);
    QVERIFY (!result.accepted);
    QCOMPARE (result.reason, reason);
    QVERIFY (result.tx_message.isEmpty ());
    QCOMPARE (target.calls, QStringList {"snapshot"}); // nothing cancelled, selected or enabled
    QVERIFY (!target.tx_enabled);
    QCOMPARE (target.selected, 0);
  }

  Q_SLOT void rejects_when_tx1_unavailable ()
  {
    MockTarget target;
    target.state = q65_state ();
    target.select_ok = false;
    auto result = StartDxTx::execute (q65_request (), target);
    QVERIFY (!result.accepted);
    QCOMPARE (result.reason, QString {StartDxTx::Reason::tx1_unavailable});
    QVERIFY (!target.calls.contains ("enable"));

    MockTarget empty_target;
    empty_target.state = q65_state ();
    empty_target.tx1 = "  ";
    result = StartDxTx::execute (q65_request (), empty_target);
    QCOMPARE (result.reason, QString {StartDxTx::Reason::tx1_unavailable});
    QVERIFY (!empty_target.calls.contains ("enable"));
  }

  Q_SLOT void rejects_if_state_changes_during_selection_data ()
  {
    QTest::addColumn<int> ("change");
    QTest::addColumn<QString> ("reason");
    QTest::newRow ("DX call") << 0 << StartDxTx::Reason::dx_call_mismatch;
    QTest::newRow ("DE call") << 1 << StartDxTx::Reason::de_call_mismatch;
    QTest::newRow ("dial frequency") << 2 << StartDxTx::Reason::dial_frequency_mismatch;
    QTest::newRow ("T/R period") << 3 << StartDxTx::Reason::tr_period_mismatch;
  }

  Q_SLOT void rejects_if_state_changes_during_selection ()
  {
    QFETCH (int, change);
    QFETCH (QString, reason);
    MockTarget target;
    target.state = q65_state ();
    target.change_state_on_select = true;
    target.state_after_select = q65_state ();
    switch (change)
      {
      case 0: target.state_after_select.dx_call = "W1AW"; break;
      case 1: target.state_after_select.de_call = "N0CALL"; break;
      case 2: target.state_after_select.dial_frequency = 432065000; break;
      case 3: target.state_after_select.tr_period = 30; break;
      }
    auto result = StartDxTx::execute (q65_request (), target);
    QCOMPARE (result.reason, reason);
    QVERIFY (!target.calls.contains ("enable"));
    QVERIFY (!target.tx_enabled);
  }

  Q_SLOT void tx1_addressing_data ()
  {
    QTest::addColumn<QString> ("message");
    QTest::addColumn<QString> ("dx_call");
    QTest::addColumn<QString> ("de_call");
    QTest::addColumn<bool> ("ok");
    QTest::newRow ("standard") << "K1ABC W9XYZ EN37" << "K1ABC" << "W9XYZ" << true;
    QTest::newRow ("no grid") << "K1ABC W9XYZ" << "K1ABC" << "W9XYZ" << true;
    QTest::newRow ("case") << "k1abc w9xyz EN37" << "K1ABC" << "W9XYZ" << true;
    QTest::newRow ("extra spaces") << "  K1ABC   W9XYZ  EN37 " << "K1ABC" << "W9XYZ" << true;
    QTest::newRow ("hashed DX") << "<PJ4/K1ABC> W9XYZ" << "PJ4/K1ABC" << "W9XYZ" << true;
    QTest::newRow ("hashed DE") << "K1ABC <W9XYZ/7>" << "K1ABC" << "W9XYZ/7" << true;
    QTest::newRow ("base calls") << "K1ABC W9XYZ EN37" << "PJ4/K1ABC" << "W9XYZ/P" << true;
    QTest::newRow ("full compound") << "PJ4/K1ABC W9XYZ/P" << "PJ4/K1ABC" << "W9XYZ/P" << true;
    QTest::newRow ("other DX") << "K1ABD W9XYZ EN37" << "K1ABC" << "W9XYZ" << false;
    QTest::newRow ("other DE") << "K1ABC W9XYY EN37" << "K1ABC" << "W9XYZ" << false;
    QTest::newRow ("swapped") << "W9XYZ K1ABC EN37" << "K1ABC" << "W9XYZ" << false;
    QTest::newRow ("DE form") << "DE W9XYZ EN37" << "K1ABC" << "W9XYZ" << false;
    QTest::newRow ("CQ") << "CQ W9XYZ EN37" << "K1ABC" << "W9XYZ" << false;
    QTest::newRow ("one word") << "K1ABC" << "K1ABC" << "W9XYZ" << false;
    QTest::newRow ("empty") << "" << "K1ABC" << "W9XYZ" << false;
    QTest::newRow ("prefix only") << "K1ABCD W9XYZ" << "K1ABC" << "W9XYZ" << false;
    QTest::newRow ("empty brackets") << "<> W9XYZ" << "K1ABC" << "W9XYZ" << false;
  }

  Q_SLOT void tx1_addressing ()
  {
    QFETCH (QString, message);
    QFETCH (QString, dx_call);
    QFETCH (QString, de_call);
    QFETCH (bool, ok);
    QCOMPARE (StartDxTx::addresses (message, dx_call, de_call), ok);
  }

  Q_SLOT void rejects_misaddressed_tx1 ()
  {
    for (auto const& tx1 : {"K1ABD W9XYZ EN37", "K1ABC N0CALL EN37", "DE W9XYZ EN37", "CQ W9XYZ EN37"})
      {
        MockTarget target;
        target.state = q65_state ();
        target.tx1 = tx1;
        auto result = StartDxTx::execute (q65_request (), target);
        QVERIFY (!result.accepted);
        QCOMPARE (result.reason, QString {StartDxTx::Reason::tx1_address_mismatch});
        QVERIFY (result.tx_message.isEmpty ());
        QVERIFY (!target.calls.contains ("enable"));
        QVERIFY (!target.tx_enabled);
      }
  }

  Q_SLOT void disables_if_state_changes_after_enable_data ()
  {
    QTest::addColumn<int> ("change");
    QTest::newRow ("DX call") << 0;
    QTest::newRow ("DE call") << 1;
    QTest::newRow ("dial frequency") << 2;
    QTest::newRow ("submode") << 3;
    QTest::newRow ("Tx2 selected") << 4;
    QTest::newRow ("nothing selected") << 5;
  }

  Q_SLOT void disables_if_state_changes_after_enable ()
  {
    QFETCH (int, change);
    MockTarget target;
    target.state = q65_state ();
    target.state_after_enable = q65_state ();
    target.change_state_on_enable = change < 4;
    switch (change)
      {
      case 0: target.state_after_enable.dx_call = "W1AW"; break;
      case 1: target.state_after_enable.de_call = "N0CALL"; break;
      case 2: target.state_after_enable.dial_frequency = 50313000; break;
      case 3: target.state_after_enable.submode = "B"; break;
      case 4: target.selection_after_enable = 2; break;
      case 5: target.selection_after_enable = -1; break;
      }
    auto result = StartDxTx::execute (q65_request (), target);
    QVERIFY (!result.accepted);
    QCOMPARE (result.reason, QString {StartDxTx::Reason::state_changed_after_enable});
    QVERIFY (result.tx_message.isEmpty ());
    QCOMPARE (target.calls.last (), QString {"disable"});
    QVERIFY (!target.tx_enabled);
  }

  //
  // ordering against selections deferred to the event loop
  //
  Q_SLOT void deferred_selection_runs_without_start ()
  {
    // control: the guard does not suppress ordinary deferred clicks
    MockTarget target;
    target.defer_selection (2);
    QTRY_COMPARE (target.selected, 2);
  }

  Q_SLOT void stale_deferred_tx2_cannot_override_accepted_tx1 ()
  {
    MockTarget target;
    target.state = q65_state ();
    // an earlier Configure generated messages while Tx1 was disabled
    // and queued a Tx2 click, then Tx1 became available and the start
    // request arrives before that click has run
    target.defer_selection (2);
    auto result = StartDxTx::execute (q65_request (), target);
    QVERIFY (result.accepted);
    QCOMPARE (target.selected, 1);
    QTest::qWait (50);
    QCoreApplication::processEvents ();
    QCOMPARE (target.selected, 1);
    QVERIFY (target.tx_enabled);
    QVERIFY (!target.calls.contains ("deferred 2"));
    QCOMPARE (target.calls, accepted_calls);
  }

  Q_SLOT void stale_deferred_tx1_is_also_cancelled ()
  {
    MockTarget target;
    target.state = q65_state ();
    target.defer_selection (1);
    QVERIFY (StartDxTx::execute (q65_request (), target).accepted);
    QTest::qWait (50);
    QVERIFY (!target.calls.contains ("deferred 1"));
    QCOMPARE (target.selected, 1);
  }

  Q_SLOT void rejected_request_leaves_deferred_selection_alone ()
  {
    MockTarget target;
    target.state = q65_state ();
    target.state.dial_frequency = 50313000;
    target.defer_selection (2);
    auto result = StartDxTx::execute (q65_request (), target);
    QCOMPARE (result.reason, QString {StartDxTx::Reason::dial_frequency_mismatch});
    QTRY_COMPARE (target.selected, 2);
    QVERIFY (!target.tx_enabled);
  }

  Q_SLOT void deferred_selection_after_start_still_runs ()
  {
    // only selections queued before the request are stale
    MockTarget target;
    target.state = q65_state ();
    QVERIFY (StartDxTx::execute (q65_request (), target).accepted);
    target.defer_selection (2);
    QTRY_COMPARE (target.selected, 2);
  }

  Q_SLOT void reports_enable_failure ()
  {
    MockTarget target;
    target.state = q65_state ();
    target.enable_ok = false;
    auto result = StartDxTx::execute (q65_request (), target);
    QVERIFY (!result.accepted);
    QCOMPARE (result.reason, QString {StartDxTx::Reason::enable_tx_failed});
    QVERIFY (result.tx_message.isEmpty ());
    QCOMPARE (target.calls.last (), QString {"disable"});
    QVERIFY (!target.tx_enabled);
  }

  //
  // MessageClient over the loop-back interface with a mock server
  // socket, no rig or WSJT-X instance involved
  //
  Q_SLOT void initTestCase ()
  {
    QVERIFY (server_.bind (QHostAddress {QHostAddress::LocalHost}, quint16 {0}));
    client_.reset (new MessageClient {"TEST-ID", "v", "r", "127.0.0.1", server_.localPort ()
                                      , QStringList {}, 1});
    // the initial heartbeat tells us the client's port
    auto datagram = next_datagram (5000, true);
    QVERIFY (!datagram.isEmpty ());
    QVERIFY (client_port_);
    client_->enable (true);
  }

  Q_SLOT void cleanupTestCase ()
  {
    client_.reset ();
  }

  Q_SLOT void client_emits_start_dx_tx ()
  {
    QSignalSpy start_spy {client_.data (), &MessageClient::start_dx_tx};
    QSignalSpy enable_spy {client_.data (), &MessageClient::enable_tx};
    send_request ("TEST-ID", 99, true);
    QVERIFY (start_spy.wait (2000));
    QCOMPARE (start_spy.count (), 1);
    auto const& args = start_spy.takeFirst ();
    QCOMPARE (args.at (0).value<quint32> (), quint32 {99});
    QCOMPARE (args.at (1).toString (), QString {"W9XYZ"});
    QCOMPARE (args.at (2).value<quint64> (), quint64 {144174000});
    QCOMPARE (args.at (3).toString (), QString {"K1ABC"});
    QCOMPARE (args.at (4).toString (), QString {"FN42"});
    QCOMPARE (args.at (5).toString (), QString {"Q65"});
    QCOMPARE (args.at (6).toString (), QString {"A"});
    QCOMPARE (args.at (7).value<quint32> (), quint32 {60});
    QCOMPARE (enable_spy.count (), 0); // distinct from Enable Tx
  }

  Q_SLOT void client_still_emits_legacy_enable_tx ()
  {
    QSignalSpy start_spy {client_.data (), &MessageClient::start_dx_tx};
    QSignalSpy enable_spy {client_.data (), &MessageClient::enable_tx};
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::EnableTx, "TEST-ID", NetworkMessage::Builder::schema_number};
    send (message);
    QVERIFY (enable_spy.wait (2000));
    QCOMPARE (start_spy.count (), 0);
  }

  Q_SLOT void client_ignores_other_ids ()
  {
    QSignalSpy start_spy {client_.data (), &MessageClient::start_dx_tx};
    send_request ("OTHER", 5, true);
    QVERIFY (!start_spy.wait (300));
    QVERIFY (next_datagram (300).isEmpty ()); // no result either
  }

  Q_SLOT void client_rejects_malformed_request ()
  {
    QSignalSpy start_spy {client_.data (), &MessageClient::start_dx_tx};
    send_request ("TEST-ID", 77, false);
    auto datagram = next_datagram ();
    QVERIFY (!datagram.isEmpty ());
    QCOMPARE (start_spy.count (), 0);
    check_result (datagram, 77, false, StartDxTx::Reason::malformed_request, QString {});
  }

  Q_SLOT void client_rejects_draft_layout_without_station_fields ()
  {
    // the earlier draft layout without DE Call and Dial Frequency must
    // never be misinterpreted as a valid request
    QSignalSpy start_spy {client_.data (), &MessageClient::start_dx_tx};
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::StartDxTx, "TEST-ID", NetworkMessage::Builder::schema_number};
    out << quint32 {78} << QByteArray {"K1ABC"} << QByteArray {"FN42"} << QByteArray {"Q65"} << QByteArray {"A"}
        << quint32 {60};
    send (message);
    auto datagram = next_datagram ();
    QCOMPARE (start_spy.count (), 0);
    check_result (datagram, 78, false, StartDxTx::Reason::malformed_request, QString {});
  }

  Q_SLOT void client_sends_result ()
  {
    client_->start_dx_tx_result (12, true, QString {}, "K1ABC W9XYZ EN37");
    check_result (next_datagram (), 12, true, QString {}, "K1ABC W9XYZ EN37");
    // identical results are not suppressed as duplicates
    client_->start_dx_tx_result (12, true, QString {}, "K1ABC W9XYZ EN37");
    check_result (next_datagram (), 12, true, QString {}, "K1ABC W9XYZ EN37");
    client_->start_dx_tx_result (13, false, StartDxTx::Reason::dx_call_mismatch, QString {});
    check_result (next_datagram (), 13, false, StartDxTx::Reason::dx_call_mismatch, QString {});
  }

  Q_SLOT void client_ignores_requests_when_disabled ()
  {
    client_->enable (false);
    QSignalSpy start_spy {client_.data (), &MessageClient::start_dx_tx};
    send_request ("TEST-ID", 6, true);
    QVERIFY (!start_spy.wait (300));
    QVERIFY (next_datagram (300).isEmpty ());
    client_->enable (true);
  }

private:
  // returns the next non-heartbeat datagram (or the first heartbeat
  // if requested), empty on timeout
  QByteArray next_datagram (int timeout = 2000, bool want_heartbeat = false)
  {
    QElapsedTimer timer;
    timer.start ();
    while (timer.elapsed () < timeout)
      {
        while (server_.hasPendingDatagrams ())
          {
            QByteArray datagram;
            datagram.resize (server_.pendingDatagramSize ());
            QHostAddress address;
            quint16 port;
            server_.readDatagram (datagram.data (), datagram.size (), &address, &port);
            client_port_ = port;
            NetworkMessage::Reader in {datagram};
            if (want_heartbeat == (NetworkMessage::Heartbeat == in.type ()))
              {
                return datagram;
              }
          }
        QTest::qWait (10);
      }
    return {};
  }

  void send (QByteArray const& message)
  {
    QVERIFY (client_port_);
    server_.writeDatagram (message, QHostAddress::LocalHost, client_port_);
  }

  void send_request (QString const& id, quint32 request_id, bool complete)
  {
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::StartDxTx, id, NetworkMessage::Builder::schema_number};
    out << request_id;
    if (complete)
      {
        out << QByteArray {"W9XYZ"} << quint64 {144174000} << QByteArray {"K1ABC"} << QByteArray {"FN42"}
            << QByteArray {"Q65"} << QByteArray {"A"} << quint32 {60};
      }
    send (message);
  }

  void check_result (QByteArray const& datagram, quint32 request_id, bool accepted
                     , QString const& reason, QString const& tx_message)
  {
    QVERIFY (!datagram.isEmpty ());
    NetworkMessage::Reader in {datagram};
    QCOMPARE (in.type (), NetworkMessage::StartDxTxResult);
    QCOMPARE (in.id (), QString {"TEST-ID"});
    quint32 id {0};
    bool ok {!accepted};
    QByteArray r, t;
    in >> id >> ok >> r >> t;
    QCOMPARE (in.status (), QDataStream::Ok);
    QCOMPARE (id, request_id);
    QCOMPARE (ok, accepted);
    QCOMPARE (QString::fromUtf8 (r), reason);
    QCOMPARE (QString::fromUtf8 (t), tx_message);
  }

  QUdpSocket server_;
  QScopedPointer<MessageClient> client_;
  quint16 client_port_ {0};
};

QTEST_MAIN (TestStartDxTx);

#include "test_start_dx_tx.moc"
