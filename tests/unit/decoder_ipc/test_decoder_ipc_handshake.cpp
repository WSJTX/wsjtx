#include <QtTest>
#include <QProcess>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUuid>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <memory>
#include <cstdio>

#include "DecoderIpc.hpp"
#include "lib/NativeSharedMemory.hpp"
#include "lib/DecoderWorkerLock.hpp"

#ifndef Q_OS_WIN
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/statvfs.h>
#endif

namespace
{
  QString lockPath (QString const& directory)
  {
    return directory + "/worker.lock";
  }

  class DecoderProcess : public QProcess
  {
  public:
    ~DecoderProcess () override
    {
      if (state () != NotRunning)
        {
          kill ();
          waitForFinished (5000);
        }
    }
    void launch (QString const& key, QString const& directory)
    {
      start (QStringLiteral (DECODER_IPC_JT9),
             {"--shmem", key, "--ipc-lock", lockPath (directory), "-w", "1", "-m", "1", "-e", directory,
              "-a", directory, "-t", directory, "-r", QStringLiteral (DECODER_IPC_SOURCE)});
    }
    bool waitForOutput (QByteArray const& marker, QByteArray& output)
    {
      auto completeLine = [&] {
        auto const position = output.indexOf (marker);
        return position >= 0 && output.indexOf ('\n', position) >= 0;
      };
      QElapsedTimer timer;
      timer.start ();
      while (timer.elapsed () < 5000)
        {
          output += readAllStandardOutput ();
          if (completeLine ()) return true;
          if (state () == NotRunning) break;
          waitForReadyRead (100);
        }
      output += readAllStandardOutput ();
      return completeLine ();
    }
  };

  QString uniqueKey ()
  {
    return DecoderIpc::memoryName (QUuid::createUuid ().toString ());
  }
}

class TestDecoderIpcHandshake final : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void readyBeforePublication ();
  void rejectsSegment_data ();
  void rejectsSegment ();
  void staleNameRecovery_data ();
  void staleNameRecovery ();
  void mappingOwnership ();
  void missingMappingFails ();
  void replacementWorkerCompletesRequest ();
  void simultaneousSessionsStayIsolated ();
  void orphanLockLifetime_data ();
  void orphanLockLifetime ();
  void blockedWorkerPreservesFiles ();
  void insufficientLinuxSharedMemory ();
};

void TestDecoderIpcHandshake::readyBeforePublication ()
{
  using namespace DecoderIpc;
  auto const key = uniqueKey ();
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  Session session;
  QCOMPARE (session.open (key, lockPath (directory.path ())), Status::Ok);
  auto source = std::make_unique<dec_data_t> ();
  QCOMPARE (session.submit (Request::snapshot (*source)).status, Status::Unavailable);
  DecoderProcess child;
  child.launch (key, directory.path ());
  QVERIFY (child.waitForStarted (5000));
  QByteArray output;
  QElapsedTimer timer;
  timer.start ();
  while (!output.contains ('\n') && timer.elapsed () < 5000)
    {
      child.waitForReadyRead (100);
      output += child.readAllStandardOutput ();
      if (child.state () == QProcess::NotRunning) break;
    }
  auto const ready = QByteArray {"<DecoderReady> version="} + QByteArray::number (DECODER_IPC_VERSION);
  QVERIFY2 (output.startsWith (ready + "\r\n")
            || output.startsWith (ready + "\n"), output.constData ());
  QVERIFY (!output.contains ("<DecodeStarted>"));
  QCOMPARE (session.acceptReady (DECODER_IPC_VERSION), Status::Ok);
  QVERIFY (session.ready ());
  session.shutdown ();
  QVERIFY (child.waitForFinished (5000));
  output += child.readAllStandardOutput ();
  QCOMPARE (child.exitStatus (), QProcess::NormalExit);
  QCOMPARE (child.exitCode (), 0);
  QVERIFY2 (!output.contains ("<DecoderError>"), output.constData ());
  QFile wisdom {directory.filePath ("jt9_wisdom.dat")};
  QVERIFY (wisdom.open (QIODevice::ReadOnly));
  auto const savedWisdom = wisdom.readAll ();
  QVERIFY (savedWisdom.contains ("fftwf_wisdom"));
  wisdom.close ();
  QVERIFY (QFile::exists (directory.filePath ("timer.out")));

  QVERIFY (session.reset ());
  child.launch (key, directory.path ());
  QVERIFY (child.waitForStarted (5000));
  output.clear ();
  QVERIFY2 (child.waitForOutput ("<DecoderReady>", output), output.constData ());
  session.shutdown ();
  QVERIFY (child.waitForFinished (5000));
  QCOMPARE (child.exitStatus (), QProcess::NormalExit);
  QCOMPARE (child.exitCode (), 0);
  QVERIFY (wisdom.open (QIODevice::ReadOnly));
  QCOMPARE (wisdom.readAll (), savedWisdom);
}

