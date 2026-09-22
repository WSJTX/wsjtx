#include "widgets/JttyN1mm.hpp"
#include "wsjtx_config.h"

#include <algorithm>
#include <array>

extern "C" void genjtty_atoms_c (Jtty::NativeAtomDescriptor const atoms[], int natoms,
                                 int tones[], int* nsym, int* status);
extern "C" void genjtty_profile_ (char*, int const*, int*, int*, fortran_charlen_t);

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

extern "C" int jtty_cpp_cq_smoke (int tones[], int* nsym, char text[], int chained)
{
  Jtty::NativeMacroContext const context {QStringLiteral ("K1ABC"), {}, 0};
  auto const compiled = Jtty::compileNativeMacro (
      1, Jtty::nativeMacroTemplate (1), context);
  *nsym = 0;
  int status = static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
  if (compiled.isNative ()) {
    auto const frame = Jtty::transmitFrame (compiled.text, chained != 0).toLatin1 ();
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
    auto text = Jtty::transmitFrame (example.raw, false).toLatin1 ();
    int packedSymbols {};
    auto profile = static_cast<int> (context.exchangeProfile);
    genjtty_profile_ (text.data (), &profile, packedTones.data (), &packedSymbols, text.size ());
    if (packedSymbols != *nsym || !std::equal (tones, tones + *nsym, packedTones.cbegin ()) ||
        text != Jtty::transmitFrame (example.canonical, false).toLatin1 ()) {
      return static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
    }
    text = Jtty::transmitFrame (example.raw, false).toLatin1 ();
    profile = static_cast<int> (Jtty::NativeExchangeProfile::None);
    genjtty_profile_ (text.data (), &profile, packedTones.data (), &packedSymbols, text.size ());
    if (packedSymbols != 2 * *nsym || text != Jtty::transmitFrame (example.raw, false).toLatin1 ()) {
      return static_cast<int> (Jtty::NativeEncodeStatus::InvalidDescriptor);
    }
  }
  return status;
}
