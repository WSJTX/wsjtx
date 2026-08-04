#include <QtTest>

#include <cstring>
#include <limits>
#include <memory>

#include "DecoderIpc.hpp"

extern "C"
{
  void decoder_ipc_fortran_probe (void *, int[10]);
}

class TestDecoderIpcProtocol final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void layoutMatchesFortran ();
  void sharedMemorySizeAllowsPlatformRounding ();
  void publicationAndClaimValidateControl ();
  void transitionsRequireMatchingGeneration ();
  void delayedCompletionCannotClobberNextRequest ();
  void compatibleShutdownReleasesWorkers ();
  void shutdownReplacesAnyState_data ();
  void shutdownReplacesAnyState ();
  void generationWrapsWithoutUsingZero ();
  void startMarkerParsing ();
  void startMarkerRejections_data ();
  void startMarkerRejections ();
  void completionMarkerParsing ();
  void completionMarkerRejections_data ();
  void completionMarkerRejections ();
};

void TestDecoderIpcProtocol::layoutMatchesFortran ()
{
  struct LegacySharedData
  {
    int ipc[3];
    dec_data_t payload;
  };

  QCOMPARE (sizeof (decoder_ipc_control_t), size_t {12});
  QCOMPARE (offsetof (decoder_ipc_control_t, state), size_t {4});
  QCOMPARE (offsetof (shared_dec_data_t, payload), size_t {12});
  QCOMPARE (sizeof (shared_dec_data_t),
            sizeof (decoder_ipc_control_t) + sizeof (dec_data_t));
  QCOMPARE (sizeof (shared_dec_data_t), sizeof (LegacySharedData));
  QCOMPARE (offsetof (LegacySharedData, ipc) + sizeof (int),
            offsetof (decoder_ipc_control_t, state));
  QCOMPARE (offsetof (LegacySharedData, payload),
            offsetof (shared_dec_data_t, payload));

  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  shared->control.generation = 41;
  int values[10] {};
  decoder_ipc_fortran_probe (shared.get (), values);

  QCOMPARE (values[0], static_cast<int> (sizeof (decoder_ipc_control_t)));
  QCOMPARE (values[1], static_cast<int> (sizeof (decoder_params_t)));
  QCOMPARE (values[2], static_cast<int> (sizeof (dec_data_t)));
  QCOMPARE (values[3], static_cast<int> (sizeof (shared_dec_data_t)));
  QCOMPARE (values[4], int {DECODER_IPC_VERSION});
  QCOMPARE (values[5], int {DECODER_IPC_IDLE});
  QCOMPARE (values[6], int {DECODER_IPC_READY});
  QCOMPARE (values[7], int {DECODER_IPC_DECODING});
  QCOMPARE (values[8], int {DECODER_IPC_COMPLETE});
  QCOMPARE (values[9], int {DECODER_IPC_SHUTDOWN});
  QCOMPARE (shared->control.generation, 42);
  QCOMPARE (shared->payload.d2[0], short {1234});
}

void TestDecoderIpcProtocol::sharedMemorySizeAllowsPlatformRounding ()
{
  auto const required = static_cast<qint64> (sizeof (shared_dec_data_t));
  QVERIFY (!DecoderIpc::hasUsableSize (required - 1));
  QVERIFY (DecoderIpc::hasUsableSize (required));
  QVERIFY (DecoderIpc::hasUsableSize (required + 80));
}

void TestDecoderIpcProtocol::publicationAndClaimValidateControl ()
{
  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  std::unique_ptr<dec_data_t> payload {new dec_data_t {}};
  DecoderIpc::initialize (*shared);

  QVERIFY (!DecoderIpc::publish (*shared, *payload, false, 0));
  QCOMPARE (shared->control.state, int {DECODER_IPC_IDLE});

  shared->control.version = DECODER_IPC_VERSION + 1;
  QVERIFY (!DecoderIpc::publish (*shared, *payload, false, 1));
  QCOMPARE (shared->control.state, int {DECODER_IPC_IDLE});

  shared->control.version = DECODER_IPC_VERSION;
  shared->control.generation = 0;
  shared->control.state = DECODER_IPC_READY;
  qint32 generation {0};
  QVERIFY (!DecoderIpc::claim (*shared, generation));
  QCOMPARE (shared->control.state, int {DECODER_IPC_READY});
}