void TestDecoderIpcHandshake::rejectsSegment_data ()
{
  QTest::addColumn<QString> ("mutation");
  QTest::addColumn<QByteArray> ("status");
  QTest::newRow ("v1") << QString {"v1"} << QByteArray {"unsupported-version"};
  QTest::newRow ("v2") << QString {"v2"} << QByteArray {"unsupported-version"};
  QTest::newRow ("v3") << QString {"v3"} << QByteArray {"unsupported-version"};
  QTest::newRow ("v4") << QString {"v4"} << QByteArray {"unsupported-version"};
  QTest::newRow ("future-version") << QString {"v%1"}.arg (DECODER_IPC_VERSION + 1)
                                  << QByteArray {"unsupported-version"};
  QTest::newRow ("header-size") << QString {"header"} << QByteArray {"wrong-header-size"};
  QTest::newRow ("payload-size") << QString {"payload"} << QByteArray {"wrong-payload-size"};
  QTest::newRow ("field-offset") << QString {"offset"} << QByteArray {"incompatible-layout"};
  QTest::newRow ("capabilities") << QString {"caps"} << QByteArray {"unsupported-capabilities"};
  QTest::newRow ("truncated") << QString {"truncated"} << QByteArray {"truncated-segment"};
}

void TestDecoderIpcHandshake::rejectsSegment ()
{
  QFETCH (QString, mutation);
  QFETCH (QByteArray, status);
  auto const key = uniqueKey ();
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  QByteArray const sentinel {"existing decoder output\n"};
  for (auto const& name : {"timer.out", "jt9_wisdom.dat"})
    {
      QFile file {directory.filePath (name)};
      QVERIFY (file.open (QIODevice::WriteOnly));
      QCOMPARE (file.write (sentinel), qint64 {sentinel.size ()});
    }
  NativeSharedMemory memory;
  bool const truncated = mutation == "truncated";
  QVERIFY2 (memory.create (key.toStdString (), truncated ? offsetof (shared_dec_data_t, payload)
                                    : sizeof (shared_dec_data_t)),
            memory.errorString ().c_str ());
  if (truncated)
    {
      auto * control = static_cast<decoder_ipc_control_t *> (memory.data ());
      *control = {1, DECODER_IPC_READY, DECODER_IPC_VERSION, 0};
      decoder_ipc_layout_t layout;
      decoder_ipc_expected_layout (&layout);
      std::memcpy (static_cast<unsigned char *> (memory.data ())
                   + offsetof (shared_dec_data_t, layout),
                   &layout, sizeof layout);
    }
  else
    {
      auto& shared = *static_cast<shared_dec_data_t *> (memory.data ());
      DecoderIpc::initialize (shared);
      shared.control.state = DECODER_IPC_READY;
      shared.control.generation = 1;
      if (mutation.startsWith ('v')) shared.control.version = mutation.mid (1).toInt ();
      if (mutation == "header") ++shared.layout.header_bytes;
      if (mutation == "payload") --shared.layout.payload_bytes;
      if (mutation == "offset") ++shared.layout.fields[7].offset;
      if (mutation == "caps") shared.layout.capabilities = 0;
    }
  QByteArray const before {static_cast<char const *> (memory.data ()), static_cast<int> (memory.size ())};
  DecoderProcess child;
  child.launch (key, directory.path ());
  QVERIFY (child.waitForStarted (5000));
  QVERIFY (child.waitForFinished (5000));
  auto const output = child.readAllStandardOutput ();
  QCOMPARE (child.exitStatus (), QProcess::NormalExit);
  QCOMPARE (child.exitCode (), 1);
  QVERIFY2 (output.contains ("<DecoderError> status=" + status), output.constData ());
  QVERIFY (!output.contains ("<DecoderReady>"));
  QVERIFY (!output.contains ("<DecodeStarted>"));
  QCOMPARE (std::memcmp (before.constData (), memory.data (), before.size ()), 0);
  for (auto const& name : {"timer.out", "jt9_wisdom.dat"})
    {
      QFile file {directory.filePath (name)};
      QVERIFY (file.open (QIODevice::ReadOnly));
      QCOMPARE (file.readAll (), sentinel);
    }
}

