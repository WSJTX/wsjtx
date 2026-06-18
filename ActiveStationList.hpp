// -*- Mode: C++ -*-
#ifndef ACTIVE_STATION_LIST_HPP
#define ACTIVE_STATION_LIST_HPP

#include <QString>
#include <QVector>

struct ActiveStationListItem
{
  float sort_key;
  QString text;
};

QVector<ActiveStationListItem> sorted_limited_active_station_items(
  QVector<ActiveStationListItem> items,
  int limit,
  bool descending);

#endif
