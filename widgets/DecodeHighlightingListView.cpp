#include "DecodeHighlightingListView.hpp"

#include <QAction>
#include <QColorDialog>

#include "models/DecodeHighlightingModel.hpp"
#include "MessageBox.hpp"

#include "moc_DecodeHighlightingListView.cpp"

DecodeHighlightingListView::DecodeHighlightingListView (QWidget * parent)
  : QListView {parent}
{
  auto * fg_colour_action = new QAction {tr ("&Foreground color ..."), this};
  addAction (fg_colour_action);
  connect (fg_colour_action, &QAction::triggered, [this] (bool /*checked*/) {
      auto const& index = currentIndex ();
      auto colour = QColorDialog::getColor (model ()->data (index, Qt::ForegroundRole).value<QBrush> ().color ()
                                            , this
                                            , tr ("Choose %1 Foreground Color")
                                                .arg (model ()->data (index).toString ()));
      if (colour.isValid ())
        {
          model ()->setData (index, QBrush {colour}, Qt::ForegroundRole);
        }
    });

  auto * unset_fg_colour_action = new QAction {tr ("&Unset foreground color"), this};
  addAction (unset_fg_colour_action);
  connect (unset_fg_colour_action, &QAction::triggered, [this] (bool /*checked*/) {
      model ()->setData (currentIndex (), QBrush {}, Qt::ForegroundRole);
    });

  auto * bg_colour_action = new QAction {tr ("&Background color ..."), this};
  addAction (bg_colour_action);
  connect (bg_colour_action, &QAction::triggered, [this] (bool /*checked*/) {
      auto const& index = currentIndex ();
      auto colour = QColorDialog::getColor (model ()->data (index, Qt::BackgroundRole).value<QBrush> ().color ()
                                            , this
                                            , tr ("Choose %1 Background Color")
                                                .arg (model ()->data (index).toString ()));
      if (colour.isValid ())
        {
          model ()->setData (index, QBrush {colour}, Qt::BackgroundRole);
        }
    });

  auto * unset_bg_colour_action = new QAction {tr ("U&nset background color"), this};
  addAction (unset_bg_colour_action);
  connect (unset_bg_colour_action, &QAction::triggered, [this] (bool /*checked*/) {
      model ()->setData (currentIndex (), QBrush {}, Qt::BackgroundRole);
    });

  auto * defaults_action = new QAction {tr ("&Reset this item to defaults"), this};
  addAction (defaults_action);
  connect (defaults_action, &QAction::triggered, [this] (bool /*checked*/) {
      auto const& index = currentIndex ();
      model ()->setData (index, model ()->data (index, DecodeHighlightingModel::EnabledDefaultRole).toBool () ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
      model ()->setData (index, model ()->data (index, DecodeHighlightingModel::ForegroundDefaultRole), Qt::ForegroundRole);
      model ()->setData (index, model ()->data (index, DecodeHighlightingModel::BackgroundDefaultRole), Qt::BackgroundRole);
    });
}

QSize DecodeHighlightingListView::sizeHint () const
{
  auto item_height = sizeHintForRow (0);
  if (item_height >= 0)
    {
      // set the height hint to exactly the space required for all the
      // items
      return {width (), (model ()->rowCount () * (item_height + 2 * spacing ())) + 2 * frameWidth ()};
    }
  return QListView::sizeHint ();
}
