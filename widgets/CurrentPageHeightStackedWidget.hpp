// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CURRENT_PAGE_HEIGHT_STACKED_WIDGET_HPP
#define CURRENT_PAGE_HEIGHT_STACKED_WIDGET_HPP

#include <QStackedWidget>

class CurrentPageHeightStackedWidget final : public QStackedWidget
{
public:
  explicit CurrentPageHeightStackedWidget (QWidget * parent = nullptr)
    : QStackedWidget {parent}
  {
    connect (this, &QStackedWidget::currentChanged, this, [this] {updateGeometry ();});
  }

  QSize sizeHint () const override
  {
    return withCurrentPageHeight (QStackedWidget::sizeHint (), false);
  }

  QSize minimumSizeHint () const override
  {
    return withCurrentPageHeight (QStackedWidget::minimumSizeHint (), true);
  }

private:
  QSize withCurrentPageHeight (QSize hint, bool minimum) const
  {
    if (auto * page = currentWidget ())
      {
        auto const pageHint = minimum ? page->minimumSizeHint () : page->sizeHint ();
        if (pageHint.isValid ()) hint.setHeight (pageHint.height ());
      }
    return hint;
  }
};

#endif
