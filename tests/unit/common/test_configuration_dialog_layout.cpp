#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTest>
#include <QWheelEvent>

#include "models/DecodeHighlightingModel.hpp"
#include "widgets/SettingsDialogLayout.hpp"

#include "ui_Configuration.h"

namespace
{
  struct DialogFixture
  {
    explicit DialogFixture (int font_delta = 0)
    {
      if (font_delta)
        {
          auto font = dialog.font ();
          font.setPointSize (font.pointSize () + font_delta);
          dialog.setFont (font);
        }

      ui.setupUi (&dialog);
      highlighting_model.set_font (dialog.font ());
      ui.highlighting_list_view->setModel (&highlighting_model);
      SettingsDialogLayout::install (ui);
    }

    QDialog dialog;
    Ui::configuration_dialog ui;
    DecodeHighlightingModel highlighting_model;
  };

  QList<QWidget *> scrollablePages (Ui::configuration_dialog const& ui)
  {
    return {
      ui.general_tab,
      ui.radio_tab,
      ui.audio_tab,
      ui.reporting_tab,
      ui.colors_tab,
      ui.advanced_tab,
      ui.alerts_tab,
      ui.filters_tab,
    };
  }

  QList<QWidget *> allPages (Ui::configuration_dialog const& ui)
  {
    return {
      ui.general_tab,
      ui.radio_tab,
      ui.audio_tab,
      ui.tx_macros_tab,
      ui.reporting_tab,
      ui.frequencies_tab,
      ui.colors_tab,
      ui.advanced_tab,
      ui.alerts_tab,
      ui.filters_tab,
    };
  }

  void settleLayouts ()
  {
    QCoreApplication::sendPostedEvents ();
    QApplication::processEvents ();
  }

  void showDialog (QDialog& dialog, QSize size)
  {
    dialog.ensurePolished ();
    dialog.layout ()->activate ();
    dialog.resize (size);
    dialog.show ();
    settleLayouts ();
  }

  QRect geometryIn (QWidget const * widget, QWidget const * ancestor)
  {
    return {widget->mapTo (ancestor, {}), widget->size ()};
  }

  bool isFullyInside (QWidget const * widget, QWidget const * ancestor)
  {
    return ancestor->contentsRect ().contains (geometryIn (widget, ancestor));
  }

  bool isFullyVisible (QWidget const * widget, QScrollArea const * scroll_area)
  {
    return scroll_area->viewport ()->rect ().contains (
      geometryIn (widget, scroll_area->viewport ()));
  }

  QString sizeText (QSize size)
  {
    return QString {"%1x%2"}.arg (size.width ()).arg (size.height ());
  }

  QString scrollDiagnostics (QWidget const * page, QDialog const& dialog,
                             QScrollArea const * scroll_area)
  {
    auto const * content = scroll_area->widget ();
    return QString {"%1 style=%2 font=%3/%4pt dialog=%5 viewport=%6 "
                    "content=%7 contentHint=%8 contentMinimumHint=%9 ranges=%10x%11"}
      .arg (page->objectName (), qApp->style ()->objectName (), dialog.font ().family ())
      .arg (dialog.font ().pointSizeF ())
      .arg (sizeText (dialog.size ()), sizeText (scroll_area->viewport ()->size ()),
            sizeText (content->size ()), sizeText (content->sizeHint ()),
            sizeText (content->minimumSizeHint ()))
      .arg (scroll_area->horizontalScrollBar ()->maximum ())
      .arg (scroll_area->verticalScrollBar ()->maximum ());
  }

