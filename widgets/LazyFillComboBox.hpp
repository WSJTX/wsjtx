#ifndef LAZY_FILL_COMBO_BOX_HPP__
#define LAZY_FILL_COMBO_BOX_HPP__

#include <QComboBox>

class QWidget;

//
// Class LazyFillComboBox
//
// QComboBox derivative that signals show and hide of the pop up list.
//
class LazyFillComboBox final
  : public QComboBox
{
  Q_OBJECT

public:
  Q_SIGNAL void about_to_show_popup ();
  Q_SIGNAL void popup_hidden ();

  explicit LazyFillComboBox (QWidget * parent = nullptr)
    : QComboBox {parent}
  {
  }

  void showPopup () override
  {
    Q_EMIT about_to_show_popup ();
    QComboBox::showPopup ();
  }

  void hidePopup () override
  {
    QComboBox::hidePopup ();
    Q_EMIT popup_hidden ();
  }
};

#endif
