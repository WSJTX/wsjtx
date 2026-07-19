#ifndef FOX_OPERATOR_ACTIONS_HPP
#define FOX_OPERATOR_ACTIONS_HPP

#include <QQueue>
#include <QString>

namespace FoxOperatorActions
{
  inline QString callsign (QString const& line)
  {
    return line.left (12).trimmed ();
  }

  inline int queuedHoundIndex (QQueue<QString> const& queue, QString const& line)
  {
    auto const target = callsign (line);
    if (target.isEmpty ()) return -1;

    for (int index = 0; index < queue.size (); ++index)
      {
        if (callsign (queue.at (index)) == target) return index;
      }
    return -1;
  }

  inline bool moveQueuedHoundToFront (QQueue<QString>& queue, QString const& line)
  {
    auto const index = queuedHoundIndex (queue, line);
    if (index < 0) return false;

    if (index > 0)
      {
        auto const live_line = queue.takeAt (index);
        queue.prepend (live_line);
      }
    return true;
  }

  inline bool removeQueuedHound (QQueue<QString>& queue, QString const& line)
  {
    auto const target = callsign (line);
    if (target.isEmpty ()) return false;

    bool removed {false};
    for (int index = queue.size () - 1; index >= 0; --index)
      {
        if (callsign (queue.at (index)) == target)
          {
            queue.removeAt (index);
            removed = true;
          }
      }
    return removed;
  }

  template<typename QsoMap>
  bool timeoutIfActive (QQueue<QString> const& in_progress, QsoMap& qsos,
                        QString const& line, int timeout_value)
  {
    auto const target = callsign (line);
    if (target.isEmpty () || !in_progress.contains (target)) return false;

    auto qso = qsos.find (target);
    if (qso == qsos.end ()) return false;

    qso.value ().ncall = timeout_value;
    return true;
  }
}

#endif // FOX_OPERATOR_ACTIONS_HPP
