#include <QtTest>

#include <QCoreApplication>
#include <QBuffer>
#include <QFile>
#include <QProcess>
#include <QVector>

#include <cstdio>
#include <memory>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

#include "DecoderOutputFramer.hpp"

namespace
{
  char const fixtureOption[] = "--decoder-output-fixture";

  int runFixture (QStringList const& arguments)
  {
    if (arguments.size () < 3) return 2;

    auto const payload = QByteArray::fromBase64 (arguments[2].toLatin1 ());
#ifdef Q_OS_WIN
    if (-1 == _setmode (_fileno (stdout), _O_BINARY)) return 3;
#endif
    QFile output;
    if (!output.open (stdout, QIODevice::WriteOnly, QFileDevice::DontCloseHandle))
      {
        return 4;
      }
    if (output.write (payload) != payload.size () || !output.flush ()) return 5;
    return arguments.contains ("--hold") ? QCoreApplication::exec () : 0;
  }
}

class TestDecoderOutputFramer final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void rejectedRecordDoesNotStopDrain ();
  void staleFramesRemainAssociatedWithTheirGeneration ();
  void completeStateWaitsForStdoutDrain ();
  void recordTerminatorsArePreserved ();
  void malformedFramesAreReported ();
  void killedFrameCanBeReplaced ();
};

void TestDecoderOutputFramer::rejectedRecordDoesNotStopDrain ()
{
  QByteArray const output {
    "<DecodeStarted> gen=7\n"
    "record rejected by policy\n"
    "<DecodeFinished>   2  17   123456 gen=7\n"};
  DecoderOutputFramer framer;
  int started {0};
  int records {0};
  int finished {0};
  DecoderOutputFramer::EventHandler handler {
    [&] (DecoderOutputFramer::Event const& event) {
      if (DecoderOutputFramer::EventType::Started == event.type) ++started;
      if (DecoderOutputFramer::EventType::Record == event.type)
        {
          ++records;
          return;
        }
      if (DecoderOutputFramer::EventType::Finished == event.type) ++finished;
    }};

  QProcess process;
  connect (&process, &QProcess::readyReadStandardOutput, this, [&] {
      framer.drain (process, handler);
    });
  process.start (QCoreApplication::applicationFilePath (),
                 {fixtureOption, QString::fromLatin1 (output.toBase64 ())});
  QVERIFY (process.waitForStarted ());
  QVERIFY (process.waitForFinished ());
  framer.drain (process, handler);

  QCOMPARE (started, 1);
  QCOMPARE (records, 1);
  QCOMPARE (finished, 1);
  QCOMPARE (framer.currentGeneration (), qint32 {0});
}

void TestDecoderOutputFramer::staleFramesRemainAssociatedWithTheirGeneration ()
{
  QByteArray const output {
    "<DecodeStarted> gen=7\n"
    "stale record\n"
    "<DecodeFinished>   0   0        0 gen=7\n"
    "<DecodeStarted> gen=8\n"
    "current record\n"
    "<DecodeFinished>   0   1        0 gen=8\n"};
  DecoderOutputFramer framer;
  QVector<qint32> recordGenerations;
  QVector<QByteArray> acceptedRecords;
  QVector<qint32> finishedGenerations;
  qint32 const activeGeneration {8};
  DecoderOutputFramer::EventHandler handler {
    [&] (DecoderOutputFramer::Event const& event) {
      if (DecoderOutputFramer::EventType::Record == event.type)
        {
          recordGenerations.append (event.generation);
          if (activeGeneration == event.generation)
            {
              acceptedRecords.append (event.rawLine);
            }
        }
      else if (DecoderOutputFramer::EventType::Finished == event.type)
        {
          finishedGenerations.append (event.generation);
        }
    }};

  QProcess process;
  process.start (QCoreApplication::applicationFilePath (),
                 {fixtureOption, QString::fromLatin1 (output.toBase64 ())});
  QVERIFY (process.waitForStarted ());
  QVERIFY (process.waitForFinished ());
  framer.drain (process, handler);

  QCOMPARE (recordGenerations, QVector<qint32> ({7, 8}));
  QCOMPARE (acceptedRecords, QVector<QByteArray> ({"current record\n"}));
  QCOMPARE (finishedGenerations, QVector<qint32> ({7, 8}));
}

void TestDecoderOutputFramer::completeStateWaitsForStdoutDrain ()
{
  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  std::unique_ptr<dec_data_t> payload {new dec_data_t {}};
  DecoderIpc::initialize (*shared);
  QVERIFY (DecoderIpc::publish (*shared, *payload, false, 21));
  qint32 generation {0};
  QVERIFY (DecoderIpc::claim (*shared, generation));
  QVERIFY (DecoderIpc::finish (*shared, generation));

  QByteArray output {
    "<DecodeStarted> gen=21\n"
    "buffered record\n"
    "<DecodeFinished>   0   1        0 gen=21\n"};
  QBuffer buffer {&output};
  QVERIFY (buffer.open (QIODevice::ReadOnly));
  QCOMPARE (DecoderIpc::state (*shared), qint32 {DECODER_IPC_COMPLETE});

  DecoderOutputFramer framer;
  int records {0};
  bool consumed {false};
  framer.drain (buffer, [&] (DecoderOutputFramer::Event const& event) {
      if (DecoderOutputFramer::EventType::Record == event.type) ++records;
      if (DecoderOutputFramer::EventType::Finished == event.type)
        {
          consumed = DecoderIpc::consume (*shared, event.generation);
        }
    });

  QCOMPARE (records, 1);
  QVERIFY (consumed);
  QCOMPARE (DecoderIpc::state (*shared), qint32 {DECODER_IPC_IDLE});
}

