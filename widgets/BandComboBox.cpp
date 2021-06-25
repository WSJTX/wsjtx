#include "BandComboBox.hpp"

#include <QAbstractItemView>
#include <QScrollBar>
#include <QDebug>
#include "models/FrequencyList.hpp"

BandComboBox::BandComboBox (QWidget * parent)
  : QComboBox {parent}
{
}

// Fix up broken QComboBox item view rendering which doesn't allow for
// a vertical scroll bar in width calculations and ends up eliding the
// item text.
void BandComboBox::showPopup ()
{
  auto minimum_width = view ()->sizeHintForColumn (FrequencyList_v2::frequency_mhz_column);
  if (count () > maxVisibleItems ())
    {
      // for some as yet unknown reason, in FT8 mode the scrollbar
      // width is oversize on the first call here
      minimum_width += view ()->verticalScrollBar ()->width ();
    }
  view ()->setMinimumWidth (minimum_width);
  QComboBox::showPopup ();
}
