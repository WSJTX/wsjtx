#ifndef FAST_DECODE_HPP
#define FAST_DECODE_HPP

#include <QFuture>
#include <array>

struct FastDecodeResult
{
  std::array<int, 15> arguments {};
  std::array<char, 8000> messages {};
};

// Capture all inputs before returning; retrieve outputs only after completion.
QFuture<FastDecodeResult> startFastDecode (short const samples[], int const arguments[],
                                         double period, char const myCall[],
                                         char const hisCall[]);

#endif
