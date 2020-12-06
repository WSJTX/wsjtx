#ifndef CHECKABLE_ITEM_COMBO_BOX_HPP__
#define CHECKABLE_ITEM_COMBO_BOX_HPP__

#include <QScopedPointer>

#include "LazyFillComboBox.hpp"

class QStandardItemModel;
class QStandardItem;

/**
 * @brief QComboBox with support of checkboxes
 * http://stackoverflow.com/questions/8422760/combobox-of-checkboxes
 */
class CheckableItemComboBox
  : public LazyFillComboBox
{
  Q_OBJECT

public:
  explicit CheckableItemComboBox (QWidget * parent = nullptr);
  QStandardItem * addCheckItem (QString const& label, QVariant const& data, Qt::CheckState checkState);

protected:
  bool eventFilter (QObject *, QEvent *) override;

private:
  void update_text();

  Q_SLOT void model_data_changed ();
  Q_SLOT void item_pressed (QModelIndex const&);

private:
  QScopedPointer<QStandardItemModel> model_;
};

#endif
