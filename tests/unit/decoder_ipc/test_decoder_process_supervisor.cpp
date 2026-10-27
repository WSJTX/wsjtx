// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

#include "DecoderProcessSupervisor.hpp"

namespace
{
  using Kind = DecoderBackend::Failure::Kind;

  volatile std::sig_atomic_t termSeen {0};

  extern "C" void noteTerm (int)
  {
    termSeen = 1;
  }

  void touch (QString const& path)
  {
    QFile file {path};
    file.open (QIODevice::WriteOnly);
  }

  // The test executable doubles as the decoder: --stub <behaviour> <directory>.
  // Every behaviour except "exit" ends when <directory>/stop appears, or after a minute.
  int runStub (QByteArray const& behaviour, QString const& directory)
  {
#ifdef Q_OS_WIN
    // Text mode would write each \n as \r\n.
    _setmode (_fileno (stdout), _O_BINARY);
    _setmode (_fileno (stderr), _O_BINARY);
#else
    if ("stubborn" == behaviour) std::signal (SIGTERM, noteTerm);
#endif
    if ("exit" == behaviour)
      {
        std::fputs ("fatal: no handshake\n", stderr);
        return 7;
      }
    if ("silent" != behaviour)
      {
        std::fputs ("ready\n", stdout);
        std::fflush (stdout);
      }
    for (int tick = 0; tick < 600; ++tick)
      {
        if (termSeen) touch (directory + "/term");
        if (QFile::exists (directory + "/stop"))
          {
            if ("crash" != behaviour) return 0;
            std::fputs ("fatal: lost the decoder\n", stderr);
            return 3;
          }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
      }
    return 0;
  }

  class FakeTransport final
    : public DecoderProcessSupervisor::Transport
  {
  public:
    DecoderProcessSupervisor::Command command () override
    {
      ++commands;
      return {program, arguments, QProcessEnvironment::systemEnvironment ()};
    }
    void prepareStart () override {++starts;}
    void requestStop () override
    {
      ++stops;
      if (stopCooperates) touch (stopFile);
    }
    bool prepareReplacement () override
    {
      ++replacements;
      return replacementAvailable;
    }
    void startupFailed () override {++startupFailures;}

    QString program;
    QStringList arguments;
    QString stopFile;
    bool stopCooperates {true};
    bool replacementAvailable {true};
    int commands {0};
    int starts {0};
    int stops {0};
    int replacements {0};
    int startupFailures {0};
  };

  class Harness
  {
  public:
    Harness (QByteArray const& behaviour, QString const& program = {})
      : supervisor {transport, [this] (QByteArray const& line) {diagnosticLines << line;}}
      , launched {&supervisor, &DecoderProcessSupervisor::launched}
      , ready {&supervisor, &DecoderProcessSupervisor::becameReady}
      , restarts {&supervisor, &DecoderProcessSupervisor::restartInitiated}
      , failures {&supervisor, &DecoderProcessSupervisor::failed}
    {
      transport.program = program.isEmpty () ? QCoreApplication::applicationFilePath () : program;
      transport.arguments = QStringList {"--stub", QString::fromLatin1 (behaviour), directory.path ()};
      transport.stopFile = directory.filePath ("stop");
      QObject::connect (&supervisor, &DecoderProcessSupervisor::outputAvailable, [this] {
          events << QByteArrayLiteral ("output");
          auto& channel = supervisor.channel ();
          while (channel.canReadLine ())
            {
              if ("ready" == channel.readLine ().trimmed ()) supervisor.markReady ();
            }
        });
      QObject::connect (&supervisor, &DecoderProcessSupervisor::failed, [this] {
          events << QByteArrayLiteral ("failed");
        });
    }

    ~Harness ()
    {
      supervisor.prepareForClose ();
      touch (transport.stopFile);
      supervisor.shutdownAtClose ();
    }

    DecoderBackend::Failure failure (int index = 0) const
    {
      return failures.at (index).at (0).value<DecoderBackend::Failure> ();
    }

    QTemporaryDir directory;
    FakeTransport transport;
    QList<QByteArray> diagnosticLines;
    QList<QByteArray> events;
    DecoderProcessSupervisor supervisor;
    QSignalSpy launched;
    QSignalSpy ready;
    QSignalSpy restarts;
    QSignalSpy failures;
  };
}

class TestDecoderProcessSupervisor final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase ();
  void handshakeMakesTheDecoderReady ();
  void missingHandshakeFailsAtTheFiveSecondDeadline ();
  void launchFailureFailsStartup ();
  void exitBeforeHandshakeDrainsOutputThenFailsStartup ();
  void unexpectedExitReportsCodeAndStderrTail ();
  void restartTerminatesThenKillsThenRelaunches ();
  void restartOfAnExitedDecoderRelaunchesAtOnce ();
  void restartWithoutReplacementStateFails ();
  void closeStopsACooperativeDecoderWithoutEscalation ();
  void closeTerminatesThenKillsAStubbornDecoder ();
  void closingSilencesProcessEvents ();
};

