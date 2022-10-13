//
// Moved from Configuration.cpp
//

#include "MessageItemDelegate.hpp"

#include <QLineEdit>
#include <QRegExpValidator>

//
// Class MessageItemDelegate
//
//	Item delegate for message entry such as free text message macros.
//
MessageItemDelegate::MessageItemDelegate(QObject *parent): QStyledItemDelegate{parent}
{
}

QWidget *MessageItemDelegate::createEditor(QWidget *parent, QStyleOptionViewItem const &, QModelIndex const &) const
{
  QRegularExpression message_alphabet{"[- @A-Za-z0-9+./?#<>;$]*"};
  auto editor = new QLineEdit{parent};
  editor->setFrame(false);
  editor->setValidator(new QRegularExpressionValidator{message_alphabet, editor});
  return editor;
}
