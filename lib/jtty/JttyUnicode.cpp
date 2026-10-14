// SPDX-License-Identifier: GPL-3.0-or-later
#include "JttyTransmit.hpp"

namespace Jtty::Encoder
{
  bool isSpace (char16_t c)
  {
    return c == 0x20 || (c >= 0x09 && c <= 0x0D) || c == 0x85 || c == 0xA0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F
        || c == 0x205F || c == 0x3000;
  }

  std::u16string trimmed (std::u16string const& text)
  {
    std::size_t first = 0;
    std::size_t last = text.size ();
    while (first < last && isSpace (text[first])) ++first;
    while (last > first && isSpace (text[last - 1])) --last;
    return text.substr (first, last - first);
  }
}
