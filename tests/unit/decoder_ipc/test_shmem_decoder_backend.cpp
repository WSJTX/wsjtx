// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <memory>

#include "DecoderIpc.hpp"
#include "ShmemDecoderBackend.hpp"
#include "lib/NativeSharedMemory.hpp"
#include "lib/decoder_ipc_control.h"

namespace
{
  using Kind = DecoderBackend::Output::Kind;
  using FailureKind = DecoderBackend::Failure::Kind;

  void touch (QString const& path)
  {
    QFile file {path};
    file.open (QIODevice::WriteOnly);
  }

  class ForeignCapture final
    : public CapturedDecode
  {
  public:
    decoder_params_t const& settings () const override {return params;}
    decoder_params_t params {};
  };

  class Harness
  {
  public:
    Harness ()
      : key {DecoderIpc::memoryName (QUuid::createUuid ().toString ())}
      , data {new dec_data_t {}}
    {
      opened = DecoderIpc::Status::Ok == session.open (key, directory.filePath ("worker.lock"));
      attached = peer.attach (key.toStdString ());
      backend.reset (new ShmemDecoderBackend {session, *data, mutex, [] (QByteArray const&) {}});
      launched.reset (new QSignalSpy {backend.get (), &DecoderBackend::launched});
      ready.reset (new QSignalSpy {backend.get (), &DecoderBackend::becameReady});
      failures.reset (new QSignalSpy {backend.get (), &DecoderBackend::failed});
      restarts.reset (new QSignalSpy {backend.get (), &DecoderBackend::restartInitiated});
      drains.reset (new QSignalSpy {backend.get (), &DecoderBackend::outputAvailable});
      QObject::connect (backend.get (), &DecoderBackend::outputAvailable, [this] {
          backend->deliverOutput ([this] (DecoderBackend::Output const& output) {
              outputs << output;
            });
        });
    }

    ~Harness ()
    {
      backend->prepareForClose ();
      touch (directory.filePath ("stop"));
      backend->shutdownAtClose ();
    }

    bool valid () const {return directory.isValid () && opened && attached;}

    void start ()
    {
      DecoderBackend::LaunchSettings launch;
      launch.programDirectory = QStringLiteral (SHMEM_WORKER_DIRECTORY);
      launch.writeableDataDirectory = directory.path ();
      launch.temporaryDirectory = directory.path ();
      launch.dataDirectory = directory.path ();
      launch.environment = QProcessEnvironment::systemEnvironment ();
      backend->start (launch);
    }

    void say (QByteArray const& line)
    {
      QFile script {directory.filePath ("script")};
      script.open (QIODevice::Append);
      script.write (line + '\n');
    }

    bool startReady ()
    {
      start ();
      say ("<DecoderReady> version=" + QByteArray::number (DECODER_IPC_VERSION));
      return QTest::qWaitFor ([this] {return ready->count () == 1;}, 20000);
    }

    shared_dec_data_t& shared () {return *static_cast<shared_dec_data_t *> (peer.data ());}

    DecoderBackend::Failure failure () const
    {
      return failures->at (0).at (0).value<DecoderBackend::Failure> ();
    }

    QTemporaryDir directory;
    QString key;
    DecoderIpc::Session session;
    std::unique_ptr<dec_data_t> data;
    QMutex mutex;
    NativeSharedMemory peer;
    bool opened {false};
    bool attached {false};
    std::unique_ptr<ShmemDecoderBackend> backend;
    std::unique_ptr<QSignalSpy> launched;
    std::unique_ptr<QSignalSpy> ready;
    std::unique_ptr<QSignalSpy> failures;
    std::unique_ptr<QSignalSpy> restarts;
    std::unique_ptr<QSignalSpy> drains;
    QList<DecoderBackend::Output> outputs;
  };

  void prepareJt9Request (dec_data_t& data)
  {
    data.params.nutc = 1234;
    data.params.nmode = 9;
    data.params.ntrperiod = 60;
    data.params.kin = 60 * RX_SAMPLE_RATE;
    data.params.newdat = true;
    data.d2[0] = 23;
  }
}

