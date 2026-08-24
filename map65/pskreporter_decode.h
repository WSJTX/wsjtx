#ifndef MAP65_PSKREPORTER_DECODE_H
#define MAP65_PSKREPORTER_DECODE_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "../Network/PSKReporterIPFIX.hpp"

namespace Map65PSKReporter
{
  bool isValidMap65Callsign(QString const& callsign);

  QList<PSKReporterIPFIX::Spot> parseMap65PSKReporterSpots(
      QStringList const& decodeLines,
      QString const& receiverCallsign,
      QString const& receiverLocator,
      QDateTime const& nowUtc,
      QStringList& seenDecodes);
}

#endif
