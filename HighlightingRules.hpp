#ifndef HIGHLIGHTING_RULES_HPP__
#define HIGHLIGHTING_RULES_HPP__

#include <QString>

namespace HighlightingRules
{
  bool matchesDirectionalCall (QString const& configured_entries,
                               QString const& directional_call);
}

#endif
