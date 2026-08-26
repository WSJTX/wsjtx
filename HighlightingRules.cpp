#include "HighlightingRules.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace HighlightingRules
{
  namespace
  {
    // True if `token` + `delimiter` occurs in `entries` starting at the
    // beginning of the string or right after a previous entry's own ','
    // or ';' terminator -- so an unrelated, longer configured entry (e.g.
    // "AK1ABC,") can't spuriously match as a substring tail of a lookup
    // for "K1ABC", and so the first entry in a list matches even when (per
    // the documented format, e.g. "VK;ZL;T88;") it has no delimiter before it.
    bool containsDelimitedToken (QString const& entries, QString const& token, QChar delimiter)
    {
      QRegularExpression const re (QStringLiteral ("(?:^|[,;])")
        + QRegularExpression::escape (token) + QRegularExpression::escape (delimiter));
      return re.match (entries).hasMatch ();
    }
  }

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
      containsDelimitedToken (configured_entries, call, ',')
      || containsDelimitedToken (configured_entries, call.left (3), ';')
      || containsDelimitedToken (configured_entries, call.left (2), ';')
      || (containsDelimitedToken (configured_entries, call.left (1), ';') && call.at (1).isDigit ())
      || containsDelimitedToken (configured_entries, call.left (1) + "*", ';');
    if (!matched) return false;

    return !configured_entries.contains ("!" + call.left (2) + "!")
        && !configured_entries.contains ("!" + call.left (3) + "!");
  }
}
