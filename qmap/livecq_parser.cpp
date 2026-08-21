#include "livecq_parser.h"

#include "../validators/LiveCQCallsign.hpp"

namespace
{
  bool isReportableMessageType(QString const& messageType)
  {
    return messageType == "CQ" || messageType == "QRZ" || messageType == "CQV"
      || messageType == "CQH" || messageType == "QRT";
  }

  bool parseFrequency(QString const& token, int frequencyOffset,
                      int& receiveFrequency, int& scheduledFrequency)
  {
    auto const fields = token.split('.');
    if (fields.size() != 2 || fields.at(0).isEmpty() || fields.at(1).isEmpty()) {
      return false;
    }

    bool ok = false;
    auto const integerPart = fields.at(0).toInt(&ok);
    if (!ok) {
      return false;
    }
    auto const fractionalPart = fields.at(1).toInt(&ok);
    if (!ok) {
      return false;
    }

    receiveFrequency = frequencyOffset + fractionalPart;
    if (receiveFrequency <= frequencyOffset + 500) {
      scheduledFrequency = integerPart;
    } else {
      scheduledFrequency = integerPart + 1;
      receiveFrequency -= 1000;
    }
    return true;
  }
}

namespace QMapLiveCQ
{
  bool parse(QStringList const& tokens, int frequencyOffset, Record& record)
  {
    if (tokens.size() < 9 || tokens.size() > 11
        || !isReportableMessageType(tokens.at(6).trimmed())) {
      return false;
    }

    Record parsed;
    parsed.grid = "--";
    auto const messageType = tokens.at(6).trimmed();
    QString frequencyToken;

    if (tokens.size() == 9) {
      parsed.callsign = tokens.at(7).trimmed();
      if (!LiveCQ::isValidCallsign(parsed.callsign)) {
        return false;
      }
      parsed.message = messageType + " " + parsed.callsign;
      frequencyToken = tokens.at(8);
    } else if (tokens.size() == 10) {
      auto const firstCandidate = tokens.at(7).trimmed();
      if (LiveCQ::isValidCallsign(firstCandidate)) {
        parsed.callsign = firstCandidate;
        parsed.grid = tokens.at(8).trimmed();
        parsed.message = messageType + " " + parsed.callsign + " " + parsed.grid;
      } else {
        parsed.callsign = tokens.at(8).trimmed();
        if (!LiveCQ::isValidCallsign(parsed.callsign)) {
          return false;
        }
        parsed.message = messageType + " " + parsed.callsign;
      }
      frequencyToken = tokens.at(9);
    } else {
      parsed.callsign = tokens.at(8).trimmed();
      if (!LiveCQ::isValidCallsign(parsed.callsign)) {
        return false;
      }
      parsed.grid = tokens.at(9).trimmed();
      parsed.message = messageType + " " + parsed.callsign + " " + parsed.grid;
      frequencyToken = tokens.at(10);
    }

    if (!parseFrequency(frequencyToken, frequencyOffset,
                        parsed.receiveFrequency, parsed.scheduledFrequency)) {
      return false;
    }

    record = parsed;
    return true;
  }
}
