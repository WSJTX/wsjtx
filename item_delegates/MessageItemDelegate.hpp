//
//
//

#ifndef WSJTX_MESSAGEITEMDELEGATE_H
#define WSJTX_MESSAGEITEMDELEGATE_H

#include <QStyledItemDelegate>

class MessageItemDelegate: public QStyledItemDelegate
        {
    Q_OBJECT

public:
    explicit MessageItemDelegate(QObject *parent = nullptr);
    QWidget *createEditor(QWidget *parent, QStyleOptionViewItem const & /* option*/
            , QModelIndex const & /* index */
    ) const override;
};
#endif //WSJTX_MESSAGEITEMDELEGATE_H
