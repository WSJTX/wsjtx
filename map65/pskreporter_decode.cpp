#include "pskreporter_decode.h"

#include "../validators/LiveCQCallsign.hpp"
#include "../Network/DecodedTime.hpp"

#include <QtGlobal>

namespace
{
  bool isReportableMessageType(QString const& messageType)
  {
    return messageType == "CQ" || messageType == "QRZ"
      || messageType == "CQV" || messageType == "CQH"
      || messageType == "QRT";
  }

  bool parseSpot(QStringList const& tokens,
                 QString const& receiverCallsign,
                 QString const& receiverLocator,
                 QDateTime const& nowUtc,
                 PSKReporterIPFIX::Spot& spot)
  {
    if (tokens.size() < 9 || receiverCallsign.length() < 3
        || receiverLocator.length() < 4
        || !isReportableMessageType(tokens.at(5).trimmed())) {
      return false;
    }

    bool ok = false;
    auto const frequencyMHz = tokens.at(0).trimmed().toDouble(&ok);
    if (!ok) {
      return false;
    }

    auto const spotTime = DecodedTime::spotTime(tokens.at(3).trimmed(), nowUtc, 60);
    if (!spotTime.isValid()) {
      return false;
    }

    QString callsign;
    QString locator = "--";
    QString mode;
    QString modeToken;

    if (tokens.at(7).contains('.')) {
      callsign = tokens.at(6).trimmed().toUpper();
      modeToken = tokens.at(8).trimmed();
    } else if (tokens.size() >= 10 && tokens.at(8).contains('.')) {
      callsign = tokens.at(6).trimmed().toUpper();
      if (Map65PSKReporter::isValidMap65Callsign(callsign)) {
        locator = tokens.at(7).trimmed();
      } else {
        callsign = tokens.at(7).trimmed().toUpper();
      }
      modeToken = tokens.at(9).trimmed();
    } else if (tokens.size() >= 11 && tokens.at(9).contains('.')) {
      callsign = tokens.at(7).trimmed().toUpper();
      locator = tokens.at(8).trimmed();
      modeToken = tokens.at(10).trimmed();
    } else {
      return false;
    }

    if (!Map65PSKReporter::isValidMap65Callsign(callsign)) {
      return false;
    }
    if (modeToken.contains('#')) {
      mode = "JT65" + modeToken.back();
    } else if (modeToken.contains(':')) {
      mode = "Q65-60" + modeToken.back();
    } else {
      return false;
    }

    spot = {
      callsign,
      locator,
      -10,
      static_cast<Radio::Frequency>(qRound64(frequencyMHz * 1000000)),
      mode,
      spotTime
    };
    return true;
  }
}

namespace Map65PSKReporter
{
  bool isValidMap65Callsign(QString const& callsign)
  {
    return LiveCQ::isValidCallsign(callsign);
  }

  QList<PSKReporterIPFIX::Spot> parseMap65PSKReporterSpots(
      QStringList const& decodeLines,
      QString const& receiverCallsign,
      QString const& receiverLocator,
      QDateTime const& nowUtc,
      QStringList& seenDecodes)
  {
    QList<PSKReporterIPFIX::Spot> spots;
    for (auto const& line : decodeLines) {
      auto const tokens = line.split(' ', Qt::SkipEmptyParts);
      if (tokens.size() < 6
          || !isReportableMessageType(tokens.at(5).trimmed())
          || receiverCallsign.length() < 3
          || receiverLocator.length() < 4
          || !seenDecodes.filter(line.left(53)).isEmpty()) {
        continue;
      }

      seenDecodes.append(line);
      PSKReporterIPFIX::Spot spot;
      if (parseSpot(tokens, receiverCallsign, receiverLocator, nowUtc, spot)) {
        spots.append(spot);
      }
    }
    return spots;
  }
}
