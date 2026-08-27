#include <cmath>
#include <limits>

#include <QtTest/QtTest>

#include "widgets/WaterfallScale.hpp"

class TestWaterfallScale final
  : public QObject
{
  Q_OBJECT

private slots:
  void frequencyToPixelClampsAtVisibleEndpoints ();
  void pixelToFrequencyRemainsUnclamped ();
  void conversionsRoundTripAtPixelResolution ();
  void supportedConfigurationsPreserveScaleArithmetic ();
  void emptyScaleQueriesAreSafe ();
  void derivedValuesUpdateAfterEveryMutation ();
  void maximumFrequencyPreservesFiveKilohertzRounding ();
  void majorDivisionThresholdsPreserveStrictBoundaries ();
  void gridLayoutPreservesAlignedLabelsAndCounts ();
  void gridLayoutPreservesNonAlignedLabelOrigin ();
  void divisionCountsIgnoreTinyFloatingPointOvershoot ();
  void majorTickPositionsUseFullPrecision ();
  void twoHundredHertzDivisionsUseFourMinorTicks ();
};

namespace
{
  WaterfallScale scaleWith(double hzPerPixel, int width = 1000,
                           int startHz = 0)
  {
    WaterfallScale scale;
    scale.setWidth(width);
    scale.setStartHz(startHz);
    scale.setFftBinWidthHz(hzPerPixel);
    return scale;
  }
}

void TestWaterfallScale::frequencyToPixelClampsAtVisibleEndpoints ()
{
  auto scale = scaleWith(0.5, 1000, 200);
  scale.setBinsPerPixel(4);

  QCOMPARE(scale.spanHz(), 2000.0f);
  QCOMPARE(scale.xFromFreqHz(200.0f), 0);
  QCOMPARE(scale.xFromFreqHz(2200.0f), 1000);
  QCOMPARE(scale.xFromFreqHz(-1000.0f), 0);
  QCOMPARE(scale.xFromFreqHz(4000.0f), 1000);
  QCOMPARE(scale.xFromFreqHz(201.0f), 1);
}

void TestWaterfallScale::pixelToFrequencyRemainsUnclamped ()
{
  auto scale = scaleWith(2.0, 1000, 200);

  QCOMPARE(scale.freqHzFromX(-10), 180.0f);
  QCOMPARE(scale.freqHzFromX(0), 200.0f);
  QCOMPARE(scale.freqHzFromX(1000), 2200.0f);
  QCOMPARE(scale.freqHzFromX(1010), 2220.0f);
}

void TestWaterfallScale::conversionsRoundTripAtPixelResolution ()
{
  auto scale = scaleWith(1500.0 / 2048.0, 1537, 125);
  scale.setBinsPerPixel(7);

  for(int x : {0, 1, 127, 768, 1536, 1537}) {
    QCOMPARE(scale.xFromFreqHz(scale.freqHzFromX(x)), x);
  }

  double const halfPixel = scale.hzPerPixel() / 2.0;
  for(float frequency : {125.0f, 126.0f, 999.25f, 4000.0f, 7999.0f}) {
    int const x = scale.xFromFreqHz(frequency);
    if(x > 0 && x < scale.width()) {
      QVERIFY(std::abs(scale.freqHzFromX(x) - frequency) <= halfPixel);
    }
  }
}

void TestWaterfallScale::supportedConfigurationsPreserveScaleArithmetic ()
{
  double const fftBinWidths[] = {
    1500.0 / 2048.0,
    1500.0 / 6144.0,
    1500.0 / 12288.0,
    1500.0 / 32768.0
  };

  for(double fftBinWidth : fftBinWidths) {
    for(int startHz : {0, 100, 2500}) {
      for(int width : {50, 1000, 2048}) {
        for(int binsPerPixel : {1, 4, 1000}) {
          WaterfallScale scale;
          scale.setWidth(width);
          scale.setStartHz(startHz);
          scale.setBinsPerPixel(binsPerPixel);
          scale.setFftBinWidthHz(fftBinWidth);

          QCOMPARE(scale.width(), width);
          QCOMPARE(scale.startHz(), startHz);
          QCOMPARE(scale.binsPerPixel(), binsPerPixel);
          QCOMPARE(scale.fftBinWidthHz(), fftBinWidth);
          QCOMPARE(scale.hzPerPixel(), binsPerPixel * fftBinWidth);
          QCOMPARE(scale.spanHz(), float(width * binsPerPixel * fftBinWidth));
          QCOMPARE(scale.freqHzFromX(width),
                   float(startHz + width * binsPerPixel * fftBinWidth));
        }
      }
    }
  }

  WaterfallScale scale;
  scale.setBinsPerPixel(0);
  QCOMPARE(scale.binsPerPixel(), 1);
  scale.setBinsPerPixel(-100);
  QCOMPARE(scale.binsPerPixel(), 1);

  scale.setWidth(-100);
  QCOMPARE(scale.width(), 0);
}

void TestWaterfallScale::emptyScaleQueriesAreSafe ()
{
  WaterfallScale scale;

  QCOMPARE(scale.spanHz(), 0.0f);
  QCOMPARE(scale.xFromFreqHz(5000.0f), 0);
  QCOMPARE(scale.maxFreqHz(), 0);

  scale.setStartHz(200);
  QCOMPARE(scale.xFromFreqHz(5000.0f), 0);
  QCOMPARE(scale.maxFreqHz(), 200);
}

