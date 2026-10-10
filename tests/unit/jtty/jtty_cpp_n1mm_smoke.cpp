#include "widgets/JttyN1mm.hpp"
#include "wsjtx_config.h"

#include <algorithm>
#include <array>

extern "C" void genjtty_atoms_c (Jtty::NativeAtomDescriptor const atoms[], int natoms,
                                 int tones[], int* nsym, int* status);
extern "C" void genjtty_profile_ (char*, int const*, int*, int*, int*, int const*, fortran_charlen_t);

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
    int const isFinal = 1;
    auto profile = static_cast<int> (context.exchangeProfile);
    genjtty_profile_ (text.data (), &profile, packedTones.data (), &packedSymbols,
                      frameStarts.data (), &isFinal, text.size ());
    if (packedSymbols != *nsym || !std::equal (tones, tones + *nsym, packedTones.cbegin ()) ||
        text != Jtty::transmitFrame (example.canonical).toLatin1 ()) {
      return static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
    }
    text = Jtty::transmitFrame (example.raw).toLatin1 ();
    profile = static_cast<int> (Jtty::NativeExchangeProfile::None);
    genjtty_profile_ (text.data (), &profile, packedTones.data (), &packedSymbols,
                      frameStarts.data (), &isFinal, text.size ());
    if (packedSymbols != 2 * *nsym || text != Jtty::transmitFrame (example.raw).toLatin1 ()) {
      return static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
    }
  }
  return status;
}

extern "C" int jtty_cpp_rtty_line_refused ()
{
  // 80 characters; RTTY Roundup writes the serial as 005, which makes it 81.
  QString const line = QString (73, QLatin1Char {'A'}) + QStringLiteral (" 599 05");
  auto encodesWith = [] (Jtty::NativeExchangeProfile exchange) {
    return [profile = static_cast<int> (exchange)] (QString const& text) {
      auto frame = Jtty::transmitFrame (text).toLatin1 ();
      std::array<int, 16 * 59> tones {};
      std::array<int, 16> frameStarts {};
      int symbols {};
      int const isFinal = 0;
      genjtty_profile_ (frame.data (), &profile, tones.data (), &symbols,
                        frameStarts.data (), &isFinal, frame.size ());
      return symbols > 0;
    };
  };
  auto const rtty = Jtty::encodeTransmitLine (line, encodesWith (Jtty::NativeExchangeProfile::RttyRoundup));
  auto const none = Jtty::encodeTransmitLine (line, encodesWith (Jtty::NativeExchangeProfile::None));
  return !rtty && none && none->size () == 1;
}

extern "C" int jtty_cpp_rtty_line_tones (int tones[], int* nsym)
{
  // 79 characters; with the serial written as 005 it is exactly 80.
  QString const line = QString (72, QLatin1Char {'A'}) + QStringLiteral (" 599 05");
  int const profile = static_cast<int> (Jtty::NativeExchangeProfile::RttyRoundup);
  auto encode = [&] (QString const& text, int isFinal) {
    auto frame = Jtty::transmitFrame (text).toLatin1 ();
    std::array<int, 16> frameStarts {};
    *nsym = 0;
    genjtty_profile_ (frame.data (), &profile, tones, nsym, frameStarts.data (), &isFinal, frame.size ());
    return *nsym > 0;
  };
  auto const segments = Jtty::encodeTransmitLine (line, [&] (QString const& text) { return encode (text, 0); });
  return segments && segments->size () == 1 && encode (segments->front ().text, 1);
}
