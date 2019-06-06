#ifndef DECODE_HIGHLIGHTING_LIST_VIEW_HPP_
#define DECODE_HIGHLIGHTING_LIST_VIEW_HPP_

#include <QListView>

class QWidget;

// Class Decode Highlighting List View
//
// Sub-class of  a QListView that  adds a  context menu to  adjust the
// foreground and background colour roles  of the the underlying model
// item that lies  at the context menu right-click  position.  It also
// constrains the  vertical size hint  to limit the height  to exactly
// that of the sum of the items.
// 
class DecodeHighlightingListView final
  : public QListView
{
  Q_OBJECT

public:
  explicit DecodeHighlightingListView (QWidget * parent = nullptr);

private:
  QSize sizeHint () const override;
};

#endif
