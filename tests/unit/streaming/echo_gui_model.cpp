// SPDX-License-Identifier: GPL-3.0-or-later
// The GUI's Echo measurement, which test_streaming_echo compares the codec's
// with: MainWindow::dataSink's Echo block at m_ihsym == m_hsymStop (9), with
// save_echo_params_ and avecho_ called as it calls them, on a period's
// samples, one process for the whole script as the GUI keeps avecho's and
// decode_echo's state and the echocom commons.
//
// usage: echo_gui_model <script>
// Script lines, in order:
//   audio <file>            16-bit little-endian samples
//   spread <hz>             Astro's spread: echocom2's fspread_self and
//                           fspread_dx (astro0) and m_fSpread, the same value
//   doppler <hz>            m_fAudioShift as the int nDop, and m_fDop (nDopTotal)
//   doppler_total <hz>      m_fDop alone, when it differs from the audio shift
//   rit <hz>                Astro's nfRIT
//   freq <hz>               f1, 1500 plus m_fDither
//   kind tone|cw|message    the Fixed Tone, CW and Message buttons
//   spacing <hz>            sbToneSpacing
//   text <call>|-           leEchoMessage ('-': empty); itone from gen_echocall_
//                           of it, as MainWindow::guiUpdate fills it at transmit
//   average <n>             sbEchoAvg
//   auto 0|1                m_auto
//   clear                   the Clear Avg button: m_nclearave 1, and
//                           EchoGraph::clearAvg's nsum, blue and red zeroed
//   period live|file <first>  the Echo block on the period of samples
//                           [first, first + 36000) (zeros past the audio),
//                           live or from a WAV file (m_diskData)
//   pack <first> <file>     the period with the parameters packed as dataSink
//                           packs a live one before avecho, as its first 15
//                           samples, written to file. Save All packs again
//                           after avecho (its spread, Fixed Tone too) and saves
//                           the whole buffer, so this is a file of the same
//                           layout, not the GUI's saved WAV itself
// Output per period, a float as the hex of its bits and an array as base64 of
// its float32 LE values:
//   echo <level> <snr> <db_err> <dfreq> <quality> <width> <nsum> <call hex>
//        <blue> <red> <doppler_total> <doppler> <rit> <freq> <spread> <spacing>
// the last six as save_echo_params_ read them back from a file (0 live).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <QByteArray>

#include "wsjtx_config.h"
#include "commons.h"

extern "C" {
  void save_echo_params_ (int * ndoptotal, int * ndop, int * nfrit, float * f1, float * fspread,
                          int * toneSpacing, int itone[], short id2[], int * idir);
  void avecho_ (short id2[], int * dop, int * nfrit, int * nauto, int * navg, int * nqual, float * f1,
                float * level, float * sigdb, float * snr, float * dfreq, float * width, bool * bDiskData,
                bool * bEchoCall, char const * txcall, char rxcall[], fortran_charlen_t, fortran_charlen_t);
  void gen_echocall_ (char * basecall, int itone[], fortran_charlen_t);
  extern struct {
    float fspread_self;
    float fspread_dx;
  } echocom2_;
}

namespace
{
// The 3 s period at 12000 Hz.
constexpr int PeriodSamples = 36000;

std::string bits (float value)
{
  std::uint32_t word;
  std::memcpy (&word, &value, sizeof word);
  char text[9];
  std::snprintf (text, sizeof text, "%08x", unsigned (word));
  return text;
}

std::string base64 (float const * values, int count)
{
  QByteArray bytes;
  for (int i = 0; i < count; ++i)
    {
      std::uint32_t word;
      std::memcpy (&word, values + i, sizeof word);
      for (int shift = 0; shift < 32; shift += 8) bytes.append (char ((word >> shift) & 0xffu));
    }
  return bytes.toBase64 ().toStdString ();
}

std::string hex (char const * text, int count)
{
  std::string out;
  char two[3];
  for (int i = 0; i < count; ++i)
    {
      std::snprintf (two, sizeof two, "%02x", unsigned (std::uint8_t (text[i])));
      out += two;
    }
  return out;
}

[[noreturn]] void fail (std::string const& what)
{
  std::cerr << "echo_gui_model: " << what << std::endl;
  std::exit (2);
}

struct Gui
{
  std::vector<short> audio;
  float spread {0};
  int doppler {0};
  int dopplerTotal {0};
  bool totalSet {false};
  int rit {0};
  float freq {1500};
  std::string kind {"tone"};
  int spacing {10};
  std::string text;
  int average {10};
  bool autoOn {false};
  int nclearave {1};          // MainWindow's m_nclearave starts at 1
  std::array<int, 6> itone {};

  // The period's samples as d2 holds them at the Echo block: the Detector's
  // buffer from the period's start, zeros past the audio.
  std::vector<short> period (int first) const
  {
    std::vector<short> d2 (PeriodSamples, 0);
    if (first < int (audio.size ()))
      std::copy_n (audio.begin () + first, std::min (PeriodSamples, int (audio.size ()) - first), d2.begin ());
    return d2;
  }

  // The spacing as the GUI's Echo line shows it (MainWindow::dataSink).
  static int shown (int ndf)
  {
    if (ndf < 0 || ndf > 30) ndf = 0;
    return ndf;
  }

  // The GUI's itone after a transmission of the Echo message or CW.
  void transmitTones ()
  {
    itone.fill (0);
    auto call = (text + "      ").substr (0, 6);
    gen_echocall_ (&call[0], itone.data (), fortran_charlen_t (6));
  }

