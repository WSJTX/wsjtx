#include <QtTest>

#include "widgets/JttyMessages.hpp"

class TestJttyMessages final
  : public QObject
{
  Q_OBJECT

private slots:
  void prepareTransmitText_data ()
  {
    QTest::addColumn<QString> ("message");
    QTest::addColumn<QString> ("expected");
    QTest::addColumn<bool> ("substituted");
    QTest::addColumn<bool> ("truncated");

    QTest::newRow ("empty") << QString {} << QString {}
                            << false << false;
    QTest::newRow ("supported") << QString {"CQ KA1ABC CQ"} << QString {"CQ KA1ABC CQ"}
                                << false << false;
    QTest::newRow ("lowercase-preserved") << QString {"cq ka1abc cq"} << QString {"cq ka1abc cq"}
                                          << false << false;
    QTest::newRow ("tab") << QString {"HELLO\tWORLD"} << QString {"HELLO#WORLD"}
                          << true << false;
    QTest::newRow ("cr-lf") << QString {"HELLO\r\nWORLD"} << QString {"HELLO##WORLD"}
                            << true << false;
    QTest::newRow ("nul") << (QString {"A"} + QChar::Null + QString {"B"}) << QString {"A B"}
                          << true << false;
    QTest::newRow ("display-space-marker") << QString {"A~B"} << QString {"A B"}
                                           << true << false;
    QTest::newRow ("exactly-80") << QString (80, QLatin1Char {'A'}) << QString (80, QLatin1Char {'A'})
                                 << false << false;
    QTest::newRow ("truncated") << QString (81, QLatin1Char {'A'}) << QString (80, QLatin1Char {'A'})
                                << false << true;
    QTest::newRow ("unsupported-past-limit")
        << (QString (80, QLatin1Char {'A'}) + QString {"\t"})
        << QString (80, QLatin1Char {'A'})
        << false << true;
    QTest::newRow ("substitution-and-truncation")
        << (QString (79, QLatin1Char {'A'}) + QString {"\tB"})
        << (QString (79, QLatin1Char {'A'}) + QString {"#"})
        << true << true;
  }

  void prepareTransmitText ()
  {
    QFETCH (QString, message);
    QFETCH (QString, expected);
    QFETCH (bool, substituted);
    QFETCH (bool, truncated);

    auto const prepared = Jtty::prepareTransmitText (message);

    QCOMPARE (prepared.text, expected);
    QCOMPARE (prepared.substituted, substituted);
    QCOMPARE (prepared.truncated, truncated);
    QCOMPARE (prepared.changed (), substituted || truncated);
  }

  void withChainedSpacing_data ()
  {
    QTest::addColumn<QString> ("message");
    QTest::addColumn<bool> ("isChained");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("not-chained-unchanged") << QString {"CQ KA1ABC CQ"} << false
                                            << QString {"CQ KA1ABC CQ"};
    QTest::newRow ("chained-gets-leading-space") << QString {"TU DE KA1ABC"} << true
                                                 << QString {" TU DE KA1ABC"};
    QTest::newRow ("not-chained-empty-stays-empty") << QString {} << false << QString {};
    QTest::newRow ("chained-at-max-length-drops-last-char")
        << QString (Jtty::maxTransmitLength, QLatin1Char {'A'}) << true
        << (QString {" "} + QString (Jtty::maxTransmitLength - 1, QLatin1Char {'A'}));
  }

  void withChainedSpacing ()
  {
    QFETCH (QString, message);
    QFETCH (bool, isChained);
    QFETCH (QString, expected);

    QCOMPARE (Jtty::withChainedSpacing (message, isChained), expected);
  }

  void formatSerialNumber_data ()
  {
    QTest::addColumn<int> ("serialNumber");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("minimum") << 1 << QString {"001"};
    QTest::newRow ("single-digit") << 5 << QString {"005"};
    QTest::newRow ("two-digit") << 57 << QString {"057"};
    QTest::newRow ("three-digit") << 101 << QString {"101"};
    QTest::newRow ("four-digit") << 1234 << QString {"1234"};
    QTest::newRow ("spinbox-maximum") << 5000 << QString {"5000"};
  }

  void formatSerialNumber ()
  {
    QFETCH (int, serialNumber);
    QFETCH (QString, expected);

    QCOMPARE (Jtty::formatSerialNumber (serialNumber), expected);
  }
};

QTEST_MAIN (TestJttyMessages)
#include "test_jtty_messages.moc"
