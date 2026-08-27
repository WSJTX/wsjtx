#include "SettingsDialogLayout.hpp"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QLayout>
#include <QObject>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QString>
#include <QTableView>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "ui_Configuration.h"

namespace
{
  char const scroll_area_marker[] = "wsjtxSettingsPageScrollArea";
  char const wheel_forwarder_marker[] = "wsjtxSettingsWheelForwarder";

  class WheelEventForwarder final : public QObject
  {
  public:
    WheelEventForwarder (QWidget * source, QScrollArea * target)
      : QObject {source}
      , target_ {target}
    {
      source->installEventFilter (this);
    }

  protected:
    bool eventFilter (QObject * watched, QEvent * event) override
    {
      if (event->type () == QEvent::Wheel && target_
          && (target_->horizontalScrollBar ()->maximum ()
              > target_->horizontalScrollBar ()->minimum ()
              || target_->verticalScrollBar ()->maximum ()
                 > target_->verticalScrollBar ()->minimum ()))
        {
          event->ignore ();
          QApplication::sendEvent (target_->viewport (), event);
          return true;
        }

      return QObject::eventFilter (watched, event);
    }

  private:
    QPointer<QScrollArea> target_;
  };

  QScrollArea * findPageScrollArea (QWidget * page)
  {
    if (!page)
      {
        return nullptr;
      }

    for (auto * scroll_area : page->findChildren<QScrollArea *> (QString {}, Qt::FindDirectChildrenOnly))
      {
        if (scroll_area->property (scroll_area_marker).toBool ())
          {
            return scroll_area;
          }
      }

    return nullptr;
  }

  QScrollArea * createScrollArea (QWidget * page, QWidget * content)
  {
    auto * scroll_area = new QScrollArea {page};
    scroll_area->setObjectName (page->objectName () + "_scroll_area");
    scroll_area->setProperty (scroll_area_marker, true);
    scroll_area->setFrameShape (QFrame::NoFrame);
    scroll_area->setFocusPolicy (Qt::NoFocus);
    scroll_area->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    scroll_area->setSizeAdjustPolicy (QAbstractScrollArea::AdjustIgnored);
    scroll_area->setHorizontalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    scroll_area->setVerticalScrollBarPolicy (Qt::ScrollBarAsNeeded);
    scroll_area->setWidgetResizable (true);
    scroll_area->setWidget (content);
    scroll_area->viewport ()->setFocusPolicy (Qt::NoFocus);
    scroll_area->viewport ()->setBackgroundRole (page->backgroundRole ());
    scroll_area->viewport ()->setAutoFillBackground (true);
    scroll_area->horizontalScrollBar ()->setFocusPolicy (Qt::NoFocus);
    scroll_area->verticalScrollBar ()->setFocusPolicy (Qt::NoFocus);

    QObject::connect (qApp, &QApplication::focusChanged, scroll_area,
                      [scroll_area, content] (QWidget *, QWidget * focused) {
                        if (focused && (focused == content || content->isAncestorOf (focused)))
                          {
                            QPointer<QWidget> target {focused};
                            QTimer::singleShot (0, scroll_area, [scroll_area, content, target] {
                                if (target && (target == content || content->isAncestorOf (target)))
                                  {
                                    auto const margin = qMax (4, scroll_area->fontMetrics ().height () / 2);
                                    scroll_area->ensureWidgetVisible (target, margin, margin);
                                  }
                              });
                          }
                      });

    return scroll_area;
  }

  QWidget * createContent (QWidget * page)
  {
    auto * content = new QWidget;
    content->setObjectName (page->objectName () + "_scroll_content");
    content->setFocusPolicy (Qt::NoFocus);
    content->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->setBackgroundRole (page->backgroundRole ());
    content->setAutoFillBackground (true);
    return content;
  }

  QScrollArea * installPageScrollArea (QWidget * page)
  {
    if (!page)
      {
        return nullptr;
      }

    if (auto * scroll_area = findPageScrollArea (page))
      {
        return scroll_area;
      }

    auto * content_layout = page->layout ();
    if (!content_layout)
      {
        return nullptr;
      }

    auto * content = createContent (page);
    content->setLayout (content_layout);
    content_layout->setSizeConstraint (QLayout::SetMinAndMaxSize);

    auto * scroll_area = createScrollArea (page, content);

    auto * page_layout = new QVBoxLayout {page};
    page_layout->setContentsMargins (0, 0, 0, 0);
    page_layout->setSpacing (0);
    page_layout->addWidget (scroll_area);

    return scroll_area;
  }

  void keepOneTableRowVisible (QTableView * table)
  {
    if (!table)
      {
        return;
      }

    auto const header_height = table->horizontalHeader ()->sizeHint ().height ();
    auto const row_height = table->fontMetrics ().height () + 4;
    table->setMinimumHeight (header_height + row_height + 2 * table->frameWidth ());
  }

  void forwardWheelEvents (QWidget * source, QScrollArea * target)
  {
    if (!source || !target || source->property (wheel_forwarder_marker).toBool ())
      {
        return;
      }

    source->setProperty (wheel_forwarder_marker, true);
    new WheelEventForwarder {source, target};
  }
}

void SettingsDialogLayout::install (Ui::configuration_dialog const& ui)
{
  for (auto * page : {
       ui.general_tab,
       ui.radio_tab,
       ui.audio_tab,
       ui.reporting_tab,
       ui.colors_tab,
       ui.advanced_tab,
       ui.alerts_tab,
       ui.filters_tab,
       })
    {
      installPageScrollArea (page);
    }

  keepOneTableRowVisible (ui.frequencies_table_view);
  keepOneTableRowVisible (ui.stations_table_view);
  forwardWheelEvents (ui.highlighting_list_view->viewport (),
                      findPageScrollArea (ui.colors_tab));
  ui.configuration_tabs->tabBar ()->setUsesScrollButtons (true);
}

QScrollArea * SettingsDialogLayout::pageScrollArea (QWidget * page)
{
  return findPageScrollArea (page);
}

QSize SettingsDialogLayout::boundedWindowSize (QSize requested, QSize available,
                                               QMargins frame_margins)
{
  available.rwidth () -= frame_margins.left () + frame_margins.right ();
  available.rheight () -= frame_margins.top () + frame_margins.bottom ();
  available.setWidth (qMax (1, available.width ()));
  available.setHeight (qMax (1, available.height ()));
  return requested.boundedTo (available);
}
