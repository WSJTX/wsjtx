#include <QtTest>

#include "qmap/plotter.h"

class TestPlotterInitialization final : public QObject
{
  Q_OBJECT

private slots:
  void sizesHiddenPlotter();
};

void TestPlotterInitialization::sizesHiddenPlotter()
{
  CPlotter plotter;

  QVERIFY (!plotter.isVisible ());
  plotter.ensureSized (1000, 280);

  QCOMPARE (plotter.plotWidth (), 1000);
  QVERIFY (plotter.centerFreq () >= 0);
  QVERIFY (plotter.binsPerPixel () > 0);
}

QTEST_MAIN (TestPlotterInitialization)

#include "test_plotter_initialization.moc"
