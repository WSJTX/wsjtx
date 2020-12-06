#include "DecodesModel.hpp"

#include <QStandardItem>
#include <QModelIndex>
#include <QVariant>
#include <QTime>
#include <QString>
#include <QFont>
#include <QList>

namespace
{
  char const * const headings[] = {
    QT_TRANSLATE_NOOP ("DecodesModel", "Client"),
    QT_TRANSLATE_NOOP ("DecodesModel", "Time"),
    QT_TRANSLATE_NOOP ("DecodesModel", "Snr"),
    QT_TRANSLATE_NOOP ("DecodesModel", "DT"),
    QT_TRANSLATE_NOOP ("DecodesModel", "DF"),
    QT_TRANSLATE_NOOP ("DecodesModel", "Md"),
    QT_TRANSLATE_NOOP ("DecodesModel", "Confidence"),
    QT_TRANSLATE_NOOP ("DecodesModel", "Live"),
    QT_TRANSLATE_NOOP ("DecodesModel", "Message"),
  };

  QString confidence_string (bool low_confidence)
  {
    return low_confidence ? QT_TRANSLATE_NOOP ("DecodesModel", "low") : QT_TRANSLATE_NOOP ("DecodesModel", "high");
  }

  QString live_string (bool off_air)
  {
    return off_air ? QT_TRANSLATE_NOOP ("DecodesModel", "no") : QT_TRANSLATE_NOOP ("DecodesModel", "yes");
  }

  QFont text_font {"Courier", 10};

  QList<QStandardItem *> make_row (MessageServer::ClientKey const& key, QTime time, qint32 snr
                                   , float delta_time, quint32 delta_frequency, QString const& mode
                                   , QString const& message, bool low_confidence, bool off_air, bool is_fast)
  {
    auto client_item = new QStandardItem {QString {"%1(%2)"}.arg (key.second).arg (key.first.toString ())};
    client_item->setData (QVariant::fromValue (key));

    auto time_item = new QStandardItem {time.toString (is_fast || "~" == mode ? "hh:mm:ss" : "hh:mm")};
    time_item->setData (time);
    time_item->setTextAlignment (Qt::AlignRight);

    auto snr_item = new QStandardItem {QString::number (snr)};
    snr_item->setData (snr);
    snr_item->setTextAlignment (Qt::AlignRight);

    auto dt = new QStandardItem {QString::number (delta_time)};
    dt->setData (delta_time);
    dt->setTextAlignment (Qt::AlignRight);

    auto df = new QStandardItem {QString::number (delta_frequency)};
    df->setData (delta_frequency);
    df->setTextAlignment (Qt::AlignRight);

    auto md = new QStandardItem {mode};
    md->setTextAlignment (Qt::AlignHCenter);

    auto confidence = new QStandardItem {confidence_string (low_confidence)};
    confidence->setTextAlignment (Qt::AlignHCenter);

    auto live = new QStandardItem {live_string (off_air)};
    live->setTextAlignment (Qt::AlignHCenter);

    QList<QStandardItem *> row {
      client_item, time_item, snr_item, dt, df, md, confidence, live, new QStandardItem {message}};
    Q_FOREACH (auto& item, row)
      {
        item->setEditable (false);
        item->setFont (text_font);
        item->setTextAlignment (item->textAlignment () | Qt::AlignVCenter);
      }
    return row;
  }
}

DecodesModel::DecodesModel (QObject * parent)
  : QStandardItemModel {0, sizeof headings / sizeof headings[0], parent}
{
  int column {0};
  for (auto const& heading : headings)
    {
      setHeaderData (column++, Qt::Horizontal, tr (heading));
    }
}

void DecodesModel::add_decode (bool is_new, ClientKey const& key, QTime time, qint32 snr, float delta_time
                               , quint32 delta_frequency, QString const& mode, QString const& message
                               , bool low_confidence, bool off_air, bool is_fast)
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
                  && item (row, 4)->data ().toUInt () == delta_frequency
                  && data (index (row, 5)).toString () == mode
                  && data (index (row, 7)).toString () == confidence_string (low_confidence)
                  && data (index (row, 6)).toString () == live_string (off_air)
                  && data (index (row, 8)).toString () == message)
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
          insertRow (target_row + 1, make_row (key, time, snr, delta_time, delta_frequency, mode
                                               , message, low_confidence, off_air, is_fast));
          return;
        }
    }

  appendRow (make_row (key, time, snr, delta_time, delta_frequency, mode, message, low_confidence
                       , off_air, is_fast));
}

void DecodesModel::decodes_cleared (ClientKey const& key)
{
  for (auto row = rowCount () - 1; row >= 0; --row)
    {
      if (item (row, 0)->data ().value<ClientKey> () == key)
        {
          removeRow (row);
        }
    }
}

void DecodesModel::do_reply (QModelIndex const& source, quint8 modifiers)
{
  auto row = source.row ();
  Q_EMIT reply (item (row, 0)->data ().value<ClientKey> ()
                , item (row, 1)->data ().toTime ()
                , item (row, 2)->data ().toInt ()
                , item (row, 3)->data ().toFloat ()
                , item (row, 4)->data ().toInt ()
                , data (index (row, 5)).toString ()
                , data (index (row, 8)).toString ()
                , confidence_string (true) == data (index (row, 7)).toString ()
                , modifiers);
}

#include "moc_DecodesModel.cpp"
