#include <QtTest>

#include "qmap/decode_label.h"
#include "qmap/qmap_decode_record.h"

namespace
{
  QByteArray makeRow (QByteArray const& message)
  {
    QByteArray row (static_cast<int> (QMapDecodeRowSize), ' ');
    row.replace (0, 6, "123430");
    row.replace (6, 9, "  123.456");
    row.replace (15, 7, "  120.0");
    row.replace (22, 7, "   0.12");
    row.replace (29, 5, "  -15");
    row.replace (36, 3, "30A");
    row.replace (41, message.size (), message);
    return row;
  }
}

class TestQMapDecodeRecord final : public QObject
{
  Q_OBJECT

private slots:
  void parsesFixedWidthRecord();
  void rejectsMalformedRecord();
  void replacesLabelWithLatestRecord();
  void prunesExpiredLabelsAcrossMidnight();
};

void TestQMapDecodeRecord::parsesFixedWidthRecord()
{
  auto const row = makeRow ("CQ POTA W3SZ/2 FN20");
  auto const record = parseQMapDecodeRecord (row);

  QVERIFY (record);
  QCOMPARE (record->raw, row);
  QCOMPARE (record->time, QString {"123430"});
  QCOMPARE (record->callsign, QString {"W3SZ/2"});
  QCOMPARE (record->grid, QString {"FN20"});
  QCOMPARE (record->submode, QString {"30A"});
  QCOMPARE (record->receiveFrequencyKHz, 123.456);
  QCOMPARE (record->scheduledFrequencyKHz, 120.0);
  QCOMPARE (record->snr, -15);
  QCOMPARE (record->secondsSinceMidnight, 12 * 3600 + 34 * 60 + 30);
  QVERIFY (record->secondHalf);
  QVERIFY (record->cq);
}

void TestQMapDecodeRecord::rejectsMalformedRecord()
{
  QVERIFY (!parseQMapDecodeRecord (QByteArray {"short"}));

  auto row = makeRow ("CQ K1ABC FN42");
  row.replace (0, 6, "256099");
  QVERIFY (!parseQMapDecodeRecord (row));
}

void TestQMapDecodeRecord::replacesLabelWithLatestRecord()
{
  auto firstRow = makeRow ("CQ K1ABC FN42");
  auto secondRow = firstRow;
  secondRow.replace (6, 9, "  222.333");
  secondRow.replace (29, 5, "  -07");
  auto const first = parseQMapDecodeRecord (firstRow);
  auto const second = parseQMapDecodeRecord (secondRow);
  QVERIFY (first);
  QVERIFY (second);

  QList<QMapDecodeLabel> labels;
  upsertQMapDecodeLabel (labels, *first);
  upsertQMapDecodeLabel (labels, *second);

  QCOMPARE (labels.size (), 1);
  QCOMPARE (labels.first ().raw, secondRow);
  QCOMPARE (labels.first ().receiveFrequencyKHz, 222.333);
  QCOMPARE (labels.first ().snr, -7);
}

void TestQMapDecodeRecord::prunesExpiredLabelsAcrossMidnight()
{
  auto record = *parseQMapDecodeRecord (makeRow ("CQ K1ABC FN42"));
  record.secondsSinceMidnight = 23 * 3600 + 59 * 60;
  QList<QMapDecodeLabel> labels {record};

  pruneQMapDecodeLabels (labels, 60);
  QCOMPARE (labels.size (), 1);

  pruneQMapDecodeLabels (labels, 3 * 60 + 1);
  QVERIFY (labels.isEmpty ());
}

QTEST_GUILESS_MAIN (TestQMapDecodeRecord)

#include "test_qmap_decode_record.moc"
