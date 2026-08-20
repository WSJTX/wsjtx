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

  Q_SLOT void callsign_prefix_matching_data ()
  {
    QTest::addColumn<QString> ("configured_entries");
    QTest::addColumn<QString> ("call");
    QTest::addColumn<bool> ("matches");

    QTest::newRow ("exact callsign") << "W1AW," << "W1AW" << true;
    QTest::newRow ("exact callsign, no match") << "W1AW," << "K1ABC" << false;
    QTest::newRow ("3-char prefix") << ";OK9;" << "OK9ABC" << true;
    QTest::newRow ("3-char prefix, unanchored substring is not a match") << ";HOK9;" << "OK9ABC" << false;
    QTest::newRow ("2-char prefix") << ";VK;" << "VK2ABC" << true;
    QTest::newRow ("bare 1-char prefix matches digit-second call") << ";K;" << "K1ABC" << true;
    QTest::newRow ("bare 1-char prefix does not match compound entity") << ";K;" << "KH6ABC" << false;
    QTest::newRow ("wildcard 1-char prefix matches digit-second call") << ";K*;" << "K1ABC" << true;
    QTest::newRow ("wildcard 1-char prefix matches compound entity") << ";K*;" << "KH6ABC" << true;
    QTest::newRow ("exclusion overrides wildcard for 3-char prefix") << ";K*;!KH6!" << "KH6ABC" << false;
    QTest::newRow ("exclusion overrides wildcard for 2-char prefix") << ";K*;!KL!" << "KL7ABC" << false;
    QTest::newRow ("all three named exclusions apply") << ";K*;!KH6!KL!KP!" << "KP4ABC" << false;
    QTest::newRow ("exclusion does not affect plain matches") << ";K*;!KH6!" << "K1ABC" << true;
    QTest::newRow ("exclusion does not affect an unrelated 2-char prefix") << ";K*;VK;!KH6!" << "VK2ABC" << true;
    QTest::newRow ("no configured entries") << "" << "K1ABC" << false;
    QTest::newRow ("call too short") << ";K*;" << "K1" << false;
  }

  Q_SLOT void callsign_prefix_matching ()
  {
    QFETCH (QString, configured_entries);
    QFETCH (QString, call);
    QFETCH (bool, matches);

    QCOMPARE (HighlightingRules::matchesCallsignPrefix (configured_entries, call), matches);
  }
};

QTEST_MAIN (TestHighlightingRules);

#include "test_highlighting_rules.moc"
