#include "ActiveStationList.hpp"

#include <algorithm>

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
