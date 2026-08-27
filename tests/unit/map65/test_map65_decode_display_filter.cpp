#include "map65_decode_display_filter.h"

#include <QtTest>

namespace
{
QString jt65Line(int frequencyHz, int utc, QString const& message)
{
  QByteArray const text = message.leftJustified(22, ' ').toLatin1();
  return QString::asprintf("%3d%5d%4d%6.4d%5.1f%5d # %-22s",
                           frequencyHz / 1000, frequencyHz % 1000, 0, utc,
                           0.0, -20, text.constData());
}

QString q65Line(int frequencyHz, int utc, QString const& message)
{
  QByteArray const text = message.leftJustified(28, ' ').toLatin1();
  return QString::asprintf("%3d%5d%4d%6.4d%5.1f%5d : %-28s",
                           frequencyHz / 1000, frequencyHz % 1000, 0, utc,
                           0.0, -20, text.constData());
}
}

class TestMap65DecodeDisplayFilter final : public QObject
{
  Q_OBJECT

private slots:
  void suppressesOnlyMatchingFinalDecode()
  {
    Map65DecodeDisplayFilter filter;
    QVERIFY(filter.handleControlLine("<Map65DecodePass> early\n"));
    QVERIFY(filter.shouldDisplay(jt65Line(125000, 1234, "CQ K1ABC FN42")));
    filter.completeEarlyPass();

    QVERIFY(filter.handleControlLine("<Map65DecodePass> final\n"));
    QVERIFY(!filter.shouldDisplay(jt65Line(125020, 1234, "CQ K1ABC FN42")));
    QVERIFY(filter.shouldDisplay(jt65Line(125021, 1234, "CQ K1ABC FN42")));
    QVERIFY(filter.shouldDisplay(jt65Line(125000, 1235, "CQ K1ABC FN42")));
    QVERIFY(filter.shouldDisplay(jt65Line(125000, 1234, "CQ K2XYZ EM00")));
    QVERIFY(filter.shouldDisplay(q65Line(125000, 1234, "CQ K1ABC FN42")));
  }

  void displaysFinalOnlyDecode()
  {
    Map65DecodeDisplayFilter filter;
    QVERIFY(filter.handleControlLine("<Map65DecodePass> early"));
    filter.completeEarlyPass();
    QVERIFY(filter.handleControlLine("<Map65DecodePass> final"));

    QString const decode = q65Line(125000, 2, "W1AAA K2BBB EM00");
    QVERIFY(filter.shouldDisplay(decode));
    QVERIFY(filter.shouldDisplay(decode));
  }

  void manualDiskAndUnknownPassesFailOpen()
  {
    Map65DecodeDisplayFilter filter;
    QString const decode = jt65Line(125000, 2, "CQ K1ABC FN42");

    QVERIFY(filter.shouldDisplay(decode));
    QVERIFY(filter.handleControlLine("<Map65DecodePass> manual"));
    QVERIFY(filter.shouldDisplay(decode));
    QVERIFY(filter.shouldDisplay(decode));
    QVERIFY(filter.handleControlLine("<Map65DecodePass> disk"));
    QVERIFY(filter.shouldDisplay(decode));
    QVERIFY(filter.handleControlLine("<Map65DecodePass> invalid"));
    QVERIFY(filter.shouldDisplay(decode));
    QVERIFY(filter.shouldDisplay("Signal too strong, or suspect data?"));

    QString malformed = decode;
    malformed.replace(28, 3, " ? ");
    QVERIFY(filter.shouldDisplay(malformed));
  }

  void completedCycleDoesNotRetainDuplicates()
  {
    Map65DecodeDisplayFilter filter;
    QString const decode = q65Line(125000, 2, "W1AAA K2BBB EM00");

    QVERIFY(filter.handleControlLine("<Map65DecodePass> early"));
    QVERIFY(filter.shouldDisplay(decode));
    filter.completeEarlyPass();
    QVERIFY(filter.handleControlLine("<Map65DecodePass> final"));
    QVERIFY(!filter.shouldDisplay(decode));
    filter.completeCycle();
    QVERIFY(filter.handleControlLine("<Map65DecodePass> final"));
    QVERIFY(filter.shouldDisplay(decode));
  }
};

QTEST_APPLESS_MAIN(TestMap65DecodeDisplayFilter)

#include "test_map65_decode_display_filter.moc"
