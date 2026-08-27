#ifndef TCI_PROTOCOL_HPP__
#define TCI_PROTOCOL_HPP__

#include <QString>
#include <QtGlobal>

namespace TciProtocol
{
  inline bool parse_frequency (QString const& text, quint64 * frequency)
  {
    if (!frequency || text.isEmpty ())
      {
        return false;
      }

    for (auto const character : text)
      {
        if (character < QLatin1Char {'0'} || character > QLatin1Char {'9'})
          {
            return false;
          }
      }

    bool ok {false};
    auto const parsed = text.toULongLong (&ok, 10);
    if (!ok)
      {
        return false;
      }

    *frequency = parsed;
    return true;
  }

  inline bool frequency_difference_exceeds (quint64 lhs, quint64 rhs, quint64 threshold)
  {
    return lhs > rhs ? lhs - rhs > threshold : rhs - lhs > threshold;
  }
}

#endif
