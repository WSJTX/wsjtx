// SPDX-License-Identifier: GPL-3.0-or-later
// The GUI's receive chain on recorded audio, which test_streaming_spectrum
// compares the stream's rows with: refspectrum_ through ReferenceSpectrumInput
// at MainWindow::dataSink's blocks, symspec_ as dataSink calls it, hspec_ as
// fastSink calls it with bmsk144 false, and refspectrum_ as the JTTY receive
// path drives it (widgets/mainwindow_jtty_receive.cpp). A JTTY reception's
// rows are symspec_'s over its samples as one period from the rows' first
// sample, the codec's rows; jttyspectrum gives the GUI's JttySpectrum's.
//
// usage: spectrum_gui_model <script>
// Script lines, in order:
//   audio <file>                 16-bit little-endian samples
//   datadir <dir>                where refspec.dat is
//   low_sidelobes 0|1            stop <half-symbols>
//   reference off|measure|apply  the Measure and Ref Spec settings
//   erase                        Erase reference spectrum
//   equalized <file>             each period's samples after equalization are appended here
//   period live|file <first> <count>        samples [first, first+count) as one period
//   msk144 <first> <count> <trperiod>       one MSK144 period of live audio
//   fastjt9 <first> <count> <trperiod>      one fast JT9 period of live audio
//   jtty <first> <count> <reception> <origin> [rows_from]
//                                one live JTTY reception; origin numbers its first sample, and
//                                its rows take the samples from rows_from on (default 0)
//   jttyspectrum <first> <count> <reception> <origin> [rows_from]
//                                the same with the GUI's JttySpectrum for the rows
// Output: a float as the hex of its bits, an array as base64 of its float32 LE values.
//   spectrum <k> <hsym> <power> <peak> <df> <bins> <data>
//   ref write|load <ref(1:3456)>
//   fast <k> <first> <columns> <power> <peak> <green> <data>
//   jtty|jttyspectrum <reception> <begin> <end> <power> <peak> <df> <bins> <data>
//   end
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <QByteArray>

#include "wsjtx_config.h"
#include "commons.h"
#include "ReferenceSpectrum.hpp"
#include "widgets/JttySpectrum.hpp"

extern "C" {
  void symspec_ (struct dec_data *, int * k, int * nsps, int * ingain, bool * bLowSidelobes, int * minw,
                 float * px, float s[], float * df3, int * nhsym, int * npts8, float * pxmax, int * npct);
  void hspec_ (short int d2[], int * k, int * nutc0, int * ntrperiod, int * nrxfreq, int * ntol, bool * bmsk144,
               bool * btrain, double const pcoeffs[], int * ingain, char const * mycall, char const * hiscall,
               bool * bshmsg, bool * bswl, char const * ddir, float green[], float s[], int * jh, float * pxmax,
               float * rmsNoGain, char line[], fortran_charlen_t, fortran_charlen_t, fortran_charlen_t,
               fortran_charlen_t);
  void refspectrum_ (short int d2[], int * ninput, bool * bclearrefspec, bool * brefspec, bool * buseref,
                     char const * c_fname, fortran_charlen_t);
}

namespace
{
constexpr int Hsym = 3456;
constexpr int Msk144Block = 7 * 512;
constexpr int RefPoints = 3456;
constexpr int FastColumns = 703;

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

bool exists (std::string const& path)
{
  std::ifstream file {path};
  return bool (file);
}

class Model
{
public:
  Model () : data_ {new dec_data_t {}} {}

  void run (std::istream& script)
  {
    std::string line;
    while (std::getline (script, line))
      {
        std::istringstream words {line};
        std::string keyword;
        if (!(words >> keyword)) continue;
        if ("audio" == keyword) load (rest (words));
        else if ("datadir" == keyword) refspec_ = rest (words) + "/refspec.dat";
        else if ("low_sidelobes" == keyword) {int v; words >> v; lowSidelobes_ = v != 0;}
        else if ("stop" == keyword) words >> stop_;
        else if ("reference" == keyword) reference (rest (words));
        else if ("erase" == keyword) erase ();
        else if ("equalized" == keyword) equalized_ = rest (words);
        else if ("period" == keyword)
          {
            std::string kind;
            int first, count;
            words >> kind >> first >> count;
            check (first, count);
            if ("live" == kind) live (first, count);
            else if ("file" == kind) file (first, count);
            else fail ("period kind");
          }
        else if ("msk144" == keyword)
          {
            int first, count, trperiod;
            words >> first >> count >> trperiod;
            check (first, count);
            fast (first, count, trperiod, Msk144Block);
          }
        else if ("fastjt9" == keyword)
          {
            int first, count, trperiod;
            words >> first >> count >> trperiod;
            check (first, count);
            fast (first, count, trperiod, Hsym);
          }
        else if ("jtty" == keyword || "jttyspectrum" == keyword)
          {
            int first, count;
            long long reception, origin;
            int rowsFrom = 0;
            words >> first >> count >> reception >> origin;
            if (!(words >> rowsFrom)) rowsFrom = 0;
            check (first, count);
            jtty (first, count, reception, origin, rowsFrom, "jttyspectrum" == keyword);
          }
        else fail ("keyword " + keyword);
      }
  }

private:
  static std::string rest (std::istringstream& words)
  {
    std::string value;
    std::getline (words >> std::ws, value);
    return value;
  }

