#include <functional>
#include <stdexcept>

#include <QtTest>

#include "Transceiver/TransceiverBase.hpp"
#include "FrequencyAdapter.hpp"

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
    std::function<void ()> mode_refresh;
    int rx_commands = 0;
    int tx_commands = 0;
    int mode_commands = 0;
    bool readback_expected = false;
    bool mode_readable = false;
    bool readable = true;
    int resolution_code = 0;
    void observe_rx (Frequency value) {update_rx_frequency (value, Basis::Readback);}
    void observe_tx (Frequency value) {update_other_frequency (value, Basis::Readback);}
    void defer_rx () {defer_frequency (Path::Rx);}
    void defer_tx () {defer_frequency (Path::Tx);}
    void expire () {fail_pending_frequencies ();}
    void poll_complete () {frequency_poll_complete ();}
    void fail () {offline (QStringLiteral ("CAT disconnected"));}
    void meters () {update_level (42); update_PTT (false); update_complete (true);}
    void report_mode (MODE mode, bool transmitting = false)
    {
      update_PTT (transmitting);
      observe_mode (mode);
      update_complete (true);
    }

  private:
    int do_start () override {observe_rx (14074000); observe_tx (0); return resolution_code;}
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
    bool mode_readback_expected () const override {return mode_readable;}
    void refresh_mode_observation () override {if (mode_refresh) mode_refresh ();}
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
  void mode_preparation_is_not_lost_when_audio_changes ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    auto state = rig.state ();
    state.mode (Transceiver::DIG_U);
    state.audio (true);
    QSignalSpy statuses {&rig, &Transceiver::mode_status};
    rig.set (state, 2);
    QCOMPARE (rig.mode_commands, 1);
    QCOMPARE (statuses.size (), 2);
    QCOMPARE (qvariant_cast<Transceiver::ModeStatus> (statuses.last ().at (0)).kind,
              Transceiver::ModeStatusKind::Applied);
  }

  void unchanged_mode_preparation_uses_existing_backend_observation ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.mode_readable = true;
    rig.report_mode (Transceiver::DIG_U);
    auto state = rig.state ();
    state.mode (Transceiver::DIG_U);
    rig.mode_refresh = [&] {rig.report_mode (Transceiver::DIG_U);};
    QSignalSpy statuses {&rig, &Transceiver::mode_status};
    rig.set (state, 2);
    QCOMPARE (rig.rx_commands, 0);
    QCOMPARE (rig.mode_commands, 0);
    QCOMPARE (statuses.size (), 2);
    auto const result = qvariant_cast<Transceiver::ModeStatus> (statuses.last ().at (0));
    QCOMPARE (result.kind, Transceiver::ModeStatusKind::Observed);
    QCOMPARE (result.mode, Transceiver::DIG_U);
  }

  void frequency_mode_preparation_requires_observation_when_readable ()
  {
    using Kind = Transceiver::ModeStatusKind;
    using Status = Transceiver::ModeStatus;
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.mode_readable = true;
    QSignalSpy statuses {&rig, &Transceiver::mode_status};
    auto state = request (10, 14074000, 0);
    state.mode (Transceiver::DIG_U);
    rig.set (state, 2);
    QCOMPARE (statuses.size (), 1);
    QCOMPARE (qvariant_cast<Status> (statuses.last ().at (0)).kind, Kind::Preparing);
    rig.report_mode (Transceiver::CW);
    QCOMPARE (statuses.size (), 1);
    rig.report_mode (Transceiver::DIG_U);
    QCOMPARE (statuses.size (), 2);
    QCOMPARE (qvariant_cast<Status> (statuses.last ().at (0)).kind, Kind::Observed);
    QCOMPARE (qvariant_cast<Status> (statuses.last ().at (0)).mode, Transceiver::DIG_U);
    QCOMPARE (statuses.last ().at (1).toUInt (), 2u);
  }

  void frequency_mode_preparation_uses_completion_without_readback ()
  {
    using Kind = Transceiver::ModeStatusKind;
    using Status = Transceiver::ModeStatus;
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    QSignalSpy statuses {&rig, &Transceiver::mode_status};
    auto state = request (10, 14074000, 0);
    state.mode (Transceiver::DIG_U);
    rig.set (state, 2);
    QCOMPARE (statuses.size (), 2);
    QCOMPARE (qvariant_cast<Status> (statuses.last ().at (0)).kind, Kind::Applied);
    QCOMPARE (qvariant_cast<Status> (statuses.last ().at (0)).mode, Transceiver::DIG_U);
  }

  void repeated_frequency_commands_do_not_extend_mode_settlement ()
  {
    using Kind = Transceiver::ModeStatusKind;
    using Status = Transceiver::ModeStatus;
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.mode_readable = true;
    QSignalSpy statuses {&rig, &Transceiver::mode_status};
    auto state = request (10, 14074000, 0);
    state.mode (Transceiver::DIG_U);
    QElapsedTimer elapsed;
    elapsed.start ();
    rig.set (state, 2);
    rig.report_mode (Transceiver::CW);
    QTest::qWait (900);
    rig.set (state, 3);
    rig.report_mode (Transceiver::CW);
    QTRY_COMPARE_WITH_TIMEOUT (qvariant_cast<Status> (statuses.last ().at (0)).kind,
                              Kind::Observed, 1300);
    QVERIFY (elapsed.elapsed () < 2200);
    QCOMPARE (qvariant_cast<Status> (statuses.last ().at (0)).mode, Transceiver::CW);
    QCOMPARE (statuses.last ().at (1).toUInt (), 3u);
  }

  void unknown_or_transmitting_mode_is_not_an_override ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    QSignalSpy statuses {&rig, &Transceiver::mode_status};
    rig.report_mode (Transceiver::UNK);
    rig.report_mode (Transceiver::CW, true);
    rig.meters ();
    QVERIFY (statuses.isEmpty ());
    rig.report_mode (Transceiver::CW);
    QCOMPARE (statuses.size (), 1);
    rig.report_mode (Transceiver::CW);
    QCOMPARE (statuses.size (), 1);
  }

  void unreadable_mode_during_preparation_reports_unavailable ()
  {
    using Kind = Transceiver::ModeStatusKind;
    using Status = Transceiver::ModeStatus;
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.start (1);
    rig.mode_readable = true;
    QSignalSpy statuses {&rig, &Transceiver::mode_status};
    auto state = request (10, 14074000, 0);
    state.mode (Transceiver::DIG_U);
    rig.set (state, 2);
    rig.report_mode (Transceiver::UNK);
    QTRY_COMPARE_WITH_TIMEOUT (qvariant_cast<Status> (statuses.last ().at (0)).kind,
                              Kind::Unavailable, 2200);
    QCOMPARE (qvariant_cast<Status> (statuses.last ().at (0)).mode, Transceiver::UNK);
  }

  void quantized_readback_completes_only_within_negotiated_resolution_data ()
  {
    QTest::addColumn<int> ("code");
    QTest::addColumn<int> ("delta");
    QTest::addColumn<bool> ("tx");
    QTest::addColumn<bool> ("established");
    for (auto code : {0, -1, 1, -2, 2, -3, 3})
      {
        auto const hz = code == 0 ? 1 : std::abs (code) == 1 ? 10 : std::abs (code) == 2 ? 20 : 100;
        QList<int> const deltas = code ? QList<int> {0, hz - 1, -(hz - 1), hz, -hz, hz + 1, -(hz + 1)}
                                      : QList<int> {0, 1, -1, 2, -2};
        for (auto tx : {false, true})
          for (auto delta : deltas)
            {
              auto name = QString {"code%1-%2-delta%3"}.arg (code).arg (tx ? "TX" : "RX").arg (delta);
              QTest::newRow (qPrintable (name)) << code << delta << tx << (std::abs (delta) < hz);
            }
      }
  }

  void quantized_readback_completes_only_within_negotiated_resolution ()
  {
    QFETCH (int, code);
    QFETCH (int, delta);
    QFETCH (bool, tx);
    QFETCH (bool, established);
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.resolution_code = code;
    rig.start (1);
    rig.readback_expected = true;
    auto const target = Radio::Frequency {144200003};
    auto state = request (10, target, target);
    state.frequency_request (tx ? Path::Rx : Path::Tx, {});
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (state, 2);
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome, Outcome::Pending);
    auto const observed = static_cast<Radio::Frequency> (static_cast<Radio::FrequencyDelta> (target) + delta);
    if (tx) rig.observe_tx (observed);
    else rig.observe_rx (observed);
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome,
              established ? Outcome::Established : Outcome::Pending);
    rig.readable = false;
    for (int i = 0; i < 10; ++i) rig.poll_complete ();
    rig.readable = true;
    for (int i = 0; i < 2; ++i)
      {
        rig.poll_complete ();
        QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome,
                  established ? Outcome::Established : Outcome::Pending);
      }
    rig.poll_complete ();
    auto const result = qvariant_cast<Result> (results.last ().at (0));
    QCOMPARE (result.outcome, established ? Outcome::Established : Outcome::Failed);
    QCOMPARE (result.request.generation, quint64 {1});
    QCOMPARE (result.request.id, quint64 {tx ? 11u : 10u});
    QCOMPARE (result.request.path, tx ? Path::Tx : Path::Rx);
    QCOMPARE (result.request.target, target);
    if (established)
      {
        QCOMPARE (result.basis, Basis::Readback);
        QCOMPARE (result.applied, observed);
      }
  }

  void calibrated_quantized_readback_preserves_nominal_mapping_data ()
  {
    QTest::addColumn<int> ("code");
    QTest::addColumn<bool> ("tx");
    QTest::addColumn<double> ("intercept");
    for (auto code : {-1, 1, -2, 2, -3, 3})
      for (auto tx : {false, true})
        for (auto intercept : {-3., 3.})
          {
            auto name = QString {"code%1-%2-intercept%3"}.arg (code).arg (tx ? "TX" : "RX").arg (intercept);
            QTest::newRow (qPrintable (name)) << code << tx << intercept;
          }
  }

  void calibrated_quantized_readback_preserves_nominal_mapping ()
  {
    QFETCH (int, code);
    QFETCH (bool, tx);
    QFETCH (double, intercept);
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.resolution_code = code;
    rig.readback_expected = true;
    FrequencyAdapter adapter;
    adapter.new_connection ();
    std::optional<Result> completion;
    std::optional<Evidence> observation;
    connect (&rig, &Transceiver::resolution, [&] (int value) {
      adapter.resolution (FrequencyAdapter::resolution_hz (value));
    });
    connect (&rig, &Transceiver::frequency_observation, [&] (Evidence const& evidence) {
      if (auto translated = adapter.observe (evidence)) observation = *translated;
    });
    connect (&rig, &Transceiver::frequency_result, [&] (Result const& result) {
      if (auto translated = adapter.complete (result)) completion = *translated;
    });
    rig.start (1);
    FrequencyAdapter::Projection projection;
    projection.intercept = intercept;
    auto const nominal = Radio::Frequency {144200000};
    auto const path = tx ? Path::Tx : Path::Rx;
    Request intent {adapter.generation (), 10, path, nominal};
    QVERIFY (adapter.submit (intent, projection));
    auto command = adapter.take_dispatch (path);
    QVERIFY (command);
    QCOMPARE (command->target, static_cast<Radio::Frequency> (nominal + intercept));
    auto state = request (10, command->target, command->target);
    state.frequency_request (tx ? Path::Rx : Path::Tx, {});
    state.frequency_request (path, *command);
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (state, 2);
    QVERIFY (completion);
    QCOMPARE (completion->outcome, Outcome::Pending);
    QVERIFY (!observation);
    if (tx) rig.observe_tx (nominal);
    else rig.observe_rx (nominal);
    QVERIFY (observation);
    QCOMPARE (observation->value, nominal);
    QVERIFY (same_operation (observation->operation, intent));
    QCOMPARE (observation->operation.target, intent.target);
    for (int i = 0; i < 3; ++i) rig.poll_complete ();
    auto const physical = qvariant_cast<Result> (results.last ().at (0));
    QCOMPARE (physical.outcome, Outcome::Established);
    QCOMPARE (physical.basis, Basis::Readback);
    QCOMPARE (physical.applied, nominal);
    QVERIFY (same_operation (physical.request, *command));
    QCOMPARE (physical.request.target, command->target);
    QCOMPARE (completion->outcome, Outcome::Established);
    QCOMPARE (completion->applied, nominal);
    QCOMPARE (completion->request.target, intent.target);
    QCOMPARE (adapter.decode (path, nominal), nominal);
    QVERIFY (!adapter.snapshot (path).pending);
    QVERIFY (!adapter.snapshot (tx ? Path::Rx : Path::Tx).established);
  }

  void quantized_paths_complete_independently ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.resolution_code = -1;
    rig.readback_expected = true;
    rig.start (1);
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    auto const state = request (10, 144200003, 144201003);
    rig.set (state, 2);
    rig.observe_rx (144200000);
    auto const rx = qvariant_cast<Result> (results.last ().at (0));
    QCOMPARE (rx.request.id, quint64 {10});
    QCOMPARE (rx.request.path, Path::Rx);
    QCOMPARE (rx.outcome, Outcome::Established);
    rig.observe_tx (144201013);
    for (int i = 0; i < 2; ++i) rig.poll_complete ();
    for (auto const& arguments : results)
      {
        auto const result = qvariant_cast<Result> (arguments.at (0));
        if (result.request.path == Path::Tx) QCOMPARE (result.outcome, Outcome::Pending);
      }
    rig.poll_complete ();
    auto const tx = qvariant_cast<Result> (results.last ().at (0));
    QCOMPARE (tx.request.id, quint64 {11});
    QCOMPARE (tx.request.path, Path::Tx);
    QCOMPARE (tx.request.target, state.tx_frequency ());
    QCOMPARE (tx.outcome, Outcome::Failed);
  }

  void restart_renegotiates_readback_resolution ()
  {
    Transceiver::logger_type logger;
    FrequencyRig rig {&logger};
    rig.resolution_code = -3;
    rig.readback_expected = true;
    rig.start (1);
    auto state = request (10, 144200003, 0);
    state.frequency_request (Path::Tx, {});
    QSignalSpy results {&rig, &Transceiver::frequency_result};
    rig.set (state, 2);
    rig.observe_rx (144199904);
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome, Outcome::Established);
    rig.resolution_code = 0;
    rig.start (3);
    rig.set (state, 4);
    rig.observe_rx (144200002);
    for (int i = 0; i < 2; ++i) rig.poll_complete ();
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome, Outcome::Pending);
    rig.poll_complete ();
    QCOMPARE (qvariant_cast<Result> (results.last ().at (0)).outcome, Outcome::Failed);
  }

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