  void sendWheelEvent (QWidget * target, QPoint angle_delta)
  {
    auto const local_position = QPointF {target->rect ().center ()};
    auto const global_position = QPointF {target->mapToGlobal (local_position.toPoint ())};
    QWheelEvent event {local_position, global_position, {}, angle_delta,
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
    QApplication::sendEvent (target, &event);
    settleLayouts ();
  }
}

class TestConfigurationDialogLayout final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void pagesRetainIdentityAndScrollingPolicy ();
  void windowSizeIsBoundedByAvailableGeometry ();
  void compactControlsKeepNaturalGeometry_data ();
  void compactControlsKeepNaturalGeometry ();
  void constrainedDialogKeepsNavigationAndActionsReachable_data ();
  void constrainedDialogKeepsNavigationAndActionsReachable ();
  void preferredDialogSizeKeepsPagesUsable_data ();
  void preferredDialogSizeKeepsPagesUsable ();
  void keyboardFocusRevealsOffscreenControls ();
  void wheelEventsScrollThePage ();
  void colorsPageOwnsVerticalScrolling ();
};

void TestConfigurationDialogLayout::pagesRetainIdentityAndScrollingPolicy ()
{
  DialogFixture fixture;
  auto& ui = fixture.ui;

  auto const pages = allPages (ui);
  QStringList const page_names {
    "general_tab",
    "radio_tab",
    "audio_tab",
    "tx_macros_tab",
    "reporting_tab",
    "frequencies_tab",
    "colors_tab",
    "advanced_tab",
    "alerts_tab",
    "filters_tab",
  };

  QCOMPARE (ui.configuration_tabs->count (), page_names.size ());
  for (int index = 0; index < pages.size (); ++index)
    {
      QCOMPARE (ui.configuration_tabs->widget (index), pages.at (index));
      QCOMPARE (pages.at (index)->objectName (), page_names.at (index));
      QVERIFY (!ui.configuration_tabs->tabText (index).isEmpty ());
    }

  SettingsDialogLayout::install (ui);
  for (auto * page : scrollablePages (ui))
    {
      auto * scroll_area = SettingsDialogLayout::pageScrollArea (page);
      QVERIFY (scroll_area);
      QCOMPARE (scroll_area->parentWidget (), page);
      QCOMPARE (scroll_area->objectName (), page->objectName () + "_scroll_area");
      QCOMPARE (scroll_area->focusPolicy (), Qt::NoFocus);
      QCOMPARE (scroll_area->viewport ()->focusPolicy (), Qt::NoFocus);
      QCOMPARE (scroll_area->horizontalScrollBar ()->focusPolicy (), Qt::NoFocus);
      QCOMPARE (scroll_area->verticalScrollBar ()->focusPolicy (), Qt::NoFocus);
      QCOMPARE (scroll_area->frameShape (), QFrame::NoFrame);
      QCOMPARE (scroll_area->horizontalScrollBarPolicy (), Qt::ScrollBarAsNeeded);
      QCOMPARE (scroll_area->verticalScrollBarPolicy (), Qt::ScrollBarAsNeeded);
      QVERIFY (scroll_area->widgetResizable ());
      QVERIFY (scroll_area->widget ());
      QCOMPARE (scroll_area->widget ()->focusPolicy (), Qt::NoFocus);
      QCOMPARE (page->findChildren<QScrollArea *> (QString {}, Qt::FindDirectChildrenOnly).size (), 1);
      QVERIFY (scroll_area->widget ()->findChildren<QScrollArea *>().isEmpty ());
    }

  auto * general_scroll_area = SettingsDialogLayout::pageScrollArea (ui.general_tab);
  QVERIFY (general_scroll_area->widget ()->isAncestorOf (ui.callsign_line_edit));
  QCOMPARE (ui.callsign_label->buddy (), ui.callsign_line_edit);
  auto * colors_scroll_area = SettingsDialogLayout::pageScrollArea (ui.colors_tab);
  QVERIFY (colors_scroll_area->widget ()->isAncestorOf (ui.highlighting_list_view));
  QCOMPARE (ui.highlighting_list_view->verticalScrollBarPolicy (), Qt::ScrollBarAlwaysOff);
  QCOMPARE (SettingsDialogLayout::pageScrollArea (ui.tx_macros_tab), nullptr);
  QCOMPARE (SettingsDialogLayout::pageScrollArea (ui.frequencies_tab), nullptr);
  QVERIFY (ui.configuration_tabs->tabBar ()->usesScrollButtons ());
}

