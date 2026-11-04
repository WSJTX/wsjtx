#include <memory>

#include <QtTest>

#include "Transceiver/EmulateSplitTransceiver.hpp"
#include "Transceiver/TransceiverBase.hpp"
#include "OperatingFrequency.hpp"

class ReadbackTransceiver final : public TransceiverBase
{
public:
  explicit ReadbackTransceiver (logger_type * logger) : TransceiverBase {logger, nullptr} {}
  bool delayed = false;
  Frequency commanded = 0;
  int commands = 0;
  void readback (Frequency value) { update_rx_frequency (value, RigFrequency::Basis::Readback); }

private:
  int do_start () override { readback (14074000); return 0; }
  void do_stop () override {}
  void do_frequency (Frequency value, MODE, bool) override
  {
    commanded = value;
    ++commands;
    if (!delayed) readback (value);
  }
  void do_tx_frequency (Frequency, MODE, bool) override {}
  void do_mode (MODE mode) override { update_mode (mode); }
  void do_ptt (bool value) override { update_PTT (value); }
  bool frequency_readback_expected (RigFrequency::Path, Frequency) const override { return true; }
};

class FakeTransceiver final
  : public Transceiver
{
  Q_OBJECT

public:
  FakeTransceiver ()
    : Transceiver {nullptr, nullptr}
  {
  }

  void set (TransceiverState const& state, unsigned sequence_number) noexcept override
  {
    last_state = state;
    last_sequence_number = sequence_number;
  }

  void start (unsigned sequence_number) noexcept override
  {
    last_sequence_number = sequence_number;
  }

  void stop () noexcept override {}

  void send_update (TransceiverState const& state, unsigned sequence_number)
  {
    Q_EMIT update (state, sequence_number);
  }

  void send_receive_audio (ReceiveAudio audio) { Q_EMIT receiveAudio (audio); }
  void send_resolution (int code) { Q_EMIT resolution (code); }
  void send_result (RigFrequency::Result const& result) { Q_EMIT frequency_result (result); }
  void send_observation (RigFrequency::Evidence const& evidence)
  { Q_EMIT frequency_observation (evidence); }

  TransceiverState last_state;
  unsigned last_sequence_number {0};
};