class TestShmemDecoderBackend final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase ();
  void readyHandshakeMakesTheBackendAvailable ();
  void launchesTheWorkerOnTheSession ();
  void handshakeFailures_data ();
  void handshakeFailures ();
  void secondReadyIsAnInvalidHandshake ();
  void outputAfterCloseOrStartupFailureIsDropped_data ();
  void outputAfterCloseOrStartupFailureIsDropped ();
  void cooperativeRestartRelaunchesOnAResetSegment ();
  void stuckWorkerIsTerminatedAndReplaced ();
  void cycleDeliversRecordsCountsAndRetainedAudio ();
  void failedSubmitKeepsTheActiveRequestsLabel ();
  void unconsumedCompletionIsLost ();
  void outputOutsideTheActiveRequestIsNotCurrent ();
  void abandonDecodeForgetsTheRequestAndItsFraming ();
  void capturedDecodeKeepsItsAudioAndSettings ();
  void progressFollowsTheActiveRequest ();
};

void TestShmemDecoderBackend::initTestCase ()
{
  qRegisterMetaType<DecoderBackend::Failure> ();
}

void TestShmemDecoderBackend::readyHandshakeMakesTheBackendAvailable ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  harness.start ();
  QCOMPARE (harness.launched->count (), 1);
  QTRY_VERIFY_WITH_TIMEOUT (harness.backend->running (), 20000);
  QVERIFY (!harness.backend->available ());

  harness.say ("<DecoderReady> version=" + QByteArray::number (DECODER_IPC_VERSION));
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready->count (), 1, 20000);
  QVERIFY (harness.backend->available ());
  QVERIFY (!harness.backend->schedulesDecodes ());
  QVERIFY (harness.session.ready ());
  QCOMPARE (harness.failures->count (), 0);
  QVERIFY (harness.outputs.isEmpty ());
}

void TestShmemDecoderBackend::launchesTheWorkerOnTheSession ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  harness.start ();
  auto const recorded = [&harness] {
    QFile file {harness.directory.filePath ("arguments")};
    return file.open (QIODevice::ReadOnly) ? file.readAll () : QByteArray {};
  };
  QTRY_VERIFY_WITH_TIMEOUT (recorded ().endsWith ("OMP_STACKSIZE=10M\n"), 20000);
  auto const program = QDir::toNativeSeparators (QStringLiteral (SHMEM_WORKER_DIRECTORY)).toLocal8Bit ();
  auto const path = QDir::toNativeSeparators (harness.directory.path ()).toLocal8Bit ();
  auto const threads = QByteArray::number (qMin (qMax (QThread::idealThreadCount () - 1, 1), 3));
  QList<QByteArray> const expected {
    "-s", harness.key.toLocal8Bit (),
    "--ipc-lock", harness.session.workerLockPath ().toLocal8Bit (),
    "-w", "1", "-m", threads, "-e", program, "-a", path, "-t", path, "-r", path,
    "OMP_STACKSIZE=10M", ""};
  QCOMPARE (recorded ().split ('\n'), expected);
}

void TestShmemDecoderBackend::handshakeFailures_data ()
{
  QTest::addColumn<QByteArray> ("line");
  QTest::addColumn<int> ("kind");
  QTest::addColumn<int> ("code");
  QTest::addColumn<QString> ("reason");
  QTest::newRow ("old version") << QByteArray {"<DecoderReady> version=3"}
                                << int (FailureKind::HandshakeInvalid) << 3 << QString {};
  QTest::newRow ("contract refused") << QByteArray {"<DecoderError> status=layout-mismatch"}
                                     << int (FailureKind::HandshakeRefused) << 0
                                     << QStringLiteral ("layout-mismatch");
  QTest::newRow ("unreadable ready") << QByteArray {"<DecoderReady> version=four"}
                                     << int (FailureKind::HandshakeMalformed) << 0 << QString {};
  QTest::newRow ("cycle before ready") << QByteArray {"<DecodeStarted> gen=1"}
                                       << int (FailureKind::HandshakeMalformed) << 0 << QString {};
}

