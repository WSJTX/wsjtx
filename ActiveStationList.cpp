#include "ActiveStationList.hpp"

#include <algorithm>
#include <QTime>

QVector<ActiveStationListItem> sorted_limited_active_station_items(
  QVector<ActiveStationListItem> items,
  int limit,
  bool descending)
{
  if(limit <= 0) return {};

  std::stable_sort(items.begin(), items.end(),
    [descending](ActiveStationListItem const& lhs, ActiveStationListItem const& rhs) {
      return descending ? lhs.sort_key > rhs.sort_key : lhs.sort_key < rhs.sort_key;
    });

  if(items.size() > limit) items.resize(limit);
  return items;
}

QVector<ActiveStationListItem> q65_active_station_items(
  QVector<Q65ActiveStation> const& stations,
  Q65ActiveStationFilter const& filter)
{
  QVector<ActiveStationListItem> rows;
  for(auto const& station : stations) {
    if(filter.wantedOnly and station.worked) continue;
    if(filter.readyOnly and !station.ready2call) continue;
    // Clamped at 0 (not wrapped) so a station heard after filter.nowUtc, or across
    // a UTC-midnight rollover, never reads back as ~1440 minutes old.
    int const age=int(qMax(qint64 {0}, station.lastHeard.secsTo(filter.nowUtc))/60);
    if(age>filter.maxAgeMinutes) continue;
    int const secondsSinceMidnight=QTime {0, 0}.secsTo(station.lastHeard.time());
    int odd=0;
    if(station.submode.left(2)=="30" and (secondsSinceMidnight%60)==0) odd=1;
    if(station.submode.left(2)=="60" and (secondsSinceMidnight%120)==0) odd=1;
    char c2[3]={32,32,0};
    if(!station.worked) c2[0]=35;          //# for not in log
    if(station.ready2call) c2[1]=42;       //* for ready to call
    QString const call=(station.call+"     ").left(8);
    QString const grid4=(station.grid4+"... ").left(4);
    rows.append({float(station.fsked),
                 QString::asprintf("%7.3f %5.1f  %+03d  %3s  %8s %4s %3d %3d %2s\n",
                                   station.frx, station.fsked, station.snr,
                                   station.submode.toLatin1().constData(),
                                   call.toLatin1().constData(),
                                   grid4.toLatin1().constData(), odd, age, c2)});
  }
  return rows;
}
