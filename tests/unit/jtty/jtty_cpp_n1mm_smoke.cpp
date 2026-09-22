#include "widgets/JttyN1mm.hpp"

#include <algorithm>

extern "C" void genjtty_atoms_c (Jtty::NativeAtomDescriptor const atoms[], int natoms,
                                 int tones[], int* nsym, int* status);

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
