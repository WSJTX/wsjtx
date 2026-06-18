#include <QtTest>

#include "AutoRespondSelectionLatch.hpp"

class TestAutoRespondSelectionLatch : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void selectForClearsAfterDelay()
  {
    AutoRespondSelectionLatch latch;

    latch.selectFor(10);

    QVERIFY(latch.isSelected());
    QTRY_VERIFY_WITH_TIMEOUT(!latch.isSelected(), 250);
  }

  Q_SLOT void selectForRestartsResetDelay()
  {
    AutoRespondSelectionLatch latch;

    latch.selectFor(30);
    QTest::qWait(10);
    latch.selectFor(100);

    QVERIFY(latch.isSelected());
    QTest::qWait(50);
    QVERIFY(latch.isSelected());
    QTRY_VERIFY_WITH_TIMEOUT(!latch.isSelected(), 250);
  }
};

QTEST_MAIN(TestAutoRespondSelectionLatch)
#include "test_auto_respond_selection_latch.moc"
