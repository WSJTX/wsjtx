#ifndef QMAP_DECODE_LABEL_H
#define QMAP_DECODE_LABEL_H

#include <algorithm>

#include <QList>

#include "qmap_decode_record.h"

using QMapDecodeLabel = QMapDecodeRecord;
constexpr int QMapDecodeLabelMaximum = 100;
constexpr int QMapDecodeLabelLifetimeSecs = 3 * 60;

inline void upsertQMapDecodeLabel (QList<QMapDecodeLabel>& labels,
                                   QMapDecodeRecord const& record)
{
  for (auto& label : labels) {
    if (label.callsign == record.callsign) {
      label = record;
      return;
    }
  }
  if (labels.size () >= QMapDecodeLabelMaximum) labels.removeFirst ();
  labels.append (record);
}

inline void pruneQMapDecodeLabels (QList<QMapDecodeLabel>& labels, int nowSeconds)
{
  labels.erase (std::remove_if (labels.begin (), labels.end (),
    [nowSeconds] (QMapDecodeLabel const& label) {
      int delta = nowSeconds - label.secondsSinceMidnight;
      if (delta < -43200) delta += 86400;
      else if (delta > 43200) delta -= 86400;
      return delta > QMapDecodeLabelLifetimeSecs;
    }), labels.end ());
}

#endif // QMAP_DECODE_LABEL_H