void TestDecoderProcessSupervisor::initTestCase ()
{
  qRegisterMetaType<DecoderBackend::Failure> ();
}

void TestDecoderProcessSupervisor::handshakeMakesTheDecoderReady ()
{
  Harness harness {"ready"};
  QVERIFY (harness.directory.isValid ());
  harness.supervisor.start ();
  QCOMPARE (harness.launched.count (), 1);
  QCOMPARE (harness.transport.starts, 1);
  QVERIFY (harness.supervisor.starting ());

  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 1, 20000);
  QVERIFY (harness.supervisor.ready ());
  QVERIFY (harness.supervisor.running ());
  QVERIFY (!harness.supervisor.starting ());
  QCOMPARE (harness.failures.count (), 0);
  QCOMPARE (harness.transport.commands, 1);
}

void TestDecoderProcessSupervisor::missingHandshakeFailsAtTheFiveSecondDeadline ()
{
  Harness harness {"silent"};
  QElapsedTimer clock;
  clock.start ();
  harness.supervisor.start ();

  QTRY_COMPARE_WITH_TIMEOUT (harness.failures.count (), 1, 20000);
  // Coarse timers may fire up to 5% early.
  QVERIFY2 (clock.elapsed () >= 4750 && clock.elapsed () < 7000,
            qPrintable (QStringLiteral ("failed after %1 ms").arg (clock.elapsed ())));
  QCOMPARE (harness.failure ().kind, Kind::StartTimeout);
  QCOMPARE (harness.supervisor.phase (), DecoderProcessSupervisor::Phase::Failed);
  QCOMPARE (harness.transport.startupFailures, 1);
  QCOMPARE (harness.ready.count (), 0);
}

void TestDecoderProcessSupervisor::launchFailureFailsStartup ()
{
  QTemporaryDir missing;
  Harness harness {"ready", missing.filePath ("no-such-decoder")};
  harness.supervisor.start ();

  QTRY_COMPARE_WITH_TIMEOUT (harness.failures.count (), 1, 20000);
  auto const failure = harness.failure ();
  QCOMPARE (failure.kind, Kind::StartError);
  QVERIFY (!failure.reason.isEmpty ());
  QCOMPARE (failure.program, missing.filePath ("no-such-decoder"));
  QCOMPARE (harness.supervisor.phase (), DecoderProcessSupervisor::Phase::Failed);
  QCOMPARE (harness.transport.startupFailures, 1);
}

void TestDecoderProcessSupervisor::exitBeforeHandshakeDrainsOutputThenFailsStartup ()
{
  Harness harness {"exit"};
  harness.supervisor.start ();

  QTRY_COMPARE_WITH_TIMEOUT (harness.failures.count (), 1, 20000);
  auto const failure = harness.failure ();
  QCOMPARE (failure.kind, Kind::StartExit);
  QCOMPARE (failure.code, 7);
  QVERIFY (harness.events.size () >= 2);
  QCOMPARE (harness.events.last (), QByteArrayLiteral ("failed"));
  QCOMPARE (harness.events.at (harness.events.size () - 2), QByteArrayLiteral ("output"));
  QVERIFY (harness.supervisor.failureDetail ("reason").endsWith ("fatal: no handshake\n"));
}

void TestDecoderProcessSupervisor::unexpectedExitReportsCodeAndStderrTail ()
{
  Harness harness {"crash"};
  harness.supervisor.start ();
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 1, 20000);

  touch (harness.transport.stopFile);
  QTRY_COMPARE_WITH_TIMEOUT (harness.failures.count (), 1, 20000);
  auto const failure = harness.failure ();
  QCOMPARE (failure.kind, Kind::Exit);
  QCOMPARE (failure.code, 3);
  QVERIFY (!failure.crashed);
  QCOMPARE (failure.program, harness.transport.program);
  QCOMPARE (failure.arguments, harness.transport.arguments);
  QVERIFY (harness.supervisor.diagnosticsTail ().endsWith ("fatal: lost the decoder\n"));
  auto const detail = harness.supervisor.failureDetail ("The decoder stopped.");
  QVERIFY2 (detail.startsWith ("The decoder stopped.\n\n")
            && detail.endsWith ("fatal: lost the decoder\n"), qPrintable (detail));
  QVERIFY (harness.diagnosticLines.contains ("fatal: lost the decoder"));
  QCOMPARE (harness.transport.replacements, 0);
  QCOMPARE (harness.launched.count (), 1);
}