void TestDecoderIpcHandshake::staleNameRecovery_data ()
{
  QTest::addColumn<bool> ("compatible");
  QTest::newRow ("compatible") << true;
  QTest::newRow ("incompatible") << false;
}

void TestDecoderIpcHandshake::staleNameRecovery ()
{
  QFETCH (bool, compatible);
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  auto const key = uniqueKey ();
  NativeSharedMemory memory;
#ifdef Q_OS_WIN
  QVERIFY (memory.create (key.toStdString (), sizeof (shared_dec_data_t)));
#else
  // A crashed creator leaves its name behind while decoder attachments stay alive.
  auto const descriptor = shm_open (key.toStdString ().c_str (), O_CREAT | O_EXCL | O_RDWR, 0600);
  QVERIFY (descriptor >= 0);
  auto cleanup = qScopeGuard ([&] { NativeSharedMemory::remove (key.toStdString ()); });
  auto const resized = ftruncate (descriptor, sizeof (shared_dec_data_t));
  auto const closed = close (descriptor);
  QCOMPARE (resized, 0);
  QCOMPARE (closed, 0);
  auto const attached = memory.attach (key.toStdString ());
  QVERIFY2 (attached, memory.errorString ().c_str ());
#endif
  auto& shared = *static_cast<shared_dec_data_t *> (memory.data ());
  DecoderIpc::initialize (shared);
  shared.payload.d2[0] = 41;
  if (!compatible) shared.control.version = 2;
  QByteArray const before {static_cast<char const *> (memory.data ()), static_cast<int> (memory.size ())};
  DecoderIpc::Session session;
#ifdef Q_OS_WIN
  QCOMPARE (session.open (key, lockPath (directory.path ())), DecoderIpc::Status::Orphaned);
  QVERIFY (!session.errorString ().isEmpty ());
#else
  QCOMPARE (session.open (key, lockPath (directory.path ())), DecoderIpc::Status::Ok);
#endif
  if (compatible)
    {
      QCOMPARE (DecoderIpc::state (shared), int {DECODER_IPC_SHUTDOWN});
      QCOMPARE (shared.payload.d2[0], short {41});
    }
  else
    {
      QCOMPARE (std::memcmp (before.constData (), memory.data (), before.size ()), 0);
    }
  QVERIFY (memory.detach ());
#ifdef Q_OS_WIN
  QCOMPARE (session.open (key, lockPath (directory.path ())), DecoderIpc::Status::Ok);
#endif
  NativeSharedMemory replacement;
  QVERIFY2 (replacement.attach (key.toStdString ()), replacement.errorString ().c_str ());
  auto const& fresh = *static_cast<shared_dec_data_t const *> (replacement.data ());
  QCOMPARE (DecoderIpc::state (fresh), int {DECODER_IPC_IDLE});
  QCOMPARE (fresh.control.version, int {DECODER_IPC_VERSION});
  QCOMPARE (fresh.payload.d2[0], short {0});
}

