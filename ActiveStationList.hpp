// -*- Mode: C++ -*-
#ifndef ACTIVE_STATION_LIST_HPP
#define ACTIVE_STATION_LIST_HPP

#include <QDateTime>
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

struct Q65ActiveStation
{
  QString call;
  QString grid4;
  double frx;
  double fsked;
  int snr;
  QDateTime lastHeard;           // when this station was last decoded
  QString submode;
  bool ready2call;
  bool worked;
};

struct Q65ActiveStationFilter
{
  QDateTime nowUtc;              // reference time for age calculation
  int maxAgeMinutes;
  bool wantedOnly;
  bool readyOnly;
};

// Unnumbered Active Stations rows keyed by Fsked.
QVector<ActiveStationListItem> q65_active_station_items(
  QVector<Q65ActiveStation> const& stations,
  Q65ActiveStationFilter const& filter);

#endif