  [[noreturn]] static void fail (std::string const& what)
  {
    std::cerr << "spectrum_gui_model: " << what << std::endl;
    std::exit (2);
  }

  void check (int first, int count) const
  {
    if (first < 0 || count < 0 || first + count > int (audio_.size ()) || count > NTMAX * RX_SAMPLE_RATE)
      fail ("samples out of range");
  }

  void load (std::string const& path)
  {
    std::ifstream file {path, std::ios::binary};
    if (!file) fail ("audio " + path);
    std::vector<char> bytes {std::istreambuf_iterator<char> {file}, {}};
    audio_.resize (bytes.size () / 2);
    for (std::size_t i = 0; i < audio_.size (); ++i)
      audio_[i] = short (std::uint16_t (std::uint8_t (bytes[2 * i])) | std::uint16_t (std::uint8_t (bytes[2 * i + 1])) << 8);
  }

  void reference (std::string const& mode)
  {
    if ("off" == mode) refSpec_ = useRef_ = false;
    else if ("measure" == mode) {refSpec_ = true; useRef_ = false;}
    else if ("apply" == mode) {refSpec_ = false; useRef_ = true;}
    else fail ("reference " + mode);
  }

  // The codec's ref_spectrum_clear: refspec.dat deleted, the measurement
  // reset, and both Measure and Ref Spec off. The GUI's Erase leaves
  // Measure on, so a measurement running there starts again from scratch.
  void erase ()
  {
    std::remove (refspec_.c_str ());
    refSpec_ = useRef_ = false;
    clear_ = true;
  }

  // One call of refspectrum_, with what it wrote to refspec.dat or loaded
  // from it: a measuring call's file is moved aside first, and an applying
  // call's curve is marked first, so that a write or a load shows.
  void refspectrum (short * samples, int count)
  {
    bool const measuring = refSpec_ && count > 0;
    bool const applying = !refSpec_ && useRef_ && count > 0;
    auto const held = refspec_ + ".held";
    if (measuring) std::rename (refspec_.c_str (), held.c_str ());
    std::vector<float> before;
    if (applying)
      {
        before.assign (spectra_.ref + 1, spectra_.ref + 1 + RefPoints);
        std::fill (spectra_.ref + 1, spectra_.ref + 1 + RefPoints, std::nanf (""));
      }
    refspectrum_ (samples, &count, &clear_, &refSpec_, &useRef_, refspec_.c_str (), fortran_charlen_t (refspec_.size ()));
    if (measuring)
      {
        if (exists (refspec_))
          {
            std::remove (held.c_str ());
            std::cout << "ref write " << base64 (spectra_.ref + 1, RefPoints) << '\n';
          }
        else std::rename (held.c_str (), refspec_.c_str ());
      }
    if (applying)
      {
        bool const loaded = std::any_of (spectra_.ref + 1, spectra_.ref + 1 + RefPoints, [] (float v) {return !std::isnan (v);});
        if (loaded) std::cout << "ref load " << base64 (spectra_.ref + 1, RefPoints) << '\n';
        else std::copy (before.begin (), before.end (), spectra_.ref + 1);
      }
  }

  // MainWindow::dataSink's equalization of live audio.
  void equalize (int k)
  {
    if (clear_)
      {
        int count = 0;
        refspectrum (data_->d2, count);
      }
    clear_ = false;
    referenceInput_.consume (data_->d2, k, refSpec_ ? 1 : useRef_ ? 2 : 0,
                             [this] (short * samples, int count) {refspectrum (samples, count);});
  }

  struct Row
  {
    float power, peak, df;
    int bins;
  };

