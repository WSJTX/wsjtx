#include <memory>

#include <QtTest>

#include "Transceiver/EmulateSplitTransceiver.hpp"

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
