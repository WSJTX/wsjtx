#include <limits>

#include <QtTest>

#include "Transceiver/TCIProtocol.hpp"

class TestTciProtocol final
  : public QObject
{
  Q_OBJECT

private slots:
  void parsesFrequency_data ()
  {
    QTest::addColumn<QString> ("text");
    QTest::addColumn<quint64> ("expected");

    QTest::newRow ("ordinary HF") << QString {"14074000"} << quint64 {14074000u};
    QTest::newRow ("below signed 32-bit boundary") << QString {"2147483647"} << quint64 {2147483647u};
    QTest::newRow ("at signed 32-bit boundary") << QString {"2147483648"} << quint64 {2147483648u};
    QTest::newRow ("above signed 32-bit boundary") << QString {"2147483649"} << quint64 {2147483649u};
    QTest::newRow ("10.368 GHz") << QString {"10368000000"} << quint64 {10368000000u};
    QTest::newRow ("upper supported TCI range") << QString {"250000000000"} << quint64 {250000000000u};
    QTest::newRow ("leading zeros") << QString {"00014074000"} << quint64 {14074000u};
    QTest::newRow ("zero") << QString {"0"} << quint64 {0u};
    QTest::newRow ("quint64 maximum")
      << QString {"18446744073709551615"} << (std::numeric_limits<quint64>::max) ();
  }

  void parsesFrequency ()
  {
    QFETCH (QString, text);
    QFETCH (quint64, expected);

    quint64 frequency {99u};
    QVERIFY (TciProtocol::parse_frequency (text, &frequency));
    QCOMPARE (frequency, expected);
  }

  void rejectsMalformedFrequency_data ()
  {
    QTest::addColumn<QString> ("text");

    QTest::newRow ("empty") << QString {};
    QTest::newRow ("negative") << QString {"-14074000"};
    QTest::newRow ("positive sign") << QString {"+14074000"};
    QTest::newRow ("leading whitespace") << QString {" 14074000"};
    QTest::newRow ("trailing whitespace") << QString {"14074000 "};
    QTest::newRow ("group separator") << QString {"14,074,000"};
    QTest::newRow ("decimal") << QString {"14074.000"};
    QTest::newRow ("exponent") << QString {"14074e3"};
    QTest::newRow ("trailing text") << QString {"14074000Hz"};
    QTest::newRow ("unicode digit") << QString::fromUtf8 ("١٤٠٧٤٠٠٠");
    QTest::newRow ("replacement character")
      << QString {"14"} + QChar {QChar::ReplacementCharacter} + QString {"074000"};
    QTest::newRow ("overflow") << QString {"18446744073709551616"};
  }

  void rejectsMalformedFrequency ()
  {
    QFETCH (QString, text);

    quint64 frequency {99u};
    QVERIFY (!TciProtocol::parse_frequency (text, &frequency));
    QCOMPARE (frequency, quint64 {99u});
  }

  void rejectsMissingOutput ()
  {
    QVERIFY (!TciProtocol::parse_frequency (QString {"14074000"}, nullptr));
  }

  void comparesFrequencyDifference_data ()
  {
    QTest::addColumn<quint64> ("lhs");
    QTest::addColumn<quint64> ("rhs");
    QTest::addColumn<bool> ("expected");

    QTest::newRow ("ordinary HF below threshold")
      << quint64 {14074000u} << quint64 {15073999u} << false;
    QTest::newRow ("below threshold")
      << quint64 {10368000000u} << quint64 {10368999999u} << false;
    QTest::newRow ("exact threshold")
      << quint64 {10368000000u} << quint64 {10369000000u} << false;
    QTest::newRow ("above threshold")
      << quint64 {10368000000u} << quint64 {10369000001u} << true;
    QTest::newRow ("above threshold reversed")
      << quint64 {10369000001u} << quint64 {10368000000u} << true;
    QTest::newRow ("signed boundary adjacent")
      << quint64 {2147483647u} << quint64 {2147483648u} << false;
    QTest::newRow ("upper supported range")
      << quint64 {250000000000u} << quint64 {249998999999u} << true;
    QTest::newRow ("quint64 upper edge")
      << (std::numeric_limits<quint64>::max) ()
      << (std::numeric_limits<quint64>::max) () - quint64 {1000001u} << true;
  }

  void comparesFrequencyDifference ()
  {
    QFETCH (quint64, lhs);
    QFETCH (quint64, rhs);
    QFETCH (bool, expected);

    QCOMPARE (TciProtocol::frequency_difference_exceeds (lhs, rhs, quint64 {1000000u}), expected);
  }
};

QTEST_MAIN (TestTciProtocol)

#include "test_tci_protocol.moc"
