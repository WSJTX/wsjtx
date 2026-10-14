// -*- Mode: C++ -*-
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef JTTY_TRANSMIT_HPP
#define JTTY_TRANSMIT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace Jtty
{
  enum class NativeAtomKind : std::int8_t
  {
    Call = 0,
    ExchangeNumber = 1,
    ExchangeLocation = 2,
    ExchangePair = 3,
    ExchangeNumberTime = 4,
    Control = 5,
    Grid4 = 6,
    Text5 = 7
  };

  enum class CallAction : std::int8_t
  {
    Cq = 0,
    Call = 1,
    TuCq = 2,
    CallTu = 3,
    CallAgn = 4,
    TuNowCall = 5
  };

  enum class ExchangeRole : std::int8_t
  {
    FieldOnly = 0,
    Full = 1
  };

  enum class NumberKind : std::int8_t
  {
    Serial = 0,
    CqZone = 1,
    ItuZone = 2,
    Age = 3,
    Power = 4,
    Check = 5,
    FirstLicenseYear = 6,
    Generic = 7
  };

  enum class LocationKind : std::int8_t
  {
    StateProvince = 0,
    ArrlRacSection = 1,
    CountryPrefix = 2,
    Qth = 3,
    LocalAdministrativeCode = 4
  };

  enum class PairSchema : std::int8_t
  {
    ZoneLocation = 0,
    ClassSection = 1
  };

  enum class NativeExchangeProfile
  {
    // Shared with the Fortran text packer; None leaves text inference unprofiled.
    None = 0,
    FieldDay = 1,
    RttyRoundup = 2
  };

  struct NativeAtomDescriptor
  {
    std::int8_t kind {};
    std::int8_t subtype {};
    std::int8_t role {};
    std::int8_t reserved {};
    std::int32_t value {};
    char text[9] {};
  };

  static_assert (std::is_standard_layout<NativeAtomDescriptor>::value,
                 "NativeAtomDescriptor must remain C-compatible");
  static_assert (offsetof (NativeAtomDescriptor, kind) == 0, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, subtype) == 1, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, role) == 2, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, reserved) == 3, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, value) == 4, "atom ABI mismatch");
  static_assert (offsetof (NativeAtomDescriptor, text) == 8, "atom ABI mismatch");
  static_assert (sizeof (NativeAtomDescriptor) == 20, "atom ABI mismatch");

  // JTTY_ENCODE_* in lib/jtty/jtty_source_codec.f90.
  enum class NativeEncodeStatus
  {
    Ok = 0,
    InvalidDescriptor = 1,
    UnknownSection = 2,
    Unencodable = 3
  };

  enum class NativeMacroStatus
  {
    Native,
    LiteralFallback,
    InvalidRuntime
  };

  // Fixed width of a JTTY transmit frame; genjtty_text_c expects exactly this
  // many characters.
  inline constexpr int maxMessageLength = 80;
  inline constexpr int maxTransmitLength = maxMessageLength;

  // MAX_FRAMES in lib/jtty/jtty_mod.f90, the bits of a frame, and the
  // symbols genjtty_frames sends for each frame.
  inline constexpr int maxTransmitFrames = 16;
  inline constexpr int transmitFrameBits = 34;
  inline constexpr int transmitFrameSymbols = 59;

  enum class TransmitTextStatus
  {
    Encoded,
    Empty,              // nothing but spaces to send
    EncodingFailed
  };
}

extern "C" {
  // lib/jtty/genjtty.f90. genjtty_text_c rewrites msg, a maxTransmitLength-
  // character field, as the text its frames carry; frames receives each
  // frame's transmitFrameBits bits as '0'/'1' characters, frame_starts each
  // frame's 1-based column in msg; status is a NativeEncodeStatus. itone holds
  // maxTransmitFrames * transmitFrameSymbols ints, frames maxTransmitFrames *
  // transmitFrameBits chars, frame_starts maxTransmitFrames ints; all are written.
  void genjtty_text_c (char msg[], int exchange_profile, int is_final, int itone[], int * nsym,
                       char frames[], int * nframes, int frame_starts[], int * status);
  void genjtty_atoms_c (Jtty::NativeAtomDescriptor const atoms[], int natoms,
                        int itone[], int * nsym, int * status);
  // genjtty_atoms_c's tones and the frames behind them, from one packing.
  void genjtty_atoms_frames_c (Jtty::NativeAtomDescriptor const atoms[], int natoms,
                               int itone[], int * nsym, char frames[], int * nframes,
                               int * status);

  // lib/jtty/gen_jttywave.f90
  void gen_jttywave_ (int const itone[], int * nsym, int * nsps, float * bt, float * fsample,
                      float * f0, float xjunk[], float wave[], int * icmplx, int * nwave);
}

// The JTTY transmit encoding. Text is UTF-16 code units, as in a QString,
// and is handled one unit at a time.
namespace Jtty::Encoder
{
  // QChar::isSpace, QString::trimmed, QString::simplified and QString::toUpper
  // as Qt 5 has them.
  bool isSpace (char16_t c);
  std::u16string trimmed (std::u16string const& text);
  std::u16string simplified (std::u16string const& text);
  std::u16string toUpper (std::u16string const& text);

  struct PreparedTransmitText
  {
    std::u16string text;
    bool substituted {false};
  };

  PreparedTransmitText prepareTransmitText (std::u16string const& message);

  struct TransmitTextSegment
  {
    int offset {0};
    int length {0};
    std::u16string text;
  };

  // Source spans include separator spaces so every submitted character is accounted for.
  TransmitTextSegment nextTransmitTextSegment (std::u16string const& message, int offset,
                                               int maximumLength = maxTransmitLength);

