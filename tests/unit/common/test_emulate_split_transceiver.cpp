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

  TransceiverState last_state;
  unsigned last_sequence_number {0};
};

class TestEmulateSplitTransceiver : public QObject
{
  Q_OBJECT

private:
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
