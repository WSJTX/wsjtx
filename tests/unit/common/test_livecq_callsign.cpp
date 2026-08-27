#include <QtTest>

#include "validators/LiveCQCallsign.hpp"

class TestLiveCQCallsign final : public QObject
{
  Q_OBJECT

private slots:
  void acceptsThreeCharacterCalls()
  {
    QVERIFY(LiveCQ::isValidCallsign("N5L"));
    QVERIFY(LiveCQ::isValidCallsign("K1A"));
    QVERIFY(LiveCQ::isValidCallsign("GM/N5L"));
  }

  void preservesExistingValidCalls()
  {
    for (auto const& callsign : {QString {"K1ABC"}, QString {"YW18FIFA"},
                                 QString {"QU1RK"}, QString {"<K1ABC>"},
                                 QString {"K1ABC/P"}}) {
      QVERIFY2(LiveCQ::isValidCallsign(callsign), qPrintable(callsign));
    }
  }

  void rejectsEmptyShortAndInvalidCalls()
  {
    for (auto const& callsign : {QString {}, QString {"N"}, QString {"N5"},
                                 QString {"KABC"}, QString {"Q1ABC"},
                                 QString {"K1AB1"}, QString {"K1ABCDE"},
                                 QString {"K1AB-C"}, QString {"K1AB?"}}) {
      QVERIFY2(!LiveCQ::isValidCallsign(callsign), qPrintable(callsign));
    }
  }
};

QTEST_APPLESS_MAIN(TestLiveCQCallsign)

#include "test_livecq_callsign.moc"