  // The live packing of dataSink's Echo block (MainWindow::dataSink).
  void pack (std::vector<short>& d2, int& nDopTotal, int& nDop, int& nfrit, float& f1, float& width, int& ndf)
  {
    int idir = 1;
    if ("tone" != kind)
      {
        if ("message" == kind) ndf = spacing;
        save_echo_params_ (&nDopTotal, &nDop, &nfrit, &f1, &width, &ndf, itone.data (), d2.data (), &idir);
      }
  }

  void measure (int first, bool disk)
  {
    auto d2 = period (first);
    float dBerr = 0.0f;
    int nfrit = rit;
    int nauto = autoOn ? 1 : 0;
    int nqual = 0;
    float f1 = freq;
    float xlevel = 0.0f;
    float sigdb = 0.0f;
    float dfreq = 0.0f;
    float width = spread;
    echocom2_.fspread_self = spread;
    echocom2_.fspread_dx = spread;
    echocom_.nclearave = nclearave;
    int nDop = doppler;
    int nDopTotal = totalSet ? dopplerTotal : doppler;
    int navg = average;
    int ndf = 0;
    if (!disk) pack (d2, nDopTotal, nDop, nfrit, f1, width, ndf);
    bool echoCall = "message" == kind;
    float inBandSpread = 0.0f;
    if (disk)
      {
        int idir = -1;
        save_echo_params_ (&nDopTotal, &nDop, &nfrit, &f1, &width, &ndf, itone.data (), d2.data (), &idir);
        echoCall = ndf >= 10 && ndf <= 30;
        inBandSpread = width;
      }
    bool diskData = disk;
    auto const txcall = (text + "      ").substr (0, 6);
    char rxcall[7] {};
    avecho_ (d2.data (), &nDop, &nfrit, &nauto, &navg, &nqual, &f1, &xlevel, &sigdb, &dBerr, &dfreq, &width,
             &diskData, &echoCall, txcall.c_str (), rxcall, fortran_charlen_t (6), fortran_charlen_t (6));
    nclearave = 0;
    std::cout << "echo " << bits (xlevel) << ' ' << bits (sigdb) << ' ' << bits (dBerr) << ' ' << bits (dfreq)
              << ' ' << nqual << ' ' << bits (width) << ' ' << echocom_.nsum << ' ' << hex (rxcall, 6) << ' '
              << base64 (echocom_.blue, 4096) << ' ' << base64 (echocom_.red, 4096);
    if (disk)
      std::cout << ' ' << nDopTotal << ' ' << nDop << ' ' << nfrit << ' ' << bits (f1) << ' '
                << bits (inBandSpread) << ' ' << shown (ndf);
    else std::cout << " 0 0 0 0 0 0";
    std::cout << '\n';
  }

  void packOut (int first, std::string const& path)
  {
    auto d2 = period (first);
    int nfrit = rit;
    float f1 = freq;
    float width = spread;
    int nDop = doppler;
    int nDopTotal = totalSet ? dopplerTotal : doppler;
    int ndf = 0;
    pack (d2, nDopTotal, nDop, nfrit, f1, width, ndf);
    std::ofstream file {path, std::ios::binary};
    if (!file) fail ("cannot write " + path);
    for (auto sample : d2)
      {
        auto const word = std::uint16_t (sample);
        file.put (char (word & 0xffu)).put (char (word >> 8));
      }
  }
};

std::string rest (std::istringstream& words)
{
  std::string value;
  std::getline (words >> std::ws, value);
  return value;
}
}

int main (int argc, char * argv[])
{
  if (argc != 2) fail ("usage: echo_gui_model <script>");
  std::ifstream script {argv[1]};
  if (!script) fail ("cannot read the script");
  Gui gui;
  std::string line;
  while (std::getline (script, line))
    {
      std::istringstream words {line};
      std::string keyword;
      if (!(words >> keyword)) continue;
      if ("audio" == keyword)
        {
          std::ifstream file {rest (words), std::ios::binary};
          if (!file) fail ("cannot read the audio");
          std::vector<char> bytes {std::istreambuf_iterator<char> {file}, std::istreambuf_iterator<char> {}};
          gui.audio.resize (bytes.size () / 2);
          for (std::size_t i = 0; i < gui.audio.size (); ++i)
            gui.audio[i] = short (std::uint16_t (std::uint8_t (bytes[2 * i]))
                                  | std::uint16_t (std::uint8_t (bytes[2 * i + 1])) << 8);
        }
      else if ("spread" == keyword) words >> gui.spread;
      else if ("doppler" == keyword) words >> gui.doppler;
      else if ("doppler_total" == keyword)
        {
          words >> gui.dopplerTotal;
          gui.totalSet = true;
        }
      else if ("rit" == keyword) words >> gui.rit;
      else if ("freq" == keyword) words >> gui.freq;
      else if ("kind" == keyword) words >> gui.kind;
      else if ("spacing" == keyword) words >> gui.spacing;
      else if ("text" == keyword)
        {
          words >> gui.text;
          if ("-" == gui.text) gui.text.clear ();
          gui.transmitTones ();
        }
      else if ("average" == keyword) words >> gui.average;
      else if ("auto" == keyword)
        {
          int on;
          words >> on;
          gui.autoOn = on != 0;
        }
      else if ("clear" == keyword)
        {
          gui.nclearave = 1;
          echocom_.nsum = 0;
          std::fill_n (echocom_.blue, 4096, 0.0f);
          std::fill_n (echocom_.red, 4096, 0.0f);
        }
      else if ("period" == keyword)
        {
          std::string how;
          int first;
          if (!(words >> how >> first) || ("live" != how && "file" != how)) fail ("period live|file <first>");
          gui.measure (first, "file" == how);
        }
      else if ("pack" == keyword)
        {
          int first;
          if (!(words >> first)) fail ("pack <first> <file>");
          gui.packOut (first, rest (words));
        }
      else fail ("unknown line: " + line);
    }
  return 0;
}