void TestDecoderIpcProtocol::transitionsRequireMatchingGeneration ()
{
  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  std::unique_ptr<dec_data_t> payload {new dec_data_t {}};
  DecoderIpc::initialize (*shared);
  payload->params.nutc = 123456;
  payload->d2[0] = 17;

  QVERIFY (DecoderIpc::publish (*shared, *payload, true, 7));
  QCOMPARE (shared->control.state, int {DECODER_IPC_READY});
  QCOMPARE (shared->control.generation, 7);
  QCOMPARE (shared->payload.params.nutc, 123456);
  QCOMPARE (shared->payload.d2[0], short {17});

  payload->params.nutc = 654321;
  payload->d2[0] = 99;
  QVERIFY (!DecoderIpc::publish (*shared, *payload, true, 8));
  QCOMPARE (shared->payload.params.nutc, 123456);
  QCOMPARE (shared->payload.d2[0], short {17});

  qint32 claimed {0};
  QVERIFY (DecoderIpc::claim (*shared, claimed));
  QCOMPARE (claimed, qint32 {7});
  QVERIFY (!DecoderIpc::publish (*shared, *payload, true, 8));
  QCOMPARE (shared->payload.params.nutc, 123456);
  QCOMPARE (shared->payload.d2[0], short {17});
  QVERIFY (!DecoderIpc::consume (*shared, 6));
  QCOMPARE (shared->control.state, int {DECODER_IPC_DECODING});
  QVERIFY (!DecoderIpc::consume (*shared, 7));
  QCOMPARE (shared->control.state, int {DECODER_IPC_DECODING});
  QVERIFY (DecoderIpc::finish (*shared, 7));
  QVERIFY (DecoderIpc::consume (*shared, 7));
  QCOMPARE (shared->control.state, int {DECODER_IPC_IDLE});
  QVERIFY (!DecoderIpc::consume (*shared, 7));

  QVERIFY (DecoderIpc::publish (*shared, *payload, false, 8));
  QCOMPARE (shared->payload.params.nutc, 654321);
  QCOMPARE (shared->control.generation, 8);
  QCOMPARE (shared->payload.d2[0], short {17});
  QVERIFY (!DecoderIpc::consume (*shared, 8));
  DecoderIpc::shutdown (*shared);
}

void TestDecoderIpcProtocol::delayedCompletionCannotClobberNextRequest ()
{
  std::unique_ptr<shared_dec_data_t> shared {new shared_dec_data_t};
  std::unique_ptr<dec_data_t> payload {new dec_data_t};
  DecoderIpc::initialize (*shared);
  payload->params = {};

  QVERIFY (DecoderIpc::publish (*shared, *payload, false, 10));
  qint32 claimed {0};
  QVERIFY (DecoderIpc::claim (*shared, claimed));
  QVERIFY (DecoderIpc::finish (*shared, 10));
  QVERIFY (DecoderIpc::consume (*shared, 10));
  QVERIFY (DecoderIpc::publish (*shared, *payload, false, 11));

  QVERIFY (!DecoderIpc::finish (*shared, 10));
  QCOMPARE (shared->control.generation, 11);
  QCOMPARE (shared->control.state, int {DECODER_IPC_READY});

  DecoderIpc::shutdown (*shared);
  QCOMPARE (shared->control.state, int {DECODER_IPC_SHUTDOWN});
}

void TestDecoderIpcProtocol::compatibleShutdownReleasesWorkers ()
{
  decoder_ipc_control_t control {17, 0, 0};
  DecoderIpc::shutdown (control);

  QCOMPARE (control.state, int {DECODER_IPC_SHUTDOWN});
  QCOMPARE (control.version, 1);
}

void TestDecoderIpcProtocol::shutdownReplacesAnyState_data ()
{
  QTest::addColumn<int> ("state");
  QTest::newRow ("idle") << int {DECODER_IPC_IDLE};
  QTest::newRow ("ready") << int {DECODER_IPC_READY};
  QTest::newRow ("decoding") << int {DECODER_IPC_DECODING};
  QTest::newRow ("complete") << int {DECODER_IPC_COMPLETE};
}

