#ifndef WATERFALL_SCALE_HPP_
#define WATERFALL_SCALE_HPP_

#include <algorithm>
#include <cmath>

#include <QString>
#include <QVector>

class WaterfallScale
{
public:
  struct GridLayout
  {
    int majorSpacingHz;
    double pixelsPerDivision;
    int overlayDivisionCount;
    int scaleDivisionCount;
    double firstMajorDivisionOffset;
    int verticalGridOffsetPixels;
    int minorDivisionCount;
    QVector<QString> labels;
  };

  int width() const { return m_width; }
  void setWidth(int width) { m_width = std::max(0, width); }

  int startHz() const { return m_startHz; }
  void setStartHz(int startHz) { m_startHz = startHz; }

  int binsPerPixel() const { return m_binsPerPixel; }
  void setBinsPerPixel(int binsPerPixel)
  {
    m_binsPerPixel = std::max(1, binsPerPixel);
  }

  double fftBinWidthHz() const { return m_fftBinWidthHz; }
  void setFftBinWidthHz(double fftBinWidthHz)
  {
    m_fftBinWidthHz = fftBinWidthHz;
  }

  double hzPerPixel() const
  {
    return m_binsPerPixel * m_fftBinWidthHz;
  }

  float spanHz() const
  {
    return float(m_width * hzPerPixel());
  }

  int xFromFreqHz(float frequencyHz) const
  {
    auto const span = spanHz();
    if(!(span > 0.0f)) return 0;

    int x = int(m_width * (frequencyHz - m_startHz) / span + 0.5);
    if(x < 0) return 0;
    if(x > m_width) return m_width;
    return x;
  }

  float freqHzFromX(int x) const
  {
    return float(m_startHz + x * m_binsPerPixel * m_fftBinWidthHz);
  }

  int maxFreqHz() const
  {
    return freqHzFromX(xFromFreqHz(5000.0f));
  }

  GridLayout gridLayout() const
  {
    auto const span = spanHz();
    int majorSpacingHz = 10;
    if(span > 100) majorSpacingHz = 20;
    if(span > 250) majorSpacingHz = 50;
    if(span > 500) majorSpacingHz = 100;
    if(span > 1000) majorSpacingHz = 200;
    if(span > 2500) majorSpacingHz = 500;

    double const pixelsPerDivision = majorSpacingHz / hzPerPixel();
    int const scaleDivisionCount = divisionCount(
      m_width * hzPerPixel() / majorSpacingHz);
    int const overlayDivisionCount = scaleDivisionCount + 1;

    double startDivision = double(m_startHz) / majorSpacingHz;
    startDivision -= int(startDivision);
    int const verticalGridOffsetPixels =
      startDivision * pixelsPerDivision + 0.5;

    int labelFrequency =
      (m_startHz + majorSpacingHz - 1) / majorSpacingHz;
    labelFrequency *= majorSpacingHz;
    double const firstMajorDivisionOffset =
      double(labelFrequency - m_startHz) / majorSpacingHz;

    QVector<QString> labels(scaleDivisionCount + 1);
    for(int i = 0; i <= scaleDivisionCount; ++i) {
      labels[i].setNum(labelFrequency);
      labelFrequency += majorSpacingHz;
    }

    return {majorSpacingHz,
            pixelsPerDivision,
            overlayDivisionCount,
            scaleDivisionCount,
            firstMajorDivisionOffset,
            verticalGridOffsetPixels,
            majorSpacingHz == 200 ? 4 : 5,
            labels};
  }

private:
  static int divisionCount(double divisions)
  {
    constexpr double tolerance = 0.0001;
    return int(std::ceil(divisions - tolerance));
  }

  int m_width {0};
  int m_startHz {0};
  int m_binsPerPixel {1};
  double m_fftBinWidthHz {1500.0 / 2048.0};
};

#endif