  // symspec_ at k as dataSink calls it, into s_; false when it made no row.
  bool symspec (int k, Row& row)
  {
    int nsps = 2 * Hsym;
    int ingain = 0;
    int nsmo = 0;
    int npts8 = 0;
    int npct = 0;
    float df3 = 0;
    row = {};
    symspec_ (data_.get (), &k, &nsps, &ingain, &lowSidelobes_, &nsmo, &row.power, s_, &df3, &ihsym_, &npts8,
              &row.peak, &npct);
    if (k >= 2048 && k <= NTMAX * RX_SAMPLE_RATE) symspecK0_ = k;
    if (ihsym_ <= 0) return false;
    row.df = df3;
    row.bins = std::min (NSMAX, int (std::lround (5000.0f / df3)));
    return true;
  }

  void spectrum (int k)
  {
    Row row;
    if (!symspec (k, row)) return;
    std::cout << "spectrum " << k << ' ' << ihsym_ << ' ' << bits (row.power) << ' ' << bits (row.peak) << ' '
              << bits (row.df) << ' ' << row.bins << ' ' << base64 (s_, row.bins) << '\n';
  }

  // symspec_ starts a new data block only when k goes down: one call at the
  // last sample it takes makes the next, at k = 3456, start one.
  void restartSymspec ()
  {
    if (symspecK0_ > Hsym) return;
    Row unused;
    symspec (NTMAX * RX_SAMPLE_RATE, unused);
  }

  void keep (short const * samples, int count)
  {
    if (equalized_.empty ()) return;
    std::ofstream file {equalized_, std::ios::binary | std::ios::app};
    for (int i = 0; i < count; ++i)
      {
        auto const word = std::uint16_t (samples[i]);
        file.put (char (word & 0xffu)).put (char (word >> 8));
      }
  }

  // Detector writes each block of the period into d2 from its start, and
  // dataSink sees its end; the samples after the last whole block never
  // reach dataSink.
  void live (int first, int count)
  {
    std::fill_n (data_->d2, count, short {0});
    std::copy_n (audio_.data () + first, count, data_->d2);
    for (int k = Hsym; k <= count; k += Hsym)
      {
        equalize (k);
        spectrum (k);
      }
    keep (data_->d2, count);
    std::cout << "end\n";
  }

  // MainWindow::diskDat: a file is never equalized.
  void file (int first, int count)
  {
    std::fill_n (data_->d2, count, short {0});
    std::copy_n (audio_.data () + first, count, data_->d2);
    for (int n = 1; n <= stop_; ++n)
      {
        int const k = n * Hsym;
        if (k > count) break;
        spectrum (k);
      }
    keep (data_->d2, count);
    std::cout << "end\n";
  }

  // MainWindow::fastSink's hspec_ call per block, MSK144's 3584 samples or
  // fast JT9's 3456, after dataSink's equalization.
  void fast (int first, int count, int trperiod, int block)
  {
    std::fill_n (data_->d2, count, short {0});
    std::copy_n (audio_.data () + first, count, data_->d2);
    for (int k = block; k <= count; k += block)
      {
        equalize (k);
        int nutc0 = 0;
        int nTRpDepth = trperiod + 1000 * 3;
        int rxfreq = 1500;
        int ftol = 50;
        // The codec's flag; in MSK144 fastSink passes true while monitoring,
        // which leaves hspec_'s k0 at k - 3584 after a decoding call.
        bool bmsk144 = false;
        bool train = false;
        double const pcoeffs[5] {};
        int ingain = 0;
        bool shmsg = false;
        bool swl = false;
        char const mycall[] = "            ";
        char const hiscall[] = "            ";
        char const datadir[] = ".";
        float pxmax = 0;
        float rmsNoGain = 0;
        char line[80];
        line[0] = 0;
        int kk = k;
        int const before = fastJh_;
        bool const newPeriod = k < fastK0_;
        hspec_ (data_->d2, &kk, &nutc0, &nTRpDepth, &rxfreq, &ftol, &bmsk144, &train, pcoeffs, &ingain, mycall, hiscall,
                &shmsg, &swl, datadir, fastGreen_, fastS_, &fastJh_, &pxmax, &rmsNoGain, line, 12, 12,
                fortran_charlen_t (std::strlen (datadir)), 80);
        // hspec_ draws nothing past 30 s; it keeps its k for the next call only when it draws.
        if (k > 30 * RX_SAMPLE_RATE || k < 512) continue;
        fastK0_ = k;
        int const firstColumn = newPeriod ? 0 : std::min (before + 1, FastColumns - 1);
        int const columns = fastJh_ - firstColumn + 1;
        if (columns <= 0) continue;
        std::cout << "fast " << k << ' ' << firstColumn << ' ' << columns << ' ' << bits (rmsNoGain) << ' '
                  << bits (pxmax) << ' ' << base64 (fastGreen_ + firstColumn, columns) << ' '
                  << base64 (fastS_ + 64 * firstColumn, 64 * columns) << '\n';
      }
    keep (data_->d2, count);
    std::cout << "end\n";
  }

