#include <QtTest>

#include "qmap/qmap_ipc.h"

class TestQMapIpc final : public QObject
{
  Q_OBJECT

private slots:
  void leavesIncompleteBatchPending();
  void acknowledgesCompletedBatch_data();
  void acknowledgesCompletedBatch();
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

QTEST_GUILESS_MAIN (TestQMapIpc)

#include "test_qmap_ipc.moc"
