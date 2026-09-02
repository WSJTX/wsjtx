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

  void jttyLineTimeLabel_data ()
  {
    QTest::addColumn<QDateTime> ("diskDateTime");
    QTest::addColumn<qint32> ("utcDiskRaw");
    QTest::addColumn<float> ("tsyncSeconds");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("real-date-anchors-tsync")
        << QDateTime {QDate {2026, 8, 28}, QTime {19, 45, 7}, Qt::UTC}
        << qint32 {194507} << 3.0f << QString {"194510"};
    QTest::newRow ("real-date-wraps-past-midnight")
        << QDateTime {QDate {2026, 8, 28}, QTime {23, 59, 58}, Qt::UTC}
        << qint32 {235958} << 5.0f << QString {"000003"};
    QTest::newRow ("subsecond-offset-preserves-current-second")
        << QDateTime {QDate {2026, 8, 28}, QTime {19, 45, 7}, Qt::UTC}
        << qint32 {194507} << 0.6f << QString {"194507"};
    QTest::newRow ("dummy-date-falls-back-to-raw-digits")
        << QDateTime {} << qint32 {2} << 0.0f << QString {"000002"};
    QTest::newRow ("dummy-date-fallback-still-anchors-tsync")
        << QDateTime {} << qint32 {2} << 5.0f << QString {"000007"};
    QTest::newRow ("neither-anchor-usable")
        << QDateTime {} << qint32 {999999} << 0.0f << QString {};
  }

  void jttyLineTimeLabel ()
  {
    QFETCH (QDateTime, diskDateTime);
    QFETCH (qint32, utcDiskRaw);
    QFETCH (float, tsyncSeconds);
    QFETCH (QString, expected);

    QCOMPARE (Jtty::jttyLineTimeLabel (diskDateTime, utcDiskRaw, tsyncSeconds), expected);
  }

  void parseDecodeLine_data ()
  {
    QTest::addColumn<QString> ("line");
    QTest::addColumn<int> ("frequency");
    QTest::addColumn<QString> ("message");
    QTest::addColumn<bool> ("valid");

    QTest::newRow ("four-digit-frequency")
        << QString {"1500  CQ K1ABC"} << 1500 << QString {"CQ K1ABC"} << true;
    QTest::newRow ("three-digit-frequency-with-padding")
        << QString {" 500  599 K1ABC "} << 500 << QString {"599 K1ABC"} << true;
    QTest::newRow ("numeric-leading-message-is-preserved")
        << QString {"1500  599 K1ABC"} << 1500 << QString {"599 K1ABC"} << true;
    QTest::newRow ("malformed-frequency")
        << QString {"CQ K1ABC"} << 0 << QString {"CQ K1ABC"} << false;
    QTest::newRow ("empty")
        << QString {} << 0 << QString {} << false;
  }

  void parseDecodeLine ()
  {
    QFETCH (QString, line);
    QFETCH (int, frequency);
    QFETCH (QString, message);
    QFETCH (bool, valid);

    auto const decoded = Jtty::parseDecodeLine (line);

    QCOMPARE (decoded.frequency, frequency);
    QCOMPARE (decoded.message, message);
    QCOMPARE (decoded.valid, valid);
  }

  void wrapMessage_data ()
  {
    QTest::addColumn<QString> ("text");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("short-unchanged") << QString {"HELLO"} << QString {"HELLO"};
    QTest::newRow ("exactly-40-unchanged")
        << QString (40, QLatin1Char {'A'}) << QString (40, QLatin1Char {'A'});
    QTest::newRow ("joes-example")
        << QString {"MAYBE CLAUDE COULD HELP US MAKE THE 3 MIN TRANSITION SEAMLESS"}
        << QString {"MAYBE CLAUDE COULD HELP US MAKE THE 3\n  MIN TRANSITION SEAMLESS"};
    QTest::newRow ("break-at-boundary")
        << (QString (39, QLatin1Char {'A'}) + QString {" B"})
        << (QString (39, QLatin1Char {'A'}) + QString {"\n  B"});
    QTest::newRow ("hard-break-no-blank")
        << QString (50, QLatin1Char {'A'})
        << (QString (40, QLatin1Char {'A'}) + QString {"\n  "}
            + QString (10, QLatin1Char {'A'}));
    QTest::newRow ("multiple-wrap-points")
        << QString {"AAAAAAAAAA BBBBBBBBBB CCCCCCCCCC DDDDDDDDDD EEEEEEEEEE FFFFFFFFFF GGGGGGGGGG"}
        << QString {"AAAAAAAAAA BBBBBBBBBB CCCCCCCCCC\n  DDDDDDDDDD EEEEEEEEEE FFFFFFFFFF\n  GGGGGGGGGG"};
  }

  void wrapMessage ()
  {
    QFETCH (QString, text);
    QFETCH (QString, expected);

    QCOMPARE (Jtty::wrapMessage (text), expected);
  }

  void decodeLineOrderUsesStartTimeThenSlotId ()
  {
    QVector<float> const startTimes {5.0f, 1.0f, 3.0f, 3.0f};
    QVector<int> const slotIds {9, 4, 7, 2};
    QVector<int> const expected {1, 3, 2, 0};

    QCOMPARE (Jtty::decodeLineOrder (startTimes, slotIds), expected);
  }
};

QTEST_MAIN (TestJttyMessages)
#include "test_jtty_messages.moc"