void TestShmemDecoderBackend::handshakeFailures ()
{
  QFETCH (QByteArray, line);
  QFETCH (int, kind);
  QFETCH (int, code);
  QFETCH (QString, reason);
  Harness harness;
  QVERIFY (harness.valid ());
  harness.start ();
  harness.say (line);
  harness.say ("<DecoderReady> version=" + QByteArray::number (DECODER_IPC_VERSION));

  QTRY_COMPARE_WITH_TIMEOUT (harness.failures->count (), 1, 20000);
  auto const failure = harness.failure ();
  QCOMPARE (int (failure.kind), kind);
  QCOMPARE (failure.code, code);
  QCOMPARE (failure.reason, reason);
  QTest::qWait (300);
  QCOMPARE (harness.ready->count (), 0);
  QVERIFY (!harness.backend->ready ());
  QVERIFY (!harness.session.ready ());
  QCOMPARE (harness.failures->count (), 1);
  QVERIFY (harness.outputs.isEmpty ());
}

void TestShmemDecoderBackend::secondReadyIsAnInvalidHandshake ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  QVERIFY (harness.startReady ());
  harness.say ("<DecoderReady> version=" + QByteArray::number (DECODER_IPC_VERSION));
  QTRY_COMPARE_WITH_TIMEOUT (harness.failures->count (), 1, 20000);
  QCOMPARE (harness.failure ().kind, FailureKind::HandshakeInvalid);
  QCOMPARE (harness.failure ().code, int {DECODER_IPC_VERSION});
  QVERIFY (!harness.backend->available ());
  QVERIFY (!harness.session.ready ());
}

void TestShmemDecoderBackend::outputAfterCloseOrStartupFailureIsDropped_data ()
{
  QTest::addColumn<bool> ("closing");
  QTest::newRow ("after close") << true;
  QTest::newRow ("after startup failure") << false;
}

void TestShmemDecoderBackend::outputAfterCloseOrStartupFailureIsDropped ()
{
  QFETCH (bool, closing);
  Harness harness;
  QVERIFY (harness.valid ());
  if (closing)
    {
      QVERIFY (harness.startReady ());
      prepareJt9Request (*harness.data);
      QVERIFY (harness.backend->submit (DecoderBackend::Audio::Current));
      harness.backend->prepareForClose ();
    }
  else
    {
      harness.start ();
      harness.say ("<DecoderReady> version=3");
      QTRY_COMPARE_WITH_TIMEOUT (harness.failures->count (), 1, 20000);
    }
  auto const drained = harness.drains->count ();
  auto const generation = QByteArray::number (closing ? harness.backend->activeRequest () : 1);
  harness.say ("<DecodeStarted> gen=" + generation);
  harness.say ("0000 -10  0.1 1500 @  CQ K1ABC FN42");
  harness.say ("<DecodeFinished>   1   1        0 gen=" + generation);
  harness.say ("not a frame");
  QTRY_VERIFY_WITH_TIMEOUT (harness.drains->count () > drained
                            && !harness.backend->hasPendingOutput (), 20000);
  QTest::qWait (300);
  QVERIFY (harness.outputs.isEmpty ());
  QCOMPARE (harness.failures->count (), closing ? 0 : 1);
}

void TestShmemDecoderBackend::cooperativeRestartRelaunchesOnAResetSegment ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  QVERIFY (harness.startReady ());
  prepareJt9Request (*harness.data);
  QVERIFY (harness.backend->submit (DecoderBackend::Audio::Current));
  QVERIFY (harness.backend->activeRequest () > 0);

  harness.backend->restart ();
  QCOMPARE (harness.restarts->count (), 1);
  QVERIFY (harness.backend->restartInProgress ());
  QVERIFY (!harness.backend->available ());
  QCOMPARE (DecoderIpc::state (harness.shared ()), qint32 {DECODER_IPC_SHUTDOWN});

  // The replacement replays the script, so it repeats the handshake.
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready->count (), 2, 20000);
  QCOMPARE (harness.launched->count (), 2);
  QCOMPARE (harness.failures->count (), 0);
  QFile exits {harness.directory.filePath ("exits")};
  QVERIFY (exits.open (QIODevice::ReadOnly));
  QCOMPARE (exits.readAll (), QByteArray {"shutdown\n"});
  QVERIFY (harness.session.ready ());
  QCOMPARE (DecoderIpc::state (harness.shared ()), qint32 {DECODER_IPC_IDLE});
  QCOMPARE (DecoderIpc::generation (harness.shared ()), qint32 {0});
  QCOMPARE (harness.backend->activeRequest (), qint64 {0});

  QVERIFY (harness.backend->submit (DecoderBackend::Audio::Current));
  QVERIFY (harness.backend->activeRequest () > 0);
}

