#include <QtTest>

#include "Rr73Policy.hpp"

class TestRr73Policy final : public QObject
{
  Q_OBJECT

private slots:
  void tx4AllowsRr73_data ();
  void tx4AllowsRr73 ();
};

void TestRr73Policy::tx4AllowsRr73_data ()
{
  QTest::addColumn<QString> ("mode");
  QTest::addColumn<bool> ("shortMessages");
  QTest::addColumn<bool> ("allowed");

  QTest::newRow ("FT8") << QString {"FT8"} << false << true;
  QTest::newRow ("FT4") << QString {"FT4"} << false << true;
  QTest::newRow ("FST4") << QString {"FST4"} << false << true;
  QTest::newRow ("MSK144") << QString {"MSK144"} << false << true;
  QTest::newRow ("MSK144 shorthand") << QString {"MSK144"} << true << false;
  QTest::newRow ("Q65") << QString {"Q65"} << false << true;
  QTest::newRow ("Q65 shorthand") << QString {"Q65"} << true << false;
  QTest::newRow ("JT65") << QString {"JT65"} << false << false;
  QTest::newRow ("JT4") << QString {"JT4"} << false << false;
  QTest::newRow ("FST4W") << QString {"FST4W"} << false << false;
}

void TestRr73Policy::tx4AllowsRr73 ()
{
  QFETCH (QString, mode);
  QFETCH (bool, shortMessages);
  QFETCH (bool, allowed);

  QCOMPARE (Rr73Policy::tx4AllowsRr73 (mode, shortMessages), allowed);
}

QTEST_GUILESS_MAIN (TestRr73Policy)

#include "test_rr73_policy.moc"