void TestConfigurationDialogLayout::windowSizeIsBoundedByAvailableGeometry ()
{
  QMargins const frame_margins {10, 20, 10, 20};
  QSize const available {1200, 900};

  QCOMPARE (SettingsDialogLayout::boundedWindowSize ({800, 600}, available, frame_margins),
            QSize (800, 600));
  QCOMPARE (SettingsDialogLayout::boundedWindowSize ({1400, 1000}, available, frame_margins),
            QSize (1180, 860));
  QCOMPARE (SettingsDialogLayout::boundedWindowSize ({1400, 500}, available, frame_margins),
            QSize (1180, 500));
  QCOMPARE (SettingsDialogLayout::boundedWindowSize ({100, 100}, {10, 10}, frame_margins),
            QSize (1, 1));
}

void TestConfigurationDialogLayout::compactControlsKeepNaturalGeometry_data ()
{
  QTest::addColumn<int> ("font_delta");
  QTest::newRow ("default-font") << 0;
  QTest::newRow ("larger-accessibility-font") << 4;
}

void TestConfigurationDialogLayout::compactControlsKeepNaturalGeometry ()
{
  QFETCH (int, font_delta);

  DialogFixture fixture {font_delta};
  auto& dialog = fixture.dialog;
  auto& ui = fixture.ui;
  showDialog (dialog, {640, 480});

  ui.configuration_tabs->setCurrentWidget (ui.general_tab);
  settleLayouts ();
  QVERIFY (qAbs (ui.grid_line_edit->geometry ().center ().y ()
                 - ui.use_dynamic_grid->geometry ().center ().y ()) <= 2);

  ui.configuration_tabs->setCurrentWidget (ui.advanced_tab);
  settleLayouts ();
  for (auto const& group_and_controls : {
         qMakePair (ui.groupBox_10,
                    QList<QWidget *> {ui.cbx2ToneSpacing, ui.cbx4ToneSpacing}),
         qMakePair (ui.groupBox_7,
                    QList<QWidget *> {ui.rbLowSidelobes, ui.rbMaxSensitivity}),
       })
    {
      auto * group = group_and_controls.first;
      QCOMPARE (group->sizePolicy ().verticalPolicy (), QSizePolicy::Minimum);
      QVERIFY (group->height () >= group->sizeHint ().height ());
      for (auto * control : group_and_controls.second)
        {
          QVERIFY (isFullyInside (control, group));
        }
    }
}

void TestConfigurationDialogLayout::constrainedDialogKeepsNavigationAndActionsReachable_data ()
{
  QTest::addColumn<int> ("font_delta");
  QTest::newRow ("default-font") << 0;
  QTest::newRow ("larger-accessibility-font") << 4;
}

