#include "DecodedGridHistory.hpp"

#include "Decoder/decodedtext.h"
#include "Radio.hpp"

void DecodedGridHistory::observe(DecodedText const& message)
{
  if (!message.isStandardMessage() || message.isLowConfidence()) return;
  auto const fields = parseDecodedMessage(message.message());
  if (fields.grid.isEmpty() || !Radio::is_callsign(fields.sender)) return;
  auto& grid = grids_[fields.sender];
  if (!grid.startsWith(fields.grid)) grid = fields.grid;
}
