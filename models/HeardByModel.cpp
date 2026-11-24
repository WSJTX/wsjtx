#include "HeardByModel.hpp"

#include <algorithm>
#include <QDateTime>
#include <QStringList>

HeardByModel::HeardByModel (QObject * parent)
  : QAbstractTableModel {parent}
{
}

void HeardByModel::set_band_of (BandOf f)
{
  band_of_ = std::move (f);
}

void HeardByModel::set_distance_to (DistanceTo f)
{
  distance_to_ = std::move (f);
  recompute_distances ();
}

void HeardByModel::set_bearing_to (BearingTo f)
{
  bearing_to_ = std::move (f);
  recompute_distances ();
}

void HeardByModel::recompute_distances ()
{
  for (auto it = rows_.begin (); it != rows_.end (); ++it)
    {
      it->distance = distance_to_ && !it->locator.isEmpty () ? distance_to_ (it->locator) : -1;
      it->bearing = bearing_to_ && !it->locator.isEmpty () ? bearing_to_ (it->locator) : -1;
    }
  refresh (now_ ? now_ : -1);
}

QString HeardByModel::base_mode (QString const& raw)
{
  auto const mode = raw.trimmed ().toUpper ();
  static QStringList const bases {"Q65", "JT4", "JT65", "JT9"};
  for (auto const& base : bases)
    if (mode.size () == base.size () + 1 && mode.startsWith (base) && mode.back ().isLetter ())
      return base;
  return mode;
}

void HeardByModel::set_filter (QString const& mode, QStringList const& bands)
{
  auto const m = base_mode (mode);
  if (m == mode_filter_ && bands == band_filter_) return;
  mode_filter_ = m;
  band_filter_ = bands;
  refresh (now_ ? now_ : -1);    // same clock as the last refresh
}

bool HeardByModel::add_report (HeardByReport const& report)
{
  if (report.receiver.isEmpty ()) return false;
  if (report.id)
    {
      if (seen_.contains (report.id)) return false;
      if (seen_.size () > 100000) seen_.clear ();
      seen_.insert (report.id);
    }
  Key const key {report.receiver, band_of_ ? band_of_ (report.frequency) : QString {}, report.mode};
  auto it = rows_.find (key);
  if (it == rows_.end ())
    {
      Row r;
      r.receiver = key.receiver;
      r.band = key.band;
      r.mode = key.mode;
      it = rows_.insert (key, r);
    }
  if (report.when >= it->when)
    {
      it->locator = report.locator;
      it->frequency = report.frequency;
      it->snr = report.snr;
      it->when = report.when;
      it->distance = distance_to_ && !it->locator.isEmpty () ? distance_to_ (it->locator) : -1;
      it->bearing = bearing_to_ && !it->locator.isEmpty () ? bearing_to_ (it->locator) : -1;
    }
  ++it->count;
  ++total_;
  if (rows_.size () > max_rows)
    {
      auto oldest = rows_.begin ();
      for (auto i = rows_.begin (); i != rows_.end (); ++i)
        if (i->when < oldest->when) oldest = i;
      rows_.erase (oldest);
    }
  return true;
}

void HeardByModel::refresh (qint64 now_secs)
{
  now_ = now_secs >= 0 ? now_secs : QDateTime::currentSecsSinceEpoch ();
  for (auto it = rows_.begin (); it != rows_.end ();)
    {
      if (now_ - it->when > keep_seconds) it = rows_.erase (it);
      else ++it;
    }
  QList<Row> list;
  for (auto const& r : qAsConst (rows_))
    {
      if (!mode_filter_.isEmpty () && base_mode (r.mode) != mode_filter_) continue;
      if (!band_filter_.isEmpty () && !band_filter_.contains (r.band)) continue;
      list << r;
    }
  std::sort (list.begin (), list.end (), [] (Row const& a, Row const& b) {
      return a.when != b.when ? a.when > b.when : a.receiver < b.receiver;
    });
  if (list.size () == shown_.size ())
    {
      // same rows: no reset, so a scrolled list stays where it is
      shown_ = list;
      if (!shown_.isEmpty ()) Q_EMIT dataChanged (index (0, 0), index (shown_.size () - 1, ColumnCount - 1));
      return;
    }
  beginResetModel ();
  shown_ = list;
  endResetModel ();
}

void HeardByModel::clear ()
{
  rows_.clear ();
  seen_.clear ();
  total_ = 0;
  refresh ();
}

QVariant HeardByModel::headerData (int section, Qt::Orientation orientation, int role) const
{
  if (Qt::Horizontal == orientation && Qt::DisplayRole == role)
    {
      switch (section)
        {
        case When: return tr ("UTC");
        case Ago: return tr ("Ago");
        case Receiver: return tr ("Heard by");
        case Locator: return tr ("Grid");
        case Band: return tr ("Band");
        case Mhz: return tr ("MHz");
        case Mode: return tr ("Mode");
        case Snr: return tr ("dB");
        case Distance: return tr ("km");
        case Reports: return tr ("Spots");
        case Bearing: return tr ("Brg");
        }
    }
  return {};
}

QVariant HeardByModel::data (QModelIndex const& index, int role) const
{
  if (!index.isValid () || index.row () >= shown_.size ()) return {};
  auto const& r = shown_[index.row ()];
  if (Qt::TextAlignmentRole == role)
    {
      switch (index.column ())
        {
        case Ago: case Mhz: case Snr: case Distance: case Reports: case Bearing:
          return int (Qt::AlignRight | Qt::AlignVCenter);
        default: return int (Qt::AlignLeft | Qt::AlignVCenter);
        }
    }
  if (role != Qt::DisplayRole) return {};
  switch (index.column ())
    {
    case When: return QDateTime::fromSecsSinceEpoch (r.when, Qt::UTC).toString ("HH:mm:ss");
    case Ago:
      {
        auto const m = qMax<qint64> (0, (now_ - r.when) / 60);
        return m < 120 ? tr ("%1m").arg (m) : tr ("%1h").arg (m / 60);
      }
    case Receiver: return r.receiver;
    case Locator: return r.locator;
    case Band: return r.band;
    case Mhz: return r.frequency ? QString::number (r.frequency / 1e6, 'f', 4) : QString {};
    case Mode: return r.mode;
    case Snr: return r.snr;
    case Distance: return r.distance < 0 ? QVariant {QString {}} : QVariant {qRound (r.distance)};
    case Reports: return r.count;
    case Bearing: return r.bearing < 0 ? QString {} : tr ("%1°").arg (qRound (r.bearing) % 360);
    }
  return {};
}
