#include "HeardByWindow.hpp"

#include <algorithm>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSettings>
#include <QStyle>
#include <QTableView>
#include <QVBoxLayout>

#include "SettingsGroup.hpp"
#include "qt_helpers.hpp"

namespace
{
  QString utc_now_hhmm ()
  {
    return QDateTime::currentDateTimeUtc ().toString ("HH:mm");
  }
}

HeardByWindow::HeardByWindow (QSettings * settings, QFont const& font,
                              ContextProvider context, HeardByModel::BandOf band_of,
                              HeardByModel::DistanceTo distance_to, QWidget * parent)
  : QWidget {parent}
  , settings_ {settings}
  , context_ {std::move (context)}
  , band_of_ {band_of}
  , table_ {new QTableView}
  , banner_ {new QLabel}
  , status_ {new QLabel}
  , credit_ {new QLabel}
  , in_use_ {new QLabel}
{
  clock_.start ();
  setWindowTitle (QApplication::applicationName () + " - " + tr ("Heard by"));
  banner_->setWordWrap (true);
  banner_->hide ();
  credit_->setText (tr ("Reports from <a href=\"https://pskreporter.info/\">PSK Reporter</a>"));
  credit_->setOpenExternalLinks (true);
  credit_->setWordWrap (true);               // so it can shrink with the window
  model_.set_band_of (std::move (band_of));
  model_.set_distance_to (std::move (distance_to));
  table_->setModel (&model_);
  table_->setSelectionMode (QAbstractItemView::NoSelection);
  table_->setEditTriggers (QAbstractItemView::NoEditTriggers);
  table_->setShowGrid (false);
  table_->verticalHeader ()->hide ();
  table_->horizontalHeader ()->setStretchLastSection (false);
  table_->horizontalHeader ()->setSectionResizeMode (QHeaderView::ResizeToContents);
  connect (table_->horizontalHeader (), &QHeaderView::sectionResized, this, [this] {fit_width (); });
  // The window may be narrowed until only the first columns show (the
  // table then scrolls), so no label sets a minimum width.
  // (a minimum width of 0 would mean "not set"; Ignored really drops the hint)
  for (auto * label : {in_use_, status_}) label->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  in_use_->setAlignment (Qt::AlignRight | Qt::AlignVCenter);
  status_->setWordWrap (true);

  auto * bottom = new QHBoxLayout;
  bottom->addWidget (status_, 1);
  bottom->addWidget (credit_);
  auto * layout = new QVBoxLayout {this};
  layout->addWidget (in_use_);
  layout->addWidget (banner_);
  layout->addWidget (table_);
  layout->addLayout (bottom);

  change_font (font);
  read_settings ();
  connect (&mqtt_, &HeardByMqtt::spot, this, [this] (QJsonObject const& s, bool) {on_spot (s); });
  connect (&mqtt_, &HeardByMqtt::status_changed, this, [this] {
      if (mqtt_.subscribed ()) own_ref_ms_ = clock_.elapsed ();      // a new silence starts now
      update_status ();
    });
  connect (&mqtt_, &HeardByMqtt::feed_checked, this, &HeardByWindow::feed_checked);
  timer_.setInterval (1000);
  connect (&timer_, &QTimer::timeout, this, &HeardByWindow::tick);
  update_status ();
}

// Children and members outlive each other in the wrong order (the feed is
// destroyed after members declared later, the table after model_), so let go
// first: no signal may reach a half-destroyed window.
HeardByWindow::~HeardByWindow ()
{
  timer_.stop ();
  disconnect (&mqtt_, nullptr, this, nullptr);     // ~HeardByMqtt () still reports "stopped"
  table_->horizontalHeader ()->disconnect (this);
  table_->setModel (nullptr);
}

void HeardByWindow::set_mqtt (HeardByMqtt::Options const& feed, Timing const& timing)
{
  mqtt_.set_options (feed);
  timing_ = timing;
}

void HeardByWindow::stop_feed ()
{
  mqtt_.stop ();
  feed_ = Feed::Unknown;
}

void HeardByWindow::on_spot (QJsonObject const& spot)
{
  model_.add_report (HeardByMqtt::report (spot));
  own_ref_ms_ = clock_.elapsed ();
  feed_ = Feed::Alive;
  feed_checked_ = utc_now_hhmm ();
  mqtt_.cancel_check ();                        // a spot of our own answers it
}

void HeardByWindow::feed_checked (bool delivering)
{
  feed_checked_ = utc_now_hhmm ();
  feed_ = delivering ? Feed::Alive : Feed::Silent;
  update_status ();
}

QString HeardByWindow::banner_text () const
{
  return banner_->isHidden () ? QString {} : banner_->text ();
}

