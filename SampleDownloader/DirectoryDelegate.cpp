#include "DirectoryDelegate.hpp"

#include <QApplication>
#include <QVariant>
#include <QString>
#include <QStyle>
#include <QModelIndex>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QStyleOptionProgressBar>

void DirectoryDelegate::paint (QPainter * painter, QStyleOptionViewItem const& option
                               , QModelIndex const& index) const
{
  if (1 == index.column ())
    {
      auto progress = index.data ().toLongLong ();
      qint64 percent;
      if (progress > 0)
        {
          percent = int (progress * 100 / index.data (Qt::UserRole).toLongLong ());
        }
#if !defined (Q_OS_DARWIN)
      QStyleOptionProgressBar progress_option;
      auto control_element = QStyle::CE_ProgressBar;
      progress_option.minimum = 0;
      progress_option.maximum = 100;
      progress_option.textAlignment = Qt::AlignCenter;
      if (progress > 0)
        {
          progress_option.progress = percent;
          progress_option.textVisible = true;
        }
      else
        {
          // not started
          progress_option.progress = -1;
        }
#else
      // workaround for broken QProgressBar item delegates on macOS
      QStyleOptionViewItem progress_option;
      auto control_element = QStyle::CE_ItemViewItem;
      progress_option.displayAlignment = Qt::AlignHCenter;
      progress_option.index = index;
      progress_option.features = QStyleOptionViewItem::HasDisplay;
#endif
      progress_option.rect = option.rect;
      progress_option.state = QStyle::State_Enabled;
      progress_option.direction = QApplication::layoutDirection ();
      progress_option.fontMetrics = QApplication::fontMetrics ();
      progress_option.text = QString::number (progress > 0 ? percent : 0) + '%';
      QApplication::style ()->drawControl (control_element, &progress_option, painter);
    }
  else
    {
      QStyledItemDelegate::paint (painter, option, index);
    }
}
