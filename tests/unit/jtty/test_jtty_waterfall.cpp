#include <QtTest>
#include <QMouseEvent>
#include <QSignalSpy>

#include "commons.h"
#include "widgets/plotter.h"

namespace {
dec_data_t storage;
}
dec_data_t& dec_data = storage;
QVector<QColor> g_ColorTbl (256, Qt::black);

class TestablePlotter final : public CPlotter
{
public:
  using CPlotter::mouseDoubleClickEvent;
  using CPlotter::mouseReleaseEvent;

  TestablePlotter ()
  {
    setMode ("JTTY");
    setNsps (0, 6912);
    setCurrent (false);
    setPlot2dGain (0);
    setPlot2dZero (0);
    setCumulative (true);
    setLinearAvg (false);
    setReference (false);
    setQ65_Sync (false);
    setVHF (false);
    setSingleDecode (false);
    setFlatten (false, false);
    setWaterfallAvg (2);
    setBars (false);
    setTimestamp (1);
    setDiskUTC (-1);
    setDataFromDisk (false);
    setTol (100);
    resize (700, 350);
    show ();
    QCoreApplication::processEvents ();
  }

  void push (JttyWaterfallRow const& row)
  {
    std::array<float, 2048> values {};
    if (row.gap) values.fill (1.e30f);
    setJttyRow (row);
    draw (values.data (), true, false);
  }

  void clickRow (int row)
  {
    QMouseEvent event {QEvent::MouseButtonDblClick, QPointF (100, waterfallTop () + row),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    mouseDoubleClickEvent (&event);
  }
};

class TestJttyWaterfall final : public QObject
{
  Q_OBJECT
private slots:
  void rowIdentitySurvivesAverageResizeAndReplot ()
  {
    TestablePlotter plotter;
    QSignalSpy selected (&plotter, &CPlotter::jttyDecodeAgainAtSample);
    qint64 const start = qint64 (1) << 34;
    plotter.push ({12, start, start + 6912, 180000, false});
    plotter.setWaterfallAvg (11);
    plotter.push ({12, start + 6912, start + 6912 + 38016, 183168, false});
    plotter.resize (1000, 450);
    QCoreApplication::processEvents ();
    plotter.replot ();
    plotter.clickRow (1);
    QCOMPARE (selected.size (), 1);
    QCOMPARE (selected[0][0].toULongLong (), quint64 (12));
    QCOMPARE (selected[0][1].toLongLong (), start + 6912);
    plotter.clickRow (0);
    QCOMPARE (selected.size (), 2);
    QCOMPARE (selected[1][1].toLongLong (), start + 6912 + 38016);
  }

  void gapsAndUnpopulatedRowsDoNotSelectUnrelatedAudio ()
  {
    TestablePlotter plotter;
    QSignalSpy selected (&plotter, &CPlotter::jttyDecodeAgainAtSample);
    plotter.push ({4, 0, 3456, 288, false});
    plotter.push ({4, 3456, 3456, 288, true});
    plotter.push ({5, 0, 3456, 190000, false});
    plotter.clickRow (1);
    plotter.clickRow (10);
    QCOMPARE (selected.size (), 0);
    plotter.clickRow (2);
    QCOMPARE (selected.size (), 1);
    QCOMPARE (selected[0][0].toULongLong (), quint64 (4));
    plotter.clickRow (0);
    QCOMPARE (selected.size (), 2);
    QCOMPARE (selected[1][0].toULongLong (), quint64 (5));
  }

  static WaterfallCalls::Label label (QString const& call, float freq,
                                      WaterfallCalls::Kind kind = WaterfallCalls::Kind::Cq)
  {
    WaterfallCalls::Label l;
    l.call = call;
    l.freq = freq;
    l.width = WaterfallCalls::jttySignalWidth;
    l.kind = kind;
    return l;
  }

  void callStripSitsBetweenScaleAndWaterfall ()
  {
    TestablePlotter plotter;
    QCOMPARE (plotter.waterfallTop (), 30 + CPlotter::callStripHeight);
    plotter.setMode ("FT8");
    QCOMPARE (plotter.waterfallTop (), 30);        // other modes: no strip
    plotter.setMode ("JTTY");
    QCOMPARE (plotter.waterfallTop (), 30 + CPlotter::callStripHeight);
  }

  void labelsAreShownInTheStripAndClickSelectsTheirSignal ()
  {
    TestablePlotter plotter;
    plotter.resize (1500, 400);
    QCoreApplication::processEvents ();
    plotter.setCallLabels ({label ("DL3WDG", 200), label ("K1ABC", 500),
                            label ("K2DEF", 800)});
    QCOMPARE (plotter.shownCallLabels (), (QStringList {"DL3WDG", "K1ABC", "K2DEF"}));
    for (auto const& call : plotter.shownCallLabels ()) {
      auto const rect = plotter.callLabelRect (call);
      QVERIFY (rect.top () >= 30);
      QVERIFY (rect.bottom () < 30 + CPlotter::callStripHeight);
    }
    QSignalSpy freq (&plotter, &CPlotter::setFreq1);
    QSignalSpy clicked (&plotter, &CPlotter::jttyCallClicked);
    auto const rect = plotter.callLabelRect ("K1ABC");
    QMouseEvent click {QEvent::MouseButtonRelease, QPointF (rect.center ()),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    plotter.mouseReleaseEvent (&click);
    QCOMPARE (freq.size (), 1);
    QCOMPARE (freq[0][0].toInt (), 500);          // that signal's frequency, not the pixel
    QCOMPARE (clicked.size (), 1);                 // the call goes with it, for DX Call
    QCOMPARE (clicked[0][0].toString (), QString ("K1ABC"));
    QCOMPARE (clicked[0][1].toInt (), 500);
    plotter.setCallLabels ({});
    QVERIFY (plotter.shownCallLabels ().isEmpty ());
  }

  void noStripLabelsOutsideJtty ()
  {
    TestablePlotter plotter;
    plotter.resize (1500, 400);
    QCoreApplication::processEvents ();
    plotter.setCallLabels ({label ("DL3WDG", 1000)});
    QCOMPARE (plotter.shownCallLabels ().size (), 1);
    plotter.setMode ("FT8");
    QCOMPARE (plotter.waterfallTop (), 30);
  }
};

QTEST_MAIN (TestJttyWaterfall)
#include "test_jtty_waterfall.moc"