void TestDecoderIpcProtocol::shutdownReplacesAnyState ()
{
  QFETCH (int, state);
  decoder_ipc_control_t control {17, state, DECODER_IPC_VERSION};

  DecoderIpc::shutdown (control);

  QCOMPARE (control.generation, 17);
  QCOMPARE (control.state, int {DECODER_IPC_SHUTDOWN});
  QCOMPARE (control.version, int {DECODER_IPC_VERSION});
}

void TestDecoderIpcProtocol::generationWrapsWithoutUsingZero ()
{
  QCOMPARE (DecoderIpc::nextGeneration (0), qint32 {1});
  QCOMPARE (DecoderIpc::nextGeneration (-1), qint32 {1});
  QCOMPARE (DecoderIpc::nextGeneration (41), qint32 {42});
  QCOMPARE (DecoderIpc::nextGeneration (std::numeric_limits<qint32>::max ()), qint32 {1});
}

void TestDecoderIpcProtocol::startMarkerParsing ()
{
  qint32 generation {0};
  QVERIFY (DecoderIpc::parseStart ("<DecodeStarted> gen=42\r\n", &generation));
  QCOMPARE (generation, qint32 {42});
}

void TestDecoderIpcProtocol::startMarkerRejections_data ()
{
  QTest::addColumn<QByteArray> ("line");
  QTest::newRow ("embedded") << QByteArray {"noise <DecodeStarted> gen=42"};
  QTest::newRow ("missing generation") << QByteArray {"<DecodeStarted>"};
  QTest::newRow ("zero generation") << QByteArray {"<DecodeStarted> gen=0"};
  QTest::newRow ("negative generation") << QByteArray {"<DecodeStarted> gen=-1"};
  QTest::newRow ("overflow generation") << QByteArray {"<DecodeStarted> gen=2147483648"};
  QTest::newRow ("trailing garbage") << QByteArray {"<DecodeStarted> gen=42 extra"};
}

void TestDecoderIpcProtocol::startMarkerRejections ()
{
  QFETCH (QByteArray, line);
  qint32 generation {0};
  QVERIFY (!DecoderIpc::parseStart (line, &generation));
}

void TestDecoderIpcProtocol::completionMarkerParsing ()
{
  DecoderIpc::Completion completion {};
  QVERIFY (DecoderIpc::parseCompletion (
      "<DecodeFinished>   2  17   123456 gen=42\r\n", &completion));
  QCOMPARE (completion.synchronized, qint32 {2});
  QCOMPARE (completion.decoded, qint32 {17});
  QCOMPARE (completion.average, qint32 {123456});
  QCOMPARE (completion.generation, qint32 {42});
}

void TestDecoderIpcProtocol::completionMarkerRejections_data ()
{
  QTest::addColumn<QByteArray> ("line");
  QTest::newRow ("embedded") << QByteArray {"noise <DecodeFinished>   2  17   123456 gen=42"};
  QTest::newRow ("legacy") << QByteArray {"<DecodeFinished>   2  17   123456"};
  QTest::newRow ("zero generation") << QByteArray {"<DecodeFinished>   2  17   123456 gen=0"};
  QTest::newRow ("negative generation") << QByteArray {"<DecodeFinished>   2  17   123456 gen=-1"};
  QTest::newRow ("overflow generation") << QByteArray {"<DecodeFinished>   2  17   123456 gen=2147483648"};
  QTest::newRow ("overflow field") << QByteArray {"<DecodeFinished>****  17   123456 gen=42"};
  QTest::newRow ("trailing garbage") << QByteArray {"<DecodeFinished>   2  17   123456 gen=42 extra"};
}

void TestDecoderIpcProtocol::completionMarkerRejections ()
{
  QFETCH (QByteArray, line);
  DecoderIpc::Completion completion {};
  QVERIFY (!DecoderIpc::parseCompletion (line, &completion));
}

QTEST_GUILESS_MAIN (TestDecoderIpcProtocol)

#include "test_decoder_ipc_protocol.moc"
