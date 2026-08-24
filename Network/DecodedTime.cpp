#include "DecodedTime.hpp"

#include <QTime>
#include <QtMath>

namespace
{
  constexpr int secondsPerDay {24 * 60 * 60};

  bool parseTime(QString const& encodedTime, QTime& time, int& secondsSinceMidnight)
  {
    if (encodedTime.size() != 4 && encodedTime.size() != 6) {
      return false;
    }
    for (auto const character : encodedTime) {
      if (!character.isDigit()) {
        return false;
      }
    }

    bool ok = false;
    auto const hours = encodedTime.mid(0, 2).toInt(&ok);
    if (!ok) {
      return false;
    }
    auto const minutes = encodedTime.mid(2, 2).toInt(&ok);
    if (!ok) {
      return false;
    }
    auto const seconds = encodedTime.size() == 6
      ? encodedTime.mid(4, 2).toInt(&ok) : 0;
    if (!ok) {
      return false;
    }

    time = QTime {hours, minutes, seconds};
    if (!time.isValid()) {
      return false;
    }
    secondsSinceMidnight = time.hour() * 60 * 60 + time.minute() * 60 + time.second();
    return true;
  }
}

namespace DecodedTime
{
  QDateTime spotTime(QString const& encodedTime, QDateTime const& nowUtc,
                     double periodSeconds)
  {
    QTime time;
    int secondsSinceMidnight {0};
    if (!parseTime(encodedTime, time, secondsSinceMidnight) || !nowUtc.isValid()) {
      return {};
    }

    auto const currentDate = nowUtc.toUTC().date();
    auto const spotDate = secondsSinceMidnight + qCeil(periodSeconds) >= secondsPerDay
      ? currentDate.addDays(-1) : currentDate;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return {spotDate, time, QTimeZone::UTC};
#else
    return {spotDate, time, Qt::UTC};
#endif
  }
}