void TestDecoderOutputFramer::recordTerminatorsArePreserved ()
{
  QByteArray const output {
    "<DecodeStarted> gen=9\r\n"
    "first record\r\n"
    "second record\n"
    "<DecodeFinished>   0   2        0 gen=9\r\n"};
  DecoderOutputFramer framer;
  QVector<QByteArray> records;

  QProcess process;
  process.start (QCoreApplication::applicationFilePath (),
                 {fixtureOption, QString::fromLatin1 (output.toBase64 ())});
  QVERIFY (process.waitForStarted ());
  QVERIFY (process.waitForFinished ());
  framer.drain (process, [&] (DecoderOutputFramer::Event const& event) {
      if (DecoderOutputFramer::EventType::Record == event.type)
        {
          records.append (event.rawLine);
        }
    });

  QCOMPARE (records, QVector<QByteArray> ({"first record\r\n",
                                           "second record\n"}));
}

void TestDecoderOutputFramer::malformedFramesAreReported ()
{
  QByteArray output {
    "unframed record\n"
    "<DecodeStarted> gen=0\n"
    "<DecodeStarted> gen=14\n"
    "<DecodeFinished>   0   0        0 gen=15\n"
    "<DecodeFinished>   0   0        0 gen=14\n"};
  QBuffer buffer {&output};
  QVERIFY (buffer.open (QIODevice::ReadOnly));

  DecoderOutputFramer framer;
  QVector<DecoderOutputFramer::EventType> eventTypes;
  framer.drain (buffer, [&] (DecoderOutputFramer::Event const& event) {
      eventTypes.append (event.type);
    });

  QCOMPARE (eventTypes, QVector<DecoderOutputFramer::EventType> ({
      DecoderOutputFramer::EventType::Malformed,
      DecoderOutputFramer::EventType::Malformed,
      DecoderOutputFramer::EventType::Started,
      DecoderOutputFramer::EventType::Malformed,
      DecoderOutputFramer::EventType::Finished}));
  QCOMPARE (framer.currentGeneration (), qint32 {0});
}

void TestDecoderOutputFramer::killedFrameCanBeReplaced ()
{
  DecoderOutputFramer framer;
  QVector<DecoderOutputFramer::Event> events;
  DecoderOutputFramer::EventHandler handler {
    [&] (DecoderOutputFramer::Event const& event) {events.append (event);}};

  QProcess abandoned;
  connect (&abandoned, &QProcess::readyReadStandardOutput, this, [&] {
      framer.drain (abandoned, handler);
    });
  QByteArray const abandonedOutput {
    "<DecodeStarted> gen=11\n"
    "abandoned record\n"};
  abandoned.start (QCoreApplication::applicationFilePath (),
                   {fixtureOption,
                    QString::fromLatin1 (abandonedOutput.toBase64 ()),
                    "--hold"});
  QVERIFY (abandoned.waitForStarted ());
  QVERIFY (abandoned.waitForReadyRead ());
  framer.drain (abandoned, handler);
  QCOMPARE (framer.currentGeneration (), qint32 {11});

  abandoned.kill ();
  QTRY_COMPARE_WITH_TIMEOUT (abandoned.state (), QProcess::NotRunning, 2000);
  framer.reset ();
  events.clear ();

  QByteArray const replacementOutput {
    "<DecodeStarted> gen=12\n"
    "replacement record\n"
    "<DecodeFinished>   0   1        0 gen=12\n"};
  QProcess replacement;
  connect (&replacement, &QProcess::readyReadStandardOutput, this, [&] {
      framer.drain (replacement, handler);
    });
  replacement.start (QCoreApplication::applicationFilePath (),
                     {fixtureOption,
                      QString::fromLatin1 (replacementOutput.toBase64 ())});
  QVERIFY (replacement.waitForStarted ());
  QVERIFY (replacement.waitForFinished ());
  framer.drain (replacement, handler);

  QCOMPARE (events.size (), 3);
  QCOMPARE (events[0].type, DecoderOutputFramer::EventType::Started);
  QCOMPARE (events[0].generation, qint32 {12});
  QCOMPARE (events[1].type, DecoderOutputFramer::EventType::Record);
  QCOMPARE (events[1].rawLine, QByteArray {"replacement record\n"});
  QCOMPARE (events[2].type, DecoderOutputFramer::EventType::Finished);
  QCOMPARE (events[2].generation, qint32 {12});
}

int main (int argc, char ** argv)
{
  QCoreApplication application {argc, argv};
  auto const arguments = application.arguments ();
  if (arguments.contains (fixtureOption)) return runFixture (arguments);

  TestDecoderOutputFramer test;
  return QTest::qExec (&test, argc, argv);
}

#include "test_decoder_output_framer.moc"
