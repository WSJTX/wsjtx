#include <QtTest>

#include <cstring>

#include "qmap/qmap_ipc.h"

class TestQMapIpc final : public QObject
{
  Q_OBJECT

private slots:
  void leavesIncompleteBatchPending();
  void acknowledgesCompletedBatch_data();
  void acknowledgesCompletedBatch();
  void decoderPublicationPreservesRequests();
};

void TestQMapIpc::leavesIncompleteBatchPending()
{
  QMapDecodeBlock shared {};
  shared.ndecodes = 3;

  QVERIFY (!acknowledgeQMapDecodeBatch (shared));
  QCOMPARE (shared.ndecodes, 3);
}

void TestQMapIpc::acknowledgesCompletedBatch_data()
{
  QTest::addColumn<int> ("completion");
  QTest::newRow ("live") << 1;
  QTest::newRow ("disk") << 2;
}

void TestQMapIpc::acknowledgesCompletedBatch()
{
  QFETCH (int, completion);
  QMapDecodeBlock shared {};
  shared.ndecodes = 3;
  shared.nQDecoderDone = completion;

  QVERIFY (acknowledgeQMapDecodeBatch (shared));
  QCOMPARE (shared.ndecodes, 0);
  QCOMPARE (shared.nQDecoderDone, 0);
}

void TestQMapIpc::decoderPublicationPreservesRequests()
{
  QMapSharedMemory shared {};
  shared.decodes.nWDecoderBusy = 1;
  shared.decodes.nWTransmitting = 60;
  shared.decodes.kHzRequested = 144;
  shared.click.action = QMapClickAction::Select;
  std::memset (shared.click.selectedDecode, 'x', sizeof shared.click.selectedDecode);
  QMapDecodeBlock publication {};
  publication.ndecodes = 2;

  publishQMapDecodeBlock (shared, publication);

  QCOMPARE (shared.decodes.ndecodes, 2);
  QCOMPARE (shared.decodes.nWDecoderBusy, 1);
  QCOMPARE (shared.decodes.nWTransmitting, 60);
  QCOMPARE (shared.decodes.kHzRequested, 144);
  QCOMPARE (shared.click.action, QMapClickAction::Select);
  QByteArray const selectedDecode {shared.click.selectedDecode,
                                   static_cast<int> (sizeof shared.click.selectedDecode)};
  QCOMPARE (selectedDecode, QByteArray (static_cast<int> (QMapDecodeRowSize), 'x'));
}

QTEST_GUILESS_MAIN (TestQMapIpc)

#include "test_qmap_ipc.moc"
