#ifndef SQLITE_DATE_TIME_DELEGATE_HPP_
#define SQLITE_DATE_TIME_DELEGATE_HPP_

#include <QStyledItemDelegate>

//
// Class SQLiteDateTimeDelegte
//
//	Item delegate for editing a date and time stored as milliseconds
//	since the Unix epoch and displayed or edited as a QDateTime
//	showing UTC
//
class SQLiteDateTimeDelegate final
  : public QStyledItemDelegate
{
public:
  explicit SQLiteDateTimeDelegate (QObject * parent = nullptr);
  QWidget * createEditor (QWidget * parent, QStyleOptionViewItem const&, QModelIndex const&) const override;
  void setEditorData (QWidget * editor, QModelIndex const&) const override;
  void setModelData (QWidget * editor, QAbstractItemModel *, QModelIndex const&) const override;
};

#endif
