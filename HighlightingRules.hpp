#ifndef HIGHLIGHTING_RULES_HPP__
#define HIGHLIGHTING_RULES_HPP__

#include <QString>

namespace HighlightingRules
{
  bool matchesDirectionalCall (QString const& configured_entries,
                               QString const& directional_call);

  // Matches a decoded callsign against a "Highlight orange/blue callsigns"
  // entry list. Supported syntax:
  //   CALL,             exact callsign
  //   ;PFX;             2- or 3-character prefix
  //   ;P;               1-character prefix -- only matches calls whose 2nd
  //                      character is a digit (e.g. ";K;" matches K1ABC but
  //                      not KH6ABC), unless...
  //   ;P*;              ...a wildcard is used, which matches any call
  //                      starting with that single-character prefix
  //   !PFX!             excludes a 2- or 3-character prefix even when a
  //                      broader wildcard would otherwise match it (e.g.
  //                      ";K*;!KH6!KL!KP!" matches all K-calls except the
  //                      compound-prefix entities normally split out as
  //                      their own DXCC country)
  bool matchesCallsignPrefix (QString const& configured_entries, QString const& call);
}

#endif