void TestDecoderIpcHandshake::mappingOwnership ()
{
  auto const name = uniqueKey ().toStdString ();
  NativeSharedMemory owner;
  QVERIFY2 (owner.create (name, 4096), owner.errorString ().c_str ());
  std::memset (owner.data (), 0x5a, 4096);
  QByteArray const before {static_cast<char const *> (owner.data ()), 4096};
  {
    NativeSharedMemory duplicate;
    QVERIFY (!duplicate.create (name, 4096));
    QVERIFY (duplicate.alreadyExists ());
    QVERIFY (!duplicate.isAttached ());
    QVERIFY (!duplicate.errorString ().empty ());
  }
  QCOMPARE (std::memcmp (owner.data (), before.constData (), before.size ()), 0);
  NativeSharedMemory peer;
  QVERIFY2 (peer.attach (name), peer.errorString ().c_str ());
  QVERIFY (peer.size () >= 4096);
  QCOMPARE (std::memcmp (owner.data (), peer.data (), 4096), 0);
  QCOMPARE (static_cast<unsigned char *> (owner.data ())[0], static_cast<unsigned char> (0x5a));
  QVERIFY (peer.detach ());
  QVERIFY2 (peer.attach (name), peer.errorString ().c_str ());
  QVERIFY (owner.detach ());
  QCOMPARE (static_cast<unsigned char *> (peer.data ())[0], static_cast<unsigned char> (0x5a));
#ifndef Q_OS_WIN
  NativeSharedMemory retired;
  QVERIFY (!retired.attach (name));
#endif
  QVERIFY (peer.detach ());
  NativeSharedMemory retiredAfterLastPeer;
  QVERIFY (!retiredAfterLastPeer.attach (name));
}

void TestDecoderIpcHandshake::missingMappingFails ()
{
  auto const key = uniqueKey ();
  NativeSharedMemory memory;
  QVERIFY (!memory.attach (key.toStdString ()));
  QVERIFY (memory.notFound ());
  QVERIFY (!memory.isAttached ());
  QVERIFY (!memory.errorString ().empty ());
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  DecoderProcess child;
  child.launch (key, directory.path ());
  QVERIFY (child.waitForStarted (5000));
  QVERIFY (child.waitForFinished (5000));
  auto const output = child.readAllStandardOutput ();
  QCOMPARE (child.exitStatus (), QProcess::NormalExit);
  QCOMPARE (child.exitCode (), 1);
  QVERIFY2 (output.contains ("<DecoderError>"), output.constData ());
  QVERIFY (!output.contains ("<DecoderReady>"));
}

