#include "FrequencyDeltaDelegate.hpp"

#include "widgets/FrequencyDeltaLineEdit.hpp"

FrequencyDeltaDelegate::FrequencyDeltaDelegate (QObject * parent)
  : QStyledItemDelegate {parent}
{
}

QWidget * FrequencyDeltaDelegate::createEditor (QWidget * parent, QStyleOptionViewItem const&
                                                , QModelIndex const&) const
{
  auto * editor = new FrequencyDeltaLineEdit {parent};
  editor->setFrame (false);
  return editor;
}

void FrequencyDeltaDelegate::setEditorData (QWidget * editor, QModelIndex const& index) const
{
  static_cast<FrequencyDeltaLineEdit *> (editor)->frequency_delta (index.model ()->data (index, Qt::EditRole).value<Radio::FrequencyDelta> ());
}

void FrequencyDeltaDelegate::setModelData (QWidget * editor, QAbstractItemModel * model, QModelIndex const& index) const
{
  bool ok;
  auto const frequency_delta = static_cast<FrequencyDeltaLineEdit *> (editor)->frequency_delta (&ok);
  if (ok)
    {
      model->setData (index, frequency_delta, Qt::EditRole);
    }
}
