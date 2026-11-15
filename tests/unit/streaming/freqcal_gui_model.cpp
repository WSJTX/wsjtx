// SPDX-License-Identifier: GPL-3.0-or-later
// The GUI's FreqCal measurement, which test_streaming_freqcal compares the
// codec's with: freqcal_ as MainWindow::dataSink calls it, at each 3456-sample
// block k of a period whose samples d2 holds from its start, when
// !(m_ihsym % 8) && m_ihsym > 8 && m_ihsym <= m_hsymStop, with
// m_hsymStop = ((int(m_TRperiod/0.288))/8)*8 (MainWindow::fixStop).
//
// usage: freqcal_gui_model <script>
// Script lines, in order:
//   audio <file>                 16-bit little-endian samples
//   ntr|rxfreq|ntol|nkhz <n>     the TR period, Rx Freq, F Tol and dial kHz
//   period <first> <count>       samples [first, first+count) as one period:
//                                dataSink at each whole block's end
// Output per call: "measure <k> <line>", the line as freqcal_ writes it, up
// to its NUL; then "end" after each period.
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "wsjtx_config.h"

extern "C" void freqcal_ (short d2[], int * k, int * nkhz, int * noffset, int * ntol, char line[],
                          fortran_charlen_t);

namespace
{
constexpr int SampleRate = 12000;
// MainWindow::initializeFFT(6912): dataSink's block, half of nsps.
constexpr int Block = 3456;
// freqcal's id2 is 30 s long (lib/freqcal.f90).
constexpr int Captured = 30 * SampleRate;

int fail (std::string const& why)
{
  std::cerr << "freqcal_gui_model: " << why << '\n';
  return 1;
}
}

int main (int argc, char * argv[])
{
  if (argc != 2) return fail ("usage: freqcal_gui_model <script>");
  std::ifstream script {argv[1]};
  if (!script) return fail ("cannot read the script");
  std::vector<short> audio;
  double trperiod = 15;
  int rxfreq = 1500, ntol = 50, nkhz = 0;
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
      else if ("ntr" == keyword) words >> trperiod;
      else if ("rxfreq" == keyword) words >> rxfreq;
      else if ("ntol" == keyword) words >> ntol;
      else if ("nkhz" == keyword) words >> nkhz;
      else if ("period" == keyword)
        {
          int first, count;
          if (!(words >> first >> count)) return fail ("period needs first and count");
          if (first < 0 || count < 0 || first + count > int (audio.size ()) || count > Captured)
            return fail ("period outside the audio");
          int const hsymStop = ((int (trperiod / 0.288)) / 8) * 8;
          std::vector<short> d2 (Captured, 0);
          std::copy_n (audio.begin () + first, count, d2.begin ());
          for (int ihsym = 1; ihsym * Block <= count; ++ihsym)
            {
              if (ihsym % 8 || ihsym <= 8 || ihsym > hsymStop) continue;
              int k = ihsym * Block;
              int offset = rxfreq;
              int ftol = ntol;
              int dial = nkhz;
              char line[80];
              std::fill_n (line, 80, '\0');
              freqcal_ (d2.data (), &k, &dial, &offset, &ftol, line, fortran_charlen_t (80));
              std::string const measured {line, std::find (line, line + 80, '\0')};
              std::cout << "measure " << ihsym * Block << ' ' << measured << '\n';
            }
          std::cout << "end\n";
        }
      else return fail ("unknown line: " + text);
    }
  return 0;
}
