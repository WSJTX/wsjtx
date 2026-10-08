#include "widgets/JttyN1mm.hpp"
#include "lib/jtty/JttyTransmit.hpp"

#include <algorithm>
#include <array>

extern "C" int jtty_cpp_n1mm_smoke (int tones[], int* nsym)
{
  auto const compiled = Jtty::compileN1mmMessage (
      QStringLiteral ("[[JTTY:CALL_EXCH]] W9XYZ 32D EMA"),
      Jtty::NativeExchangeProfile::FieldDay);
  *nsym = 0;
  int status = static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
  if (compiled.isNative ()) {
    genjtty_atoms_c (compiled.atoms.constData (), compiled.atoms.size (),
                    tones, nsym, &status);
  }
  return status;
}

extern "C" int jtty_cpp_cq_smoke (int tones[], int* nsym, char text[])
{
  Jtty::NativeMacroContext const context {QStringLiteral ("K1ABC"), {}, 0};
  auto const compiled = Jtty::compileNativeMacro (
      1, Jtty::nativeMacroTemplate (1), context);
  *nsym = 0;
  int status = static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
  if (compiled.isNative ()) {
    auto const frame = Jtty::transmitFrame (compiled.text).toLatin1 ();
    std::copy (frame.cbegin (), frame.cend (), text);
    genjtty_atoms_c (compiled.atoms.constData (), compiled.atoms.size (),
                    tones, nsym, &status);
  }
  return status;
}

extern "C" int jtty_cpp_rtty_smoke (int tones[], int* nsym)
{
  Jtty::NativeMacroContext context {
      QStringLiteral ("K1ABC"), {}, 1, Jtty::NativeExchangeProfile::RttyRoundup,
      QStringLiteral ("DX")};
  *nsym = 0;
  int status = static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
  std::array<int, 16 * 59> packedTones {};
  struct Example {int serial; QString raw; QString canonical;};
  std::array<Example, 3> const examples {{
      {1, QStringLiteral ("599 001"), QStringLiteral ("599 001")},
      {5, QStringLiteral ("599 05"), QStringLiteral ("599 005")},
      {123, QStringLiteral ("599 0123"), QStringLiteral ("599 123")}}};
  for (auto const& example : examples) {
    context.serialNumber = example.serial;
    auto const compiled = Jtty::compileNativeMacro (8, Jtty::nativeMacroTemplate (8), context);
    if (!compiled.isNative () || compiled.text != example.canonical) {
      return static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
    }
    genjtty_atoms_c (compiled.atoms.constData (), compiled.atoms.size (), tones, nsym, &status);
    if (status != static_cast<int> (Jtty::NativeEncodeStatus::Ok) || *nsym <= 0) return status;
    auto text = Jtty::transmitFrame (example.raw).toLatin1 ();
    int packedSymbols {};
    std::array<int, 16> frameStarts {};
    std::array<char, 16 * 34> frames {};
    int nframes {};
    int textStatus {};
    int const isFinal = 1;
    auto profile = static_cast<int> (context.exchangeProfile);
    genjtty_text_c (text.data (), profile, isFinal, packedTones.data (), &packedSymbols,
                    frames.data (), &nframes, frameStarts.data (), &textStatus);
    if (packedSymbols != *nsym || !std::equal (tones, tones + *nsym, packedTones.cbegin ()) ||
        text != Jtty::transmitFrame (example.canonical).toLatin1 ()) {
      return static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
    }
    text = Jtty::transmitFrame (example.raw).toLatin1 ();
    profile = static_cast<int> (Jtty::NativeExchangeProfile::None);
    genjtty_text_c (text.data (), profile, isFinal, packedTones.data (), &packedSymbols,
                    frames.data (), &nframes, frameStarts.data (), &textStatus);
    if (packedSymbols != 2 * *nsym || text != Jtty::transmitFrame (example.raw).toLatin1 ()) {
      return static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
    }
  }
  return status;
}

extern "C" int jtty_cpp_rtty_line_refused ()
{
  // 80 characters; RTTY Roundup writes the serial as 005, which makes it 81.
  auto const line = Jtty::toEncoder (QString (73, QLatin1Char {'A'}) + QStringLiteral (" 599 05"));
  auto const rtty = Jtty::Encoder::encodeTransmitText (line, Jtty::NativeExchangeProfile::RttyRoundup, true);
  auto const none = Jtty::Encoder::encodeTransmitText (line, Jtty::NativeExchangeProfile::None, true);
  return rtty.status == Jtty::TransmitTextStatus::EncodingFailed && rtty.segments.empty ()
      && none.status == Jtty::TransmitTextStatus::Encoded && none.segments.size () == 1;
}

extern "C" int jtty_cpp_rtty_line_tones (int tones[], int* nsym)
{
  // 79 characters; with the serial written as 005 it is exactly 80.
  auto const line = Jtty::toEncoder (QString (72, QLatin1Char {'A'}) + QStringLiteral (" 599 05"));
  auto const encoded = Jtty::Encoder::encodeTransmitText (line, Jtty::NativeExchangeProfile::RttyRoundup, true);
  *nsym = 0;
  if (encoded.status != Jtty::TransmitTextStatus::Encoded || encoded.segments.size () != 1
      || !encoded.segments.front ().final) {
    return 0;
  }
  auto const& segmentTones = encoded.segments.front ().transmit.tones;
  std::copy (segmentTones.cbegin (), segmentTones.cend (), tones);
  *nsym = static_cast<int> (segmentTones.size ());
  return 1;
}
