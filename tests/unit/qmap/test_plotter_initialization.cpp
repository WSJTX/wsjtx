#include <memory>

#include <QtTest>
#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QSignalSpy>

#include "qmap/plotter.h"
#include "qmap/qmap_runtime_config.h"

class TestablePlotter final : public CPlotter
{
public:
  using CPlotter::mousePressEvent;
};

class TestPlotterInitialization final : public QObject
{
  Q_OBJECT

private slots:
  void sizesHiddenPlotter();
  void appliesANewSpanAtOnce();
  void remembersASpanGivenBeforeSizing();
  void scalesAtTheActiveRateWithoutSetFsample();
  void setFsampleRefreshesTheScaling();
  void ignoresOutOfSpanAndControlClicks();
  void paintsLabelsInTwoDSpectrumMode();
};

void TestPlotterInitialization::sizesHiddenPlotter()
{
  // CPlotter embeds a ~12.5 MB m_zwf array as a direct member (qmap/plotter.h),
  // so it must be heap-allocated -- a stack-local instance overflows the
  // default thread stack.
  auto const plotter = std::make_unique<CPlotter> ();

  QVERIFY (!plotter->isVisible ());
  plotter->ensureSized (1000, 280);

  QCOMPARE (plotter->plotWidth (), 1000);
  QVERIFY (plotter->centerFreq () >= 0);
  QVERIFY (plotter->binsPerPixel () > 0);
}

void TestPlotterInitialization::appliesANewSpanAtOnce()
{
  auto const plotter = std::make_unique<CPlotter> ();
  plotter->ensureSized (1000, 280);
  plotter->setFsample (96000);

  plotter->applySpan (30);
  int const narrowBins = plotter->binsPerPixel ();
  float const narrowSpan = plotter->m_fSpan;
  plotter->applySpan (60);
  int const wideBins = plotter->binsPerPixel ();
  float const wideSpan = plotter->m_fSpan;

  // Twice the span needs about twice the bins per pixel, and the displayed span follows
  QVERIFY2 (wideBins >= 2 * narrowBins - 1 && wideBins <= 2 * narrowBins + 1,
            "bins per pixel should follow the span");
  QVERIFY (narrowSpan > 27.f && narrowSpan < 31.f);
  QVERIFY (wideSpan > 57.f && wideSpan < 61.f);
  QCOMPARE (plotter->m_nSpan, 60);
}

void TestPlotterInitialization::remembersASpanGivenBeforeSizing()
{
  // WideGraph's constructor sets the span box (and so the span) before the plot has a size
  auto const plotter = std::make_unique<CPlotter> ();
  plotter->applySpan (45);
  QCOMPARE (plotter->m_nSpan, 45);
  plotter->ensureSized (1000, 280);
  QCOMPARE (plotter->plotWidth (), 1000);
  QVERIFY (plotter->m_fSpan > 42.f && plotter->m_fSpan < 46.f);
}

void TestPlotterInitialization::scalesAtTheActiveRateWithoutSetFsample()
{
  // A 256 kHz run sizes the plot before MainWindow hands it the rate; the scaling must already be right
  qmap_runtime::setActive (256000, 131072);
  {
    auto const plotter = std::make_unique<CPlotter> ();
    plotter->applySpan (240);
    plotter->ensureSized (1000, 280);
    // 240 kHz across 1000 pixels at 1.953 Hz per bin is about 123 bins per pixel (328 at 96 kHz scaling)
    QVERIFY2 (plotter->binsPerPixel () >= 122 && plotter->binsPerPixel () <= 124,
              qPrintable (QString::number (plotter->binsPerPixel ())));
  }
  qmap_runtime::setActive (96000, 32768);
}

void TestPlotterInitialization::setFsampleRefreshesTheScaling()
{
  qmap_runtime::setActive (192000, 65536);
  {
    auto const plotter = std::make_unique<CPlotter> ();
    plotter->applySpan (100);
    plotter->ensureSized (1000, 280);
    int const atActive = plotter->binsPerPixel ();
    plotter->setFsample (96000);                    // a later rate change re-derives the scaling
    QVERIFY (plotter->binsPerPixel () > atActive);
  }
  qmap_runtime::setActive (96000, 32768);
}

void TestPlotterInitialization::ignoresOutOfSpanAndControlClicks()
{
  qRegisterMetaType<DecodeClickGesture> ("DecodeClickGesture");
  auto const plotter = std::make_unique<TestablePlotter> ();
  plotter->ensureSized (1000, 280);
  plotter->setFsample (96000);
  plotter->show ();
  QCoreApplication::processEvents ();
  plotter->SetStartFreq (100);
  plotter->m_fSpan = 100.f;

  QMapDecodeLabel label;
  label.callsign = "K1ABC";
  label.receiveFrequencyKHz = 150.;
  label.raw = QByteArray (static_cast<int> (QMapDecodeRowSize), 'x');
  plotter->setDecodeLabels ({label});

  QSignalSpy clicked (plotter.get (), &CPlotter::decodeLabelClicked);
  QVERIFY (clicked.isValid ());
  QImage image (plotter->size (), QImage::Format_ARGB32);
  image.fill (Qt::transparent);
  plotter->render (&image);
  QPoint labelPoint;
  for (int y = 30; y < 60 && labelPoint.isNull (); ++y) {
    for (int x = 0; x < image.width () && labelPoint.isNull (); ++x) {
      auto const color = image.pixelColor (x, y);
      if (color.red () > 200 && color.green () > 200 && color.blue () < 100)
        labelPoint = {x, y};
    }
  }
  QVERIFY (!labelPoint.isNull ());
  QMouseEvent event {QEvent::MouseButtonPress, QPointF (labelPoint),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
  plotter->mousePressEvent (&event);
  QCOMPARE (clicked.size (), 1);

  clicked.clear ();
  label.receiveFrequencyKHz = 1000.;
  plotter->setDecodeLabels ({label});
  QMouseEvent outOfSpanEvent {QEvent::MouseButtonPress, QPointF (999, 35),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
  plotter->mousePressEvent (&outOfSpanEvent);
  QCOMPARE (clicked.size (), 0);

  label.receiveFrequencyKHz = 150.;
  plotter->setDecodeLabels ({label});
  QMouseEvent controlEvent {QEvent::MouseButtonPress,
                            QPointF (labelPoint.x (), labelPoint.y ()),
                            Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier};
  plotter->mousePressEvent (&controlEvent);
  QCOMPARE (clicked.size (), 0);
}

void TestPlotterInitialization::paintsLabelsInTwoDSpectrumMode()
{
  auto const plotter = std::make_unique<CPlotter> ();
  plotter->ensureSized (1000, 280);
  plotter->setFsample (96000);
  plotter->set2Dspec (true);
  plotter->SetStartFreq (100);

  QMapDecodeLabel label;
  label.callsign = "K1ABC";
  label.receiveFrequencyKHz = plotter->startFreq () + plotter->m_fSpan / 2.;
  plotter->setDecodeLabels ({label});
  plotter->show ();
  QCoreApplication::processEvents ();

  QImage image (plotter->size (), QImage::Format_ARGB32);
  image.fill (Qt::transparent);
  plotter->render (&image);

  int yellowPixels = 0;
  for (int y = 30; y < 60; ++y) {
    for (int x = 470; x < 530; ++x) {
      auto const color = image.pixelColor (x, y);
      if (color.red () > 200 && color.green () > 200 && color.blue () < 100)
        ++yellowPixels;
    }
  }
  QVERIFY (yellowPixels > 0);
}

QTEST_MAIN (TestPlotterInitialization)

#include "test_plotter_initialization.moc"