void TestConfigurationDialogLayout::constrainedDialogKeepsNavigationAndActionsReachable ()
{
  QFETCH (int, font_delta);

  QWidget host;
  DialogFixture fixture {font_delta};
  auto& dialog = fixture.dialog;
  auto& ui = fixture.ui;
  dialog.setParent (&host, Qt::Widget);
  host.show ();
  QSize const constrained_size {640, 480};
  showDialog (dialog, constrained_size);

  QCOMPARE (dialog.size (), constrained_size);
  QVERIFY (ui.configuration_tabs->isVisibleTo (&dialog));
  QVERIFY (isFullyInside (ui.configuration_tabs, &dialog));
  QVERIFY (ui.configuration_dialog_button_box->isVisibleTo (&dialog));
  QVERIFY (isFullyInside (ui.configuration_dialog_button_box, &dialog));

  auto * ok = ui.configuration_dialog_button_box->button (QDialogButtonBox::Ok);
  auto * cancel = ui.configuration_dialog_button_box->button (QDialogButtonBox::Cancel);
  QVERIFY (ok);
  QVERIFY (cancel);
  auto const button_box_geometry = geometryIn (ui.configuration_dialog_button_box, &dialog);

  for (int index = 0; index < ui.configuration_tabs->count (); ++index)
    {
      ui.configuration_tabs->setCurrentIndex (index);
      settleLayouts ();

      QCOMPARE (ui.configuration_tabs->currentWidget (), allPages (ui).at (index));
      QVERIFY (ui.configuration_tabs->currentWidget ()->isVisibleTo (&dialog));
      QVERIFY (ui.configuration_tabs->tabBar ()->rect ().intersects (
        ui.configuration_tabs->tabBar ()->tabRect (index)));
      QVERIFY (isFullyInside (ui.configuration_dialog_button_box, &dialog));
      QCOMPARE (geometryIn (ui.configuration_dialog_button_box, &dialog), button_box_geometry);
      QVERIFY (ok->isVisibleTo (&dialog));
      QVERIFY (cancel->isVisibleTo (&dialog));
      QVERIFY (isFullyInside (ok, &dialog));
      QVERIFY (isFullyInside (cancel, &dialog));

      if (auto * scroll_area = SettingsDialogLayout::pageScrollArea (
            ui.configuration_tabs->currentWidget ()))
        {
          auto const minimum_hint = scroll_area->widget ()->minimumSizeHint ();
          auto const message = scrollDiagnostics (ui.configuration_tabs->currentWidget (),
                                                  dialog, scroll_area);
          QVERIFY2 (scroll_area->widget ()->width () >= minimum_hint.width (),
                    qPrintable (message));
          QVERIFY2 (scroll_area->widget ()->height () >= minimum_hint.height (),
                    qPrintable (message));
        }
    }

  ui.configuration_tabs->setCurrentWidget (ui.frequencies_tab);
  settleLayouts ();
  for (auto * table : {ui.frequencies_table_view, ui.stations_table_view})
    {
      QVERIFY (table->height () >= table->minimumHeight ());
      QVERIFY (table->viewport ()->height () >= table->fontMetrics ().height ());
      QVERIFY (isFullyInside (table, ui.frequencies_tab));
    }

  ui.configuration_tabs->setCurrentWidget (ui.advanced_tab);
  settleLayouts ();
  auto * advanced_scroll_area = SettingsDialogLayout::pageScrollArea (ui.advanced_tab);
  QVERIFY (advanced_scroll_area->verticalScrollBar ()->maximum () > 0);
  advanced_scroll_area->verticalScrollBar ()->setValue (
    advanced_scroll_area->verticalScrollBar ()->maximum ());
  advanced_scroll_area->horizontalScrollBar ()->setValue (
    advanced_scroll_area->horizontalScrollBar ()->maximum ());
  settleLayouts ();
  QCOMPARE (geometryIn (ui.configuration_dialog_button_box, &dialog), button_box_geometry);

  dialog.resize ({420, 480});
  settleLayouts ();
  QVERIFY (ui.configuration_tabs->tabBar ()->width ()
           < ui.configuration_tabs->tabBar ()->sizeHint ().width ());
  for (int index = 0; index < ui.configuration_tabs->count (); ++index)
    {
      ui.configuration_tabs->setCurrentIndex (index);
      settleLayouts ();
      QVERIFY (ui.configuration_tabs->tabBar ()->rect ().intersects (
        ui.configuration_tabs->tabBar ()->tabRect (index)));
      QVERIFY (isFullyInside (ui.configuration_dialog_button_box, &dialog));
    }
}

void TestConfigurationDialogLayout::preferredDialogSizeKeepsPagesUsable_data ()
{
  QTest::addColumn<int> ("font_delta");
  QTest::newRow ("default-font") << 0;
  QTest::newRow ("larger-accessibility-font") << 4;
}