  // An empty result rejects oversized text before it reaches the fixed-width codec.
  std::u16string transmitFrame (std::u16string const& message);

  // What the transmit queue sends of a segment.
  struct TransmitSegment
  {
    std::u16string text;
    std::vector<int> tones;
    // 0-indexed start offset into text of each encoded frame.
    std::vector<int> frameCharStarts;
  };

  struct EncodedTransmitSegment
  {
    TransmitSegment transmit;
    TransmitTextSegment source;     // the prepared characters it carries; offset into the whole text
    std::vector<std::string> frames;  // the frames behind transmit.tones, transmitFrameBits '0'/'1' each
    bool final {false};             // its last frame ends the message
  };

  struct EncodedTransmitText
  {
    TransmitTextStatus status {TransmitTextStatus::Empty};
    bool substituted {false};       // prepareTransmitText replaced a character
    // In transmit order; on EncodingFailed, those encoded before the failure.
    std::vector<EncodedTransmitSegment> segments;
  };

  // Text as the transmitter sends it. Each line is cut into segments by
  // nextTransmitTextSegment; a segment that does not encode fails the text
  // rather than being cut shorter, since the rest would continue the
  // receiver's 80-column message. The last frame of a line's last segment
  // ends the message unless the line is the text's last and isFinal is
  // false; every other segment leaves it open, so the receiver extends one
  // growing message (jtty_mdecode.f90 stitches a continuation onto it).
  EncodedTransmitText encodeTransmitText (std::u16string const& message,
                                          NativeExchangeProfile profile, bool isFinal);

  struct NativeAtomEncoding
  {
    std::vector<int> tones;         // empty when the atoms do not encode
    std::vector<std::string> frames;  // the frames behind tones
    std::u16string error;
  };

  // A compiled function key's atoms as genjtty_atoms_c sends them, always
  // ending the message.
  NativeAtomEncoding encodeNativeAtoms (std::vector<NativeAtomDescriptor> const& atoms);

  // The transmitted audio of tones: gen_jttywave's waveform at sampleRate,
  // lowest tone f0 Hz, scaled to 16 bits, clamped and rounded.
  std::vector<std::int16_t> renderTransmitTones (int const tones[], int nsym, int sampleRate,
                                                 float f0);

  struct NativeMacroContext
  {
    std::u16string myCall;
    std::u16string hisCall;
    int serialNumber {};
    NativeExchangeProfile exchangeProfile {NativeExchangeProfile::None};
    std::u16string configuredExchange;
    std::u16string grid;
    int snr {-10};  // SNR of received signal, for %R
  };

  // The macro context of a station's settings: the configured exchange is the
  // profile's, a Field Day exchange normalized, and none without a profile.
  NativeMacroContext nativeMacroContext (std::u16string const& myCall,
                                         std::u16string const& hisCall, int serialNumber,
                                         std::u16string const& grid, int snr,
                                         NativeExchangeProfile profile,
                                         std::u16string const& exchange);

  struct NativeMacroCompilation
  {
    NativeMacroStatus status {NativeMacroStatus::LiteralFallback};
    std::vector<NativeAtomDescriptor> atoms;
    std::u16string text;
    std::u16string error;

    bool isNative () const
    {
      return status == NativeMacroStatus::Native;
    }
  };

  // The templates sent as native atoms. A template is one when it equals a
  // form after simplified and toUpper, or, for the 599 %N forms, exactly.
  enum class NativeForm
  {
    None,
    Cq,                 // CQ %M CQ
    CallExchange,       // %H %E
    CallTuCq,           // %H TU CQ %M CQ
    MyCall,             // %M
    HisCall,            // %H
    TuNowExchange,      // TU NOW %Q %E
    CallAgn,            // %H AGN?
    Exchange,           // %E
    CallSerial,         // %H 599 %N
    TuNowSerial,        // TU NOW %Q 599 %N
    Serial,             // 599 %N
    CallGrid,           // %H %G
    TuNowGrid,          // TU NOW %Q %G
    Grid,               // %G
    ReportGrid          // 599 %G
  };

  std::u16string nativeFormTemplate (NativeForm form);

  std::u16string formatSerialNumber (int serialNumber);
  std::u16string formatSnr (int snr);
  std::u16string expandLiteralMacro (std::u16string macroTemplate,
                                     NativeMacroContext const& context);

  bool isNativeCall (std::u16string const& rawCall);
  NativeAtomDescriptor nativeCallAtom (CallAction action, std::u16string const& rawCall);
  NativeAtomDescriptor nativeLocationAtom (ExchangeRole role, LocationKind kind,
                                           std::u16string const& token);
  NativeAtomDescriptor nativeClassSectionAtom (int count, char16_t classLetter,
                                               std::u16string const& section);
  NativeAtomDescriptor nativeControlAtom (int phraseId);
  NativeAtomDescriptor nativeGridAtom (ExchangeRole role, std::u16string const& grid);

  bool isDecimal (std::u16string const& value);
  std::u16string normalizedFieldDayExchange (std::u16string const& value);
  bool isCanonicalBase36Token (std::u16string const& value);
  std::u16string controlPhrase (int phraseId);
  int controlPhraseId (std::u16string const& macroTemplate);

  struct NativeExchangeCompilation
  {
    bool valid {false};
    NativeAtomDescriptor atom;
    std::u16string text;
    std::u16string error;
  };

  NativeExchangeCompilation nativeExchange (NativeMacroContext const& context);
  NativeExchangeCompilation nativeGridExchange (NativeMacroContext const& context,
                                                ExchangeRole role);
  NativeMacroCompilation compileNativeMacro (std::u16string const& macroTemplate,
                                             NativeMacroContext const& context);
}

#endif