void TestDecoderIpcHandshake::replacementWorkerCompletesRequest ()
{
  using namespace DecoderIpc;
  auto const key = uniqueKey ();
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  Session session;
  QCOMPARE (session.open (key, lockPath (directory.path ())), Status::Ok);
  NativeSharedMemory peer;
  QVERIFY (peer.attach (key.toStdString ()));
  auto const* retained = peer.data ();
  auto payload = std::make_unique<dec_data_t> ();
  payload->params.nmode = 8;
  payload->params.ntrperiod = 15;
  payload->params.nzhsym = 50;
  payload->params.newdat = true;
  payload->params.nfa = 200;
  payload->params.nfb = 4000;
  qint32 previousGeneration {0};
  DecoderProcess child;
  for (int worker = 0; worker < 2; ++worker)
    {
      child.launch (key, directory.path ());
      QVERIFY (child.waitForStarted (5000));
      QByteArray output;
      QVERIFY2 (child.waitForOutput ("<DecoderReady>", output), output.constData ());
      QCOMPARE (session.acceptReady (DECODER_IPC_VERSION), Status::Ok);
      auto const submission = session.submit (Request::snapshot (*payload, {11, 12, worker + 1, 180000}));
      QVERIFY (submission);
      QVERIFY (submission.generation.value () > previousGeneration);
      previousGeneration = submission.generation.value ();
      QVERIFY2 (child.waitForOutput ("<DecodeFinished>", output), output.constData ());
      Completion completion {};
      bool completed {false};
      for (auto const& line : output.split ('\n'))
        if (parseCompletion (line, &completion)) completed = true;
      QVERIFY2 (completed, output.constData ());
      QCOMPARE (completion.generation, submission.generation.value ());
      QCOMPARE (session.complete (completion), Status::Ok);
      QVERIFY2 (!output.contains ("<DecodeRejected>"), output.constData ());
      QVERIFY2 (!output.contains ("<DecoderError>"), output.constData ());
      if (worker == 0)
        {
          child.kill ();
          QVERIFY (child.waitForFinished (5000));
          QCOMPARE (child.state (), QProcess::NotRunning);
          QVERIFY (session.reset ());
          QVERIFY (!session.ready ());
          QVERIFY (peer.data () == retained);
          QCOMPARE (state (*static_cast<shared_dec_data_t const *> (retained)), int {DECODER_IPC_IDLE});
        }
    }
  session.shutdown ();
  QVERIFY (child.waitForFinished (5000));
  QCOMPARE (child.exitStatus (), QProcess::NormalExit);
  QCOMPARE (child.exitCode (), 0);
}

void TestDecoderIpcHandshake::simultaneousSessionsStayIsolated ()
{
  using namespace DecoderIpc;
  auto const firstKey = uniqueKey ();
  auto const secondKey = uniqueKey ();
  QTemporaryDir firstDirectory;
  QTemporaryDir secondDirectory;
  QVERIFY (firstDirectory.isValid ());
  QVERIFY (secondDirectory.isValid ());
  Session first;
  Session second;
  QCOMPARE (first.open (firstKey, lockPath (firstDirectory.path ())), Status::Ok);
  QCOMPARE (second.open (secondKey, lockPath (secondDirectory.path ())), Status::Ok);
  DecoderProcess firstChild;
  DecoderProcess secondChild;
  firstChild.launch (firstKey, firstDirectory.path ());
  secondChild.launch (secondKey, secondDirectory.path ());
  QVERIFY (firstChild.waitForStarted (5000));
  QVERIFY (secondChild.waitForStarted (5000));
  QByteArray firstOutput;
  QByteArray secondOutput;
  QVERIFY2 (firstChild.waitForOutput ("<DecoderReady>", firstOutput), firstOutput.constData ());
  QVERIFY2 (secondChild.waitForOutput ("<DecoderReady>", secondOutput), secondOutput.constData ());
  QCOMPARE (first.acceptReady (DECODER_IPC_VERSION), Status::Ok);
  QCOMPARE (second.acceptReady (DECODER_IPC_VERSION), Status::Ok);
  first.shutdown ();
  QVERIFY (firstChild.waitForFinished (5000));
  QCOMPARE (firstChild.exitCode (), 0);
  first.detach ();
  QCOMPARE (secondChild.state (), QProcess::Running);
  auto payload = std::make_unique<dec_data_t> ();
  payload->params.nmode = 8;
  payload->params.ntrperiod = 15;
  payload->params.nzhsym = 50;
  payload->params.newdat = true;
  payload->params.nfa = 200;
  payload->params.nfb = 4000;
  auto const submission = second.submit (Request::snapshot (*payload, {21, 22, 1, 180000}));
  QVERIFY (submission);
  QVERIFY2 (secondChild.waitForOutput ("<DecodeFinished>", secondOutput), secondOutput.constData ());
  Completion completion {};
  bool completed {false};
  for (auto const& line : secondOutput.split ('\n'))
    if (parseCompletion (line, &completion)) completed = true;
  QVERIFY2 (completed, secondOutput.constData ());
  QCOMPARE (second.complete (completion), Status::Ok);
  QVERIFY2 (!secondOutput.contains ("<DecodeRejected>"), secondOutput.constData ());
  QVERIFY2 (!secondOutput.contains ("<DecoderError>"), secondOutput.constData ());
  second.shutdown ();
  QVERIFY (secondChild.waitForFinished (5000));
  QCOMPARE (secondChild.exitStatus (), QProcess::NormalExit);
  QCOMPARE (secondChild.exitCode (), 0);
}

