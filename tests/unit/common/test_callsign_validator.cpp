#include <QtTest>

#include "validators/CallsignValidator.hpp"

class TestCallsignValidator final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void normalizeStoredInput_data ();
  void normalizeStoredInput ();
};

void TestCallsignValidator::normalizeStoredInput_data ()
{
  QTest::addColumn<QString> ("input");
  QTest::addColumn<int> ("max_length");
  QTest::addColumn<bool> ("allow_compound");
  QTest::addColumn<QString> ("expected");

  QTest::newRow ("valid") << QString {"K1JT"} << 6 << false << QString {"K1JT"};
  QTest::newRow ("lowercase") << QString {"k1jt"} << 6 << false << QString {"K1JT"};
  QTest::newRow ("boundary whitespace") << QString {" k1jt "} << 6 << false << QString {"K1JT"};
  QTest::newRow ("trim before truncation") << QString {" K1JT73"} << 6 << false << QString {"K1JT73"};
  QTest::newRow ("truncate valid") << QString {"K1JT73X"} << 6 << false << QString {"K1JT73"};
  QTest::newRow ("compound allowed") << QString {"K1JT/P"} << 6 << true << QString {"K1JT/P"};
  QTest::newRow ("compound rejected") << QString {"K1JT/P"} << 6 << false << QString {};
  QTest::newRow ("punctuation") << QString {"K1?JT"} << 6 << false << QString {};
  QTest::newRow ("internal whitespace") << QString {"K1 JT"} << 6 << false << QString {};
  QTest::newRow ("non-ASCII") << QString::fromUtf8 ("K1\xc3\x98JT") << 6 << false << QString {};
  QTest::newRow ("empty") << QString {} << 6 << false << QString {};
}

void TestCallsignValidator::normalizeStoredInput ()
{
  QFETCH (QString, input);
  QFETCH (int, max_length);
  QFETCH (bool, allow_compound);
  QFETCH (QString, expected);

  QCOMPARE (CallsignValidator::normalizeStoredInput (
      input, max_length, allow_compound), expected);
}

QTEST_GUILESS_MAIN (TestCallsignValidator)

#include "test_callsign_validator.moc"
