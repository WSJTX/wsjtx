#ifndef QMAP_DECODE_RECORD_H
#define QMAP_DECODE_RECORD_H

#include <optional>

#include <QByteArray>
#include <QString>

#include "qmap_ipc.h"

struct QMapDecodeRecord
{
  QByteArray raw;
  QString time;
  QString callsign;
  QString grid;
  QString submode;
  double receiveFrequencyKHz {};
  double scheduledFrequencyKHz {};
  int snr {};
  int secondsSinceMidnight {};
  bool secondHalf {};
  bool cq {};
};

std::optional<QMapDecodeRecord> parseQMapDecodeRecord (QByteArray const& row);

#endif // QMAP_DECODE_RECORD_H
