#include <QtTest>

#include "qmap/decode_click_coalescer.h"
#include "qmap/qmap_click_policy.h"

class TestDecodeClickCoalescer final : public QObject
{
  Q_OBJECT

private slots:
  void disarmsBeforeSingleClick();
  void coalescesUpdatedRecordByIdentity();
  void keepsDifferentRecordsSeparate();
  void protectsDoubleClickWhenDisarmIsOverwritten();
};

namespace
{
  struct DispatchEvent
  {
    QByteArray row;
    DecodeClickGesture gesture;
  };
}

void TestDecodeClickCoalescer::disarmsBeforeSingleClick()
{
  QList<DispatchEvent> dispatched;
  DecodeClickCoalescer coalescer {this, [&dispatched] (QByteArray const& row,
                                                        DecodeClickGesture gesture) {
    dispatched.append ({row, gesture});
  }, 20};

  coalescer.press ("K1ABC", "first");
  QCOMPARE (dispatched.size (), 1);
  QVERIFY (dispatched.first ().gesture == DecodeClickGesture::Press);
  QTRY_COMPARE_WITH_TIMEOUT (dispatched.size (), 2, 100);
  QCOMPARE (dispatched.last ().row, QByteArray {"first"});
  QVERIFY (dispatched.last ().gesture == DecodeClickGesture::SingleClick);
}

void TestDecodeClickCoalescer::coalescesUpdatedRecordByIdentity()
{
  QList<DispatchEvent> dispatched;
  DecodeClickCoalescer coalescer {this, [&dispatched] (QByteArray const& row,
                                                        DecodeClickGesture gesture) {
    dispatched.append ({row, gesture});
  }, 20};

  coalescer.press ("K1ABC", "old record");
  coalescer.doubleClick ("K1ABC", "new record");
  QCOMPARE (dispatched.size (), 2);
  QCOMPARE (dispatched.last ().row, QByteArray {"new record"});
  QVERIFY (dispatched.last ().gesture == DecodeClickGesture::DoubleClick);
  QTest::qWait (30);
  QCOMPARE (dispatched.size (), 2);
}

void TestDecodeClickCoalescer::keepsDifferentRecordsSeparate()
{
  QList<DispatchEvent> dispatched;
  DecodeClickCoalescer coalescer {this, [&dispatched] (QByteArray const& row,
                                                        DecodeClickGesture gesture) {
    dispatched.append ({row, gesture});
  }, 20};

  coalescer.press ("K1ABC", "first");
  coalescer.press ("K2XYZ", "second");
  QCOMPARE (dispatched.size (), 3);
  QCOMPARE (dispatched.at (1).row, QByteArray {"first"});
  QVERIFY (dispatched.at (1).gesture == DecodeClickGesture::SingleClick);
  QVERIFY (dispatched.last ().gesture == DecodeClickGesture::Press);
  QTRY_COMPARE_WITH_TIMEOUT (dispatched.size (), 4, 100);
  QCOMPARE (dispatched.last ().row, QByteArray {"second"});
  QVERIFY (dispatched.last ().gesture == DecodeClickGesture::SingleClick);
}

void TestDecodeClickCoalescer::protectsDoubleClickWhenDisarmIsOverwritten()
{
  auto const failedDoubleClick = qmapClickPolicy (true, false, true, true);
  QVERIFY (failedDoubleClick.disarmBeforeQsy);
  QVERIFY (!failedDoubleClick.enableAutoTx);
  QVERIFY (!failedDoubleClick.restartTransmission);

  auto const acceptedDoubleClick = qmapClickPolicy (true, true, false, true);
  QVERIFY (acceptedDoubleClick.enableAutoTx);
  QVERIFY (acceptedDoubleClick.restartTransmission);

  auto const failedSingleClick = qmapClickPolicy (false, false, true, false);
  QVERIFY (failedSingleClick.disableAutoTx);
}

QTEST_GUILESS_MAIN (TestDecodeClickCoalescer)

#include "test_decode_click_coalescer.moc"
