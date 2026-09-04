#include <memory>

#include <QtTest>
#include <QThread>

#include "Transceiver/TransceiverBase.hpp"
#include "Transceiver/TransceiverFactory.hpp"
#include "Transceiver/TxInhibitTransceiver.hpp"

class FakeInhibitTransceiver final
  : public Transceiver
{
  Q_OBJECT

public:
  FakeInhibitTransceiver ()
    : Transceiver {nullptr, nullptr}
  {
  }

  void set (TransceiverState const& state, unsigned sequence_number) noexcept override
  {
    ++set_calls;
    last_set_thread = QThread::currentThread ();
    last_state = state;
    last_sequence_number = sequence_number;
  }

  void start (unsigned sequence_number) noexcept override
  {
    started = true;
    last_sequence_number = sequence_number;
  }

  void stop () noexcept override
  {
    stopped = true;
  }

  void send_update (TransceiverState const& state, unsigned sequence_number)
  {
    Q_EMIT update (state, sequence_number);
  }

  TransceiverState last_state;
  unsigned last_sequence_number {0};
  bool stopped {false};
  bool started {false};
  int set_calls {0};
  QThread * last_set_thread {nullptr};
};

class RecordingTransceiverBase final
  : public TransceiverBase
{
public:
  explicit RecordingTransceiverBase (logger_type * logger)
    : TransceiverBase {logger, nullptr}
  {
  }

  QStringList operations;

private:
  int do_start () override {return 0;}
  void do_stop () override {}
  void do_frequency (Frequency, MODE, bool) override {}
  void do_tx_frequency (Frequency, MODE, bool) override {}
  void do_mode (MODE) override {}

  void do_ptt (bool on) override
  {
    operations.append (QStringLiteral ("ptt:%1").arg (on));
    update_PTT (on);
  }

  void do_tune (bool on) override
  {
    operations.append (QStringLiteral ("tune:%1").arg (on));
  }
};

class TestTxInhibitTransceiver : public QObject
{
  Q_OBJECT

private:
  Q_SIGNAL void command (QString controller, quint32 ttl, QString station);

  Q_SLOT void holdFiltersPttAndTuneWithoutChangingReportedRigState ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    QSignalSpy updates {&transceiver, &Transceiver::update};
    transceiver.start (10);
    QVERIFY (!status.isEmpty ());

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    requested.tune (true);
    transceiver.set (requested, 11);
    QVERIFY (fake->last_state.ptt ());
    QVERIFY (fake->last_state.tune ());

    transceiver.tx_inhibit_command (QStringLiteral ("W1AW"), 600, QStringLiteral ("W1AW"));
    QTRY_VERIFY (!fake->last_state.ptt ());
    QVERIFY (!fake->last_state.tune ());

