#include <QtTest>
#include <QFontMetricsF>
#include <QPainter>

#include "commons.h"
#include "widgets/plotter.h"

namespace {
dec_data_t storage {};
}
dec_data_t& dec_data = storage;
QVector<QColor> g_ColorTbl (256, Qt::black);

class TestPlotterTimestamps final : public QObject
{
  Q_OBJECT
private slots:
  void rendersCompleteLabel_data ()
  {
    QTest::addColumn<int> ("pointSize");
    QTest::addColumn<int> ("timestamp");
    QTest::addColumn<double> ("period");
    for (int size : {9, 12, 18, 24}) {
      for (int alignment : {1, 2}) {
        for (double period : {15., 60.}) {
          auto const name = QString {"%1pt-%2-%3s"}.arg (size).arg (alignment).arg (period);
          QTest::newRow (qPrintable (name)) << size << alignment << period;
        }
      }
    }
  }

  void rendersCompleteLabel ()
  {
    QFETCH (int, pointSize);
    QFETCH (int, timestamp);
    QFETCH (double, period);
    auto const originalFont = QApplication::font ();
    auto font = originalFont;
    font.setPointSize (pointSize);
    QApplication::setFont (font);

    CPlotter plotter;
    plotter.setMode (period < 60. ? "FT8" : "JT65");
    plotter.setNsps (period, 6912);
    plotter.setCurrent (false);
    plotter.setCumulative (false);
    plotter.setLinearAvg (false);
    plotter.setReference (false);
    plotter.setQ65_Sync (false);
    plotter.setVHF (false);
    plotter.setSingleDecode (false);
    plotter.setFlatten (false, false);
    plotter.setWaterfallAvg (2);
    plotter.setBars (false);
    plotter.setTimestamp (timestamp);
    plotter.setDiskUTC (123456);
    plotter.setDataFromDisk (true);
    plotter.setRxBand ("40m");
    plotter.setTol (100);
    plotter.resize (700, 350);
    plotter.setAttribute (Qt::WA_DontShowOnScreen);
    plotter.show ();
    QCoreApplication::processEvents ();

    QPixmap reference (plotter.width (), 100);
    reference.fill (Qt::black);
    auto const blank = reference.toImage ();
    QPainter painter (&reference);
    auto const metrics = painter.fontMetrics ();
    QString const label = period < 60. ? "12:34:56    40m" : "12:34    40m";
    qreal const x = timestamp == 2
      ? reference.width () - 5 - QFontMetricsF {painter.font ()}.horizontalAdvance (label) : 5;
    painter.setPen (Qt::white);
    // The point overload positions the baseline without a clipping rectangle.
    painter.drawText (QPointF {x, qreal (metrics.ascent ())}, label);
    painter.end ();
    QVERIFY (reference.toImage () != blank);

    std::array<float, 2048> values {};
    values.fill (1.1e30f);
    plotter.draw (values.data (), true, false);
    values.fill (0);
    for (int row = 0; row < metrics.height (); ++row) {
      plotter.draw (values.data (), true, false);
    }
    QPixmap actual (plotter.size ());
    plotter.render (&actual);
    QApplication::setFont (originalFont);

    QCOMPARE (actual.copy (0, 30, plotter.width (), metrics.height ()).toImage (),
              reference.copy (0, 0, plotter.width (), metrics.height ()).toImage ());
    auto const separator = actual.toImage ();
    for (int x = 0; x < plotter.width (); ++x) {
      QCOMPARE (separator.pixelColor (x, 30 + metrics.height ()), QColor {Qt::green});
    }
  }
};

QTEST_MAIN (TestPlotterTimestamps)
#include "test_plotter_timestamps.moc"