void TestShmemDecoderBackend::stuckWorkerIsTerminatedAndReplaced ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  touch (harness.directory.filePath ("ignore-shutdown"));
  QVERIFY (harness.startReady ());

  QElapsedTimer clock;
  clock.start ();
  harness.backend->restart ();
  QTRY_COMPARE_WITH_TIMEOUT (harness.launched->count (), 2, 20000);
  // The worker ignores SHUTDOWN, so the 250 ms rung stops it; coarse timers may fire 5% early.
  QVERIFY2 (clock.elapsed () >= 230 && clock.elapsed () < 2500,
            qPrintable (QStringLiteral ("relaunched after %1 ms").arg (clock.elapsed ())));
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready->count (), 2, 20000);
  QVERIFY (!QFile::exists (harness.directory.filePath ("exits")));
  QCOMPARE (harness.failures->count (), 0);
  QVERIFY (harness.backend->available ());
}

void TestShmemDecoderBackend::cycleDeliversRecordsCountsAndRetainedAudio ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  QVERIFY (harness.startReady ());
  QVERIFY (!harness.backend->retainedAudio ().available);
  QCOMPARE (harness.backend->activeRequest (), qint64 {0});

  prepareJt9Request (*harness.data);
  QVERIFY (harness.backend->submit (DecoderBackend::Audio::Current));
  auto const request = harness.backend->activeRequest ();
  QVERIFY (request > 0);
  QCOMPARE (harness.shared ().payload.d2[0], short {23});
  auto const published = harness.backend->lastSubmission ();
  QCOMPARE (published.params.nutc, 1234);
  QCOMPARE (published.samples, static_cast<short const *> (harness.data->d2));
  QCOMPARE (published.metadata.valid_samples, int32_t {60 * RX_SAMPLE_RATE});
  qint32 claimed {0};
  QVERIFY (DecoderIpc::claim (harness.shared (), claimed));
  QCOMPARE (qint64 {claimed}, request);
  QVERIFY (DecoderIpc::finish (harness.shared (), claimed));

  auto const generation = QByteArray::number (request);
  QByteArray const record {"0000 -10  0.1 1500 @  CQ K1ABC FN42"};
  harness.say ("<DecodeStarted> gen=" + generation);
  harness.say (record);
  harness.say ("<DecodeFinished>   2   1        7 gen=" + generation);
  QTRY_COMPARE_WITH_TIMEOUT (harness.outputs.size (), 3, 20000);

  QCOMPARE (harness.outputs.at (0).kind, Kind::CycleStarted);
  QVERIFY (harness.outputs.at (0).current);
  QCOMPARE (harness.outputs.at (0).request, request);
  for (auto const& output : harness.outputs) QCOMPARE (output.period, qint32 {1234});
  QCOMPARE (harness.outputs.at (1).kind, Kind::Record);
  QVERIFY (harness.outputs.at (1).current);
  QCOMPARE (harness.outputs.at (1).text, record + '\n');
  auto const& finished = harness.outputs.at (2);
  QCOMPARE (finished.kind, Kind::CycleFinished);
  QVERIFY (finished.current);
  QCOMPARE (finished.counts.synchronized, 2);
  QCOMPARE (finished.counts.decoded, 1);
  QCOMPARE (finished.counts.average, 7);
  QCOMPARE (harness.backend->activeRequest (), qint64 {0});
  QCOMPARE (DecoderIpc::state (harness.shared ()), qint32 {DECODER_IPC_IDLE});

  auto retained = harness.backend->retainedAudio ();
  QVERIFY (retained.available);
  QCOMPARE (retained.mode, 9);
  QCOMPARE (retained.period, 60);
  QVERIFY (retained.fromCurrentInput);

  harness.data->params.nutc = 1235;
  QVERIFY (harness.backend->submit (DecoderBackend::Audio::Retained));
  auto const again = harness.backend->activeRequest ();
  QVERIFY (again > request);
  QCOMPARE (harness.backend->lastSubmission ().params.nutc, 1235);
  QVERIFY (DecoderIpc::claim (harness.shared (), claimed));
  QVERIFY (DecoderIpc::finish (harness.shared (), claimed));
  auto const repeated = QByteArray::number (again);
  harness.say ("<DecodeStarted> gen=" + repeated);
  harness.say ("<DecodeFinished>   0   0        0 gen=" + repeated);
  QTRY_COMPARE_WITH_TIMEOUT (harness.outputs.size (), 5, 20000);
  QCOMPARE (harness.outputs.at (3).period, qint32 {1235});
  QCOMPARE (harness.outputs.at (4).kind, Kind::CycleFinished);
  QCOMPARE (harness.outputs.at (4).period, qint32 {1235});

  harness.backend->beginInput ();
  QVERIFY (!harness.backend->retainedAudio ().fromCurrentInput);
  QVERIFY (harness.backend->retainedAudio ().available);
  harness.backend->discardRetainedAudio ();
  QVERIFY (!harness.backend->retainedAudio ().available);
}

