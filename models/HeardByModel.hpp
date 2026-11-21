#ifndef HEARD_BY_MODEL_HPP__
#define HEARD_BY_MODEL_HPP__

#include <functional>
#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "Network/HeardByRecords.hpp"

//
// HeardByModel - the stations that heard us: one row per receiver,
// band and mode with the latest report and how many reports.
// Reports may be added at any time; the visible list (newest
// first, filtered, old rows dropped) is rebuilt by refresh(), which the
// window calls once a second so the view is not reset per report.
//
class HeardByModel final
  : public QAbstractTableModel
{
  Q_OBJECT

public:
  enum Column {When, Ago, Receiver, Locator, Snr, Distance, Reports, Band, Mhz, Mode, ColumnCount};

  struct Row
  {
    QString receiver;
    QString locator;
    QString band;
    QString mode;
    quint64 frequency {0};
    int snr {0};
    int count {0};
    qint64 when {0};            // epoch seconds
    double distance {-1};       // km, < 0 unknown
  };

  static int constexpr keep_seconds = 6 * 3600;
  static int constexpr max_rows = 2000;

  // Band name for a frequency in Hz (empty if none) and distance in km
  // from our grid to a locator (< 0 if unknown).  Supplied by the
  // window so that the model needs nothing of Configuration.
  using BandOf = std::function<QString (quint64)>;
  using DistanceTo = std::function<double (QString const&)>;

  explicit HeardByModel (QObject * parent = nullptr);

  void set_band_of (BandOf);
  void set_distance_to (DistanceTo);
  // Show only this mode / these bands (empty = all).  More than one
  // band covers a satellite whose uplink and downlink are on two bands.
  void set_filter (QString const& mode, QStringList const& bands);
  void set_filter (QString const& mode, QString const& band)
  {
    set_filter (mode, band.isEmpty () ? QStringList {} : QStringList {band});
  }
  // Distances depend on our grid: call when it changes.
  void recompute_distances ();

  // False if the report was already counted (a poll repeats recent
  // ones).  An older report than the row's latest only counts.
  bool add_report (HeardByRecords::Report const&);

  // Drop old rows, apply the filter, sort newest first.  now_secs < 0 = now.
  void refresh (qint64 now_secs = -1);
  void clear ();

  int total () const {return total_;}            // reports counted
  int all_rows () const {return rows_.size ();}  // before the filter
  Row const& visible (int i) const {return shown_[i];}

  // "Q65A" -> "Q65" etc., so that the filter follows the main window's mode.
  static QString base_mode (QString const&);

  int rowCount (QModelIndex const& = {}) const override {return shown_.size ();}
  int columnCount (QModelIndex const& = {}) const override {return ColumnCount;}
  QVariant data (QModelIndex const&, int role) const override;
  QVariant headerData (int section, Qt::Orientation, int role) const override;

private:
  struct Key
  {
    QString receiver, band, mode;
    bool operator== (Key const& o) const {return receiver == o.receiver && band == o.band && mode == o.mode;}
  };
  friend uint qHash (Key const& k, uint seed)
  {
    return qHash (k.receiver, seed) ^ qHash (k.band, seed + 1) ^ qHash (k.mode, seed + 2);
  }

  QHash<Key, Row> rows_;
  QList<Row> shown_;
  QSet<qint64> seen_;
  QString mode_filter_;
  QStringList band_filter_;
  BandOf band_of_;
  DistanceTo distance_to_;
  qint64 now_ {0};
  int total_ {0};
};

#endif
