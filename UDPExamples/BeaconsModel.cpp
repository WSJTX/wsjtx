#include "BeaconsModel.hpp"

#include <QStandardItem>
#include <QFont>

namespace
{
  char const * const headings[] = {
    QT_TRANSLATE_NOOP ("BeaconsModel", "Client"),
    QT_TRANSLATE_NOOP ("BeaconsModel", "Time"),
    QT_TRANSLATE_NOOP ("BeaconsModel", "Snr"),
    QT_TRANSLATE_NOOP ("BeaconsModel", "DT"),
    QT_TRANSLATE_NOOP ("BeaconsModel", "Frequency"),
    QT_TRANSLATE_NOOP ("BeaconsModel", "Drift"),
    QT_TRANSLATE_NOOP ("BeaconsModel", "Grid"),
    QT_TRANSLATE_NOOP ("BeaconsModel", "Power"),
    QT_TRANSLATE_NOOP ("BeaconsModel", "Live"),
    QT_TRANSLATE_NOOP ("BeaconsModel", "Callsign"),
  };

  QString live_string (bool off_air)
  {
    return off_air ? QT_TRANSLATE_NOOP ("BeaconsModel", "no") : QT_TRANSLATE_NOOP ("BeaconsModel", "yes");
  }

  QFont text_font {"Courier", 10};

  QList<QStandardItem *> make_row (MessageServer::ClientKey const& key, QTime time, qint32 snr, float delta_time
                                   , Frequency frequency, qint32 drift, QString const& callsign
                                   , QString const& grid, qint32 power, bool off_air)
  {
    auto client_item = new QStandardItem {QString {"%1(%2)"}.arg (key.second).arg (key.first.toString ())};
    client_item->setData (QVariant::fromValue (key));

    auto time_item = new QStandardItem {time.toString ("hh:mm")};
    time_item->setData (time);
    time_item->setTextAlignment (Qt::AlignRight);

    auto snr_item = new QStandardItem {QString::number (snr)};
    snr_item->setData (snr);
    snr_item->setTextAlignment (Qt::AlignRight);

    auto dt = new QStandardItem {QString::number (delta_time)};
    dt->setData (delta_time);
    dt->setTextAlignment (Qt::AlignRight);

    auto freq = new QStandardItem {Radio::pretty_frequency_MHz_string (frequency)};
    freq->setData (frequency);
    freq->setTextAlignment (Qt::AlignRight);

    auto dri = new QStandardItem {QString::number (drift)};
    dri->setData (drift);
    dri->setTextAlignment (Qt::AlignRight);

    auto gd = new QStandardItem {grid};
    gd->setTextAlignment (Qt::AlignRight);

    auto pwr = new QStandardItem {QString::number (power)};
    pwr->setData (power);
    pwr->setTextAlignment (Qt::AlignRight);

    auto live = new QStandardItem {live_string (off_air)};
    live->setTextAlignment (Qt::AlignHCenter);

    QList<QStandardItem *> row {
      client_item, time_item, snr_item, dt, freq, dri, gd, pwr, live, new QStandardItem {callsign}};
    Q_FOREACH (auto& item, row)
      {
        item->setEditable (false);
        item->setFont (text_font);
        item->setTextAlignment (item->textAlignment () | Qt::AlignVCenter);
      }
    return row;
  }
}

BeaconsModel::BeaconsModel (QObject * parent)
  : QStandardItemModel {0, sizeof headings / sizeof headings[0], parent}
{
  int column {0};
  for (auto const& heading : headings)
    {
      setHeaderData (column++, Qt::Horizontal, tr (heading));
    }
}

void BeaconsModel::add_beacon_spot (bool is_new, ClientKey const& key, QTime time, qint32 snr, float delta_time
                                    , Frequency frequency, qint32 drift, QString const& callsign
                                    , QString const& grid, qint32 power, bool off_air)
{
  if (!is_new)
    {
      int target_row {-1};
      for (auto row = 0; row < rowCount (); ++row)
        {
          if (item (row, 0)->data ().value<ClientKey> () == key)
            {
              auto row_time = item (row, 1)->data ().toTime ();
              if (row_time == time
                  && item (row, 2)->data ().toInt () == snr
                  && item (row, 3)->data ().toFloat () == delta_time
                  && item (row, 4)->data ().value<Frequency> () == frequency
                  && data (index (row, 5)).toInt () == drift
                  && data (index (row, 7)).toString () == grid
                  && data (index (row, 8)).toInt () == power
                  && data (index (row, 6)).toString () == live_string (off_air)
                  && data (index (row, 9)).toString () == callsign)
                {
                  return;
                }
              if (time <= row_time)
                {
                  target_row = row; // last row with same time
                }
            }
        }
      if (target_row >= 0)
        {
          insertRow (target_row + 1, make_row (key, time, snr, delta_time, frequency, drift, callsign, grid, power, off_air));
          return;
        }
    }

  appendRow (make_row (key, time, snr, delta_time, frequency, drift, callsign, grid, power, off_air));
}

void BeaconsModel::decodes_cleared (ClientKey const& key)
{
  for (auto row = rowCount () - 1; row >= 0; --row)
    {
      if (item (row, 0)->data ().value<ClientKey> () == key)
        {
          removeRow (row);
        }
    }
}

#include "moc_BeaconsModel.cpp"
