#include "HeardByWindow.hpp"

#include <algorithm>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
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
  int constexpr first_lookback_secs = 6 * 3600;
  int constexpr later_lookback_secs = 3600;

  QString utc_now_hhmm ()
  {
    return QDateTime::currentDateTimeUtc ().toString ("HH:mm");
  }
}

// A busy band and mode: if nothing arrives here, the feed is silent.
char const * const HeardByWindow::canary_topic = "pskr/filter/v2/20m/FT8/+/+/+/+/+/+";

HeardByWindow::HeardByWindow (QSettings * settings, QFont const& font, QNetworkAccessManager * network,
                              ContextProvider context, HeardByModel::BandOf band_of,
                              HeardByModel::DistanceTo distance_to, QWidget * parent,
                              QString const& records_url)
  : QWidget {parent}
  , settings_ {settings}
  , context_ {std::move (context)}
  , band_of_ {band_of}
  , records_ {network, this, records_url}
  , source_box_ {new QComboBox}
  , table_ {new QTableView}
  , banner_ {new QLabel}
  , status_ {new QLabel}
  , credit_ {new QLabel}
  , in_use_ {new QLabel}
{
  clock_.start ();
  setWindowTitle (QApplication::applicationName () + " - " + tr ("Heard by"));
  source_box_->addItem (tr ("PSK records"), "records");
  source_box_->addItem (tr ("Live feed"), "mqtt");
  source_box_->setToolTip (tr ("PSK records: PSK Reporter's stored reports, read every 5 minutes.\n"
                               "Live feed (MQTT): reports as PSK Reporter receives them, faster; the feed\n"
                               "can stop for a while, which the window then says."));
  banner_->setWordWrap (true);
  banner_->hide ();
  credit_->setText (tr ("Reports from <a href=\"https://pskreporter.info/\">PSK Reporter</a>"));
  credit_->setOpenExternalLinks (true);
  credit_->setWordWrap (true);               // so it can shrink with the window
  canary_timer_.setSingleShot (true);
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
  // table then scrolls), so no label or box sets a minimum width.
  source_box_->setSizeAdjustPolicy (QComboBox::AdjustToMinimumContentsLengthWithIcon);
  source_box_->setMinimumContentsLength (6);
  // (a minimum width of 0 would mean "not set"; Ignored really drops the hint)
  for (auto * label : {in_use_, status_}) label->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  in_use_->setAlignment (Qt::AlignRight | Qt::AlignVCenter);
  status_->setWordWrap (true);

  auto * top = new QHBoxLayout;
  top->addWidget (source_box_);
  top->addWidget (in_use_, 1);
  auto * bottom = new QHBoxLayout;
  bottom->addWidget (status_, 1);
  bottom->addWidget (credit_);
  auto * layout = new QVBoxLayout {this};
  layout->addLayout (top);
  layout->addWidget (banner_);
  layout->addWidget (table_);
  layout->addLayout (bottom);

  change_font (font);
  read_settings ();
  connect (source_box_, QOverload<int>::of (&QComboBox::currentIndexChanged), this, [this] {
      // A fresh list from the new source alone; switched to the live
      // feed with the window open, no records query fills it in either,
      // so every row then came over the feed.
      model_.clear ();
      error_.clear ();
      first_result_ = false;
      asked_ = false;
      next_poll_ms_ = 0;
      skip_history_ = Source::Mqtt == source () && isVisible ();
      restart ();
    });
  connect (&records_, &HeardByRecords::finished, this, &HeardByWindow::on_records);
  connect (&mqtt_, &HeardByMqtt::spot, this, [this] (QJsonObject const& s, bool) {on_spot (s); });
  connect (&mqtt_, &HeardByMqtt::status_changed, this, [this] {
      if (mqtt_.subscribed ()) own_ref_ms_ = clock_.elapsed ();      // a new silence starts now
      update_status ();
    });
  connect (&canary_, &HeardByMqtt::spot, this, [this] {
      if (canary_.running () && canary_timer_.isActive ()) canary_done (true);
    });
  connect (&canary_timer_, &QTimer::timeout, this, [this] {canary_done (false); });
  timer_.setInterval (1000);
  connect (&timer_, &QTimer::timeout, this, &HeardByWindow::tick);
  update_status ();
}

void HeardByWindow::set_mqtt (HeardByMqtt::Options const& feed, HeardByMqtt::Options const& canary,
                              Timing const& timing)
{
  mqtt_.set_options (feed);
  canary_.set_options (canary);
  timing_ = timing;
}

HeardByWindow::Source HeardByWindow::source () const
{
  return source_box_->currentData ().toString () == "mqtt" ? Source::Mqtt : Source::Records;
}

void HeardByWindow::set_source (Source s)
{
  source_box_->setCurrentIndex (Source::Mqtt == s ? 1 : 0);
}

void HeardByWindow::stop_feed ()
{
  canary_timer_.stop ();
  canary_.stop ();
  mqtt_.stop ();
  feed_ = Feed::Unknown;
}

void HeardByWindow::restart ()
{
  stop_feed ();
  own_ref_ms_ = clock_.elapsed ();
  last_canary_ms_ = -1;
  if (isVisible ()) tick ();
  else update_status ();
}

void HeardByWindow::on_spot (QJsonObject const& spot)
{
  model_.add_report (HeardByMqtt::report (spot));
  own_ref_ms_ = clock_.elapsed ();
  feed_ = Feed::Alive;
  feed_checked_ = utc_now_hhmm ();
  if (canary_.running ())
    {
      canary_timer_.stop ();
      canary_.stop ();
    }
}