void TestDecoderProcessSupervisor::restartTerminatesThenKillsThenRelaunches ()
{
#ifdef Q_OS_WIN
  QSKIP ("the stub decoder relies on POSIX signal semantics");
#else
  Harness harness {"stubborn"};
  harness.transport.stopCooperates = false;
  harness.supervisor.start ();
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 1, 20000);

  QElapsedTimer clock;
  clock.start ();
  harness.supervisor.restart ();
  QCOMPARE (harness.restarts.count (), 1);
  QCOMPARE (harness.transport.stops, 1);
  QCOMPARE (harness.supervisor.phase (), DecoderProcessSupervisor::Phase::StopRequested);
  QVERIFY (harness.supervisor.restartInProgress ());

  // The stub ignores SIGTERM, so only the kill a second after the terminate ends it.
  QTRY_VERIFY_WITH_TIMEOUT (QFile::exists (harness.directory.filePath ("term")), 20000);
  auto const terminated = clock.elapsed ();
  QTRY_COMPARE_WITH_TIMEOUT (harness.launched.count (), 2, 20000);
  QVERIFY2 (terminated < 1000,
            qPrintable (QStringLiteral ("terminated after %1 ms").arg (terminated)));
  QVERIFY2 (clock.elapsed () >= 1150 && clock.elapsed () < 2500,
            qPrintable (QStringLiteral ("relaunched after %1 ms").arg (clock.elapsed ())));
  QCOMPARE (harness.transport.replacements, 1);
  QCOMPARE (harness.transport.starts, 2);
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 2, 20000);
  QCOMPARE (harness.failures.count (), 0);
  QCOMPARE (harness.restarts.count (), 1);
#endif
}

void TestDecoderProcessSupervisor::restartOfAnExitedDecoderRelaunchesAtOnce ()
{
  Harness harness {"ready"};
  harness.supervisor.start ();
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 1, 20000);
  touch (harness.transport.stopFile);
  QTRY_VERIFY_WITH_TIMEOUT (!harness.supervisor.running (), 20000);
  QFile::remove (harness.transport.stopFile);
  harness.transport.stopCooperates = false;

  harness.supervisor.restart ();
  QCOMPARE (harness.restarts.count (), 1);
  QCOMPARE (harness.transport.replacements, 1);
  QCOMPARE (harness.launched.count (), 2);
  QCOMPARE (harness.supervisor.phase (), DecoderProcessSupervisor::Phase::ReplacementStarting);
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 2, 20000);
}

void TestDecoderProcessSupervisor::restartWithoutReplacementStateFails ()
{
  Harness harness {"ready"};
  harness.supervisor.start ();
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 1, 20000);
  touch (harness.transport.stopFile);
  QTRY_VERIFY_WITH_TIMEOUT (!harness.supervisor.running (), 20000);
  auto const exits = harness.failures.count ();

  harness.transport.replacementAvailable = false;
  harness.supervisor.restart ();
  QCOMPARE (harness.failures.count (), exits + 1);
  QCOMPARE (harness.failure (exits).kind, Kind::StateUnavailable);
  QCOMPARE (harness.launched.count (), 1);
}

void TestDecoderProcessSupervisor::closeStopsACooperativeDecoderWithoutEscalation ()
{
  Harness harness {"ready"};
  harness.supervisor.start ();
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 1, 20000);

  QElapsedTimer clock;
  clock.start ();
  harness.supervisor.prepareForClose ();
  harness.supervisor.shutdownAtClose ();
  QVERIFY2 (clock.elapsed () < 4000,
            qPrintable (QStringLiteral ("close took %1 ms").arg (clock.elapsed ())));
  QVERIFY (!harness.supervisor.running ());
  QCOMPARE (harness.transport.stops, 1);
  QCOMPARE (harness.failures.count (), 0);
}

void TestDecoderProcessSupervisor::closeTerminatesThenKillsAStubbornDecoder ()
{
#ifdef Q_OS_WIN
  QSKIP ("the stub decoder relies on POSIX signal semantics");
#else
  Harness harness {"stubborn"};
  harness.transport.stopCooperates = false;
  harness.supervisor.start ();
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 1, 20000);

  QElapsedTimer clock;
  clock.start ();
  harness.supervisor.prepareForClose ();
  harness.supervisor.shutdownAtClose ();
  QVERIFY2 (clock.elapsed () >= 5900 && clock.elapsed () < 8000,
            qPrintable (QStringLiteral ("close took %1 ms").arg (clock.elapsed ())));
  QVERIFY (!harness.supervisor.running ());
  QCOMPARE (harness.transport.stops, 1);
  QTRY_VERIFY_WITH_TIMEOUT (QFile::exists (harness.directory.filePath ("term")), 5000);
  QCOMPARE (harness.failures.count (), 0);
#endif
}

void TestDecoderProcessSupervisor::closingSilencesProcessEvents ()
{
  Harness harness {"crash"};
  harness.supervisor.start ();
  QTRY_COMPARE_WITH_TIMEOUT (harness.ready.count (), 1, 20000);

  harness.supervisor.prepareForClose ();
  touch (harness.transport.stopFile);
  QTRY_VERIFY_WITH_TIMEOUT (!harness.supervisor.running (), 20000);
  QTest::qWait (100);
  QCOMPARE (harness.failures.count (), 0);
  QCOMPARE (harness.launched.count (), 1);
}

int main (int argc, char ** argv)
{
  if (argc == 4 && QByteArray {argv[1]} == "--stub")
    return runStub (argv[2], QString::fromLocal8Bit (argv[3]));
  QCoreApplication application {argc, argv};
  TestDecoderProcessSupervisor test;
  return QTest::qExec (&test, argc, argv);
}

#include "test_decoder_process_supervisor.moc"