class TestEmulateSplitTransceiver : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void legacyRxRestorationRequiresEvidence_data ()
  {
    QTest::addColumn<int> ("resolution");
    QTest::addColumn<int> ("delta");
    QTest::newRow ("exact") << 0 << 0;
    QTest::newRow ("rounded-up") << 1 << 9;
    QTest::newRow ("rounded-down") << -1 << -9;
  }

  Q_SLOT void legacyRxRestorationRequiresEvidence ()
  {
    using namespace RigFrequency;
    QFETCH (int, resolution);
    QFETCH (int, delta);
    auto * fake = new FakeTransceiver;
    EmulateSplitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QVector<Evidence> observations;
    connect (&transceiver, &Transceiver::frequency_observation, this,
             [&] (Evidence const& report) { observations.append (report); });
    fake->send_resolution (resolution);
    Transceiver::TransceiverState state;
    state.online (true);
    state.frequency (14076000);
    state.tx_frequency (14074500);
    state.split (true);
    Request const tx {1, 2, Path::Tx, state.tx_frequency ()};
    state.frequency_request (tx);
    state.ptt (true);
    transceiver.set (state, 1);
    fake->send_result ({tx, Outcome::Established, Basis::Readback, tx.target});
    auto const restored = Radio::Frequency (Radio::FrequencyDelta (state.frequency ()) + delta);
    fake->send_observation ({1, 1, Path::Rx, restored, Basis::Readback, {}});
    QCOMPARE (observations.last ().path, Path::Tx);

    state.frequency_request (Path::Tx, {});
    state.ptt (false);
    transceiver.set (state, 2);
    QVERIFY (!fake->last_state.frequency_request (Path::Rx));
    fake->send_observation ({1, 2, Path::Rx, restored, Basis::Retained, {}});
    QCOMPARE (observations.last ().path, Path::Tx);
    fake->send_observation ({1, 3, Path::Rx, tx.target, Basis::Readback, {}});
    QCOMPARE (observations.last ().path, Path::Tx);
    // Operation-associated evidence retains the backend's logical path.
    fake->send_observation ({1, 4, Path::Tx, tx.target, Basis::Readback, tx});
    QCOMPARE (observations.last ().path, Path::Tx);
    fake->send_observation ({1, 5, Path::Rx, restored, Basis::Readback, {}});
    QCOMPARE (observations.last ().path, Path::Rx);
    fake->send_result ({tx, Outcome::Established, Basis::Readback, tx.target});
    fake->send_observation ({1, 6, Path::Tx, tx.target, Basis::Readback, tx});
    QCOMPARE (observations.last ().path, Path::Tx);
    fake->send_observation ({1, 7, Path::Rx, state.frequency () + 1000, Basis::Readback, {}});
    QCOMPARE (observations.last ().path, Path::Rx);
  }

  Q_SLOT void manualQsyAcrossTransmitCycle_data ()
  {
    QTest::addColumn<bool> ("delayed");
    QTest::addColumn<bool> ("sameFrequency");
    QTest::newRow ("synchronous-readback") << false << false;
    QTest::newRow ("delayed-readback") << true << false;
    QTest::newRow ("same-frequency-synchronous") << false << true;
    QTest::newRow ("same-frequency-delayed") << true << true;
  }

  Q_SLOT void manualQsyAcrossTransmitCycle ()
  {
    using namespace RigFrequency;
    QFETCH (bool, delayed);
    QFETCH (bool, sameFrequency);
    auto const txFrequency = Radio::Frequency {sameFrequency ? 14076000u : 14074500u};
    Transceiver::logger_type logger;
    auto * rig = new ReadbackTransceiver {&logger};
    rig->delayed = delayed;
    EmulateSplitTransceiver transceiver {&logger, std::unique_ptr<Transceiver> {rig}};
    OperatingFrequency frequency;
    frequency.setConnectionGeneration (1);
    OperatingFrequency::Context context {true, true, true, false, false, false, true};
    QVector<Evidence> observations;
    connect (&transceiver, &Transceiver::frequency_result, this,
             [&] (Result const& result) { frequency.handleResult (result); });
    connect (&transceiver, &Transceiver::frequency_observation, this,
             [&] (Evidence const& report) {
               observations.append (report);
               frequency.observe (report, context);
             });
    transceiver.start (1);
    Transceiver::TransceiverState state;
    state.online (true);
    state.split (true);
    unsigned sequence = 1;
    int requests = 0;
    auto dispatch = [&] (Request const& request) {
      ++requests;
      if (request.path == Path::Rx) state.frequency (request.target);
      else state.tx_frequency (request.target);
      state.frequency_request (request);
      transceiver.set (state, ++sequence);
      state.frequency_request (request.path, {});
      return true;
    };
    QVERIFY (frequency.requestNominal (14075000, dispatch));
    if (delayed) rig->readback (rig->commanded);
    QVERIFY (!frequency.snapshot ().pendingRx);

    rig->readback (14076000);
    QCOMPARE (frequency.rx (), Radio::Frequency {14076000});
    state.frequency (frequency.correctedRx ());
    QVERIFY (frequency.requestTx (txFrequency, dispatch));
    auto const requestsBefore = requests;
    QVERIFY (frequency.applyCorrection (Path::Rx, {}, false, dispatch));
    QCOMPARE (requests, requestsBefore);

    context.transmitting = true;
    state.ptt (true);
    transceiver.set (state, ++sequence);
    if (delayed && !sameFrequency) rig->readback (rig->commanded);
    rig->readback (txFrequency);
    QCOMPARE (observations.last ().path, Path::Tx);
    QCOMPARE (frequency.rx (), Radio::Frequency {14076000});

    context.transmitting = false;
    state.ptt (false);
    transceiver.set (state, ++sequence);
    if (!sameFrequency) QCOMPARE (rig->commanded, Radio::Frequency {14076000});
    if (delayed && !sameFrequency)
      {
        rig->readback (txFrequency);
        QCOMPARE (observations.last ().path, Path::Tx);
        QCOMPARE (frequency.rx (), Radio::Frequency {14076000});
      }
    rig->readback (14076000);
    QVERIFY (!observations.last ().operation);
    QCOMPARE (observations.last ().path, Path::Rx);
    auto const commandsBefore = rig->commands;
    QVERIFY (frequency.applyCorrection (Path::Rx, {}, false, dispatch));
    QCOMPARE (rig->commands, commandsBefore);

    rig->readback (14077000);
    QCOMPARE (frequency.rx (), Radio::Frequency {14077000});
    QCOMPARE (frequency.remembered (), frequency.rx ());
    rig->readback (txFrequency);
    QCOMPARE (observations.last ().path, Path::Rx);
    QCOMPARE (frequency.rx (), txFrequency);
    rig->readback (14078000);
    QCOMPARE (frequency.rx (), Radio::Frequency {14078000});
    QVERIFY (!frequency.recoveryRequired ());
  }

  Q_SLOT void logicalFrequencyEvidenceFollowsPhysicalPath ()
  {
    using namespace RigFrequency;
    auto * fake = new FakeTransceiver;
    EmulateSplitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QVector<Result> results;
    QVector<Evidence> observations;
    connect (&transceiver, &Transceiver::frequency_result, this,
             [&] (Result const& result) { results.append (result); });
    connect (&transceiver, &Transceiver::frequency_observation, this,
             [&] (Evidence const& report) { observations.append (report); });
    Transceiver::TransceiverState state;
    state.online (true);
    state.frequency (144200100);
    state.tx_frequency (144199900);
    state.split (true);
    Request const rx {7, 1, Path::Rx, state.frequency ()};
    Request const tx {7, 2, Path::Tx, state.tx_frequency ()};
    state.frequency_request (rx);
    state.frequency_request (tx);
    transceiver.set (state, 1);
    QCOMPARE (results.size (), 1);
    QCOMPARE (results.last ().request.path, Path::Tx);
    QCOMPARE (results.last ().basis, Basis::Emulated);
    QVERIFY (!fake->last_state.frequency_request (Path::Tx));
    fake->send_result ({rx, Outcome::Established, Basis::Readback, rx.target});
    state.frequency_request (Path::Rx, {});
    state.frequency_request (Path::Tx, {});
    state.ptt (true);
    transceiver.set (state, 2);
    auto const physical = fake->last_state.frequency_request (Path::Rx);
    QVERIFY (same_operation (physical, tx));
    QCOMPARE (physical.target, tx.target);
    QCOMPARE (results.last ().request.path, Path::Rx);
    fake->send_result ({tx, Outcome::Established, Basis::Readback, tx.target});
    fake->send_observation ({7, 1, Path::Rx, tx.target, Basis::Readback, {}});
    QCOMPARE (observations.last ().path, Path::Tx);
    QCOMPARE (observations.last ().value, tx.target);
    state.ptt (false);
    transceiver.set (state, 3);
    fake->send_result ({rx, Outcome::Established, Basis::Readback, rx.target});
    fake->send_observation ({7, 2, Path::Rx, rx.target + 1000, Basis::Readback, {}});
    QCOMPARE (observations.last ().path, Path::Rx);
    QCOMPARE (observations.last ().value, rx.target + 1000);
    // A delayed command result retains the operation's logical path.
    fake->send_result ({tx, Outcome::Established, Basis::Readback, tx.target});
    QCOMPARE (results.last ().request.id, tx.id);
    QCOMPARE (results.last ().request.path, Path::Tx);
    fake->send_observation ({7, 3, Path::Rx, rx.target + 2000, Basis::Readback, {}});
    QCOMPARE (observations.last ().path, Path::Rx);
  }

  Q_SLOT void forwardsReceiveAudio ()
  {
    auto * raw_fake = new FakeTransceiver;
    EmulateSplitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {raw_fake}};
    ReceiveAudio observed;
    connect (&transceiver, &Transceiver::receiveAudio,
             this, [&observed] (ReceiveAudio audio) { observed = std::move (audio); });
    auto block = std::make_shared<ReceiveAudioBlock> ();

    raw_fake->send_receive_audio (block);

    QCOMPARE (observed, ReceiveAudio {block});
  }

  Q_SLOT void fakeSplitDoesNotPublishTransientTxDialAsRxDial ()
  {
    auto * raw_fake = new FakeTransceiver;
    EmulateSplitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {raw_fake}};
    QSignalSpy updates {&transceiver, &Transceiver::update};

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.frequency (7074000);
    requested.tx_frequency (7072500);
    requested.split (true);
    requested.ptt (true);

    transceiver.set (requested, 17);

    QCOMPARE (raw_fake->last_state.frequency (), requested.tx_frequency ());
    QCOMPARE (raw_fake->last_state.tx_frequency (), Transceiver::Frequency {0});
    QVERIFY (!raw_fake->last_state.split ());

    Transceiver::TransceiverState wrapped_update;
    wrapped_update.online (true);
    wrapped_update.frequency (requested.tx_frequency ());
    wrapped_update.split (false);
    wrapped_update.ptt (false);

    raw_fake->send_update (wrapped_update, 17);

    QCOMPARE (updates.size (), 1);
    auto const published = qvariant_cast<Transceiver::TransceiverState> (updates.takeFirst ().at (0));
    QCOMPARE (published.frequency (), requested.frequency ());
    QCOMPARE (published.tx_frequency (), requested.tx_frequency ());
    QVERIFY (published.split ());
    QVERIFY (!published.ptt ());
  }

  Q_SLOT void receiveModeStillFollowsManualRigQsy ()
  {
    auto * raw_fake = new FakeTransceiver;
    EmulateSplitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {raw_fake}};
    QSignalSpy updates {&transceiver, &Transceiver::update};

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.frequency (7074000);
    requested.tx_frequency (7072500);
    requested.split (true);
    requested.ptt (false);

    transceiver.set (requested, 18);

    Transceiver::TransceiverState wrapped_update;
    wrapped_update.online (true);
    wrapped_update.frequency (14074000);
    wrapped_update.split (false);
    wrapped_update.ptt (false);

    raw_fake->send_update (wrapped_update, 18);

    QCOMPARE (updates.size (), 1);
    auto const published = qvariant_cast<Transceiver::TransceiverState> (updates.takeFirst ().at (0));
    QCOMPARE (published.frequency (), wrapped_update.frequency ());
    QCOMPARE (published.tx_frequency (), requested.tx_frequency ());
    QVERIFY (published.split ());
    QVERIFY (!published.ptt ());
  }
};

QTEST_MAIN (TestEmulateSplitTransceiver)
#include "test_emulate_split_transceiver.moc"