void TestDecoderIpcHandshake::orphanLockLifetime_data ()
{
  QTest::addColumn<bool> ("killed");
  QTest::newRow ("normal-exit") << false;
  QTest::newRow ("process-death") << true;
}

void TestDecoderIpcHandshake::orphanLockLifetime ()
{
#ifdef Q_OS_WIN
  QSKIP ("POSIX worker locks protect cleanup after mapping detach");
#else
  QFETCH (bool, killed);
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  auto const key = uniqueKey ();
  auto const nativeName = key.toStdString ();
  auto const descriptor = shm_open (nativeName.c_str (), O_CREAT | O_EXCL | O_RDWR, 0600);
  QVERIFY (descriptor >= 0);
  auto cleanup = qScopeGuard ([&] { NativeSharedMemory::remove (key.toStdString ()); });
  auto const resized = ftruncate (descriptor, sizeof (shared_dec_data_t));
  auto const closed = close (descriptor);
  QCOMPARE (resized, 0);
  QCOMPARE (closed, 0);
  NativeSharedMemory memory;
  QVERIFY (memory.attach (nativeName));
  auto& shared = *static_cast<shared_dec_data_t *> (memory.data ());
  DecoderIpc::initialize (shared);
  shared.payload.d2[0] = 41;

  DecoderProcess holder;
  holder.start (QCoreApplication::applicationFilePath (),
                {"--hold-worker-lock", lockPath (directory.path ()), key});
  QVERIFY (holder.waitForStarted (5000));
  QByteArray output;
  QVERIFY2 (holder.waitForOutput ("<WorkerLocked>", output), output.constData ());
  QCOMPARE (holder.write ("d", 1), qint64 {1});
  QVERIFY2 (holder.waitForOutput ("<MappingDetached>", output), output.constData ());

  DecoderIpc::Session session;
  QElapsedTimer timer;
  timer.start ();
  QCOMPARE (session.open (key, lockPath (directory.path ())), DecoderIpc::Status::Orphaned);
  QVERIFY (timer.elapsed () >= 2900);
  QVERIFY (!session.errorString ().isEmpty ());
  QCOMPARE (holder.state (), QProcess::Running);
  QCOMPARE (DecoderIpc::state (shared), int {DECODER_IPC_SHUTDOWN});
  NativeSharedMemory retained;
  QVERIFY (retained.attach (nativeName));
  auto const& stillOld = *static_cast<shared_dec_data_t const *> (retained.data ());
  QCOMPARE (stillOld.payload.d2[0], short {41});
  QCOMPARE (DecoderIpc::state (stillOld), int {DECODER_IPC_SHUTDOWN});
  QVERIFY (retained.detach ());

  if (killed) holder.kill ();
  else QCOMPARE (holder.write ("r", 1), qint64 {1});
  QVERIFY (holder.waitForFinished (5000));
  if (!killed) QCOMPARE (holder.exitCode (), 0);
  QCOMPARE (session.open (key, lockPath (directory.path ())), DecoderIpc::Status::Ok);
  NativeSharedMemory replacement;
  QVERIFY (replacement.attach (nativeName));
  auto const& fresh = *static_cast<shared_dec_data_t const *> (replacement.data ());
  QCOMPARE (fresh.payload.d2[0], short {0});
  QCOMPARE (DecoderIpc::state (fresh), int {DECODER_IPC_IDLE});
  QCOMPARE (shared.payload.d2[0], short {41});
#endif
}

