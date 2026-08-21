#include "LiveCQCallsign.hpp"

#include <QStringList>
#include <QtGlobal>

namespace LiveCQ
{
  bool isValidCallsign (QString const& callsign)
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
    for (int i = 1; i < qMin(baseLength, 4); ++i) {
      if (baseCall.at(i).isDigit()) {
        digitPosition = i;
      }
    }
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
}
