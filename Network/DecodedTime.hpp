#ifndef DECODED_TIME_HPP
#define DECODED_TIME_HPP

#include <QDateTime>
#include <QString>

namespace DecodedTime
{
  QDateTime spotTime (QString const& encodedTime, QDateTime const& nowUtc,
                      double periodSeconds);
}

#endif