void TestDecoderIpcHandshake::blockedWorkerPreservesFiles ()
{
#ifdef Q_OS_WIN
  QSKIP ("The worker lifetime lock is POSIX-only");
#else
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  auto const key = uniqueKey ();
  DecoderIpc::Session session;
  QCOMPARE (session.open (key, lockPath (directory.path ())), DecoderIpc::Status::Ok);
  DecoderWorkerLock holder {QFile::encodeName (lockPath (directory.path ())).constData ()};
  QVERIFY2 (holder.tryLock (), holder.errorString ().c_str ());
  QByteArray const sentinel {"existing decoder output\n"};
  for (auto const& name : {"timer.out", "jt9_wisdom.dat"})
    {
      QFile file {directory.filePath (name)};
      QVERIFY (file.open (QIODevice::WriteOnly));
      QCOMPARE (file.write (sentinel), qint64 {sentinel.size ()});
    }
  DecoderProcess child;
  child.launch (key, directory.path ());
  QVERIFY (child.waitForStarted (5000));
  QVERIFY (child.waitForFinished (5000));
  QCOMPARE (child.exitStatus (), QProcess::NormalExit);
  QCOMPARE (child.exitCode (), 1);
  auto const output = child.readAllStandardOutput ();
  QVERIFY2 (output.contains ("<DecoderError>"), output.constData ());
  QVERIFY (!output.contains ("<DecoderReady>"));
  for (auto const& name : {"timer.out", "jt9_wisdom.dat"})
    {
      QFile file {directory.filePath (name)};
      QVERIFY (file.open (QIODevice::ReadOnly));
      QCOMPARE (file.readAll (), sentinel);
    }
#endif
}

void TestDecoderIpcHandshake::insufficientLinuxSharedMemory ()
{
#ifdef Q_OS_LINUX
  if (qgetenv ("WSJTX_TEST_SMALL_SHM") != "1")
    QSKIP ("Run alone in a container with --shm-size=16m and WSJTX_TEST_SMALL_SHM=1");
  struct statvfs capacity {};
  QCOMPARE (statvfs ("/dev/shm", &capacity), 0);
  QVERIFY2 (capacity.f_blocks * capacity.f_frsize <= 16 * 1024 * 1024,
            "This test requires an isolated /dev/shm mount of at most 16 MiB");
  auto const name = uniqueKey ().toStdString ();
  NativeSharedMemory memory;
  QVERIFY (!memory.create (name, sizeof (shared_dec_data_t)));
  QVERIFY (!memory.isAttached ());
  auto const error = QString::fromStdString (memory.errorString ());
  QVERIFY2 (error.startsWith ("posix_fallocate:"), qPrintable (error));
  QVERIFY2 (error.endsWith (QString {"(%1)"}.arg (ENOSPC)), qPrintable (error));
  NativeSharedMemory leftover;
  QVERIFY (!leftover.attach (name));
  QVERIFY (leftover.notFound ());
#else
  QSKIP ("Linux allocation reservation regression");
#endif
}

int main (int argc, char ** argv)
{
  QCoreApplication application {argc, argv};
  auto const arguments = application.arguments ();
  if (arguments.size () == 4 && arguments[1] == "--hold-worker-lock")
    {
      DecoderWorkerLock lock {QFile::encodeName (arguments[2]).constData ()};
      if (!lock.tryLock ()) return 1;
      NativeSharedMemory memory;
      if (!memory.attach (arguments[3].toStdString ())) return 2;
      std::puts ("<WorkerLocked>");
      std::fflush (stdout);
      if (std::getchar () != 'd' || !memory.detach ()) return 3;
      std::puts ("<MappingDetached>");
      std::fflush (stdout);
      return std::getchar () == 'r' ? 0 : 4;
    }
  TestDecoderIpcHandshake test;
  return QTest::qExec (&test, argc, argv);
}
#include "test_decoder_ipc_handshake.moc"