  // The JTTY receive path's Begin and Samples events: the detector publishes
  // 3456-sample blocks from the reception's first sample. Its rows take the
  // samples from rows_from on: symspec_'s with them in d2 from its start, a
  // fresh period (the codec's rows), or with gui a JttySpectrum's begun there.
  void jtty (int first, int count, long long reception, long long origin, int rowsFrom, bool gui)
  {
    std::unique_ptr<JttySpectrum> jttySpectrum;
    if (gui)
      {
        jttySpectrum.reset (new JttySpectrum);
        jttySpectrum->begin (quint64 (reception), origin + rowsFrom, 0);
      }
    else restartSymspec ();
    int streamed = 0;   // samples in d2
    int zero = 0;
    refspectrum (data_->d2, zero);
    int referenceMode = -1;
    std::vector<short> event;
    auto const put = [] (char const * kind, long long reception, long long begin, long long end, float power,
                         float peak, float df, int bins, float const * data) {
      std::cout << kind << ' ' << reception << ' ' << begin << ' ' << end << ' ' << bits (power) << ' '
                << bits (peak) << ' ' << bits (df) << ' ' << bins << ' ' << base64 (data, bins) << '\n';
    };
    for (int offset = 0; offset < count; offset += Hsym)
      {
        int const size = std::min (Hsym, count - offset);
        event.assign (audio_.begin () + first + offset, audio_.begin () + first + offset + size);
        int const mode = refSpec_ ? 1 : useRef_ ? 2 : 0;
        if (referenceMode != mode || clear_)
          {
            int none = 0;
            refspectrum (event.data (), none);
            referenceMode = mode;
          }
        clear_ = false;
        for (int at = 0; at < size;)
          {
            int chunk = std::min (Hsym, size - at);
            if (refSpec_ || useRef_) refspectrum (event.data () + at, chunk);
            at += chunk;
          }
        keep (event.data (), size);
        int const skip = std::max (0, std::min (size, rowsFrom - offset));
        if (skip == size) continue;
        if (gui)
          {
            jttySpectrum->process (origin + offset + skip, event.data () + skip, size - skip, 0, lowSidelobes_, 0,
                                   [&] (JttySpectrumFrame const& frame) {
                                     auto const& row = frame.row;
                                     put ("jttyspectrum", row.reception, row.beginSample, row.endSample,
                                          frame.powerDb, frame.maxPowerDb, JttySpectrumFrame::BinWidth,
                                          JttySpectrumFrame::BinCount, frame.bins.data ());
                                   });
            continue;
          }
        std::copy (event.begin () + skip, event.end (), data_->d2 + streamed);
        int const before = streamed;
        streamed += size - skip;
        for (int k = (before / Hsym + 1) * Hsym; k <= streamed; k += Hsym)
          {
            Row row;
            auto const begin = origin + rowsFrom + k - Hsym;
            if (symspec (k, row)) put ("jtty", reception, begin, begin + Hsym, row.power, row.peak, row.df, row.bins, s_);
          }
      }
    std::cout << "end\n";
  }

  std::unique_ptr<dec_data_t> data_;
  std::vector<short> audio_;
  std::string refspec_ {"refspec.dat"};
  std::string equalized_;
  bool lowSidelobes_ {true};
  int stop_ {50};
  bool refSpec_ {false};
  bool useRef_ {false};
  bool clear_ {false};
  ReferenceSpectrumInput referenceInput_;
  // MainWindow::m_ihsym and dataSink's s[], which symspec_ keeps across calls,
  // and the k it keeps for the next (its k0).
  int ihsym_ {0};
  float s_[NSMAX] {};
  int symspecK0_ {99999999};
  // fastSink's fast_green, fast_s and fast_jh.
  float fastGreen_[FastColumns] {};
  float fastS_[64 * FastColumns] {};
  int fastJh_ {0};
  int fastK0_ {99999999};
};
}

int main (int argc, char * argv[])
{
  if (argc != 2) return 2;
  std::ifstream script {argv[1]};
  if (!script) return 3;
  auto model = std::unique_ptr<Model> {new Model};
  model->run (script);
  std::cout.flush ();
  return 0;
}
