#include "CheckableItemComboBox.hpp"

#include <QStyledItemDelegate>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QLineEdit>
#include <QEvent>
#include <QListView>

class CheckableItemComboBoxStyledItemDelegate
  : public QStyledItemDelegate
{
public:
  explicit CheckableItemComboBoxStyledItemDelegate (QObject * parent = nullptr)
    : QStyledItemDelegate {parent}
  {
  }

  void paint (QPainter * painter, QStyleOptionViewItem const& option, QModelIndex const& index) const override
  {
    QStyleOptionViewItem& mutable_option = const_cast<QStyleOptionViewItem&> (option);
    mutable_option.showDecorationSelected = false;
    QStyledItemDelegate::paint (painter, mutable_option, index);
  }
};

CheckableItemComboBox::CheckableItemComboBox (QWidget * parent)
  : LazyFillComboBox {parent}
  , model_ {new QStandardItemModel()}
{
  setModel (model_.data ());

  setEditable (true);
  lineEdit ()->setReadOnly (true);
  lineEdit ()->installEventFilter (this);
  setItemDelegate (new CheckableItemComboBoxStyledItemDelegate {this});

  connect (lineEdit(), &QLineEdit::selectionChanged, lineEdit(), &QLineEdit::deselect);
  connect (static_cast<QListView *> (view ()), &QListView::pressed, this, &CheckableItemComboBox::item_pressed);
  connect (model_.data (), &QStandardItemModel::dataChanged, this, &CheckableItemComboBox::model_data_changed);
}

QStandardItem * CheckableItemComboBox::addCheckItem (QString const& label, QVariant const& data
                                                     , Qt::CheckState checkState)
{
  auto * item = new QStandardItem {label};
  item->setCheckState (checkState);
  item->setData (data);
  item->setFlags (Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
  model_->appendRow (item);
  update_text ();
  return item;
}

bool CheckableItemComboBox::eventFilter (QObject * object, QEvent * event)
{
  if (object == lineEdit() && event->type () == QEvent::MouseButtonPress)
    {
      showPopup();
      return true;
    }
  return false;
}

void CheckableItemComboBox::update_text()
{
  QString text;
  for (int i = 0; i < model_->rowCount (); ++i)
    {
      if (model_->item (i)->checkState () == Qt::Checked)
        {
          if (text.size ())
            {
              text+= ", ";
            }
          text += model_->item (i)->data ().toString ();
        }
    }
  lineEdit ()->setText (text);
}

void CheckableItemComboBox::model_data_changed ()
{
  update_text ();
}

void CheckableItemComboBox::item_pressed (QModelIndex const& index)
{
  QStandardItem * item = model_->itemFromIndex (index);
  item->setCheckState (item->checkState () == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

#include "widgets/moc_CheckableItemComboBox.cpp"
