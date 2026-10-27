// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DECODE_EVENT_LINE_HPP
#define DECODE_EVENT_LINE_HPP

#include <string>

// The line jt9 prints for a wsjt-codec decode or decode_finished event, so
// that the GUI's line handling (DecodeOutputPlan::prepareLine) takes codec
// events unchanged. Each field is the event's key of the same name.
namespace DecodeEventLine
{
  struct Decode
  {
    std::string mode;           // FT8, FT4, JT9, JT65, JT4, FST4, FST4W or Q65
    std::string time;           // HHMMSS
    int snr {0};
    float dt {0.f};             // read as single precision, as the codec sends it
    int freq {0};
    std::string message;
    int ap {0};                 // FT8, FT4, FST4, FST4W
    std::string sync;           // JT65, JT4; empty for jt9's short line
    std::string flags;          // JT65, JT4
    int q {-1};                 // Q65
    int avg {0};                // Q65
    bool copied {false};        // Q65
    int period {0};             // FST4, FST4W, Q65
    bool hasSpread {false};     // FST4, FST4W
    float spread {0.f};
    int textWidth {-1};         // SuperFox, rx_messages.txt: the message's columns
    int pass {0};               // not on the line
  };

  struct Finished
  {
    int synced {0};
    int decoded {0};
    int average {0};
  };

  // Empty for a mode jt9's worker does not decode.
  std::string line (Decode const&);
  std::string line (Finished const&);
}

#endif
