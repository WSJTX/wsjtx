#include <functional>
#include <stdexcept>

#include <QtTest>

#include "Transceiver/TransceiverBase.hpp"

namespace
{
  using namespace RigFrequency;
  using State = Transceiver::TransceiverState;

  class FrequencyRig final : public TransceiverBase
  {
  public:
    explicit FrequencyRig (logger_type * logger) : TransceiverBase {logger, nullptr} {}
    std::function<void (Frequency)> rx_operation;
    std::function<void (Frequency)> tx_operation;
    int rx_commands = 0;
    int tx_commands = 0;
    int mode_commands = 0;
    bool readback_expected = false;
    bool readable = true;
    void observe_rx (Frequency value) {update_rx_frequency (value, Basis::Readback);}
    void observe_tx (Frequency value) {update_other_frequency (value, Basis::Readback);}
    void defer_rx () {defer_frequency (Path::Rx);}
    void defer_tx () {defer_frequency (Path::Tx);}
    void expire () {fail_pending_frequencies ();}
    void poll_complete () {frequency_poll_complete ();}
    void fail () {offline (QStringLiteral ("CAT disconnected"));}
    void meters () {update_level (42); update_PTT (false); update_complete (true);}

  private:
    int do_start () override {observe_rx (14074000); observe_tx (0); return 0;}
    void do_stop () override {}
    void do_frequency (Frequency value, MODE, bool) override
    {
      ++rx_commands;
      if (rx_operation) rx_operation (value);
      else update_rx_frequency (value);
    }
    void do_tx_frequency (Frequency value, MODE, bool) override
    {
      ++tx_commands;
      if (tx_operation) tx_operation (value);
      else {update_other_frequency (value); update_split (value != 0);}
    }
    void do_mode (MODE mode) override {++mode_commands; update_mode (mode);}
    void do_ptt (bool value) override {update_PTT (value);}
    bool frequency_readback_expected (Path, Frequency) const override
    {return readback_expected && readable;}
  };

  State request (quint64 id, Transceiver::Frequency rx, Transceiver::Frequency tx)
  {
    State state;
    state.online (true);
    state.frequency (rx);
    state.tx_frequency (tx);
    state.frequency_request ({1, id, Path::Rx, rx});
    state.frequency_request ({1, id + 1, Path::Tx, tx});
    return state;
  }
}