void TestConfigurationDialogLayout::preferredDialogSizeKeepsPagesUsable ()
{
  QFETCH (int, font_delta);

  QWidget host;
  DialogFixture fixture {font_delta};
  auto& dialog = fixture.dialog;
  auto& ui = fixture.ui;
  dialog.setParent (&host, Qt::Widget);
  host.show ();
  showDialog (dialog, {827, 705});
  auto const preferred_size = SettingsDialogLayout::preferredWindowSize (dialog, ui);
  QVERIFY (preferred_size.width () >= dialog.sizeHint ().width ());
  QVERIFY (preferred_size.height () >= dialog.sizeHint ().height ());
  showDialog (dialog, preferred_size);

  int vertically_scrolling_pages = 0;
  for (auto * page : scrollablePages (ui))
    {
      ui.configuration_tabs->setCurrentWidget (page);
      settleLayouts ();
      auto * scroll_area = SettingsDialogLayout::pageScrollArea (page);
      auto const message = scrollDiagnostics (page, dialog, scroll_area);
      QVERIFY2 (scroll_area->horizontalScrollBar ()->maximum () == 0,
                qPrintable (message));
      if (scroll_area->verticalScrollBar ()->maximum () > 0)
        {
          ++vertically_scrolling_pages;
        }
    }
  QVERIFY (vertically_scrolling_pages <= 1);

  ui.configuration_tabs->setCurrentWidget (ui.general_tab);
  settleLayouts ();
  QVERIFY (ui.station_group_box->height () <= ui.station_group_box->sizeHint ().height () + 2);

  dialog.resize (preferred_size.width (), qMax (480, preferred_size.height () - 200));
  settleLayouts ();
  for (auto * page : scrollablePages (ui))
    {
      ui.configuration_tabs->setCurrentWidget (page);
      settleLayouts ();
      auto * scroll_area = SettingsDialogLayout::pageScrollArea (page);
      auto const message = scrollDiagnostics (page, dialog, scroll_area);
      QVERIFY2 (scroll_area->horizontalScrollBar ()->maximum () == 0,
                qPrintable (message));
    }
}

void TestConfigurationDialogLayout::keyboardFocusRevealsOffscreenControls ()
{
  DialogFixture fixture;
  auto& dialog = fixture.dialog;
  auto& ui = fixture.ui;
  ui.configuration_tabs->setCurrentWidget (ui.advanced_tab);
  showDialog (dialog, {620, 360});
  QApplication::setActiveWindow (&dialog);
  settleLayouts ();

  auto * advanced_scroll_area = SettingsDialogLayout::pageScrollArea (ui.advanced_tab);
  auto * vertical_scroll_bar = advanced_scroll_area->verticalScrollBar ();
  QVERIFY (vertical_scroll_bar->maximum () > 0);
  vertical_scroll_bar->setValue (vertical_scroll_bar->minimum ());
  advanced_scroll_area->horizontalScrollBar ()->setValue (
    advanced_scroll_area->horizontalScrollBar ()->minimum ());
  settleLayouts ();

  QVERIFY (!isFullyVisible (ui.pbTestCloudlog, advanced_scroll_area));
  auto const button_box_geometry = geometryIn (ui.configuration_dialog_button_box, &dialog);
  ui.pbTestCloudlog->setFocus (Qt::TabFocusReason);

  QTRY_COMPARE (QApplication::focusWidget (), ui.pbTestCloudlog);
  QTRY_VERIFY (vertical_scroll_bar->value () > vertical_scroll_bar->minimum ());
  QTRY_VERIFY (isFullyVisible (ui.pbTestCloudlog, advanced_scroll_area));
  QCOMPARE (geometryIn (ui.configuration_dialog_button_box, &dialog), button_box_geometry);

  ui.configuration_tabs->setCurrentWidget (ui.filters_tab);
  dialog.resize ({420, 480});
  settleLayouts ();
  auto * filters_scroll_area = SettingsDialogLayout::pageScrollArea (ui.filters_tab);
  auto * horizontal_scroll_bar = filters_scroll_area->horizontalScrollBar ();
  QVERIFY (horizontal_scroll_bar->maximum () > 0);
  auto * ok = ui.configuration_dialog_button_box->button (QDialogButtonBox::Ok);
  ok->setFocus (Qt::TabFocusReason);
  QTRY_COMPARE (QApplication::focusWidget (), ok);
  horizontal_scroll_bar->setValue (horizontal_scroll_bar->maximum ());
  filters_scroll_area->verticalScrollBar ()->setValue (
    filters_scroll_area->verticalScrollBar ()->minimum ());
  settleLayouts ();
  auto const filters_button_box_geometry = geometryIn (
    ui.configuration_dialog_button_box, &dialog);

  auto * left_side_control = ui.Territory1;
  auto const initial_horizontal_position = horizontal_scroll_bar->value ();
  QVERIFY (!isFullyVisible (left_side_control, filters_scroll_area));
  left_side_control->setFocus (Qt::TabFocusReason);
  QTRY_COMPARE (QApplication::focusWidget (), left_side_control);
  QTRY_VERIFY (horizontal_scroll_bar->value () < initial_horizontal_position);
  QTRY_VERIFY (isFullyVisible (left_side_control, filters_scroll_area));
  QCOMPARE (geometryIn (ui.configuration_dialog_button_box, &dialog),
            filters_button_box_geometry);
}

