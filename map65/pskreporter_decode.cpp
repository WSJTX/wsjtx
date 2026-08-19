#include "pskreporter_decode.h"

#include <QTime>
#include <QtGlobal>

namespace
{
  bool isReportableMessageType(QString const& messageType)
  {
    return messageType == "CQ" || messageType == "QRZ"
      || messageType == "CQV" || messageType == "CQH"
      || messageType == "QRT";
  }

  QDateTime utcDateTime(QDate const& date, QTime const& time)
  {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QDateTime {date, time, QTimeZone::UTC};
#else
    return QDateTime {date, time, Qt::UTC};
#endif
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

    auto const encodedTime = tokens.at(3).trimmed() + "00";
    auto const timeValue = encodedTime.toInt(&ok);
    if (!ok) {
      return false;
    }

    auto const time = QTime {
      encodedTime.mid(0, 2).toInt(),
      encodedTime.mid(2, 2).toInt(),
      encodedTime.mid(4, 2).toInt()
    };
    if (!time.isValid()) {
      return false;
    }

    auto const spotDate = timeValue + 60 < 236000
      ? nowUtc.date()
      : nowUtc.addDays(-1).date();
    auto const spotTime = utcDateTime(spotDate, time);

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
    auto callsignWithoutMarkers = callsign;
    if (callsignWithoutMarkers.contains('.')
        || callsignWithoutMarkers.contains('+')
        || callsignWithoutMarkers.contains('-')
        || callsignWithoutMarkers.contains('?')) {
      return false;
    }
    callsignWithoutMarkers.remove('<');
    callsignWithoutMarkers.remove('>');
    if (callsignWithoutMarkers.length() > 11) {
      return false;
    }

    auto const parts = callsignWithoutMarkers.split('/');
    auto baseCall = callsignWithoutMarkers;
    if (parts.size() > 1) {
      baseCall = parts.at(0).length() > parts.at(1).length()
        ? parts.at(0) : parts.at(1);
    }
    baseCall = baseCall.trimmed();
    auto const baseLength = baseCall.length();
    if (baseLength < 3 || baseLength > 8) {
      return false;
    }

    if (!baseCall.at(0).isLetter() && !baseCall.at(1).isLetter()) {
      return false;
    }
    if (baseCall.at(0) == 'Q' && baseCall.mid(0, 5) != "QU1RK") {
      return false;
    }

    int digitPosition = 0;
    if (baseCall.at(1).isDigit()) digitPosition = 1;
    if (baseCall.at(2).isDigit()) digitPosition = 2;
    if (baseCall.at(3).isDigit()) digitPosition = 3;
    if (digitPosition == 0 || digitPosition == baseLength) {
      return false;
    }

    auto const suffixLength = baseLength - digitPosition - 1;
    if (suffixLength < 1 || suffixLength > 4) {
      return false;
    }
    for (int i = digitPosition + 1; i < baseLength; ++i) {
      auto const character = baseCall.at(i);
      if (character < QChar {'A'} || character > QChar {'Z'}) {
        return false;
      }
    }
    return true;
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