class TestTransceiverFrequency : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void both_paths_have_independent_results ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (request (10, 14074100, 14075200), 2);
    QCOMPARE (rig.rx_commands, 1);
    QCOMPARE (rig.tx_commands, 1);
    QCOMPARE (results.size (), 4);
    auto rx = qvariant_cast<Result> (results.at (1).at (0));
    auto tx = qvariant_cast<Result> (results.at (3).at (0));
    QCOMPARE (rx.request.path, Path::Rx);
    QCOMPARE (tx.request.path, Path::Tx);
    QCOMPARE (rx.outcome, Outcome::Established);
    QCOMPARE (tx.outcome, Outcome::Established);
    QCOMPARE (rx.basis, Basis::BackendOperation);
    QCOMPARE (tx.applied, Radio::Frequency {14075200});
  }

  void retained_reports_and_duplicate_commands_do_not_make_evidence ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    auto state = request (10, 14074100, 14075200);
    rig.set (state, 2);
    QSignalSpy observations {&rig, &Transceiver::frequency_observation};
    rig.meters ();
    rig.set (state, 3);
    QCOMPARE (observations.size (), 0);
    QCOMPARE (rig.rx_commands, 1);
    QCOMPARE (rig.tx_commands, 1);
  }

  void partial_application_and_transmit_deferral ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.tx_operation = [&] (Radio::Frequency) {rig.defer_tx ();};
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    auto state = request (10, 14074100, 14075200);
    rig.set (state, 2);
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome, Outcome::Deferred);
    QCOMPARE (rig.state ().frequency (), Radio::Frequency {14074100});
    QCOMPARE (rig.state ().tx_frequency (), Radio::Frequency {0});
    rig.tx_operation = {};
    rig.set (state, 3);
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome, Outcome::Established);
    QCOMPARE (rig.state ().tx_frequency (), Radio::Frequency {14075200});
    QCOMPARE (rig.rx_commands, 1);
  }

  void pending_readback_and_terminal_failure_are_distinct ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.rx_operation = [] (Radio::Frequency) {};
    rig.tx_operation = [] (Radio::Frequency) {};
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (request (10, 14074100, 14075200), 2);
    rig.observe_rx (14074100);
    auto const established = qvariant_cast<Result> (results.last ().at (0));
    QCOMPARE (established.outcome, Outcome::Established);
    QCOMPARE (established.basis, Basis::Readback);
    rig.expire ();
    auto const failed = qvariant_cast<Result> (results.last ().at (0));
    QCOMPARE (failed.request.path, Path::Tx);
    QCOMPARE (failed.outcome, Outcome::Failed);
  }

  void tx_can_establish_while_rx_is_deferred ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.rx_operation = [&] (Radio::Frequency) {rig.defer_rx ();};
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (request (10, 14074100, 14075200), 2);
    QCOMPARE (qvariant_cast<Result> (results.at (1).at (0)).outcome, Outcome::Deferred);
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome, Outcome::Established);
    QCOMPARE (rig.state ().frequency (), Radio::Frequency {14074000});
    QCOMPARE (rig.state ().tx_frequency (), Radio::Frequency {14075200});
  }

  void failed_tx_does_not_revoke_established_rx ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.tx_operation = [] (Radio::Frequency) {throw std::runtime_error {"TX unavailable"};};
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (request (10, 14074100, 14075200), 2);
    QCOMPARE (qvariant_cast<Result> (results.at (1).at (0)).outcome, Outcome::Established);
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome, Outcome::Failed);
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).request.path, Path::Tx);
  }

  void unchanged_frequency_still_applies_a_mode_change ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    auto state = request (10, 14074100, 14075200);
    state.mode (Transceiver::USB);
    rig.set (state, 2);
    state.mode (Transceiver::LSB);
    rig.set (state, 3);
    QCOMPARE (rig.state ().mode (), Transceiver::LSB);
    QCOMPARE (rig.mode_commands, 2);
  }

  void nested_requests_keep_their_immutable_identity ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    auto first = request (10, 14074100, 0);
    auto second = request (20, 14074200, 0);
    bool nested = false;
    rig.rx_operation = [&] (Radio::Frequency value)
      {
        if (!nested)
          {
            nested = true;
            rig.set (second, 99);
            rig.observe_rx (value);
          }
        else rig.defer_rx ();
      };
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (first, 2);
    bool first_established = false;
    bool second_deferred = false;
    for (auto const& arguments : results)
      {
        auto const result = qvariant_cast<Result> (arguments.at (0));
        first_established |= result.request.id == 10 && result.outcome == Outcome::Established;
        second_deferred |= result.request.id == 20 && result.outcome == Outcome::Deferred;
      }
    QVERIFY (first_established);
    QVERIFY (second_deferred);
  }

  void logical_tx_provenance_survives_physical_rx_and_restore ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    auto rx = request (10, 14074100, 0);
    rig.set (rx, 2);
    auto tx = rx;
    tx.frequency (14075200);
    tx.frequency_request (Path::Rx, {1, 20, Path::Tx, 14075200});
    QSignalSpy observations {&rig, &Transceiver::frequency_observation};
    rig.set (tx, 3);
    auto evidence = qvariant_cast<Evidence> (observations.last ().at (0));
    QCOMPARE (evidence.path, Path::Tx);
    QCOMPARE (evidence.operation.id, quint64 {20});
    rig.set (rx, 4);
    QCOMPARE (rig.state ().frequency (), Radio::Frequency {14074100});
    QCOMPARE (rig.rx_commands, 3);
  }

  void readable_path_waits_for_actual_application_after_command_acceptance ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.readback_expected = true;
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    QSignalSpy evidence {&rig, &Transceiver::frequency_observation};
    auto state = request (10, 14074100, 0);
    rig.set (state, 2);
    QCOMPARE (qvariant_cast<Result> (results.at (1).at (0)).outcome, Outcome::Pending);
    QCOMPARE (evidence.size (), 0);
    rig.set (state, 3);
    QCOMPARE (rig.rx_commands, 1);
    rig.observe_rx (14074000);
    rig.poll_complete ();
    auto const old = qvariant_cast<Evidence> (evidence.last ().at (0));
    QCOMPARE (old.operation.id, quint64 {10});
    QCOMPARE (old.value, Radio::Frequency {14074000});
    for (auto const& arguments : results)
      {
        auto const result = qvariant_cast<Result> (arguments.at (0));
        if (result.request.path == Path::Rx) QVERIFY (result.outcome != Outcome::Established);
      }
    rig.observe_rx (14074100);
    auto const established = qvariant_cast<Result> (results.last ().at (0));
    QCOMPARE (established.request.id, quint64 {10});
    QCOMPARE (established.outcome, Outcome::Established);
    QCOMPARE (established.basis, Basis::Readback);
  }

  void readable_path_mismatch_expires_after_three_eligible_polls ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.readback_expected = true;
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (request (10, 14074100, 0), 2);
    rig.readable = false;
    for (int i = 0; i < 10; ++i) rig.poll_complete ();
    for (int i = 0; i < 2; ++i)
      {
        rig.readable = true;
        rig.observe_rx (14074000);
        rig.poll_complete ();
        for (auto const& arguments : results)
          QVERIFY (qvariant_cast<Result> (arguments.at (0)).outcome != Outcome::Failed);
      }
    rig.observe_rx (14074000);
    rig.poll_complete ();
    auto const failed = qvariant_cast<Result> (results.last ().at (0));
    QCOMPARE (failed.request.id, quint64 {10});
    QCOMPARE (failed.outcome, Outcome::Failed);
  }

  void optimistic_current_value_does_not_establish_a_new_request ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.readback_expected = true;
    rig.set (request (10, 14074100, 0), 2);
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (request (20, 14074100, 0), 3);
    QCOMPARE (rig.rx_commands, 2);
    for (auto const& arguments : results)
      {
        auto const result = qvariant_cast<Result> (arguments.at (0));
        if (result.request.path == Path::Rx) QCOMPARE (result.outcome, Outcome::Pending);
      }
  }

  void physical_rx_restore_defers_its_pending_logical_tx ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    auto rx = request (10, 14074100, 0);
    rig.set (rx, 2);
    rig.readback_expected = true;
    auto tx = rx;
    tx.frequency (14075200);
    tx.frequency_request (Path::Rx, {1, 20, Path::Tx, 14075200});
    rig.set (tx, 3);
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (rx, 4);
    QVERIFY (!results.isEmpty ());
    auto const deferred = qvariant_cast<Result> (results.first ().at (0));
    QCOMPARE (deferred.request.id, quint64 {20});
    QCOMPARE (deferred.request.path, Path::Tx);
    QCOMPARE (deferred.outcome, Outcome::Deferred);
    rig.observe_rx (14074100);
    auto const restored = qvariant_cast<Result> (results.last ().at (0));
    QCOMPARE (restored.request.id, quint64 {10});
    QCOMPARE (restored.request.path, Path::Rx);
    QCOMPARE (restored.outcome, Outcome::Established);
  }

  void repeated_nested_failure_is_reported_once_per_connection ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    QSignalSpy failures {&rig, &Transceiver::failure};
    rig.fail ();
    rig.fail ();
    QCOMPARE (failures.size (), 1);
    rig.start (2);
    rig.fail ();
    QCOMPARE (failures.size (), 2);
  }
};

QTEST_GUILESS_MAIN (TestTransceiverFrequency)
#include "test_transceiver_frequency.moc"
