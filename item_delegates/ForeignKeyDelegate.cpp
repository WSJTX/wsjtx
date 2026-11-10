#include "ForeignKeyDelegate.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFontMetrics>
#include <QSize>
#include <QStyleOptionViewItem>
#include "CandidateKeyFilter.hpp"

ForeignKeyDelegate::ForeignKeyDelegate (QAbstractItemModel const * referenced_model
                                        , int referenced_key_column
                                        , QObject * parent
                                        , int referenced_key_role)
  : QStyledItemDelegate {parent}
  , referenced_key_column_ {referenced_key_column}
  , referenced_key_role_ {referenced_key_role}
  , referencing_key_role_ {Qt::EditRole}
  , candidate_key_filter_ {new CandidateKeyFilter {referenced_model, referenced_key_column, nullptr, referenced_key_role}}
{
}

ForeignKeyDelegate::ForeignKeyDelegate (QAbstractItemModel const * referenced_model
                                        , QAbstractItemModel const * referencing_model
                                        , int referenced_key_column
                                        , int referencing_key_column
                                        , QObject * parent
                                        , int referenced_key_role
                                        , int referencing_key_role)
  : QStyledItemDelegate {parent}
  , referenced_key_column_ {referenced_key_column}
  , referenced_key_role_ {referenced_key_role}
  , referencing_key_role_ {referencing_key_role}
  , candidate_key_filter_ {new CandidateKeyFilter {referenced_model, referencing_model, referenced_key_column, referencing_key_column, nullptr, referenced_key_role, referencing_key_role}}
{
}

ForeignKeyDelegate::~ForeignKeyDelegate ()
{
}

QWidget * ForeignKeyDelegate::createEditor (QWidget * parent
                                            , QStyleOptionViewItem const& /* option */
                                            , QModelIndex const& index) const
{
  auto editor = new QComboBox {parent};
  editor->setFrame (false);
  candidate_key_filter_->set_active_key (index);
  editor->setModel (candidate_key_filter_.data ());
  editor->setModelColumn (referenced_key_column_);
  editor->setSizeAdjustPolicy (QComboBox::AdjustToContents);
  return editor;
}

void ForeignKeyDelegate::setEditorData (QWidget * widget, QModelIndex const& index) const
{
  auto editor = qobject_cast<QComboBox *> (widget);
  if (!editor) return;
  auto const key = index.data (referencing_key_role_);
  editor->setCurrentIndex (-1);
  if (!key.isValid ()) return;
  for (int row = 0; row < editor->count (); ++row)
    {
      auto const candidate = editor->itemData (row, referenced_key_role_);
      if (candidate.userType () == key.userType () && candidate == key)
        {
          editor->setCurrentIndex (row);
          return;
        }
    }
}

void ForeignKeyDelegate::setModelData (QWidget * widget, QAbstractItemModel * model,
                                       QModelIndex const& index) const
{
  auto editor = qobject_cast<QComboBox *> (widget);
  if (!editor || editor->currentIndex () < 0) return;
  auto const key = editor->currentData (referenced_key_role_);
  if (key.isValid ()) model->setData (index, key, referencing_key_role_);
}

QSize ForeignKeyDelegate::sizeHint (QStyleOptionViewItem const& option, QModelIndex const& index) const
{
  auto size_hint = QStyledItemDelegate::sizeHint (option, index);
  QFontMetrics metrics {option.font};
  QStyleOptionComboBox combo_box_option;
  combo_box_option.rect = option.rect;
  combo_box_option.state = option.state | QStyle::State_Enabled;
  for (auto row = 0; row < candidate_key_filter_->rowCount (); ++row)
    {
      size_hint = size_hint.expandedTo (qApp->style ()->sizeFromContents (QStyle::CT_ComboBox
                                                                          , &combo_box_option
                                                                          , metrics.boundingRect (candidate_key_filter_->data (candidate_key_filter_->index (row, referenced_key_column_)).toString ()).size ()));
    }
  return size_hint;
}
