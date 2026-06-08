#include "widgets/BandHopping.hpp"

int next_band_hop_index (std::vector<bool> const& selected, int start_index)
{
  auto const entry_count = static_cast<int> (selected.size ());
  if (0 == entry_count) return -1;

  auto normalized_start = start_index % entry_count;
  if (normalized_start < 0) normalized_start += entry_count;

  for (int offset = 0; offset < entry_count; ++offset)
    {
      auto const index = (normalized_start + offset) % entry_count;
      if (selected[index]) return index;
    }

  return -1;
}
