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
}
