#include "SQLiteDateTimeDelegate.hpp"

#include <QDateTimeEdit>
#include <QDateTime>
#include <QLocale>

SQLiteDateTimeDelegate::SQLiteDateTimeDelegate (QObject * parent)
  : QStyledItemDelegate {parent}
{
}

QWidget * SQLiteDateTimeDelegate::createEditor (QWidget * parent, QStyleOptionViewItem const&
                                          , QModelIndex const&) const
{
  auto * editor = new QDateTimeEdit {parent};
  editor->setCalendarPopup (true);
  editor->setDisplayFormat (QLocale {}.dateFormat (QLocale::ShortFormat) + " hh:mm:ss");
  editor->setFrame (false);
  return editor;
}

void SQLiteDateTimeDelegate::setEditorData (QWidget * editor, QModelIndex const& index) const
{
  auto const& value = index.model ()->data (index, Qt::EditRole);
  if (value.isValid () && !value.isNull ())
    {
      static_cast<QDateTimeEdit *> (editor)->setDateTime (QDateTime::fromMSecsSinceEpoch (value.toULongLong () * 1000ull, Qt::UTC));
    }
}

void SQLiteDateTimeDelegate::setModelData (QWidget * editor, QAbstractItemModel * model, QModelIndex const& index) const
{
  QVariant data;
  auto const& value = static_cast<QDateTimeEdit *> (editor)->dateTime ();
  if (value.isValid () && !value.isNull ())
    {
      data = value.toMSecsSinceEpoch () / 1000ull;
    }
  model->setData (index, data, Qt::EditRole);
}