void HeardByWindow::start_canary ()
{
  last_canary_ms_ = clock_.elapsed ();
  canary_.start (QString::fromLatin1 (canary_topic), last_.call);
  canary_timer_.start (timing_.canary_window_ms);
}

void HeardByWindow::canary_done (bool got_spot)
{
  canary_timer_.stop ();
  bool const connected = canary_.subscribed ();
  canary_.stop ();
  feed_checked_ = utc_now_hhmm ();
  if (got_spot) feed_ = Feed::Alive;
  else if (connected) feed_ = Feed::Silent;     // subscribed to a busy band, and nothing
  else feed_ = Feed::Unknown;                   // could not even connect: no verdict
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
  set_source (settings_->value ("Source", "records").toString () == "mqtt" ? Source::Mqtt : Source::Records);
  records_.set_last_query_ms (settings_->value ("LastQueryMs", 0).toLongLong ());
}

void HeardByWindow::write_settings ()
{
  SettingsGroup g {settings_, "HeardBy"};
  settings_->setValue ("window/pos", pos ());
  settings_->setValue ("geometry", saveGeometry ());
  settings_->setValue ("Source", source_box_->currentData ().toString ());
  settings_->setValue ("LastQueryMs", records_.last_query_ms ());
}

void HeardByWindow::showEvent (QShowEvent * e)
{
  QWidget::showEvent (e);
  next_poll_ms_ = 0;
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
      error_.clear ();
      first_result_ = false;
      next_poll_ms_ = 0;
      skip_history_ = false;
      stop_feed ();
      own_ref_ms_ = clock_.elapsed ();
      last_canary_ms_ = -1;
    }
  if (grid_changed) model_.recompute_distances ();
  in_use_->setText (tr ("WSJT-X: %1 on %2")
                    .arg (c.mode.isEmpty () ? tr ("no mode") : c.mode,
                          c.band.isEmpty () ? tr ("no band") : c.band));
  model_.refresh ();
  fit_width ();
  auto const now = QDateTime::currentMSecsSinceEpoch ();
  auto const mqtt = Source::Mqtt == source ();
  auto const have_call = !c.call.trimmed ().isEmpty ();
  // Records: every poll.  MQTT: one query, for what came before.
  if (isVisible () && have_call && (!mqtt || (!first_result_ && !skip_history_))
      && !records_.busy () && now >= next_poll_ms_
      && records_.fetch (c.call, first_result_ ? later_lookback_secs : first_lookback_secs))
    {
      asked_ = true;
      write_settings ();        // keep the query time across a crash or restart
    }
  if (isVisible () && have_call && mqtt)
    {
      if (!mqtt_.running ())
        {
          own_ref_ms_ = clock_.elapsed ();
          mqtt_.start (HeardByMqtt::topic_for_sender (c.call), c.call);
        }
      // our own subscription has been quiet for a while: is it the feed or us?
      auto const t = clock_.elapsed ();
      if (mqtt_.subscribed () && !canary_.running ()
          && t - own_ref_ms_ >= timing_.silent_after_ms
          && (last_canary_ms_ < 0 || t - last_canary_ms_ >= timing_.canary_every_ms))
        start_canary ();
    }
  update_status ();
}

void HeardByWindow::on_records (HeardByRecords::Result const& result)
{
  next_poll_ms_ = QDateTime::currentMSecsSinceEpoch () + HeardByRecords::poll_delay_ms ();
  if (!result.ok)
    {
      error_ = result.error;
    }
  else
    {
      error_.clear ();
      first_result_ = true;
      // oldest first so that a row's latest report is the newest
      for (auto it = result.reports.crbegin (); it != result.reports.crend (); ++it) model_.add_report (*it);
    }
  model_.refresh ();
  fit_width ();
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
  else if (Source::Mqtt == source ())
    {
      text = tr ("%1 stations heard %2. Live feed: %3")
        .arg (model_.rowCount ()).arg (last_.call, mqtt_.status ());
      if (mqtt_.stats ().failures) text += tr (" (%1 failed connections)").arg (mqtt_.stats ().failures);
    }
  else if (!error_.isEmpty ())
    {
      text = tr ("PSK Reporter: %1").arg (error_);
    }
  else if (records_.busy ())
    {
      text = tr ("Asking PSK Reporter...");
    }
  else if (!first_result_)
    {
      text = asked_ ? tr ("Waiting for PSK Reporter") : tr ("Next check in %1 s").arg (records_.ms_until_next () / 1000);
    }
  else
    {
      auto const secs = qMax<qint64> (0, (next_poll_ms_ - QDateTime::currentMSecsSinceEpoch ()) / 1000);
      text = tr ("%1 stations heard %2. Next check in %3:%4")
        .arg (model_.rowCount ()).arg (last_.call)
        .arg (secs / 60).arg (secs % 60, 2, 10, QChar {'0'});
    }
  status_->setText (text);

  // MQTT: is the feed delivering?
  QString banner;
  QString style;
  auto const t = clock_.elapsed ();
  if (Source::Mqtt == source () && !last_.call.trimmed ().isEmpty () && mqtt_.subscribed ())
    {
      if (Feed::Silent == feed_)
        {
          banner = tr ("PSK Reporter's live feed is silent (checked %1 UTC): nothing arrives even for "
                       "a busy band, so your reports cannot be shown until it recovers. The \"PSK "
                       "Reporter records\" source does not depend on it.").arg (feed_checked_);
          style = "color: #b00020; font-weight: bold";
        }
      else if (canary_.running ())
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