void TestConfigurationDialogLayout::wheelEventsScrollThePage ()
{
  DialogFixture fixture;
  auto& dialog = fixture.dialog;
  auto& ui = fixture.ui;
  ui.configuration_tabs->setCurrentWidget (ui.advanced_tab);
  showDialog (dialog, {620, 360});

  auto * scroll_area = SettingsDialogLayout::pageScrollArea (ui.advanced_tab);
  auto * scroll_bar = scroll_area->verticalScrollBar ();
  QVERIFY (scroll_bar->maximum () > 0);
  auto const button_box_geometry = geometryIn (ui.configuration_dialog_button_box, &dialog);

  scroll_bar->setValue (scroll_bar->minimum ());
  sendWheelEvent (scroll_area->viewport (), {0, -120});
  QVERIFY (scroll_bar->value () > scroll_bar->minimum ());
  QCOMPARE (geometryIn (ui.configuration_dialog_button_box, &dialog), button_box_geometry);

}

void TestConfigurationDialogLayout::colorsPageOwnsVerticalScrolling ()
{
  DialogFixture fixture;
  auto& dialog = fixture.dialog;
  auto& ui = fixture.ui;
  ui.configuration_tabs->setCurrentWidget (ui.colors_tab);
  showDialog (dialog, {620, 360});

  auto * page_scroll_area = SettingsDialogLayout::pageScrollArea (ui.colors_tab);
  auto * page_scroll_bar = page_scroll_area->verticalScrollBar ();
  QCOMPARE (ui.highlighting_list_view->model ()->rowCount (), 16);
  QCOMPARE (ui.highlighting_list_view->verticalScrollBarPolicy (), Qt::ScrollBarAlwaysOff);
  QCOMPARE (ui.highlighting_list_view->verticalScrollBar ()->maximum (), 0);
  QVERIFY (page_scroll_bar->maximum () > 0);

  page_scroll_bar->setValue (page_scroll_bar->minimum ());
  sendWheelEvent (ui.highlighting_list_view->viewport (), {0, -120});
  QVERIFY (page_scroll_bar->value () > page_scroll_bar->minimum ());
  QCOMPARE (ui.highlighting_list_view->verticalScrollBar ()->value (), 0);
}

QTEST_MAIN (TestConfigurationDialogLayout)

#include "test_configuration_dialog_layout.moc"