void TestShmemDecoderBackend::failedSubmitKeepsTheActiveRequestsLabel ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  QVERIFY (harness.startReady ());
  prepareJt9Request (*harness.data);
  QVERIFY (harness.backend->submit (DecoderBackend::Audio::Current));
  auto const request = harness.backend->activeRequest ();

  harness.data->params.nutc = 999;
  QVERIFY (!harness.backend->submit (DecoderBackend::Audio::Current));
  QCOMPARE (harness.backend->activeRequest (), request);
  QCOMPARE (harness.backend->lastSubmission ().params.nutc, 1234);
  harness.say ("<DecodeStarted> gen=" + QByteArray::number (request));
  QTRY_COMPARE_WITH_TIMEOUT (harness.outputs.size (), 1, 20000);
  QCOMPARE (harness.outputs.at (0).period, qint32 {1234});
}

void TestShmemDecoderBackend::unconsumedCompletionIsLost ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  QVERIFY (harness.startReady ());
  prepareJt9Request (*harness.data);
  QVERIFY (harness.backend->submit (DecoderBackend::Audio::Current));
  auto const generation = QByteArray::number (harness.backend->activeRequest ());

  harness.say ("<DecodeStarted> gen=" + generation);
  harness.say ("<DecodeFinished>   0   0        0 gen=" + generation);
  QTRY_COMPARE_WITH_TIMEOUT (harness.outputs.size (), 2, 20000);
  QCOMPARE (harness.outputs.at (1).kind, Kind::CycleLost);
  QVERIFY (harness.outputs.at (1).current);
  QVERIFY (harness.backend->activeRequest () > 0);
}

void TestShmemDecoderBackend::outputOutsideTheActiveRequestIsNotCurrent ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  QVERIFY (harness.startReady ());

  harness.say ("not a frame");
  harness.say ("<DecodeStarted> gen=5");
  harness.say ("0000 -10  0.1 1500 @  CQ K1ABC FN42");
  harness.say ("<DecodeRejected> status=invalid operation=decode mode=9 input=1 analysis=1 attempt=1");
  harness.say ("<DecodeFinished> unreadable");
  harness.say ("<DecodeFinished>   0   0        0 gen=5");
  QTRY_COMPARE_WITH_TIMEOUT (harness.outputs.size (), 6, 20000);

  QList<Kind> const kinds {Kind::Unframed, Kind::CycleStarted, Kind::Record, Kind::Rejected,
                           Kind::MalformedBoundary, Kind::CycleFinished};
  for (int index = 0; index < kinds.size (); ++index)
    {
      QCOMPARE (harness.outputs.at (index).kind, kinds.at (index));
      QVERIFY (!harness.outputs.at (index).current);
    }
  QCOMPARE (harness.outputs.at (1).request, qint64 {5});
  for (auto const& output : harness.outputs) QCOMPARE (output.period, qint32 {0});
  QCOMPARE (harness.failures->count (), 0);
}

