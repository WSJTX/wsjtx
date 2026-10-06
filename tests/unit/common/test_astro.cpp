#include <QtTest>
#include <QCheckBox>
#include <QLabel>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>

#include "widgets/astro.h"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () { return 0; }

class TestAstro : public QObject
{
  Q_OBJECT

private slots:
  void restores_visible_position ();
  void recovers_unavailable_position_data ();
  void recovers_unavailable_position ();
  void keeps_title_bar_visible_after_layout_growth ();
};

void TestAstro::restores_visible_position ()
{
  bool checked = false;
  for (auto screen : QGuiApplication::screens ())
    {
      for (auto tracking : {false, true})
        {
          QTemporaryDir directory;
          QVERIFY (directory.isValid ());
          QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
          auto const saved = screen->availableGeometry ().topLeft () + QPoint {20, 30};
          settings.setValue ("Astro/window/pos", saved);
          settings.setValue ("Astro/DopplerTracking", tracking);
          Astro window {&settings, nullptr};
          window.findChild<QLabel *> ("text_label")->setText (QString {"UTC: 12:34:56\n"}.repeated (25));
          window.show ();
          QCoreApplication::processEvents ();
          if (!screen->availableGeometry ().contains (QRect {saved, window.frameGeometry ().size ()}))
            continue;
          checked = true;
          QCOMPARE (window.pos (), saved);
        }
    }
  if (!checked) QSKIP ("No screen has enough available space for the populated dialog");
}

void TestAstro::recovers_unavailable_position_data ()
{
  QTest::addColumn<QPoint> ("saved");
  QTest::addColumn<bool> ("tracking");
  auto bounds = QGuiApplication::primaryScreen ()->availableGeometry ();
  for (auto screen : QGuiApplication::screens ())
    bounds = bounds.united (screen->availableGeometry ());
  for (auto tracking : {false, true})
    {
      auto const suffix = tracking ? "-tracking" : "-compact";
      QTest::newRow (qPrintable (QString {"disconnected-right"} + suffix))
        << QPoint {bounds.right () + 1000, bounds.top ()} << tracking;
      QTest::newRow (qPrintable (QString {"disconnected-left"} + suffix))
        << QPoint {bounds.left () - 1000, bounds.top ()} << tracking;
      QTest::newRow (qPrintable (QString {"above-screens"} + suffix))
        << QPoint {bounds.left (), bounds.top () - 1000} << tracking;
      QTest::newRow (qPrintable (QString {"below-screens"} + suffix))
        << QPoint {bounds.left (), bounds.bottom () + 1000} << tracking;
      QTest::newRow (qPrintable (QString {"hidden-title-bar"} + suffix))
        << QPoint {bounds.left (), bounds.top () - 10} << tracking;
      QTest::newRow (qPrintable (QString {"clipped-right-edge"} + suffix))
        << QPoint {bounds.right () - 10, bounds.top ()} << tracking;
      QTest::newRow (qPrintable (QString {"clipped-bottom-edge"} + suffix))
        << QPoint {bounds.left (), bounds.bottom () - 10} << tracking;
    }
}

void TestAstro::recovers_unavailable_position ()
{
  QFETCH (QPoint, saved);
  QFETCH (bool, tracking);
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  settings.setValue ("Astro/window/pos", saved);
  settings.setValue ("Astro/DopplerTracking", tracking);
  QPoint recovered;
  {
    Astro window {&settings, nullptr};
    window.findChild<QLabel *> ("text_label")->setText (QString {"UTC: 12:34:56\n"}.repeated (25));
    window.show ();
    QCoreApplication::processEvents ();
    bool accessible = false;
    for (auto screen : QGuiApplication::screens ())
      {
        auto const available = screen->availableGeometry ();
        auto const frame = window.frameGeometry ();
        auto const visible = frame.intersected (available);
        accessible |= available.contains (frame.topLeft ())
          && visible.size () == frame.size ().boundedTo (available.size ());
      }
    QVERIFY (accessible);
    QVERIFY (window.pos () != saved);
    recovered = window.pos ();
    window.close ();
    QCOMPARE (settings.value ("Astro/window/pos").toPoint (), recovered);
  }
  Astro restored {&settings, nullptr};
  restored.findChild<QLabel *> ("text_label")->setText (QString {"UTC: 12:34:56\n"}.repeated (25));
  restored.show ();
  QCoreApplication::processEvents ();
  QCOMPARE (restored.pos (), recovered);
}

void TestAstro::keeps_title_bar_visible_after_layout_growth ()
{
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  Astro window {&settings, nullptr};
  window.show ();
  QCoreApplication::processEvents ();
  auto const available = QGuiApplication::primaryScreen ()->availableGeometry ();
  window.move (available.bottomRight () - QPoint {window.frameGeometry ().width () - 1,
                                                window.frameGeometry ().height () - 1});
  window.findChild<QLabel *> ("text_label")->setText (QString {"UTC: 12:34:56\n"}.repeated (25));
  window.findChild<QCheckBox *> ("cbDopplerTracking")->setChecked (true);
  QCoreApplication::processEvents ();
  auto const frame = window.frameGeometry ();
  QVERIFY (available.contains (frame.topLeft ()));
  QCOMPARE (frame.intersected (available).size (), frame.size ().boundedTo (available.size ()));
}

QTEST_MAIN (TestAstro)
#include "test_astro.moc"
