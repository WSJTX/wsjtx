// SPDX-License-Identifier: GPL-3.0-or-later
#include "DecodeEventLine.hpp"

#include <cmath>

namespace
{
  // Fortran Iw.
  std::string integer (long long n, std::size_t width)
  {
    auto const text = std::to_string (n);
    if (text.size () > width) return std::string (width, '*');
    return std::string (width - text.size (), ' ') + text;
  }

  // Fortran Fw.d as gfortran writes a single-precision value: the exact
  // value rounded half to even. x * 10^d is exact in a double for d <= 3.
  // jt9's widths always leave room for the leading zero.
  std::string fixed (float x, std::size_t width, int decimals)
  {
    double scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    double const scaled = std::fabs (static_cast<double> (x)) * scale;
    if (!std::isfinite (scaled) || scaled >= 1e15) return std::string (width, '*');
    double whole = std::floor (scaled);
    double const fraction = scaled - whole;
    if (fraction > 0.5 || (fraction == 0.5 && std::fmod (whole, 2.0) != 0)) whole += 1;
    auto digits = std::to_string (static_cast<long long> (whole));
    if (digits.size () < static_cast<std::size_t> (decimals) + 1)
      digits.insert (0, decimals + 1 - digits.size (), '0');
    auto const integerPart = digits.substr (0, digits.size () - decimals);
    auto const fractionPart = digits.substr (digits.size () - decimals);
    std::string const sign = std::signbit (x) ? "-" : "";
    auto const text = sign + integerPart + "." + fractionPart;
    if (text.size () > width) return std::string (width, '*');
    return std::string (width - text.size (), ' ') + text;
  }

  // Fortran Aw of a variable w characters long.
  std::string column (std::string const& text, std::size_t width)
  {
    if (text.size () >= width) return text.substr (0, width);
    return text + std::string (width - text.size (), ' ');
  }

  // Fortran I1.
  char digit (int n)
  {
    return n >= 0 && n <= 9 ? static_cast<char> ('0' + n) : '*';
  }

  std::string head (DecodeEventLine::Decode const& decode, std::size_t timeWidth)
  {
    return column (decode.time, timeWidth) + integer (decode.snr, 4) + fixed (decode.dt, 5, 1)
      + integer (decode.freq, 5);
  }

  std::string ap (int n)
  {
    if (!n) return "  ";
    return std::string {'a', digit (n)};
  }

  std::string q65Flags (DecodeEventLine::Decode const& decode)
  {
    std::string flags (4, ' ');
    if (decode.q >= 0)
      {
        flags = std::string {'q', digit (decode.q), decode.avg >= 2 ? digit (decode.avg) : ' ', ' '};
        if (decode.copied) flags = column (flags.substr (0, flags.find_last_not_of (' ') + 1) + '#', 4);
      }
    auto const used = flags.find_last_not_of (' ');
    return flags.substr (0, used != std::string::npos && used >= 3 ? 4 : 3);
  }
}

namespace DecodeEventLine
{
  std::string line (Decode const& decode)
  {
    auto const& mode = decode.mode;
    auto const& message = decode.message;
    if (mode == "FT8" && decode.textWidth >= 0)
      return head (decode, 6) + " ~  " + column (message, std::size_t (decode.textWidth));
    if (mode == "FT8" || mode == "FT4")
      return head (decode, 6) + (mode == "FT8" ? " ~  " : " +  ") + column (message, 37) + " " + ap (decode.ap);
    if (mode == "JT9") return head (decode, 4) + " @  " + column (message, 22);
    if (mode == "JT65")
      {
        if (decode.sync.empty ()) return head (decode, 4);
        return head (decode, 4) + " " + column (decode.sync, 2) + " " + column (message, 22) + " "
          + column (decode.flags, 3);
      }
    if (mode == "JT4")
      {
        if (decode.sync.empty ()) return head (decode, 4) + " $";
        return head (decode, 4) + " $" + column (decode.sync, 1) + " " + column (message, 22) + " "
          + column (decode.flags, 3);
      }
    if (mode == "FST4" || mode == "FST4W")
      {
        auto text = column (head (decode, decode.period < 60 ? 6 : 4) + " `  " + column (message, 37) + " "
                            + ap (decode.ap), 70);
        if (decode.hasSpread)
          text = text.substr (0, 64) + (decode.spread < 0.95f ? fixed (decode.spread, 6, 3) : fixed (decode.spread, 6, 2));
        return text;
      }
    if (mode == "Q65")
      return head (decode, decode.period < 60 ? 6 : 4) + " :  " + column (message, 37) + " " + q65Flags (decode);
    return {};
  }

  std::string line (Finished const& finished)
  {
    return "<DecodeFinished>" + integer (finished.synced, 4) + integer (finished.decoded, 4)
      + integer (finished.average, 9);
  }
}
