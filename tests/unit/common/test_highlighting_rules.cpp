#include <QtTest>

#include "HighlightingRules.hpp"

class TestHighlightingRules
  : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void directional_call_matching_data ()
  {
    QTest::addColumn<QString> ("configured_entries");
    QTest::addColumn<QString> ("directional_call");
    QTest::addColumn<bool> ("matches");

    QTest::newRow ("first entry") << "DX,POTA,SOTA," << "DX" << true;
    QTest::newRow ("middle entry") << "DX,POTA,SOTA," << "POTA" << true;
    QTest::newRow ("last entry") << "DX,POTA,SOTA," << "SOTA" << true;
    QTest::newRow ("last without trailing comma") << "DX,POTA,SOTA" << "SOTA" << true;
    QTest::newRow ("documented spaces") << "DX, POTA, SOTA," << "POTA" << true;
    QTest::newRow ("single entry") << "POTA" << "POTA" << true;
    QTest::newRow ("single trailing comma") << "POTA," << "POTA" << true;
    QTest::newRow ("legacy leading comma") << ",POTA," << "POTA" << true;
    QTest::newRow ("empty fields") << ",, POTA ,," << "POTA" << true;
    QTest::newRow ("lowercase entry") << "dx,pota," << "POTA" << true;
    QTest::newRow ("callsign suffix") << "K1DX," << "DX" << false;
    QTest::newRow ("longer token") << "NOTPOTA," << "POTA" << false;
    QTest::newRow ("prefix syntax") << "VK;ZL;T88;" << "VK" << false;
    QTest::newRow ("missing entry") << "DX,POTA," << "SOTA" << false;
    QTest::newRow ("empty configuration") << "" << "DX" << false;
    QTest::newRow ("empty directional call") << "DX," << "" << false;
  }

  Q_SLOT void directional_call_matching ()
  {
    QFETCH (QString, configured_entries);
    QFETCH (QString, directional_call);
    QFETCH (bool, matches);

    QCOMPARE (HighlightingRules::matchesDirectionalCall (configured_entries, directional_call), matches);
  }
};

QTEST_MAIN (TestHighlightingRules);

#include "test_highlighting_rules.moc"