void HeardByWindow::change_font (QFont const& font)
{
  table_->setStyleSheet (font_as_stylesheet (font));
  updateGeometry ();
}

void HeardByWindow::read_settings ()
{
  SettingsGroup g {settings_, "HeardBy"};
  move (settings_->value ("window/pos", pos ()).toPoint ());
  restoreGeometry (settings_->value ("geometry").toByteArray ());
}

void HeardByWindow::write_settings ()
{
  SettingsGroup g {settings_, "HeardBy"};
  settings_->setValue ("window/pos", pos ());
  settings_->setValue ("geometry", saveGeometry ());
}

void HeardByWindow::showEvent (QShowEvent * e)
{
  QWidget::showEvent (e);
  own_ref_ms_ = clock_.elapsed ();
  timer_.start ();
  tick ();
}

void HeardByWindow::hideEvent (QHideEvent * e)
{
  timer_.stop ();
  stop_feed ();                 // nothing is asked while the window is closed
  QWidget::hideEvent (e);
}

void HeardByWindow::closeEvent (QCloseEvent * e)
{
  timer_.stop ();
  stop_feed ();
  write_settings ();
  e->accept ();
}

void HeardByWindow::tick ()
{
  auto const c = context_ ();
  auto const grid_changed = c.grid != last_.grid;
  auto const call_changed = c.call != last_.call;
  last_ = c;
  if (call_changed)
    {
      model_.clear ();
      stop_feed ();
      own_ref_ms_ = clock_.elapsed ();
      last_check_ms_ = -1;
    }
  if (grid_changed) model_.recompute_distances ();
  in_use_->setText (tr ("WSJT-X: %1 on %2")
                    .arg (c.mode.isEmpty () ? tr ("no mode") : c.mode,
                          c.band.isEmpty () ? tr ("no band") : c.band));
  model_.refresh ();
  fit_width ();
  if (isVisible () && !c.call.trimmed ().isEmpty ())
    {
      if (!mqtt_.running ())
        {
          own_ref_ms_ = clock_.elapsed ();
          mqtt_.start (HeardByMqtt::topic_for_sender (c.call), c.call);
        }
      // our own subscription has been quiet for a while: is it the feed or us?
      auto const t = clock_.elapsed ();
      if (mqtt_.subscribed () && !mqtt_.checking ()
          && t - own_ref_ms_ >= timing_.silent_after_ms
          && (last_check_ms_ < 0 || t - last_check_ms_ >= timing_.check_every_ms))
        {
          last_check_ms_ = t;
          mqtt_.check_feed (timing_.check_window_ms);
        }
    }
  update_status ();
}

// Wider than the columns need is only empty space: cap the window there.
void HeardByWindow::fit_width ()
{
  if (!layout ()) return;
  auto const margins = layout ()->contentsMargins ();
  auto const table = table_->horizontalHeader ()->length () + 2 * table_->frameWidth ()
    + style ()->pixelMetric (QStyle::PM_ScrollBarExtent);        // room for the vertical bar
  setMaximumWidth (table + margins.left () + margins.right ());
}

QString HeardByWindow::status_text () const
{
  return status_->text ();
}

QString HeardByWindow::in_use_text () const
{
  return in_use_->text ();
}

void HeardByWindow::update_status ()
{
  QString text;
  if (last_.call.trimmed ().isEmpty ())
    {
      text = tr ("Set your call in Settings");
    }
  else
    {
      text = tr ("%1 stations heard %2. Live feed: %3")
        .arg (model_.rowCount ()).arg (last_.call, mqtt_.status ());
      if (mqtt_.stats ().failures) text += tr (" (%1 failed connections)").arg (mqtt_.stats ().failures);
    }
  status_->setText (text);

  // is the feed delivering?
  QString banner;
  QString style;
  auto const t = clock_.elapsed ();
  if (!last_.call.trimmed ().isEmpty () && mqtt_.subscribed ())
    {
      if (Feed::Silent == feed_)
        {
          banner = tr ("PSK Reporter's live feed is silent (checked %1 UTC), so your reports cannot "
                       "be shown until it recovers.").arg (feed_checked_);
          style = "color: #b00020; font-weight: bold";
        }
      else if (mqtt_.checking ())
        {
          banner = tr ("Nothing for %1 for a while - checking whether the feed delivers at all...").arg (last_.call);
        }
      else if (Feed::Alive == feed_ && t - own_ref_ms_ >= timing_.silent_after_ms)
        {
          banner = tr ("The feed is delivering (checked %1 UTC); no new reports of %2.").arg (feed_checked_, last_.call);
        }
    }
  banner_->setText (banner);
  banner_->setStyleSheet (style);
  banner_->setVisible (!banner.isEmpty ());
}