void TestShmemDecoderBackend::abandonDecodeForgetsTheRequestAndItsFraming ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  QVERIFY (harness.startReady ());
  prepareJt9Request (*harness.data);
  QVERIFY (harness.backend->submit (DecoderBackend::Audio::Current));
  auto const generation = QByteArray::number (harness.backend->activeRequest ());
  harness.say ("<DecodeStarted> gen=" + generation);
  QTRY_COMPARE_WITH_TIMEOUT (harness.outputs.size (), 1, 20000);

  harness.backend->abandonDecode ();
  QCOMPARE (harness.backend->activeRequest (), qint64 {0});
  QVERIFY (!harness.backend->retainedAudio ().available);
  harness.say ("<DecodeFinished>   0   0        0 gen=" + generation);
  QTRY_COMPARE_WITH_TIMEOUT (harness.outputs.size (), 2, 20000);
  QCOMPARE (harness.outputs.at (1).kind, Kind::MalformedBoundary);
}

void TestShmemDecoderBackend::capturedDecodeKeepsItsAudioAndSettings ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  QVERIFY (harness.startReady ());
  harness.data->params.nmode = 8;
  harness.data->params.ntrperiod = 15;
  harness.data->params.lmultift8 = true;
  harness.data->params.nutc = 120000;
  harness.data->params.kin = 15 * RX_SAMPLE_RATE;
  harness.data->d2[0] = 17;
  auto const captured = harness.backend->capture ();
  QVERIFY (captured);
  QCOMPARE (captured->settings ().nutc, 120000);

  harness.data->params.nutc = 120015;
  harness.data->d2[0] = 99;
  QVERIFY (!harness.backend->submit (ForeignCapture {}));
  QCOMPARE (harness.backend->activeRequest (), qint64 {0});
  QVERIFY (harness.backend->submit (*captured));
  QVERIFY (harness.backend->activeRequest () > 0);
  QCOMPARE (harness.shared ().payload.params.nutc, 120000);
  QCOMPARE (harness.shared ().payload.d2[0], short {17});
  QCOMPARE (harness.shared ().metadata.valid_samples, int32_t {15 * RX_SAMPLE_RATE});
  auto const described = ShmemDecoderBackend::describe (*captured);
  QCOMPARE (described.params.nutc, 120000);
  QCOMPARE (described.samples[0], short {17});
  QCOMPARE (described.metadata.valid_samples, int32_t {15 * RX_SAMPLE_RATE});
  QVERIFY (!ShmemDecoderBackend::describe (ForeignCapture {}).samples);
  harness.say ("<DecodeStarted> gen=" + QByteArray::number (harness.backend->activeRequest ()));
  QTRY_COMPARE_WITH_TIMEOUT (harness.outputs.size (), 1, 20000);
  QCOMPARE (harness.outputs.at (0).period, qint32 {120000});
}

void TestShmemDecoderBackend::progressFollowsTheActiveRequest ()
{
  Harness harness;
  QVERIFY (harness.valid ());
  QVERIFY (harness.startReady ());
  qint32 progress {-1};
  QVERIFY (!harness.backend->activeProgress (progress));

  prepareJt9Request (*harness.data);
  QVERIFY (harness.backend->submit (DecoderBackend::Audio::Current));
  qint32 claimed {0};
  QVERIFY (DecoderIpc::claim (harness.shared (), claimed));
  QVERIFY (harness.backend->activeProgress (progress));
  auto const before = progress;
  auto& control = harness.shared ().control;
  decoder_ipc_progress_bind (&control.generation, &control.state, &control.version,
                             &control.progress);
  decoder_ipc_progress_report (claimed);
  decoder_ipc_progress_unbind ();
  QVERIFY (harness.backend->activeProgress (progress));
  QVERIFY2 (progress != before, qPrintable (QStringLiteral ("%1 -> %2").arg (before).arg (progress)));

  harness.backend->abandonDecode ();
  QVERIFY (!harness.backend->activeProgress (progress));
}

QTEST_GUILESS_MAIN (TestShmemDecoderBackend)

#include "test_shmem_decoder_backend.moc"
