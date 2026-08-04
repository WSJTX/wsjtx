#include <QtTest>

#include "DecodeOperatingContext.hpp"

class TestDecodeOperatingContext final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void identityMatchesEquivalentContext ();
  void sequenceStartIsAttributionOnly ();
  void identityChangeMakesContextObsolete_data ();
  void identityChangeMakesContextObsolete ();
};

namespace
{
  DecodeOperatingContext context ()
  {
    DecodeOperatingContext result;
    result.mode = "FT8";
    result.specOp = SpecialOperatingActivity::NONE;
    result.periodFrequency = 14074000;
    result.band = "20m";
    result.sequenceStart = QDateTime::fromString ("2026-07-26T12:00:00Z", Qt::ISODate);
    result.trPeriod = 15.0;
    result.submode = 0;
    result.diskData = false;
    result.multithreadFt8 = true;
    result.ft8DecoderStart = 1;
    result.superFox = false;
    result.myCall = "K1ABC";
    return result;
  }
}

void TestDecodeOperatingContext::identityMatchesEquivalentContext ()
{
  auto const captured = context ();
  QVERIFY (captured.hasSameDecodeIdentity (context ()));
}

void TestDecodeOperatingContext::sequenceStartIsAttributionOnly ()
{
  auto captured = context ();
  auto current = context ();
  current.sequenceStart = current.sequenceStart.addSecs (15);

  QVERIFY (captured.hasSameDecodeIdentity (current));
}

void TestDecodeOperatingContext::identityChangeMakesContextObsolete_data ()
{
  QTest::addColumn<QString> ("field");
  QTest::newRow ("mode") << QString {"mode"};
  QTest::newRow ("special operation") << QString {"special operation"};
  QTest::newRow ("frequency") << QString {"frequency"};
  QTest::newRow ("band") << QString {"band"};
  QTest::newRow ("TR period") << QString {"TR period"};
  QTest::newRow ("submode") << QString {"submode"};
  QTest::newRow ("disk data") << QString {"disk data"};
  QTest::newRow ("multithread FT8") << QString {"multithread FT8"};
  QTest::newRow ("FT8 start") << QString {"FT8 start"};
  QTest::newRow ("SuperFox") << QString {"SuperFox"};
  QTest::newRow ("callsign") << QString {"callsign"};
}

void TestDecodeOperatingContext::identityChangeMakesContextObsolete ()
{
  QFETCH (QString, field);
  auto captured = context ();
  auto current = context ();

  if (field == "mode") current.mode = "FT4";
  else if (field == "special operation") current.specOp = SpecialOperatingActivity::HOUND;
  else if (field == "frequency") ++current.periodFrequency;
  else if (field == "band") current.band = "17m";
  else if (field == "TR period") current.trPeriod = 30.0;
  else if (field == "submode") ++current.submode;
  else if (field == "disk data") current.diskData = true;
  else if (field == "multithread FT8") current.multithreadFt8 = false;
  else if (field == "FT8 start") ++current.ft8DecoderStart;
  else if (field == "SuperFox") current.superFox = true;
  else if (field == "callsign") current.myCall = "K1XYZ";

  QVERIFY (!captured.hasSameDecodeIdentity (current));
}

QTEST_GUILESS_MAIN (TestDecodeOperatingContext)

#include "test_decode_operating_context.moc"
