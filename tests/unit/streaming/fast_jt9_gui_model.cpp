// SPDX-License-Identifier: GPL-3.0-or-later
// The GUI's fast JT9 decode, which test_streaming_fast_jt9 compares the
// codec's with: fast_decode_ as MainWindow::decode() calls it through
// startFastDecode (FastDecode.cpp), on a zeroed copy of the samples d2 holds
// up to m_kdone, one process for the whole script as the GUI keeps
// fast_decode's state.
//
// usage: fast_jt9_gui_model <script>
// Script lines, in order:
//   audio <file>                    16-bit little-endian samples
//   nutc|ntr|submode|minsync|rxfreq|ntol <n>
//   decode <first> <k> <newdat>     the period from sample first, decoded at
//                                   m_kdone k with m_t0 0 and m_t1 k/12000
// Output per decode: "line <text>" for each line it returns, "seconds <s>"
// for its compute time, then "end".
// Two inputs differ from the GUI's, and fast9 reads neither: the calls go
// blank, and narg[14], the GUI's Aggressive setting, is 0.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "wsjtx_config.h"

extern "C" void fast_decode_ (short[], int[], double *, char[], char[], char[], fortran_charlen_t,
                              fortran_charlen_t, fortran_charlen_t);

namespace
{
constexpr int SampleRate = 12000;
// startFastDecode's buffer: 30 s at 12000 Hz.
constexpr int Captured = 360000;

int fail (std::string const& why)
{
  std::cerr << "fast_jt9_gui_model: " << why << '\n';
  return 1;
}
}

int main (int argc, char * argv[])
{
  if (argc != 2) return fail ("usage: fast_jt9_gui_model <script>");
  std::ifstream script {argv[1]};
  if (!script) return fail ("cannot read the script");
  std::vector<short> audio;
  int nutc = 0, ntr = 15, submode = 4, minsync = 0, rxfreq = 1500, ntol = 50;
  std::string text;
  while (std::getline (script, text))
    {
      std::istringstream words {text};
      std::string keyword;
      if (!(words >> keyword)) continue;
      if ("audio" == keyword)
        {
          std::string name;
          words >> name;
          std::ifstream file {name, std::ios::binary};
          if (!file) return fail ("cannot read " + name);
          std::vector<char> bytes {std::istreambuf_iterator<char> {file}, std::istreambuf_iterator<char> {}};
          audio.resize (bytes.size () / 2);
          for (std::size_t i = 0; i < audio.size (); ++i)
            audio[i] = short (std::uint16_t (std::uint8_t (bytes[2 * i]))
                              | std::uint16_t (std::uint8_t (bytes[2 * i + 1])) << 8);
        }
      else if ("nutc" == keyword) words >> nutc;
      else if ("ntr" == keyword) words >> ntr;
      else if ("submode" == keyword) words >> submode;
      else if ("minsync" == keyword) words >> minsync;
      else if ("rxfreq" == keyword) words >> rxfreq;
      else if ("ntol" == keyword) words >> ntol;
      else if ("decode" == keyword)
        {
          int first, k, newdat;
          if (!(words >> first >> k >> newdat)) return fail ("decode needs first, k and newdat");
          if (first < 0 || first > int (audio.size ())) return fail ("decode outside the audio");
          double period = ntr;
          // MainWindow::decode(): m_kdone capped at the period, the arguments in narg.
          int kdone = std::min (k, int (SampleRate * period));
          float t0 = 0.0f;
          float t1 = float (k / 12000.0);
          std::array<int, 15> narg {};
          narg[0] = nutc;
          narg[1] = kdone;
          narg[2] = submode;
          narg[3] = newdat;
          narg[4] = minsync;
          narg[5] = 0;
          narg[6] = int (1000.0 * t0);
          narg[7] = int (1000.0 * t1);
          narg[8] = minsync < 0 ? 50 : 2;
          narg[9] = 102;
          narg[10] = rxfreq;
          narg[11] = ntol;
          narg[12] = 0;
          narg[13] = -1;
          narg[14] = 0;
          // startFastDecode: the first min(360000, kdone) samples, zeros after.
          std::vector<short> captured (Captured, 0);
          auto const available = std::max (0, std::min ({Captured, kdone, int (audio.size ()) - first}));
          std::copy_n (audio.begin () + first, available, captured.begin ());
          std::array<char, 8000> messages {};
          std::array<char, 12> myCall, hisCall;
          myCall.fill (' ');
          hisCall.fill (' ');
          auto const started = std::chrono::steady_clock::now ();
          fast_decode_ (captured.data (), narg.data (), &period, messages.data (), myCall.data (), hisCall.data (),
                        fortran_charlen_t (8000), fortran_charlen_t (12), fortran_charlen_t (12));
          auto const seconds = std::chrono::duration<double> (std::chrono::steady_clock::now () - started).count ();
          // fast_decode_done(): the lines up to the first empty one.
          for (int i = 0; i < 100 && messages[80 * i]; ++i)
            {
              std::string line {messages.data () + 80 * i, 80};
              line = line.substr (0, line.find ('\0'));
              line.erase (line.find_last_not_of (' ') + 1);
              std::cout << "line " << line << '\n';
            }
          std::cout << "seconds " << seconds << '\n' << "end\n";
        }
      else return fail ("unknown line: " + text);
    }
  return 0;
}
