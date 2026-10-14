#include <QtTest>
#include <QCheckBox>
#include <QLabel>
#include <QScreen>
#include <QRegion>
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
  void preserves_position_across_available_screens ();
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
          window.findChild<QLabel *> ("text_label")->setText (QString {"UTC: 12:34:56\n"}.repeated (10));
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

void TestAstro::preserves_position_across_available_screens ()
{
  if (QGuiApplication::screens ().size () < 2)
    QSKIP ("No multi-screen desktop is available");
  QTemporaryDir directory;
  QVERIFY (directory.isValid ());
  QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
  QPoint saved;
  QRegion available_region;
  {
    Astro window {&settings, nullptr};
    auto const label = window.findChild<QLabel *> ("text_label");
    auto const tracking = window.findChild<QCheckBox *> ("cbDopplerTracking");
    label->setText (QString {"UTC: 12:34:56\n"}.repeated (10));
    window.show ();
    QCoreApplication::processEvents ();
    auto const compact_size = window.frameGeometry ().size ();
    tracking->setChecked (true);
    QCoreApplication::processEvents ();
    auto const expanded_size = window.frameGeometry ().size ();
    QVERIFY (expanded_size != compact_size);
    auto const frame_offset = window.frameGeometry ().topLeft () - window.pos ();
    bool located = false;
    for (auto screen : QGuiApplication::screens ())
      {
        auto const siblings = screen->virtualSiblings ();
        QRegion desktop;
        for (auto sibling : siblings) desktop += sibling->availableGeometry ();
        auto const straddles = [&] (QRect const& frame) {
          if (!QRegion {frame}.subtracted (desktop).isEmpty ()) return false;
          for (auto sibling : siblings)
            if (sibling->availableGeometry ().contains (frame)) return false;
          return true;
        };
        auto const first = screen->availableGeometry ();
        for (auto sibling : siblings)
          {
            if (sibling == screen) continue;
            auto const second = sibling->availableGeometry ();
            QPoint top_left;
            if (first.right () + 1 == second.left ())
              top_left = {second.left () - compact_size.width () / 2,
                          qMax (first.top (), second.top ()) + 20};
            else if (first.bottom () + 1 == second.top ())
              top_left = {qMax (first.left (), second.left ()) + 20,
                          second.top () - compact_size.height () / 2};
            else continue;
            if (!straddles (QRect {top_left, compact_size})
                || !straddles (QRect {top_left, expanded_size})) continue;
            saved = top_left - frame_offset;
            available_region = desktop;
            located = true;
            break;
          }
        if (located) break;
      }
    if (!located) QSKIP ("No available screen boundary fits both dialog layouts");
    tracking->setChecked (false);
    QCoreApplication::processEvents ();
    window.hide ();
    window.move (saved);
    window.show ();
    QCoreApplication::processEvents ();
    QCOMPARE (window.pos (), saved);
    QVERIFY (QRegion {window.frameGeometry ()}.subtracted (available_region).isEmpty ());
    tracking->setChecked (true);
    QCoreApplication::processEvents ();
    QVERIFY (window.frameGeometry ().size () != compact_size);
    QCOMPARE (window.pos (), saved);
    QVERIFY (QRegion {window.frameGeometry ()}.subtracted (available_region).isEmpty ());
    window.close ();
    QCOMPARE (settings.value ("Astro/window/pos").toPoint (), saved);
  }
  QCOMPARE (settings.value ("Astro/window/pos").toPoint (), saved);
  Astro restored {&settings, nullptr};
  restored.findChild<QLabel *> ("text_label")->setText (QString {"UTC: 12:34:56\n"}.repeated (10));
  restored.show ();
  QCoreApplication::processEvents ();
  QCOMPARE (restored.pos (), saved);
  QVERIFY (QRegion {restored.frameGeometry ()}.subtracted (available_region).isEmpty ());
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
  auto available = QGuiApplication::primaryScreen ()->availableGeometry ();
  for (auto screen : QGuiApplication::screens ())
    if (screen->availableGeometry ().bottom () > available.bottom ())
      available = screen->availableGeometry ();
  auto const initial_height = window.frameGeometry ().height ();
  window.move (available.bottomRight () - QPoint {window.frameGeometry ().width () - 1,
                                                initial_height - 1});
  window.findChild<QLabel *> ("text_label")->setText (QString {"UTC: 12:34:56\n"}.repeated (25));
  window.findChild<QCheckBox *> ("cbDopplerTracking")->setChecked (true);
  QCoreApplication::processEvents ();
  auto const frame = window.frameGeometry ();
  QVERIFY (frame.height () > initial_height);
  bool accessible = false;
  for (auto screen : QGuiApplication::screens ())
    {
      auto const bounds = screen->availableGeometry ();
      accessible |= bounds.contains (frame.topLeft ())
        && frame.intersected (bounds).size () == frame.size ().boundedTo (bounds.size ());
    }
  QVERIFY (accessible);
}

QTEST_MAIN (TestAstro)
#include "test_astro.moc"
