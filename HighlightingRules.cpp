#include "HighlightingRules.hpp"

#include <QStringList>

namespace HighlightingRules
{
  bool matchesDirectionalCall (QString const& configured_entries,
                               QString const& directional_call)
  {
    auto const wanted = directional_call.trimmed ();
    if (wanted.isEmpty ()) return false;

    for (auto const& entry : configured_entries.split (','))
      {
        if (entry.trimmed ().compare (wanted, Qt::CaseInsensitive) == 0) return true;
      }

    return false;
  }

  bool matchesCallsignPrefix (QString const& configured_entries, QString const& call)
  {
    if (call.size () < 3) return false;

    bool const matched =
      configured_entries.contains (call + ",")
      || configured_entries.contains (";" + call.left (3) + ";")
      || configured_entries.contains (";" + call.left (2) + ";")
      || (configured_entries.contains (";" + call.left (1) + ";") && call.at (1).isDigit ())
      || configured_entries.contains (";" + call.left (1) + "*;");
    if (!matched) return false;

    return !configured_entries.contains ("!" + call.left (2) + "!")
        && !configured_entries.contains ("!" + call.left (3) + "!");
  }
}