void TestWaterfallScale::derivedValuesUpdateAfterEveryMutation ()
{
  auto scale = scaleWith(1.0, 1000, 0);

  QCOMPARE(scale.spanHz(), 1000.0f);
  QCOMPARE(scale.maxFreqHz(), 1000);

  scale.setWidth(2000);
  QCOMPARE(scale.spanHz(), 2000.0f);
  QCOMPARE(scale.maxFreqHz(), 2000);

  scale.setStartHz(1000);
  QCOMPARE(scale.freqHzFromX(0), 1000.0f);
  QCOMPARE(scale.maxFreqHz(), 3000);

  scale.setBinsPerPixel(2);
  QCOMPARE(scale.spanHz(), 4000.0f);
  QCOMPARE(scale.maxFreqHz(), 5000);

  scale.setFftBinWidthHz(0.25);
  QCOMPARE(scale.spanHz(), 1000.0f);
  QCOMPARE(scale.maxFreqHz(), 2000);
}

void TestWaterfallScale::maximumFrequencyPreservesFiveKilohertzRounding ()
{
  QCOMPARE(scaleWith(1.0, 4999).maxFreqHz(), 4999);
  QCOMPARE(scaleWith(1.0, 5000).maxFreqHz(), 5000);
  QCOMPARE(scaleWith(1.0, 6000).maxFreqHz(), 5000);
  QCOMPARE(scaleWith(2.0, 1000, 4000).maxFreqHz(), 5000);
  QCOMPARE(scaleWith(2.0, 1000, 4999).maxFreqHz(), 5001);
  QCOMPARE(scaleWith(3.0, 1666).maxFreqHz(), 4998);
}

void TestWaterfallScale::majorDivisionThresholdsPreserveStrictBoundaries ()
{
  struct Threshold
  {
    float spanHz;
    int atBoundary;
    int aboveBoundary;
  };

  Threshold const thresholds[] = {
    {100.0f, 10, 20},
    {250.0f, 20, 50},
    {500.0f, 50, 100},
    {1000.0f, 100, 200},
    {2500.0f, 200, 500}
  };

  for(auto const& threshold : thresholds) {
    auto scale = scaleWith(threshold.spanHz, 1);
    QCOMPARE(scale.gridLayout().majorSpacingHz, threshold.atBoundary);

    scale.setFftBinWidthHz(std::nextafter(
      threshold.spanHz, std::numeric_limits<float>::infinity()));
    QCOMPARE(scale.gridLayout().majorSpacingHz, threshold.aboveBoundary);
  }
}

void TestWaterfallScale::gridLayoutPreservesAlignedLabelsAndCounts ()
{
  auto const grid = scaleWith(1.0, 1000, 200).gridLayout();

  QCOMPARE(grid.majorSpacingHz, 100);
  QCOMPARE(grid.pixelsPerDivision, 100.0);
  QCOMPARE(grid.overlayDivisionCount, 11);
  QCOMPARE(grid.scaleDivisionCount, 10);
  QCOMPARE(grid.firstMajorDivisionOffset, 0.0);
  QCOMPARE(grid.verticalGridOffsetPixels, 0);
  QCOMPARE(grid.minorDivisionCount, 5);
  QCOMPARE(grid.labels.size(), 11);
  QCOMPARE(grid.labels[0], QStringLiteral("200"));
  QCOMPARE(grid.labels[1], QStringLiteral("300"));
  QCOMPARE(grid.labels[10], QStringLiteral("1200"));
}

void TestWaterfallScale::gridLayoutPreservesNonAlignedLabelOrigin ()
{
  auto const grid = scaleWith(1.0, 1000, 225).gridLayout();

  QCOMPARE(grid.majorSpacingHz, 100);
  QCOMPARE(grid.firstMajorDivisionOffset, 0.75);
  QCOMPARE(grid.verticalGridOffsetPixels, 25);
  QCOMPARE(grid.labels[0], QStringLiteral("300"));
  QCOMPARE(grid.labels[1], QStringLiteral("400"));
  QCOMPARE(grid.labels.last(), QStringLiteral("1300"));
}

void TestWaterfallScale::divisionCountsIgnoreTinyFloatingPointOvershoot ()
{
  auto scale = scaleWith(3000.025, 1);
  QCOMPARE(scale.gridLayout().majorSpacingHz, 500);
  QCOMPARE(scale.gridLayout().scaleDivisionCount, 6);
  QCOMPARE(scale.gridLayout().overlayDivisionCount, 7);

  scale.setFftBinWidthHz(3000.1);
  QCOMPARE(scale.gridLayout().scaleDivisionCount, 7);
  QCOMPARE(scale.gridLayout().overlayDivisionCount, 8);
}

void TestWaterfallScale::majorTickPositionsUseFullPrecision ()
{
  WaterfallScale scale;
  scale.setWidth(1181);
  scale.setStartHz(4349);
  scale.setBinsPerPixel(213);
  scale.setFftBinWidthHz(1500.0 / 12288.0);
  auto const grid = scale.gridLayout();

  QCOMPARE(grid.majorSpacingHz, 500);
  QCOMPARE(grid.firstMajorDivisionOffset, 151.0 / 500.0);
  QCOMPARE(int((grid.firstMajorDivisionOffset + 53)
               * grid.pixelsPerDivision), 1024);
}

void TestWaterfallScale::twoHundredHertzDivisionsUseFourMinorTicks ()
{
  auto const grid = scaleWith(1.5, 1000).gridLayout();

  QCOMPARE(grid.majorSpacingHz, 200);
  QCOMPARE(grid.minorDivisionCount, 4);
  QCOMPARE(grid.overlayDivisionCount, 9);
  QCOMPARE(grid.scaleDivisionCount, 8);
}

QTEST_APPLESS_MAIN (TestWaterfallScale)

#include "test_waterfall_scale.moc"
