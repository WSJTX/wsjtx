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
  void ft8PendingIdentityChangeMakesSnapshotObsolete_data ();
  void ft8PendingIdentityChangeMakesSnapshotObsolete ();
  void ft8PendingIdentityRejectsOtherModes ();
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
    result.ft8ThreadCount = 4;
    result.decodeDepth = 3;
    result.ft8Cycles = 3;
    result.ft8Sensitivity = 3;
    result.ft8RxFrequencySensitivity = 3;
    result.decodeLowFrequency = 200;
    result.decodeHighFrequency = 3000;
    result.ft8WideDxCallSearch = true;
    result.hideFt8DuplicateMessages = true;
    result.ft8ApEnabled = true;
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

void TestDecodeOperatingContext::ft8PendingIdentityChangeMakesSnapshotObsolete_data ()
{
  QTest::addColumn<QString> ("field");
  QTest::newRow ("FT8 threads") << QString {"FT8 threads"};
  QTest::newRow ("decode depth") << QString {"decode depth"};
  QTest::newRow ("FT8 cycles") << QString {"FT8 cycles"};
  QTest::newRow ("FT8 sensitivity") << QString {"FT8 sensitivity"};
  QTest::newRow ("FT8 RX sensitivity") << QString {"FT8 RX sensitivity"};
  QTest::newRow ("decode low frequency") << QString {"decode low frequency"};
  QTest::newRow ("decode high frequency") << QString {"decode high frequency"};
  QTest::newRow ("FT8 wide DX search") << QString {"FT8 wide DX search"};
  QTest::newRow ("hide FT8 duplicates") << QString {"hide FT8 duplicates"};
  QTest::newRow ("FT8 AP") << QString {"FT8 AP"};
}

void TestDecodeOperatingContext::ft8PendingIdentityChangeMakesSnapshotObsolete ()
{
  QFETCH (QString, field);
  auto captured = context ();
  auto current = context ();

  if (field == "FT8 threads") ++current.ft8ThreadCount;
  else if (field == "decode depth") ++current.decodeDepth;
  else if (field == "FT8 cycles") ++current.ft8Cycles;
  else if (field == "FT8 sensitivity") ++current.ft8Sensitivity;
  else if (field == "FT8 RX sensitivity") ++current.ft8RxFrequencySensitivity;
  else if (field == "decode low frequency") ++current.decodeLowFrequency;
  else if (field == "decode high frequency") ++current.decodeHighFrequency;
  else if (field == "FT8 wide DX search") current.ft8WideDxCallSearch = false;
  else if (field == "hide FT8 duplicates") current.hideFt8DuplicateMessages = false;
  else if (field == "FT8 AP") current.ft8ApEnabled = false;

  QVERIFY (captured.hasSameDecodeIdentity (current));
  QVERIFY (!captured.hasSameFt8PendingIdentity (current));
}

void TestDecodeOperatingContext::ft8PendingIdentityRejectsOtherModes ()
{
  auto captured = context ();
  auto current = context ();
  captured.mode = "Q65";
  current.mode = "Q65";
  ++current.ft8ThreadCount;

  QVERIFY (captured.hasSameDecodeIdentity (current));
  QVERIFY (!captured.hasSameFt8PendingIdentity (current));
}

QTEST_GUILESS_MAIN (TestDecodeOperatingContext)

#include "test_decode_operating_context.moc"
