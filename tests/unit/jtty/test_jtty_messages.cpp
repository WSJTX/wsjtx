#include <QtTest>

#include "widgets/JttyMessages.hpp"

class TestJttyMessages final
  : public QObject
{
  Q_OBJECT

private slots:
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