    Transceiver::TransceiverState physical;
    physical.online (true);
    physical.ptt (false);
    physical.tune (false);
    fake->send_update (physical, 11);
    QCOMPARE (updates.size (), 1);
    auto const reported = qvariant_cast<Transceiver::TransceiverState> (updates.takeFirst ().at (0));
    QVERIFY (!reported.ptt ());
    QVERIFY (!reported.tune ());
  }

  Q_SLOT void pttAndTuneTransitionsReachTransceiverBaseInSafeOrder ()
  {
    Transceiver::logger_type logger;
    auto * recorder = new RecordingTransceiverBase {&logger};
    TxInhibitTransceiver transceiver {
      &logger, std::unique_ptr<Transceiver> {recorder}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (12);

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    requested.tune (true);
    transceiver.set (requested, 13);
    QCOMPARE (recorder->operations.mid (recorder->operations.size () - 2),
              QStringList ({QStringLiteral ("tune:1"), QStringLiteral ("ptt:1")}));

    transceiver.tx_inhibit_command (QStringLiteral ("W1AW"), 600, QStringLiteral ("W1AW"));
    QTRY_COMPARE (recorder->operations.mid (recorder->operations.size () - 2),
                  QStringList ({QStringLiteral ("ptt:0"), QStringLiteral ("tune:0")}));

    transceiver.tx_inhibit_command (QStringLiteral ("W1AW"), 0, QStringLiteral ("W1AW"));
    QTRY_COMPARE (recorder->operations.mid (recorder->operations.size () - 2),
                  QStringList ({QStringLiteral ("tune:1"), QStringLiteral ("ptt:1")}));
  }

  Q_SLOT void holdBeforeFirstRequestDoesNotForwardAnOfflineState ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (15);

    transceiver.tx_inhibit_command (QStringLiteral ("W1AW"), 600, QStringLiteral ("W1AW"));
    QTRY_VERIFY (status.last ().at (1).toBool ());
    QCOMPARE (fake->set_calls, 0);

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    transceiver.set (requested, 16);
    QVERIFY (fake->last_state.online ());
    QVERIFY (!fake->last_state.ptt ());
  }

  Q_SLOT void releasesAreScopedToControllerIdentity ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (20);

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    transceiver.set (requested, 21);

    transceiver.tx_inhibit_command (QStringLiteral ("OWNER"), 600, QStringLiteral ("OWNER"));
    QTRY_VERIFY (!fake->last_state.ptt ());

    transceiver.tx_inhibit_command (QStringLiteral ("OTHER"), 0, QStringLiteral ("OTHER"));
    QTest::qWait (30);
    QVERIFY (!fake->last_state.ptt ());

    transceiver.tx_inhibit_command (QStringLiteral ("OTHER"), 600, QStringLiteral ("OWNER"));
    QTRY_COMPARE (status.last ().at (3).toUInt (), 2u);
    transceiver.tx_inhibit_command (QStringLiteral ("OWNER"), 0, QStringLiteral ("OWNER"));
    QTest::qWait (30);
    QVERIFY (!fake->last_state.ptt ());

    transceiver.tx_inhibit_command (QStringLiteral ("OTHER"), 0, QStringLiteral ("OTHER"));
    QTRY_VERIFY (fake->last_state.ptt ());
  }

  Q_SLOT void excessHoldersRemainBoundedAndFailClosed ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (22);

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    transceiver.set (requested, 23);

    for (auto i = 0; i < TxInhibitTransceiver::maximum_tracked_holds; ++i)
      {
        transceiver.tx_inhibit_command (QStringLiteral ("HOLDER_%1").arg (i), 1200, QStringLiteral ("HOLDER_%1").arg (i));
      }
    QTRY_COMPARE (status.last ().at (2).toString ().split (QStringLiteral (", ")).size (),
                  TxInhibitTransceiver::maximum_tracked_holds);

    transceiver.tx_inhibit_command (QStringLiteral ("HOLDER_0"), 2400, QStringLiteral ("HOLDER_0"));

    transceiver.tx_inhibit_command (QStringLiteral ("OVERFLOW"), 1800, QStringLiteral ("OVERFLOW"));

    transceiver.tx_inhibit_command (QStringLiteral ("OVERFLOW"), 0, QStringLiteral ("OVERFLOW"));

    QTRY_COMPARE_WITH_TIMEOUT (status.last ().at (2).toString (),
                               QStringLiteral ("HOLDER_0"), 1800);

    transceiver.tx_inhibit_command (QStringLiteral ("HOLDER_0"), 0, QStringLiteral ("HOLDER_0"));
    QTRY_VERIFY (status.last ().at (2).toString ().isEmpty ());
    QVERIFY (status.last ().at (1).toBool ());
    QVERIFY (!fake->last_state.ptt ());
    QTRY_VERIFY_WITH_TIMEOUT (fake->last_state.ptt (), 1200);
  }

  Q_SLOT void leaseExpiresAndClearedIntentDoesNotRekey ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (30);

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    transceiver.set (requested, 31);

    transceiver.tx_inhibit_command (QStringLiteral ("W2SZ"), 100, QStringLiteral ("W2SZ"));
    QTRY_VERIFY (!fake->last_state.ptt ());
    QTRY_VERIFY_WITH_TIMEOUT (fake->last_state.ptt (), 300);
    QCOMPARE (status.last ().at (5).toUInt (), 1u);

    transceiver.tx_inhibit_command (QStringLiteral ("W2SZ"), 600, QStringLiteral ("W2SZ"));
    QTRY_VERIFY (!fake->last_state.ptt ());
    requested.ptt (false);
    transceiver.set (requested, 32);
    transceiver.tx_inhibit_command (QStringLiteral ("W2SZ"), 0, QStringLiteral ("W2SZ"));
    QTest::qWait (30);
    QVERIFY (!fake->last_state.ptt ());
  }

  Q_SLOT void refreshedHoldExtendsItsDeadline ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (35);

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    transceiver.set (requested, 36);

    transceiver.tx_inhibit_command (QStringLiteral ("W2SZ"), 800, QStringLiteral ("W2SZ"));
    QTRY_VERIFY (!fake->last_state.ptt ());
    QTest::qWait (450);
    transceiver.tx_inhibit_command (QStringLiteral ("W2SZ"), 800, QStringLiteral ("W2SZ"));
    QTest::qWait (450);
    QVERIFY (!fake->last_state.ptt ());
    QTRY_VERIFY_WITH_TIMEOUT (fake->last_state.ptt (), 600);
    QCOMPARE (status.last ().at (3).toUInt (), 2u);
    QCOMPARE (status.last ().at (5).toUInt (), 1u);
  }

  Q_SLOT void stopDoesNotRestorePttFromAnActiveHold ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (37);

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    transceiver.set (requested, 38);

    transceiver.tx_inhibit_command (QStringLiteral ("W2SZ"), 100, QStringLiteral ("W2SZ"));
    QTRY_VERIFY (!fake->last_state.ptt ());
    auto const set_calls = fake->set_calls;

    transceiver.stop ();
    QVERIFY (fake->stopped);
    QVERIFY (!status.last ().at (0).toBool ());
    QVERIFY (!status.last ().at (1).toBool ());
    QTest::qWait (200);
    QCOMPARE (fake->set_calls, set_calls);
    QVERIFY (!fake->last_state.ptt ());
  }

  Q_SLOT void counterOnlyChangesEmitStatus ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (39);

    auto status_count = status.size ();

    transceiver.tx_inhibit_invalid (3);
    QCOMPARE (status.size (), ++status_count);
    QCOMPARE (status.last ().at (6).toUInt (), 3u);

    transceiver.tx_inhibit_command (QStringLiteral ("COUNTER"), 0, {});
    QCOMPARE (status.size (), ++status_count);
    QCOMPARE (status.last ().at (4).toUInt (), 1u);
    transceiver.tx_inhibit_command (QStringLiteral ("COUNTER"), 600, {});
    QCOMPARE (status.size (), ++status_count);
    transceiver.tx_inhibit_command (QStringLiteral ("COUNTER"), 600, {});
    QCOMPARE (status.size (), ++status_count);
    QCOMPARE (status.last ().at (3).toUInt (), 2u);
  }

  Q_SLOT void pttOffWithChangedTxRequestForwardsFinalState ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (41);

    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    transceiver.set (requested, 42);

    auto next_request = requested.tx_request ();
    next_request.mode = QStringLiteral ("JT9");
    next_request.fast_mode = true;
    requested.ptt (false);
    requested.tx_request (next_request);
    transceiver.set (requested, 43);

    QVERIFY (!fake->last_state.ptt ());
    QCOMPARE (fake->last_state.tx_request ().mode, QStringLiteral ("JT9"));
    QVERIFY (fake->last_state.tx_request ().fast_mode);
  }

  Q_SLOT void txRequestEqualityIncludesAllFields ()
  {
    TxEvidence::TxRequest lhs;
    auto rhs = lhs;
    QVERIFY (lhs == rhs);

    rhs.mode = QStringLiteral ("JT9");
    QVERIFY (lhs != rhs);
    rhs = lhs;
    rhs.fast_mode = true;
    QVERIFY (lhs != rhs);
    rhs = lhs;
    rhs.channel = AudioDevice::Right;
    QVERIFY (lhs != rhs);
    rhs = lhs;
    rhs.queue_epoch = TxAudioQueueEpoch {1};
    QVERIFY (lhs != rhs);
    rhs = lhs;
    rhs.start_window_open_ms = 1;
    QVERIFY (lhs != rhs);
  }

  Q_SLOT void txInhibitCapabilityMatchesPttMethod ()
  {
    QVERIFY (TransceiverFactory::supports_tx_inhibit (TransceiverFactory::PTT_method_DTR));
    QVERIFY (TransceiverFactory::supports_tx_inhibit (TransceiverFactory::PTT_method_RTS));
    QVERIFY (!TransceiverFactory::supports_tx_inhibit (TransceiverFactory::PTT_method_CAT));
    QVERIFY (!TransceiverFactory::supports_tx_inhibit (TransceiverFactory::PTT_method_VOX));
  }

  Q_SLOT void queuedCommandsApplyOnTransceiverThread ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QThread worker;
    auto * gui_thread = QThread::currentThread ();
    fake->moveToThread (&worker);
    transceiver.moveToThread (&worker);
    connect (this, &TestTxInhibitTransceiver::command,
             &transceiver, &TxInhibitTransceiver::tx_inhibit_command, Qt::QueuedConnection);
    worker.start ();
    QMetaObject::invokeMethod (&transceiver, [&] {
      transceiver.start (70);
      Transceiver::TransceiverState requested;
      requested.online (true);
      requested.ptt (true);
      transceiver.set (requested, 71);
    }, Qt::BlockingQueuedConnection);
    Q_EMIT command (QStringLiteral ("KEY"), 600, {});
    bool held_on_worker {false};
    QMetaObject::invokeMethod (&transceiver, [&] {
      held_on_worker = !fake->last_state.ptt () && fake->last_set_thread == &worker;
    }, Qt::BlockingQueuedConnection);
    Q_EMIT command (QStringLiteral ("KEY"), 0, {});
    bool released_on_worker {false};
    QMetaObject::invokeMethod (&transceiver, [&] {
      released_on_worker = fake->last_state.ptt () && fake->last_set_thread == &worker;
      transceiver.stop ();
      fake->moveToThread (gui_thread);
      transceiver.moveToThread (gui_thread);
    }, Qt::BlockingQueuedConnection);
    worker.quit ();
    worker.wait ();
    QVERIFY (held_on_worker);
    QVERIFY (released_on_worker);
  }

  Q_SLOT void noHoldAllowsPttAndTune ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    QSignalSpy status {&transceiver, &TxInhibitTransceiver::statusChanged};
    transceiver.start (50);
    QVERIFY (fake->started);
    QVERIFY (status.last ().at (0).toBool ());
    QVERIFY (!status.last ().at (1).toBool ());
    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    requested.tune (true);
    transceiver.set (requested, 51);
    QVERIFY (fake->last_state.ptt ());
    QVERIFY (fake->last_state.tune ());
    requested.ptt (false);
    requested.tune (false);
    transceiver.set (requested, 52);
    QVERIFY (!fake->last_state.ptt ());
    QVERIFY (!fake->last_state.tune ());
  }

  Q_SLOT void endedTuneAndUpdatedMetadataDoNotRestartAfterRelease ()
  {
    auto * fake = new FakeInhibitTransceiver;
    TxInhibitTransceiver transceiver {nullptr, std::unique_ptr<Transceiver> {fake}};
    transceiver.start (60);
    Transceiver::TransceiverState requested;
    requested.online (true);
    requested.ptt (true);
    requested.tune (true);
    transceiver.set (requested, 61);
    transceiver.tx_inhibit_command (QStringLiteral ("KEY"), 600, {});
    requested.ptt (false);
    requested.tune (false);
    auto request = requested.tx_request ();
    request.mode = QStringLiteral ("JT9");
    request.fast_mode = true;
    requested.tx_request (request);
    transceiver.set (requested, 62);
    transceiver.tx_inhibit_command (QStringLiteral ("KEY"), 0, {});
    QVERIFY (!fake->last_state.ptt ());
    QVERIFY (!fake->last_state.tune ());
    QVERIFY (fake->last_state.tx_request () == request);
  }

};

QTEST_MAIN (TestTxInhibitTransceiver)
#include "test_tx_inhibit_transceiver.moc"
